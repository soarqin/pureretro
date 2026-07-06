/*
 * PureRetro — Libretro environment dispatch table
 */

#include "core.h"

#include "core_internal.h"
#include "frontend.h"
#include "log.h"

#include <stddef.h>

typedef bool (*env_handler_fn)(struct frontend_state *fe, void *data);

struct env_handler_entry {
    unsigned cmd;
    const char *name;
    env_handler_fn handler;
};

static const struct env_handler_entry g_env_table[] = {
    { RETRO_ENVIRONMENT_GET_CAN_DUPE,                     "GET_CAN_DUPE",                     env_get_can_dupe                     },
    { RETRO_ENVIRONMENT_GET_OVERSCAN,                     "GET_OVERSCAN",                     env_get_overscan                     },
    { RETRO_ENVIRONMENT_SHUTDOWN,                         "SHUTDOWN",                         env_shutdown                         },
    { RETRO_ENVIRONMENT_SET_MESSAGE,                      "SET_MESSAGE",                      env_set_message                      },
    { RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL,            "SET_PERFORMANCE_LEVEL",            env_set_performance_level            },
    { RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY,             "GET_SYSTEM_DIRECTORY",             env_get_system_directory             },
    { RETRO_ENVIRONMENT_GET_LIBRETRO_PATH,                "GET_LIBRETRO_PATH",                env_get_libretro_path                },
    { RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY,        "GET_CORE_ASSETS_DIRECTORY",        env_get_core_assets_directory        },
    { RETRO_ENVIRONMENT_GET_PLAYLIST_DIRECTORY,           "GET_PLAYLIST_DIRECTORY",           env_get_playlist_directory           },
    { RETRO_ENVIRONMENT_GET_FILE_BROWSER_START_DIRECTORY, "GET_FILE_BROWSER_START_DIRECTORY", env_get_file_browser_start_directory },
    { RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME,              "SET_SUPPORT_NO_GAME",              env_set_support_no_game              },
    { RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS,            "SET_INPUT_DESCRIPTORS",            env_set_input_descriptors            },
    { RETRO_ENVIRONMENT_SET_ROTATION,                                          "SET_ROTATION",                                          env_set_rotation                                          },
    { RETRO_ENVIRONMENT_SET_PIXEL_FORMAT,                                      "SET_PIXEL_FORMAT",                                      env_set_pixel_format                                      },
    { RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK,                                 "SET_KEYBOARD_CALLBACK",                                 env_set_keyboard_callback                                 },
    { RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE,                            "SET_DISK_CONTROL_INTERFACE",                            env_set_disk_control_interface                            },
    { RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER,                               "GET_PREFERRED_HW_RENDER",                               env_get_preferred_hw_render                               },
    { RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE,                                   "GET_VARIABLE_UPDATE",                                   env_get_variable_update                                   },
    { RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK,                               "SET_FRAME_TIME_CALLBACK",                               env_set_frame_time_callback                               },
    { RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK,                                    "SET_AUDIO_CALLBACK",                                    env_set_audio_callback                                    },
    { RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE,                                  "GET_RUMBLE_INTERFACE",                                  env_get_rumble_interface                                  },
    { RETRO_ENVIRONMENT_GET_INPUT_BITMASKS,                                    "GET_INPUT_BITMASKS",                                    env_get_input_bitmasks                                    },
    { RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES,                         "GET_INPUT_DEVICE_CAPABILITIES",                         env_get_input_device_capabilities                         },
    { RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE,                                  "GET_SENSOR_INTERFACE",                                  env_get_sensor_interface                                  },
    { RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE,                                  "GET_CAMERA_INTERFACE",                                  env_get_camera_interface                                  },
    { RETRO_ENVIRONMENT_GET_LOG_INTERFACE,                                     "GET_LOG_INTERFACE",                                     env_get_log_interface                                     },
    { RETRO_ENVIRONMENT_GET_PERF_INTERFACE,                                    "GET_PERF_INTERFACE",                                    env_get_perf_interface                                    },
    { RETRO_ENVIRONMENT_GET_LOCATION_INTERFACE,                                "GET_LOCATION_INTERFACE",                                env_get_location_interface                                },
    { RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY,                                    "GET_SAVE_DIRECTORY",                                    env_get_save_directory                                    },
    { RETRO_ENVIRONMENT_SET_GEOMETRY,                                          "SET_GEOMETRY",                                          env_set_geometry                                          },
    { RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO,                                    "SET_SYSTEM_AV_INFO",                                    env_set_system_av_info                                    },
    { RETRO_ENVIRONMENT_GET_LANGUAGE,                                          "GET_LANGUAGE",                                          env_get_language                                          },
    { RETRO_ENVIRONMENT_GET_USERNAME,                                          "GET_USERNAME",                                          env_get_username                                          },
    { RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION,                    "GET_DISK_CONTROL_INTERFACE_VERSION",                    env_get_disk_control_interface_version                    },
    { RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS,                                   "GET_INPUT_MAX_USERS",                                   env_get_input_max_users                                   },
    { RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE,                                "GET_AUDIO_VIDEO_ENABLE",                                env_get_audio_video_enable                                },
    { RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE,                                "GET_TARGET_SAMPLE_RATE",                                env_get_target_sample_rate                                },
    { RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK,                      "SET_AUDIO_BUFFER_STATUS_CALLBACK",                      env_set_audio_buffer_status_callback                      },
    { RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY,                             "SET_MINIMUM_AUDIO_LATENCY",                             env_set_minimum_audio_latency                             },
    { RETRO_ENVIRONMENT_GET_FASTFORWARDING,                                    "GET_FASTFORWARDING",                                    env_get_fastforwarding                                    },
    { RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE,                               "GET_TARGET_REFRESH_RATE",                               env_get_target_refresh_rate                               },
    { RETRO_ENVIRONMENT_SET_VARIABLE,                                          "SET_VARIABLE",                                          env_set_variable                                          },
    { RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE,           "SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE",           env_set_hw_render_context_negotiation_interface           },
    { RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE,                               "GET_HW_RENDER_INTERFACE",                               env_get_hw_render_interface                               },
    { RETRO_ENVIRONMENT_GET_VFS_INTERFACE,                                     "GET_VFS_INTERFACE",                                     env_get_vfs_interface                                     },
    { RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION,                              "GET_CORE_OPTIONS_VERSION",                              env_get_core_options_version                              },
    { RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK,              "SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK",              env_set_core_options_update_display_callback              },
    { RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT,   "GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT",   env_get_hw_render_context_negotiation_interface_support   },
    { RETRO_ENVIRONMENT_GET_THROTTLE_STATE,                                    "GET_THROTTLE_STATE",                                    env_get_throttle_state                                    },
    { RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT,                                 "GET_SAVESTATE_CONTEXT",                                 env_get_savestate_context                                 },
    { RETRO_ENVIRONMENT_GET_JIT_CAPABLE,                                       "GET_JIT_CAPABLE",                                       env_get_jit_capable                                       },
    { RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION,                         "GET_MESSAGE_INTERFACE_VERSION",                         env_get_message_interface_version                         },
    { RETRO_ENVIRONMENT_SET_MESSAGE_EXT,                                       "SET_MESSAGE_EXT",                                       env_set_message_ext                                       },
    { RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK,                             "SET_PROC_ADDRESS_CALLBACK",                             env_set_proc_address_callback                             },
    { RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS,                              "SET_SUPPORT_ACHIEVEMENTS",                              env_set_support_achievements                              },
    { RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE,                           "SET_FASTFORWARDING_OVERRIDE",                           env_set_fastforwarding_override                           },
    { RETRO_ENVIRONMENT_GET_GAME_INFO_EXT,                                     "GET_GAME_INFO_EXT",                                     env_get_game_info_ext                                     },
    { RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER,                      "GET_CURRENT_SOFTWARE_FRAMEBUFFER",                      env_get_current_software_framebuffer                      },
    { RETRO_ENVIRONMENT_SET_HW_RENDER,                                         "SET_HW_RENDER",                                         env_set_hw_render                                         },
    { RETRO_ENVIRONMENT_GET_VARIABLE,                                          "GET_VARIABLE",                                          env_get_variable                                          },
    { RETRO_ENVIRONMENT_SET_VARIABLES,                                         "SET_VARIABLES",                                         env_set_variables                                         },
    { RETRO_ENVIRONMENT_SET_CORE_OPTIONS,                                      "SET_CORE_OPTIONS",                                      env_set_core_options                                      },
    { RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL,                                 "SET_CORE_OPTIONS_INTL",                                 env_set_core_options_intl                                 },
    { RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,                                   "SET_CORE_OPTIONS_V2",                                   env_set_core_options_v2                                   },
    { RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,                              "SET_CORE_OPTIONS_V2_INTL",                              env_set_core_options_v2_intl                              },
    { RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,                              "SET_CORE_OPTIONS_DISPLAY",                              env_set_core_options_display                              },
    { RETRO_ENVIRONMENT_SET_CONTROLLER_INFO,                                   "SET_CONTROLLER_INFO",                                   env_set_controller_info                                   },
    { RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE,                        "SET_DISK_CONTROL_EXT_INTERFACE",                        env_set_disk_control_ext_interface                        },
    { RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO,                                    "SET_SUBSYSTEM_INFO",                                    env_set_subsystem_info                                    },
    { RETRO_ENVIRONMENT_SET_MEMORY_MAPS,                                       "SET_MEMORY_MAPS",                                       env_set_memory_maps                                       },
    { RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE,                             "SET_CONTENT_INFO_OVERRIDE",                             env_set_content_info_override                             },
    { RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS,                              "SET_SERIALIZATION_QUIRKS",                              env_set_serialization_quirks                              },
    { RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT,                                 "SET_HW_SHARED_CONTEXT",                                 env_set_hw_shared_context                                 },
};

static const struct env_handler_entry *env_table_lookup(unsigned cmd)
{
    /* Pass 1: exact raw cmd match (preserves EXP for I-10 collision pairs). */
    for (size_t i = 0; i < sizeof(g_env_table) / sizeof(g_env_table[0]); ++i) {
        if (g_env_table[i].cmd == cmd)
            return &g_env_table[i];
    }
    /* Pass 2: stripped EXP fallback. Lets cores OR in EXPERIMENTAL without
     * needing a duplicate table entry for every handler. */
    unsigned stripped = cmd & ~RETRO_ENVIRONMENT_EXPERIMENTAL;
    if (stripped == cmd)
        return NULL;
    for (size_t i = 0; i < sizeof(g_env_table) / sizeof(g_env_table[0]); ++i) {
        if (g_env_table[i].cmd == stripped)
            return &g_env_table[i];
    }
    return NULL;
}

bool RETRO_CALLCONV core_environment(unsigned cmd, void *data)
{
    const struct env_handler_entry *entry = env_table_lookup(cmd);
    if (entry)
        return entry->handler(&g_frontend, data);
    LOG_DEBUG("Unhandled env cmd: 0x%x", cmd);
    return false;
}

