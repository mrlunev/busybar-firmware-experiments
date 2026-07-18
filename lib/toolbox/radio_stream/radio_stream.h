#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RadioStream RadioStream;

RadioStream* radio_stream_alloc(void);
void radio_stream_free(RadioStream* instance);

bool radio_stream_play(RadioStream* instance, const char* url);
bool radio_stream_play_file(RadioStream* instance, const char* path);
void radio_stream_stop(RadioStream* instance);
bool radio_stream_is_playing(RadioStream* instance);
void radio_stream_set_volume(RadioStream* instance, float volume);
const char* radio_stream_get_title(RadioStream* instance);
const char* radio_stream_get_stream_name(RadioStream* instance);

#ifdef __cplusplus
}
#endif
