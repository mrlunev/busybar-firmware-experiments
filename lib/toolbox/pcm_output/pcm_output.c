#include "pcm_output.h"

#include <furi.h>
#include <furi_hal_sai.h>
#include <audio/audio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "PcmOut"

#define PCM_DMA_BUFFER_SAMPLES 4096
#define PCM_DMA_HALF           (PCM_DMA_BUFFER_SAMPLES / 2)
/* Larger ring + higher prefill: absorb TCP/decode jitter at stream start without
 * starving SAI (see FM Radio). Cost: ~+16 KiB vs original 16k-sample ring. */
#define PCM_RING_SAMPLES       24576
#define PCM_PREFILL_SAMPLES    8192

struct PcmOutput {
    int16_t dma_buffer[PCM_DMA_BUFFER_SAMPLES];
    int16_t pcm_ring[PCM_RING_SAMPLES];
    volatile uint32_t pcm_wr;
    volatile uint32_t pcm_rd;
    bool sai_started;
    bool prefilled;
    int16_t underrun_hold;
};

static inline uint32_t ring_available(PcmOutput* out) {
    uint32_t wr = out->pcm_wr;
    uint32_t rd = out->pcm_rd;
    if(wr >= rd) return wr - rd;
    return PCM_RING_SAMPLES - rd + wr;
}

static inline uint32_t ring_free_space(PcmOutput* out) {
    return PCM_RING_SAMPLES - 1 - ring_available(out);
}

static void ring_write(PcmOutput* out, const int16_t* data, uint32_t count) {
    uint32_t wr = out->pcm_wr;
    for(uint32_t i = 0; i < count; i++) {
        out->pcm_ring[wr] = data[i];
        wr = (wr + 1) % PCM_RING_SAMPLES;
    }
    out->pcm_wr = wr;
}

static void ring_read_to_buf(PcmOutput* out, int16_t* dst, uint32_t count) {
    uint32_t rd = out->pcm_rd;
    for(uint32_t i = 0; i < count; i++) {
        dst[i] = out->pcm_ring[rd];
        rd = (rd + 1) % PCM_RING_SAMPLES;
    }
    out->pcm_rd = rd;
}

static void pcm_sai_callback(FuriHalSaiEvent event, void* context) {
    PcmOutput* out = context;
    int16_t* dst = (event == FuriHalSaiEventHalfTransfer)
        ? out->dma_buffer
        : &out->dma_buffer[PCM_DMA_HALF];

    const uint32_t need = PCM_DMA_HALF;
    uint32_t avail = ring_available(out);
    if(avail >= need) {
        ring_read_to_buf(out, dst, need);
        out->underrun_hold = dst[need - 1];
        return;
    }
    if(avail > 0) {
        ring_read_to_buf(out, dst, avail);
        int16_t pad = dst[avail - 1];
        for(uint32_t i = avail; i < need; i++) {
            dst[i] = pad;
        }
        out->underrun_hold = pad;
        return;
    }
    /* Ring empty: hold last sample instead of silence to reduce zipper/flutter. */
    int16_t pad = out->underrun_hold;
    for(uint32_t i = 0; i < need; i++) {
        dst[i] = pad;
    }
}

PcmOutput* pcm_output_alloc(void) {
    PcmOutput* out = malloc(sizeof(PcmOutput));
    if(!out) return NULL;
    memset(out, 0, sizeof(PcmOutput));
    return out;
}

void pcm_output_free(PcmOutput* out) {
    if(!out) return;
    pcm_output_stop(out);
    free(out);
}

void pcm_output_start(PcmOutput* out) {
    if(!out) return;

    /* Do not call audio_enable() here: it arms the Audio service 100ms play holdoff
     * (see audio.h). After audio_stop() the queue is empty; when the holdoff fires,
     * vanilla audio_do_load_queued_file() still reconfigures SAI before checking the
     * path — that races Internet radio PCM (lib/toolbox/radio_stream). We only need
     * to stop any .snd playback and enable the headphone amp; refcounting for JS apps
     * stays in js_audio (audio_enable/disable on app lifecycle). */
    Audio* audio = furi_record_open(RECORD_AUDIO);
    audio_stop(audio);
    furi_record_close(RECORD_AUDIO);

    furi_hal_sai_enable_amplifier();

    furi_delay_ms(100);

    out->pcm_wr = 0;
    out->pcm_rd = 0;
    out->sai_started = false;
    out->prefilled = false;
    out->underrun_hold = 0;
    memset(out->dma_buffer, 0, sizeof(out->dma_buffer));

    FURI_LOG_I(TAG, "Started");
}

void pcm_output_stop(PcmOutput* out) {
    if(!out) return;
    if(out->sai_started) {
        furi_hal_sai_stop();
        furi_hal_sai_set_callback(NULL, NULL);
        out->sai_started = false;
        FURI_LOG_I(TAG, "SAI stopped");
    }
    /* Paired with pcm_output_start: we did not use audio_enable(), so no audio_disable(). */
    furi_hal_sai_disable_amplifier();

    out->prefilled = false;
}

static void pcm_output_try_start_sai(PcmOutput* out) {
    if(out->sai_started || out->prefilled) return;
    if(ring_available(out) < PCM_PREFILL_SAMPLES) return;

    out->prefilled = true;

    furi_hal_sai_stop();
    furi_hal_sai_set_buffer(out->dma_buffer, PCM_DMA_BUFFER_SAMPLES);
    furi_hal_sai_set_callback(pcm_sai_callback, out);
    memset(out->dma_buffer, 0, sizeof(out->dma_buffer));

    int16_t* half1 = out->dma_buffer;
    int16_t* half2 = &out->dma_buffer[PCM_DMA_HALF];

    uint32_t a = ring_available(out);
    if(a >= PCM_DMA_BUFFER_SAMPLES) {
        ring_read_to_buf(out, half1, PCM_DMA_HALF);
        ring_read_to_buf(out, half2, PCM_DMA_HALF);
    } else if(a >= PCM_DMA_HALF) {
        ring_read_to_buf(out, half1, PCM_DMA_HALF);
    }

    furi_hal_sai_start();
    out->sai_started = true;
    FURI_LOG_I(TAG, "SAI started");
}

size_t pcm_output_write(PcmOutput* out, const int16_t* samples, size_t count) {
    uint32_t free_space = ring_free_space(out);
    if(count > free_space) count = free_space;
    if(count == 0) return 0;

    ring_write(out, samples, count);
    pcm_output_try_start_sai(out);
    return count;
}

size_t pcm_output_free_space(PcmOutput* out) {
    return ring_free_space(out);
}

size_t pcm_output_available(PcmOutput* out) {
    return ring_available(out);
}

bool pcm_output_is_active(PcmOutput* out) {
    return out && out->sai_started;
}
