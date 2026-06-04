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

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
