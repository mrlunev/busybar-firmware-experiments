#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RadioStream RadioStream;

typedef struct {
    bool playing;
    bool output_active;
    bool buffering;
    bool has_data;
    bool has_audio;
    uint32_t data_age_ms;
    uint32_t audio_age_ms;
    uint32_t underrun_count;
    uint32_t pcm_samples;
    uint32_t decoder_bytes;
} RadioStreamStats;

RadioStream* radio_stream_alloc(void);
void radio_stream_free(RadioStream* instance);

bool radio_stream_play(RadioStream* instance, const char* url);
bool radio_stream_play_file(RadioStream* instance, const char* path);
void radio_stream_stop(RadioStream* instance);
bool radio_stream_is_playing(RadioStream* instance);
void radio_stream_get_stats(RadioStream* instance, RadioStreamStats* stats);
void radio_stream_set_volume(RadioStream* instance, float volume);
const char* radio_stream_get_title(RadioStream* instance);
const char* radio_stream_get_stream_name(RadioStream* instance);

#ifdef __cplusplus
}
#endif
