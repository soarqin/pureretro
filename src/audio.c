/*
 * PureRetro - Audio subsystem
 *
 * SDL3 audio stream for core audio output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "audio.h"
#include "frontend.h"
#include "log.h"

/* Default queue cap: ~1 second of stereo s16 audio. Tuned at audio_init
 * once we know the real sample rate; can be raised by the core via
 * SET_MINIMUM_AUDIO_LATENCY. */
static int g_max_queued_bytes = 44100 * 2 * (int)sizeof(int16_t);
static int g_sample_rate = 0;
static unsigned g_min_latency_ms = 0;

static retro_audio_buffer_status_callback_t g_buffer_status_cb = NULL;

static int compute_max_queued_bytes(int sample_rate, unsigned latency_ms)
{
    /* Floor the cap at FRONTEND_AUDIO_BUFFER_MS so we never starve, and
     * honour the core's latency request only when it exceeds that. */
    unsigned ms = latency_ms > FRONTEND_AUDIO_BUFFER_MS
                  ? latency_ms : FRONTEND_AUDIO_BUFFER_MS;
    /* Allow at least ~250ms of headroom over the requested latency so we
     * have room to absorb bursty cores without immediately clipping. */
    if (ms < 250)
        ms = 250;
    long long bytes = (long long)sample_rate * FRONTEND_AUDIO_CHANNELS
                    * (long long)sizeof(int16_t) * (long long)ms / 1000;
    if (bytes < 4096)
        bytes = 4096;
    return (int)bytes;
}

bool audio_init(double sample_rate)
{
    SDL_AudioSpec spec;

    if (!(sample_rate > 0.0)) {
        LOG_ERROR("audio_init: invalid sample rate %f", sample_rate);
        return false;
    }

    memset(&spec, 0, sizeof(spec));
    spec.freq = (int)sample_rate;
    spec.format = SDL_AUDIO_S16;
    spec.channels = FRONTEND_AUDIO_CHANNELS;

    g_frontend.audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                         &spec, NULL, NULL);
    if (!g_frontend.audio_stream) {
        LOG_ERROR("SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        return false;
    }

    if (!SDL_ResumeAudioStreamDevice(g_frontend.audio_stream)) {
        LOG_ERROR("SDL_ResumeAudioStreamDevice failed: %s", SDL_GetError());
        SDL_DestroyAudioStream(g_frontend.audio_stream);
        g_frontend.audio_stream = NULL;
        return false;
    }

    g_sample_rate = (int)sample_rate;
    g_max_queued_bytes = compute_max_queued_bytes(g_sample_rate, g_min_latency_ms);
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
    if (!data)
        return 0;

    /* When audio is disabled (e.g. --no-audio), pretend we consumed every
     * frame. Returning 0 makes libretro cores treat the sink as a stalled
     * buffer and busy-loop trying to push the same samples again. */
    if (!g_frontend.audio_stream)
        return frames;

    /* Keep the SDL audio queue bounded to avoid multi-second latency.
     *
     * Some cores (notably PPSSPP on the Vulkan backend) can produce
     * multiple PSP frames of audio per retro_run() call when the GPU
     * FramebufferDirty() flag does not trigger Core_NextFrame in
     * time. On audio backends with large internal buffers (e.g.
     * PulseAudio on Linux), the queue can also accumulate extra
     * latency. When the queue grows past the cap, clear the stale
     * backlog and continue pushing fresh data so playback never
     * stalls. The trade-off is an audible click on each skip. */
    int queued_bytes = SDL_GetAudioStreamQueued(g_frontend.audio_stream);
    if (queued_bytes > g_max_queued_bytes)
        SDL_ClearAudioStream(g_frontend.audio_stream);

    size_t bytes = frames * FRONTEND_AUDIO_CHANNELS * sizeof(int16_t);
    if (!SDL_PutAudioStreamData(g_frontend.audio_stream, data, (int)bytes)) {
        LOG_ERROR("SDL_PutAudioStreamData failed: %s", SDL_GetError());
        return 0;
    }

    return frames;
}

void audio_set_buffer_status_callback(retro_audio_buffer_status_callback_t cb)
{
    g_buffer_status_cb = cb;
}

void audio_set_minimum_latency(unsigned ms)
{
    g_min_latency_ms = ms;
    if (g_sample_rate > 0)
        g_max_queued_bytes = compute_max_queued_bytes(g_sample_rate, ms);
    /* We deliberately do NOT tear down and reopen the SDL audio stream
     * to apply a smaller hardware latency: SDL's audio queue is already
     * bounded by g_max_queued_bytes (the only knob a minimal frontend
     * really has), and reinitializing the device mid-frame would risk
     * dropping samples or stalling cores that submit audio during
     * retro_run. The hint is honoured at the queue-depth level only. */
}

void audio_notify_buffer_status(void)
{
    if (!g_buffer_status_cb)
        return;

    bool active = (g_frontend.audio_stream != NULL) && !g_frontend.no_audio;
    unsigned occupancy = 0;
    bool underrun_likely = false;

    if (active && g_max_queued_bytes > 0) {
        int queued = SDL_GetAudioStreamQueued(g_frontend.audio_stream);
        if (queued < 0)
            queued = 0;
        long long pct = (long long)queued * 100 / (long long)g_max_queued_bytes;
        if (pct > 100)
            pct = 100;
        occupancy = (unsigned)pct;
        /* Conservative threshold: less than ~20% buffered means the next
         * frame is at risk of underrunning. Cores that listen to this
         * typically frame-skip when triggered. */
        underrun_likely = (occupancy < 20);
    }

    g_buffer_status_cb(active, occupancy, underrun_likely);
}
