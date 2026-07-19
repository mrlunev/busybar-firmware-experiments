#include "mp3_decoder.h"

#include <furi.h>
#include <stdlib.h>
#include <string.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include <minimp3/minimp3.h>

#define TAG "Mp3Dec"

/* Extra headroom for bursty TCP; keeps decoder fed during recv/decode gaps (FM Radio). */
#define MP3_BUF_SIZE 16384
#define MP3_OUTPUT_RATE 44100

struct Mp3Decoder {
    uint8_t mp3_buf[MP3_BUF_SIZE];
    uint32_t mp3_buf_fill;

    mp3dec_t mp3dec;
    int16_t decode_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int16_t mono_buf[1152];

    uint32_t src_rate;
    bool format_logged;

    /* Resampler state (persists across frames for smooth joins) */
    uint32_t resample_phase; /* fixed-point 16.16 accumulator */
    int16_t prev_sample;     /* last sample of previous frame */
};

Mp3Decoder* mp3_decoder_alloc(void) {
    Mp3Decoder* dec = malloc(sizeof(Mp3Decoder));
    if(!dec) return NULL;
    memset(dec, 0, sizeof(Mp3Decoder));
    mp3dec_init(&dec->mp3dec);
    return dec;
}

void mp3_decoder_free(Mp3Decoder* dec) {
    if(dec) free(dec);
}

void mp3_decoder_reset(Mp3Decoder* dec) {
    if(!dec) return;
    dec->mp3_buf_fill = 0;
    dec->src_rate = 0;
    dec->format_logged = false;
    dec->resample_phase = 0;
    dec->prev_sample = 0;
    mp3dec_init(&dec->mp3dec);
}

size_t mp3_decoder_space(Mp3Decoder* dec) {
    return MP3_BUF_SIZE - dec->mp3_buf_fill;
}

void mp3_decoder_feed(Mp3Decoder* dec, const uint8_t* data, size_t size) {
    size_t space = MP3_BUF_SIZE - dec->mp3_buf_fill;
    if(size > space) size = space;
    if(size == 0) return;
    memcpy(dec->mp3_buf + dec->mp3_buf_fill, data, size);
    dec->mp3_buf_fill += size;
}

size_t mp3_decoder_buffered(Mp3Decoder* dec) {
    return dec->mp3_buf_fill;
}

/**
 * Resample mono_in (src_rate) -> out_buf (44100 Hz) using linear interpolation.
 * Uses dec->resample_phase and dec->prev_sample for cross-frame continuity.
 * Returns number of output samples written.
 */
static size_t mp3_resample(
    Mp3Decoder* dec,
    const int16_t* mono_in,
    size_t in_count,
    int16_t* out_buf,
    size_t out_capacity) {
    if(dec->src_rate == MP3_OUTPUT_RATE) {
        size_t n = (in_count < out_capacity) ? in_count : out_capacity;
        memcpy(out_buf, mono_in, n * sizeof(int16_t));
        if(in_count > 0) dec->prev_sample = mono_in[in_count - 1];
        return n;
    }

    /* Fixed-point 16.16 step: how much to advance in source per output sample */
    uint32_t step = ((uint64_t)dec->src_rate << 16) / MP3_OUTPUT_RATE;
    uint32_t phase = dec->resample_phase;
    size_t out_idx = 0;

    while(out_idx < out_capacity) {
        uint32_t src_pos = phase >> 16;
        if(src_pos >= in_count) break;

        uint16_t frac = phase & 0xFFFF;
        int16_t s0 = (src_pos == 0) ? dec->prev_sample : mono_in[src_pos - 1];
        int16_t s1 = mono_in[src_pos];

        out_buf[out_idx++] = (int16_t)(s0 + (((int32_t)(s1 - s0) * frac) >> 16));
        phase += step;
    }

    /* Save fractional remainder for next call (subtract consumed input) */
    dec->resample_phase = phase - ((uint32_t)in_count << 16);
    if(in_count > 0) dec->prev_sample = mono_in[in_count - 1];

    return out_idx;
}

static size_t mp3_decoder_decode_impl(
    Mp3Decoder* dec,
    int16_t* out_buf,
    size_t out_buf_capacity,
    bool drain) {
    if(dec->mp3_buf_fill < 4) return 0;
    /* minimp3 needs several consecutive frames for reliable synchronization.
     * Decoding every short TCP fragment can make it discard a partial frame as
     * invalid data and prevents a starved live stream from ever recovering. */
    if(!drain && dec->mp3_buf_fill < MP3_BUF_SIZE) return 0;

    mp3dec_frame_info_t info = {0};
    int samples = mp3dec_decode_frame(
        &dec->mp3dec, dec->mp3_buf, dec->mp3_buf_fill, dec->decode_pcm, &info);

    if(info.frame_bytes == 0) return 0;

    if((uint32_t)info.frame_bytes <= dec->mp3_buf_fill) {
        memmove(dec->mp3_buf, dec->mp3_buf + info.frame_bytes,
                dec->mp3_buf_fill - info.frame_bytes);
        dec->mp3_buf_fill -= info.frame_bytes;
    }

    if(samples <= 0) return 0;

    if(!dec->format_logged && info.hz > 0) {
        dec->src_rate = info.hz;
        dec->format_logged = true;
        FURI_LOG_I(TAG, "MP3: %d Hz, %d ch, %d kbps", info.hz, info.channels, info.bitrate_kbps);
    }
    if(info.hz > 0 && (uint32_t)info.hz != dec->src_rate) {
        dec->src_rate = info.hz;
    }

    /* Stereo -> mono */
    int mono_count = samples;
    int16_t* mono_ptr;
    if(info.channels == 2) {
        for(int i = 0; i < samples; i++) {
            dec->mono_buf[i] = (int16_t)(
                ((int32_t)dec->decode_pcm[i * 2] + dec->decode_pcm[i * 2 + 1]) / 2);
        }
        mono_ptr = dec->mono_buf;
    } else {
        mono_ptr = dec->decode_pcm;
    }

    /* Resample to 44100 Hz */
    if(dec->src_rate == 0) dec->src_rate = MP3_OUTPUT_RATE;
    return mp3_resample(dec, mono_ptr, mono_count, out_buf, out_buf_capacity);
}

size_t mp3_decoder_decode(Mp3Decoder* dec, int16_t* out_buf, size_t out_buf_capacity) {
    return mp3_decoder_decode_impl(dec, out_buf, out_buf_capacity, false);
}

size_t mp3_decoder_drain(Mp3Decoder* dec, int16_t* out_buf, size_t out_buf_capacity) {
    return mp3_decoder_decode_impl(dec, out_buf, out_buf_capacity, true);
}
