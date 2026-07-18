#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Max output samples per decode call (accounts for 8kHz->44.1kHz upsampling) */
#define MP3_DECODER_MAX_OUTPUT_SAMPLES 6400

typedef struct Mp3Decoder Mp3Decoder;

Mp3Decoder* mp3_decoder_alloc(void);
void mp3_decoder_free(Mp3Decoder* dec);
void mp3_decoder_reset(Mp3Decoder* dec);

/** How many compressed bytes can be fed */
size_t mp3_decoder_space(Mp3Decoder* dec);

/** Feed compressed MP3 data into internal buffer */
void mp3_decoder_feed(Mp3Decoder* dec, const uint8_t* data, size_t size);

/**
 * Decode one MP3 frame into out_buf (44100 Hz, mono, 16-bit).
 * Returns number of output samples (0 if nothing to decode).
 * out_buf must hold at least MP3_DECODER_MAX_OUTPUT_SAMPLES elements.
 */
size_t mp3_decoder_decode(Mp3Decoder* dec, int16_t* out_buf, size_t out_buf_capacity);

/** How many compressed bytes are buffered */
size_t mp3_decoder_buffered(Mp3Decoder* dec);

#ifdef __cplusplus
}
#endif
