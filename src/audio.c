/*
 * PureRetro - Audio subsystem
 *
 * SDL3 audio stream for core audio output.
 */

#include "audio.h"
#include "frontend.h"
#include "log.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    /* Clamp to a sane range before handing the value to SDL. Some cores
     * report absurd rates (uninitialized memory, or a value they meant to
     * express in a different unit); the drivers below us will reject those
     * outright. 4 kHz..384 kHz covers every real device and every rate a
     * libretro core has ever plausibly asked for. Outside this window we
     * fall back to 48 kHz and let SDL's built-in resampler adapt. */
    if (sample_rate < 4000.0 || sample_rate > 384000.0) {
        LOG_WARN("audio_init: implausible sample rate %.0f Hz; using 48000 Hz",
                 sample_rate);
        sample_rate = 48000.0;
    }

    memset(&spec, 0, sizeof(spec));
    spec.freq = (int)sample_rate;
    spec.format = SDL_AUDIO_S16;
    spec.channels = FRONTEND_AUDIO_CHANNELS;

    g_frontend.audio_stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                  &spec, NULL, NULL);

    /* Some audio backends (notably WASAPI on Windows) refuse unusual
     * sample rates that libretro cores report (e.g. 32768 Hz for GBA,
     * 262144 Hz for the SNES SPC-700). SDL3 resamples transparently
     * once the stream is open, so falling back to a mainstream rate
     * still lets the core push its native-rate audio via SDL's
     * resampler; we just hand the device a rate the driver accepts.
     * Retry once at 48000 Hz before giving up. */
    if (!g_frontend.audio_stream && (int)sample_rate != 48000) {
        LOG_WARN("SDL_OpenAudioDeviceStream failed at %d Hz: %s; retrying at 48000 Hz",
                 (int)sample_rate, SDL_GetError());
        spec.freq = 48000;
        g_frontend.audio_stream =
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                      &spec, NULL, NULL);
        if (g_frontend.audio_stream)
            sample_rate = 48000.0;
    }

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
    LOG_INFO("Audio initialized at %d Hz", g_sample_rate);
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

    /* During fast-forward the core runs many frames per wall-clock second,
     * so pushing audio at native rate would either pitch-shift or pile up
     * latency. Drop samples and report them as consumed. */
    if (g_frontend.fast_forward_active)
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
        /* SDL_GetAudioStreamQueued only reports bytes still in the SDL
         * stream — it cannot see what the audio device has already pulled
         * into its hardware/driver ring buffer (typically 80-200ms on
         * desktop drivers). A near-empty SDL queue therefore does NOT
         * necessarily mean playback is about to underrun.
         *
         * To avoid false-positives that make cores frame-skip unnecessarily,
         * only flag underrun when the SDL queue is essentially drained
         * (under ~5% of cap). Cores that listen to this still get a useful
         * signal during real stalls, but ordinary steady-state playback
         * (where the device buffer absorbs the slack) no longer trips it. */
        underrun_likely = (occupancy < 5);
    }

    g_buffer_status_cb(active, occupancy, underrun_likely);
}
