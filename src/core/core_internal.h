/*
 * PureRetro — Private core module helpers
 */

#ifndef CORE_INTERNAL_H
#define CORE_INTERNAL_H

#include "frontend.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void core_controller_ports_clear(struct frontend_state *fe);
void core_subsystem_info_clear(struct frontend_state *fe);
void core_memory_maps_clear(struct frontend_state *fe);
void core_content_overrides_clear(struct frontend_state *fe);

bool env_get_can_dupe(struct frontend_state *fe, void *data);
bool env_get_overscan(struct frontend_state *fe, void *data);
bool env_shutdown(struct frontend_state *fe, void *data);
bool env_set_message(struct frontend_state *fe, void *data);
bool env_set_performance_level(struct frontend_state *fe, void *data);
bool env_get_system_directory(struct frontend_state *fe, void *data);
bool env_get_libretro_path(struct frontend_state *fe, void *data);
bool env_get_core_assets_directory(struct frontend_state *fe, void *data);
bool env_get_playlist_directory(struct frontend_state *fe, void *data);
bool env_get_file_browser_start_directory(struct frontend_state *fe, void *data);
bool env_set_support_no_game(struct frontend_state *fe, void *data);
bool env_set_input_descriptors(struct frontend_state *fe, void *data);
bool env_set_rotation(struct frontend_state *fe, void *data);
bool env_set_pixel_format(struct frontend_state *fe, void *data);
bool env_set_keyboard_callback(struct frontend_state *fe, void *data);
bool env_set_disk_control_interface(struct frontend_state *fe, void *data);
bool env_get_preferred_hw_render(struct frontend_state *fe, void *data);
bool env_get_variable_update(struct frontend_state *fe, void *data);
bool env_set_frame_time_callback(struct frontend_state *fe, void *data);
bool env_set_audio_callback(struct frontend_state *fe, void *data);
bool env_get_rumble_interface(struct frontend_state *fe, void *data);
bool env_get_input_bitmasks(struct frontend_state *fe, void *data);
bool env_get_input_device_capabilities(struct frontend_state *fe, void *data);
bool env_get_sensor_interface(struct frontend_state *fe, void *data);
bool env_get_camera_interface(struct frontend_state *fe, void *data);
bool env_get_log_interface(struct frontend_state *fe, void *data);
bool env_get_perf_interface(struct frontend_state *fe, void *data);
bool env_get_location_interface(struct frontend_state *fe, void *data);
bool env_get_save_directory(struct frontend_state *fe, void *data);
bool env_set_geometry(struct frontend_state *fe, void *data);
bool env_set_system_av_info(struct frontend_state *fe, void *data);
bool env_get_language(struct frontend_state *fe, void *data);
bool env_get_username(struct frontend_state *fe, void *data);
bool env_get_disk_control_interface_version(struct frontend_state *fe, void *data);
bool env_get_input_max_users(struct frontend_state *fe, void *data);
bool env_get_audio_video_enable(struct frontend_state *fe, void *data);
bool env_get_target_sample_rate(struct frontend_state *fe, void *data);
bool env_set_audio_buffer_status_callback(struct frontend_state *fe, void *data);
bool env_set_minimum_audio_latency(struct frontend_state *fe, void *data);
bool env_get_fastforwarding(struct frontend_state *fe, void *data);
bool env_get_target_refresh_rate(struct frontend_state *fe, void *data);
bool env_set_variable(struct frontend_state *fe, void *data);
bool env_set_hw_render_context_negotiation_interface(struct frontend_state *fe, void *data);
bool env_get_hw_render_interface(struct frontend_state *fe, void *data);
bool env_get_vfs_interface(struct frontend_state *fe, void *data);
bool env_get_core_options_version(struct frontend_state *fe, void *data);
bool env_set_core_options_update_display_callback(struct frontend_state *fe, void *data);
bool env_get_hw_render_context_negotiation_interface_support(struct frontend_state *fe, void *data);
bool env_get_throttle_state(struct frontend_state *fe, void *data);
bool env_get_savestate_context(struct frontend_state *fe, void *data);
bool env_get_jit_capable(struct frontend_state *fe, void *data);
bool env_get_message_interface_version(struct frontend_state *fe, void *data);
bool env_set_message_ext(struct frontend_state *fe, void *data);
bool env_set_proc_address_callback(struct frontend_state *fe, void *data);
bool env_set_support_achievements(struct frontend_state *fe, void *data);
bool env_set_fastforwarding_override(struct frontend_state *fe, void *data);
bool env_get_game_info_ext(struct frontend_state *fe, void *data);
bool env_get_current_software_framebuffer(struct frontend_state *fe, void *data);
bool env_set_hw_render(struct frontend_state *fe, void *data);
bool env_get_variable(struct frontend_state *fe, void *data);
bool env_set_variables(struct frontend_state *fe, void *data);
bool env_set_core_options(struct frontend_state *fe, void *data);
bool env_set_core_options_intl(struct frontend_state *fe, void *data);
bool env_set_core_options_v2(struct frontend_state *fe, void *data);
bool env_set_core_options_v2_intl(struct frontend_state *fe, void *data);
bool env_set_core_options_display(struct frontend_state *fe, void *data);
bool env_set_controller_info(struct frontend_state *fe, void *data);
bool env_set_disk_control_ext_interface(struct frontend_state *fe, void *data);
bool env_set_serialization_quirks(struct frontend_state *fe, void *data);
bool env_set_hw_shared_context(struct frontend_state *fe, void *data);
bool env_set_subsystem_info(struct frontend_state *fe, void *data);
bool env_set_memory_maps(struct frontend_state *fe, void *data);
bool env_set_content_info_override(struct frontend_state *fe, void *data);

#ifdef __cplusplus
}
#endif

#endif /* CORE_INTERNAL_H */
