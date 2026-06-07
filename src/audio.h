/*
 * PureRetro — Audio subsystem
 *
 * SDL3 audio stream playback.
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the audio subsystem with the given sample rate. */
bool audio_init(double sample_rate);

/* Shutdown the audio subsystem. */
void audio_shutdown(void);

/* Push audio samples to the SDL audio stream.
 * 'data' is an array of interleaved int16_t stereo samples.
 * 'frames' is the number of stereo frames (pairs of samples).
 * Returns the number of frames actually queued. */
size_t audio_push(const int16_t *data, size_t frames);

/* Register (or unregister with NULL) the core's audio buffer status
 * callback. Stored callbacks are invoked from audio_notify_buffer_status. */
void audio_set_buffer_status_callback(retro_audio_buffer_status_callback_t cb);

/* Request a minimum tolerable audio queue depth, in milliseconds. The
 * frontend uses this only to raise its internal queue cap; SDL's hardware
 * latency is not reinitialized (see audio.c for rationale). 0 resets to
 * the default. */
void audio_set_minimum_latency(unsigned ms);

/* If a buffer status callback is registered, compute the current queue
 * occupancy (0-100%) and invoke the callback. Safe to call every frame
 * just before retro_run(). */
void audio_notify_buffer_status(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
