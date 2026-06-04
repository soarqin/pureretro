/*
 * PureRetro — Audio subsystem
 *
 * SDL3 audio stream for core audio output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "audio.h"
#include "frontend.h"

bool audio_init(double sample_rate)
{
    SDL_AudioSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.freq = (int)sample_rate;
    spec.format = SDL_AUDIO_S16;
    spec.channels = FRONTEND_AUDIO_CHANNELS;

    g_frontend.audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                         &spec, NULL, NULL);
    if (!g_frontend.audio_stream) {
        fprintf(stderr, "SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
        return false;
    }

    if (!SDL_ResumeAudioStreamDevice(g_frontend.audio_stream)) {
        fprintf(stderr, "SDL_ResumeAudioStreamDevice failed: %s\n", SDL_GetError());
        SDL_DestroyAudioStream(g_frontend.audio_stream);
        g_frontend.audio_stream = NULL;
        return false;
    }

    return true;
}

void audio_shutdown(void)
{
    if (g_frontend.audio_stream) {
        SDL_DestroyAudioStream(g_frontend.audio_stream);
        g_frontend.audio_stream = NULL;
    }
}

size_t audio_push(const int16_t *data, size_t frames)
{
    if (!g_frontend.audio_stream || !data)
        return 0;

    size_t bytes = frames * FRONTEND_AUDIO_CHANNELS * sizeof(int16_t);
    if (!SDL_PutAudioStreamData(g_frontend.audio_stream, data, (int)bytes)) {
        fprintf(stderr, "SDL_PutAudioStreamData failed: %s\n", SDL_GetError());
        return 0;
    }

    return frames;
}
