/*
 * PureRetro — Runtime audio/video/input environment callbacks
 */

#include "core_internal.h"

#include "audio.h"
#include "core.h"
#include "frontend.h"
#include "log.h"
#include "video.h"

#include <SDL3/SDL.h>

#include <stdint.h>


bool env_set_rotation(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_rotation: NULL data"); return false; }
    unsigned rot = *(const unsigned *)data;
    if (rot > 3) {
        LOG_WARN("SET_ROTATION: ignoring invalid rotation %u (expected 0-3)",
                 rot);
        return false;
    }
    fe->video.rotation = rot;
    LOG_INFO("Core requested rotation: %u (%u degrees CCW)",
             rot, rot * 90);
    return true;
}


bool env_set_pixel_format(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_pixel_format: NULL data"); return false; }
    enum retro_pixel_format fmt = *(const enum retro_pixel_format *)data;
    const char *fmt_name = "unknown";
    switch (fmt) {
    case RETRO_PIXEL_FORMAT_0RGB1555: fmt_name = "0RGB1555"; break;
    case RETRO_PIXEL_FORMAT_XRGB8888: fmt_name = "XRGB8888"; break;
    case RETRO_PIXEL_FORMAT_RGB565:   fmt_name = "RGB565";   break;
    case RETRO_PIXEL_FORMAT_UNKNOWN:  fmt_name = "UNKNOWN";  break;
    }
    LOG_INFO("Core requested pixel format: %s (%d)", fmt_name, (int)fmt);
    /* Accept all formats. The software renderer will handle whatever the
     * core sends. Returning false here causes cores like Beetle PSX HW
     * to skip SET_HW_RENDER entirely and fall back to software. */
    fe->video.pixel_format = fmt;
    return true;
}


bool env_get_preferred_hw_render(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_preferred_hw_render: NULL data"); return false; }
    int *preferred = (int *)data;
    bool result;
    switch (fe->preferred_renderer) {
    case VIDEO_RENDERER_VULKAN: *preferred = RETRO_HW_CONTEXT_VULKAN;     result = true; break;
    case VIDEO_RENDERER_OPENGL: *preferred = RETRO_HW_CONTEXT_OPENGL_CORE; result = true; break;
    case VIDEO_RENDERER_SW:     *preferred = RETRO_HW_CONTEXT_NONE;       result = true; break;
    case VIDEO_RENDERER_NONE:
    default:                    result = false; break;
    }
    LOG_DEBUG("Core queried preferred HW render: %s (context=%d)",
              result ? "yes" : "no (no preference)", *preferred);
    return result;
}


bool env_set_frame_time_callback(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_frame_time_callback: NULL data"); return false; }
    const struct retro_frame_time_callback *cb =
        (const struct retro_frame_time_callback *)data;
    fe->frame_time_callback = cb->callback;
    fe->frame_time_reference = cb->reference;
    LOG_INFO("Core registered frame-time callback (reference=%lld us)",
             (long long)cb->reference);
    return true;
}


bool env_get_input_bitmasks(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_input_bitmasks: NULL data"); return false; }
    *(bool *)data = true;
    return true;
}


bool env_get_input_device_capabilities(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_input_device_capabilities: NULL data"); return false; }
    *(uint64_t *)data = (1 << RETRO_DEVICE_JOYPAD);
    return true;
}


bool env_set_geometry(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_set_geometry: NULL data"); return false; }
    const struct retro_game_geometry *geo =
        (const struct retro_game_geometry *)data;
    video_update_geometry(geo->base_width, geo->base_height,
                          geo->max_width, geo->max_height,
                          geo->aspect_ratio);
    return true;
}


bool env_set_system_av_info(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_system_av_info: NULL data"); return false; }
    const struct retro_system_av_info *av =
        (const struct retro_system_av_info *)data;
    g_av_info = *av;

    if (fe->video.hw_render_enabled &&
        av->geometry.max_width > 0 && av->geometry.max_height > 0) {
        video_resize(av->geometry.max_width, av->geometry.max_height);
    }
    return true;
}


bool env_get_input_max_users(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_input_max_users: NULL data"); return false; }
    /* The frontend currently maps a single keyboard to port 0 only.
     * Cores can use this to skip polling ports 1..N. */
    *(unsigned *)data = 1;
    return true;
}


bool env_get_audio_video_enable(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_audio_video_enable: NULL data"); return false; }
    /* libretro spec: bit0 = RETRO_AV_ENABLE_VIDEO (always on),
     * bit1 = RETRO_AV_ENABLE_AUDIO (gated by --no-audio),
     * bit2 = RETRO_AV_ENABLE_FAST_SAVESTATES (not used),
     * bit3 = RETRO_AV_ENABLE_HARD_DISABLE_AUDIO (never set).
     * Fast-forward state is reported via GET_FASTFORWARDING, not here. */
    *(int *)data = RETRO_AV_ENABLE_VIDEO
                   | (fe->no_audio ? 0 : RETRO_AV_ENABLE_AUDIO);
    return true;
}


bool env_get_target_sample_rate(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_target_sample_rate: NULL data"); return false; }

    /* libretro expects an unsigned Hz value here. Do not mirror
     * GET_TARGET_REFRESH_RATE's float type: cores such as mGBA consume this
     * during AV-info setup, and writing 48000.0f would be read back as the
     * integer bit pattern 0x473b8000 (1195081728 Hz). */
    unsigned rate = (g_av_info.timing.sample_rate > 0.0)
                    ? (unsigned)(g_av_info.timing.sample_rate + 0.5)
                    : FRONTEND_AUDIO_SAMPLE_RATE;
    *(unsigned *)data = rate;
    return true;
}


