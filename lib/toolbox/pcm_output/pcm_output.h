#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PcmOutput PcmOutput;

PcmOutput* pcm_output_alloc(void);
void pcm_output_free(PcmOutput* out);

/** Start output (stops audio service, configures SAI) */
void pcm_output_start(PcmOutput* out);

/** Stop output (disables SAI) */
void pcm_output_stop(PcmOutput* out);

/** Write samples to ring buffer. Returns number of samples written. */
size_t pcm_output_write(PcmOutput* out, const int16_t* samples, size_t count);

/** Free space in ring buffer (samples) */
size_t pcm_output_free_space(PcmOutput* out);

/** Data available in ring buffer (samples) */
size_t pcm_output_available(PcmOutput* out);

/** Is SAI actively playing? */
bool pcm_output_is_active(PcmOutput* out);

#ifdef __cplusplus
}
#endif