bool env_set_audio_buffer_status_callback(struct frontend_state *fe, void *data)
{
    (void)fe;
    /* data may be NULL to clear the callback; the environment call
     * itself is still considered supported. */
    const struct retro_audio_buffer_status_callback *cb =
        (const struct retro_audio_buffer_status_callback *)data;
    audio_set_buffer_status_callback(cb ? cb->callback : NULL);
    LOG_INFO("Core %s audio buffer status callback",
             cb && cb->callback ? "registered" : "unregistered");
    return true;
}


bool env_set_minimum_audio_latency(struct frontend_state *fe, void *data)
{
    (void)fe;
    unsigned ms = (data) ? *(const unsigned *)data : 0;
    audio_set_minimum_latency(ms);
    LOG_INFO("Core requested minimum audio latency: %u ms", ms);
    return true;
}


bool env_get_fastforwarding(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_fastforwarding: NULL data"); return false; }
    *(bool *)data = fe->fast_forward_active;
    return true;
}


bool env_get_target_refresh_rate(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_target_refresh_rate: NULL data"); return false; }
    float rate = 60.0f;
    SDL_Window *win = fe->video.window;
    if (win) {
        SDL_DisplayID disp = SDL_GetDisplayForWindow(win);
        if (disp) {
            const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(disp);
            if (mode && mode->refresh_rate > 0.0f)
                rate = mode->refresh_rate;
        }
    }
    *(float *)data = rate;
    return true;
}


bool env_set_hw_render_context_negotiation_interface(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_set_hw_render_context_negotiation_interface: NULL data"); return false; }
    return video_negotiate_hw_context(
        (const struct retro_hw_render_context_negotiation_interface *)data);
}


bool env_get_hw_render_interface(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_hw_render_interface: NULL data"); return false; }
    const struct retro_hw_render_interface **iface =
        (const struct retro_hw_render_interface **)data;
    if (!video_get_hw_render_interface(iface)) {
        LOG_WARN("GET_HW_RENDER_INTERFACE: no interface for active backend");
        return false;
    }
    LOG_INFO("GET_HW_RENDER_INTERFACE: returning %p",
             (const void *)*iface);
    return true;
}


bool env_get_hw_render_context_negotiation_interface_support(struct frontend_state *fe, void *data)
{
    (void)fe;
    /* Cores poll this to discover which negotiation interface versions
     * the frontend understands. We currently support the Vulkan
     * negotiation interface (handled in video_negotiate_hw_context),
     * which is the only enum value defined upstream. Return the highest
     * interface_version the frontend recognises; other API types get 0. */
    if (!data) { LOG_WARN("env_get_hw_render_context_negotiation_interface_support: NULL data"); return false; }
    struct retro_hw_render_context_negotiation_interface *iface =
        (struct retro_hw_render_context_negotiation_interface *)data;
    if (iface->interface_type ==
        RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN) {
        iface->interface_version = 2;
    } else {
        iface->interface_version = 0;
    }
    return true;
}


bool env_get_throttle_state(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_throttle_state: NULL data"); return false; }
    struct retro_throttle_state *ts = (struct retro_throttle_state *)data;
    if (fe->fast_forward_active) {
        ts->mode = RETRO_THROTTLE_FAST_FORWARD;
        ts->rate = 0.0f; /* unlimited */
    } else {
        ts->mode = RETRO_THROTTLE_NONE;
        ts->rate = (g_av_info.timing.fps > 0.0)
                   ? (float)g_av_info.timing.fps : 0.0f;
    }
    return true;
}


bool env_set_fastforwarding_override(struct frontend_state *fe, void *data)
{
    /* NULL data is a support probe per the libretro contract. */
    if (!data) {
        LOG_DEBUG("Core probed SET_FASTFORWARDING_OVERRIDE support");
        return true;
    }
    const struct retro_fastforwarding_override *ov =
        (const struct retro_fastforwarding_override *)data;
    fe->ff_override_active = ov->fastforward;
    fe->ff_inhibit_toggle  = ov->inhibit_toggle;
    fe->fast_forward_active = ov->fastforward;
    LOG_INFO("Core fast-forward override: active=%s ratio=%.2f "
             "notification=%s inhibit_toggle=%s",
             ov->fastforward ? "yes" : "no",
             ov->ratio,
             ov->notification ? "yes" : "no",
             ov->inhibit_toggle ? "yes" : "no");
    return true;
}


bool env_get_current_software_framebuffer(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_current_software_framebuffer: NULL data"); return false; }
    struct retro_framebuffer *fb = (struct retro_framebuffer *)data;
    if (fb->width == 0 || fb->height == 0)
        return false;

    /* Only honour the request when the core wants to write the buffer.
     * Read-only access would require a different mapping (we never
     * read back from the texture), so decline cleanly in that case. */
    if (!(fb->access_flags & RETRO_MEMORY_ACCESS_WRITE))
        return false;

    void *pixels = NULL;
    size_t pitch = 0;
    if (!video_get_software_framebuffer(fb->width, fb->height,
                                        fe->video.pixel_format,
                                        &pixels, &pitch))
        return false;

    fb->data = pixels;
    fb->pitch = pitch;
    fb->format = fe->video.pixel_format;
    fb->memory_flags = 0;
    return true;
}


bool env_set_hw_render(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_set_hw_render: NULL data"); return false; }
    return video_set_hw_render((struct retro_hw_render_callback *)data);
}

