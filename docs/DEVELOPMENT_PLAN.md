# PureRetro — 未实现 libretro 接口分析与开发计划

> 本文档基于 `include/libretro.h` 与 `src/core.c` 的完整对照分析，列出所有未实现（或未完整实现）的 environment command、可配置项缺失，以及开发优先级建议。
> 供后续迭代开发时按优先级逐项实施。

---

## 1. 分析范围说明

- **已明确处理（有 `case` 分支）**：如下表所列，共 58 个 command。
- **完全未处理（走到 `default`）**：约 20 个 command。
- **已 stub 但值得升级为真实实现**：如 `SET_MEMORY_MAPS` 等。
- **可配置项缺失**：部分音频/路径参数仍硬编码。

---

## 2. 当前已实现状态总览

> ✅ Real = 有意义的真实实现
> 🟡 Stub = 返回 false 或极简处理
> ❌ Missing = 完全未处理

| Command | 实现 | 说明 |
|---|---|---|
| SET_ROTATION | 🟡 Stub | 返回 false |
| GET_OVERSCAN | ✅ | 返回 false（无 overscan） |
| GET_CAN_DUPE | ✅ | 返回 true |
| SET_MESSAGE | ✅ | 打印到 stderr |
| SHUTDOWN | ✅ | 设 g_frontend.running = false |
| SET_PERFORMANCE_LEVEL | ✅ | 记录核心提示等级到 INFO 日志 |
| GET_SYSTEM_DIRECTORY | ✅ | 返回配置的系统目录路径 |
| SET_PIXEL_FORMAT | ✅ | 存储协商格式 |
| SET_INPUT_DESCRIPTORS | 🟡 Stub | 返回 false |
| SET_KEYBOARD_CALLBACK | ✅ | 存储回调 |
| SET_DISK_CONTROL_INTERFACE | ✅ | 通过 memcpy 桥接到 EXT 回调 |
| SET_HW_RENDER | ✅ | 完整 HW 上下文创建 |
| GET_VARIABLE | ✅ | CLI > 磁盘 > 默认值查找 |
| SET_VARIABLES | ✅ | v1 格式解析 |
| GET_VARIABLE_UPDATE | ✅ | 返回 false（无运行时变量更新） |
| SET_SUPPORT_NO_GAME | ✅ | 返回 true |
| GET_LIBRETRO_PATH | ✅ | 返回 core_path |
| SET_FRAME_TIME_CALLBACK | 🟡 Stub | 返回 false |
| SET_AUDIO_CALLBACK | 🟡 Stub | 返回 false |
| GET_RUMBLE_INTERFACE | 🟡 Stub | 返回 false（不计划实现） |
| GET_INPUT_DEVICE_CAPABILITIES | ✅ | 仅报告 joypad |
| GET_SENSOR_INTERFACE | 🟡 Stub | 返回 false（不计划实现） |
| GET_CAMERA_INTERFACE | 🟡 Stub | 返回 false（不计划实现） |
| GET_LOG_INTERFACE | ✅ | 桥接到 loglevel-aware logger (src/log.c) |
| GET_PERF_INTERFACE | 🟡 Stub | 返回 false（不计划实现） |
| GET_LOCATION_INTERFACE | 🟡 Stub | 返回 false（不计划实现） |
| GET_CORE_ASSETS_DIRECTORY | ✅ | 返回 --core-assets-dir 路径（默认 NULL） |
| GET_SAVE_DIRECTORY | ✅ | 返回 save_directory |
| SET_SYSTEM_AV_INFO | ✅ | 更新 g_av_info + 调整 HW 渲染目标 |
| SET_PROC_ADDRESS_CALLBACK | ❌ Missing | 完全未处理 |
| SET_SUBSYSTEM_INFO | ❌ Missing | 完全未处理 |
| SET_CONTROLLER_INFO | ✅ | Deep-copy 存储 + 日志 |
| SET_MEMORY_MAPS | ❌ Missing | 完全未处理 |
| SET_GEOMETRY | ✅ | 更新 geometry + 调整窗口 |
| GET_USERNAME | ✅ | 返回 --username 参数 |
| GET_LANGUAGE | ✅ | 返回 --lang 指定值（默认 ENGLISH） |
| GET_CURRENT_SOFTWARE_FRAMEBUFFER | ✅ | 零拷贝纹理锁定 |
| GET_HW_RENDER_INTERFACE | ✅ | 返回 VK 硬件接口 |
| SET_SUPPORT_ACHIEVEMENTS | ❌ Missing | 完全未处理 |
| SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE | ✅ | VK 设备创建协商 |
| SET_SERIALIZATION_QUIRKS | ❌ Missing | 完全未处理 |
| SET_HW_SHARED_CONTEXT | ❌ Missing | 完全未处理 |
| GET_VFS_INTERFACE | ✅ | stdio VFS 包装 |
| GET_LED_INTERFACE | 🟡 Stub | 返回 false（不计划实现） |
| GET_AUDIO_VIDEO_ENABLE | ✅ | 根据 no_audio 设 bit0/bit1 |
| GET_MIDI_INTERFACE | 🟡 Stub | 返回 false（不计划实现） |
| GET_FASTFORWARDING | ✅ | 返回 false（无快进） |
| GET_TARGET_REFRESH_RATE | ✅ | 返回显示器真实刷新率 |
| GET_INPUT_BITMASKS | ✅ | 返回 true |
| GET_CORE_OPTIONS_VERSION | ✅ | 返回 2 |
| SET_CORE_OPTIONS | ✅ | v1 选项定义 |
| SET_CORE_OPTIONS_INTL | ✅ | v1 国际化 |
| SET_CORE_OPTIONS_DISPLAY | 🟡 Stub | 返回 false |
| GET_PREFERRED_HW_RENDER | ✅ | 返回 renderer 偏好 |
| GET_DISK_CONTROL_INTERFACE_VERSION | ✅ | 返回 1 |
| SET_DISK_CONTROL_EXT_INTERFACE | ✅ | 存储回调 + 应用 `--disk-index` |
| GET_MESSAGE_INTERFACE_VERSION | ❌ Missing | 完全未处理 |
| SET_MESSAGE_EXT | ❌ Missing | 完全未处理 |
| GET_INPUT_MAX_USERS | ✅ | 返回 1（仅键盘 port 0） |
| SET_AUDIO_BUFFER_STATUS_CALLBACK | ✅ | 存储回调，每帧 retro_run 前推送占用率 |
| SET_MINIMUM_AUDIO_LATENCY | ✅ | 调整队列上限（不重启 SDL 流） |
| SET_FASTFORWARDING_OVERRIDE | ❌ Missing | 完全未处理 |
| SET_CONTENT_INFO_OVERRIDE | ❌ Missing | 完全未处理 |
| GET_GAME_INFO_EXT | ❌ Missing | 完全未处理 |
| SET_CORE_OPTIONS_V2 | ✅ | v2 选项定义 |
| SET_CORE_OPTIONS_V2_INTL | ✅ | v2 国际化 |
| SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK | ✅ | 存储回调 |
| SET_VARIABLE | ✅ | 运行时变量覆盖 |
| GET_THROTTLE_STATE | ❌ Missing | 完全未处理 |
| GET_SAVESTATE_CONTEXT | ❌ Missing | 完全未处理 |
| GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT | ❌ Missing | 完全未处理 |
| GET_JIT_CAPABLE | ❌ Missing | 完全未处理 |
| GET_MICROPHONE_INTERFACE | 🟡 Stub | 返回 false（不计划实现） |
| GET_DEVICE_POWER | ❌ Missing | 完全未处理 |
| SET_NETPACKET_INTERFACE | ❌ Missing | 完全未处理 |
| GET_PLAYLIST_DIRECTORY | ✅ | 返回 --playlist-dir 路径 |
| GET_FILE_BROWSER_START_DIRECTORY | ✅ | 返回 --file-browser-dir 路径 |
| GET_TARGET_SAMPLE_RATE | ✅ | 返回核心采样率（回退 48000） |
| GET_NETPLAY_CLIENT_INDEX | ❌ Missing | 完全未处理 |
| EXEC_MEM_ALLOC | ❌ Missing | 完全未处理 |
| EXEC_MEM_FREE | ❌ Missing | 完全未处理 |

---

## 3. 高优先级开发队列（P0–P1）

| 优先级 | Command / 配置项 | 当前状态 | 为什么重要 | 基本实现建议 |
|---|---|---|---|---|
| **P0** | `--config` 完整 key-mapping 格式文档 + 扩展 | CLI 已解析但无规格 | 用户无法配置按键。当前 `--config` 指向 keymap 文件，但格式未文档化。 | 编写 keymap 配置文件格式规格到 `docs/`；支持更多按键和 CDC 设备（analog）。 |
| **P1** | `GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT` (73) | ❌ Missing | 没有这个回调，VK 核心会假设我们只支持 v1 negotiation；而我们的 VK 后端实际上能处理 v2+ | 读取 `retro_hw_render_context_negotiation_interface_type`，VK 返回 `interface_version = N`（与我们实际支持的版本一致），其他返回 0 + true |
| **P1** | `GET_THROTTLE_STATE` (71) | ❌ Missing | 大量核心在 rewind/FF/normal 之间分支；缺这个回调时它们走错路径 | 返回 `{ RETRO_THROTTLE_NONE, (float)g_av_info.timing.fps }` |
| **P1** | `GET_SAVESTATE_CONTEXT` (72) | ❌ Missing | 核心据此决定 savestate 中是否能包含指针 | 返回 `RETRO_SAVESTATE_CONTEXT_NORMAL` |
| **P1** | `GET_JIT_CAPABLE` (74) | ❌ Missing | 桌面平台默认支持 JIT；不实现则核心可能强制走解释器 | 桌面三平台返回 `true`（iOS/Android 才是 false） |
| **P1** | `GET_MESSAGE_INTERFACE_VERSION` (59) + `SET_MESSAGE_EXT` (60) | ❌ Missing | 现代核心用 ext API 发分类/分级消息；恰好与新 logger 完美契合 | 接收 `retro_message_ext`：`TARGET_LOG` 按 `level` 走 `LOG_*`；`TARGET_OSD`/`ALL` 暂时也走 `LOG_INFO`（无 GUI）。`GET_MESSAGE_INTERFACE_VERSION` 返回 1 |
| **P1** | `SET_SERIALIZATION_QUIRKS` (44) | ❌ Missing | 核心声明 savestate 量子化怪癖；frontend 不需求任何 quirk，但需 ack | 接收 `uint64_t *`：保留核心写入值不变（我们不强制设任何 quirk），仅记录到 INFO 日志 |
| **P1** | `SET_SUPPORT_ACHIEVEMENTS` (42) | ❌ Missing | 核心声明支持成就；即便我们不实现 cheevos，承认能让核心走正确路径 | 接收 `const bool *`，记录到 `g_frontend.core_supports_achievements` + INFO 日志 |
| **P1** | `SET_HW_SHARED_CONTEXT` (44\|EXP) | ❌ Missing | GL 核心请求与 frontend 共享上下文（某些视频解码场景） | GL 后端：记录到 `g_frontend.video.hw_shared_context_requested`，下次创建 GL 上下文时用 `SDL_GL_SHARE_WITH_CURRENT_CONTEXT` |
| **P1** | `--audio-rate <Hz>` / `--audio-buffer-ms <ms>` | 缺失 | 音频采样率/缓冲区大小硬编码；调试音频问题时无法绕过核心默认 | CLI 参数：`--audio-rate` 覆盖 `g_av_info.timing.sample_rate`；`--audio-buffer-ms` 成为 `audio_set_minimum_latency` 的下限 |

---

## 4. 中优先级开发队列（P2）

| 优先级 | Command / 配置项 | 当前状态 | 为什么重要 | 基本实现建议 |
|---|---|---|---|---|
| **P2** | `SET_MEMORY_MAPS` (36) | ❌ Missing | 用于 achievements、rewind、调试 | 接收 `retro_memory_map`，保存到全局变量 |
| **P2** | `SET_PROC_ADDRESS_CALLBACK` (33) | ❌ Missing | 让 frontend 反向调用核心扩展函数；目前我们没有任何 frontend 扩展 | 存储 `get_proc_address`；记录 INFO 日志（实际暂不调用） |
| **P2** | `SET_SUBSYSTEM_INFO` (34) | ❌ Missing | Sufami Turbo、BS-X 等特殊子系统；需要配套 `--subsystem` CLI | 接收并 deep-copy `retro_subsystem_info` 数组，新增 `--subsystem <ident>` CLI 用以选择 |
| **P2** | `SET_FASTFORWARDING_OVERRIDE` (64) | ❌ Missing | 核心请求强制快进/退出快进；需要先在 run_loop 实现 FF | 增加 `g_frontend.fast_forward` 状态 + run_loop 跳过帧延迟逻辑，再实现此回调 |
| **P2** | `SET_CONTENT_INFO_OVERRIDE` (65) | ❌ Missing | 核心覆盖内容文件扩展名检测 | 接收并保存 `retro_content_info_override` 数组 |
| **P2** | `GET_GAME_INFO_EXT` (66) | ❌ Missing | 丰富内容元数据；依赖 subsystem 支持先到位 | 提供带 meta 字段的扩展信息 |

---

## 5. 低优先级 / 边缘功能（P3）

| 优先级 | Command / 配置项 | 当前状态 | 说明 |
|---|---|---|---|
| **P3** | `SET_FRAME_TIME_CALLBACK` (21) | 🟡 Stub | TAS 精准计时。需在 `run_loop` 每帧调用 |
| **P3** | `SET_AUDIO_CALLBACK` (22) | 🟡 Stub | 核心接管音频，极少使用 |
| **P3** | `SET_INPUT_DESCRIPTORS` (11) | 🟡 Stub | 提供输入标签，PureRetro 无 GUI 价值低 |
| **P3** | `SET_ROTATION` (1) | 🟡 Stub | 竖屏街机核心需要 |
| **P3** | `SET_CORE_OPTIONS_DISPLAY` (55) | 🟡 Stub | 核心选项显示类别，无 GUI 价值低 |

---

## 6. 不计划实现（Not Planned）

与 PureRetro "minimal educational frontend" 定位严重不符，或需要特殊硬件。

| Command | 不实现原因 |
|---|---|
| GET_RUMBLE_INTERFACE | 需要物理手柄 |
| GET_SENSOR_INTERFACE | 体感/移动设备专用 |
| GET_CAMERA_INTERFACE | 几乎无核心使用 |
| GET_PERF_INTERFACE | RetroArch 特有 |
| GET_LOCATION_INTERFACE | GPS 定位 |
| GET_LED_INTERFACE | 键盘 LED 等 |
| GET_MIDI_INTERFACE | MIDI 设备 |
| GET_MICROPHONE_INTERFACE | 极罕见 |
| GET_DEVICE_POWER | 仅移动平台 |
| SET_NETPACKET_INTERFACE | 联机网络栈，超出范围 |
| GET_NETPLAY_CLIENT_INDEX | 同上 |
| EXEC_MEM_ALLOC / EXEC_MEM_FREE | WiiU/3DS 专用 |

---

## 7. 历史完成的优先任务

第一轮推荐的 Top 5 immediate 任务已全部完成（详见 `AGENTS.md` 中的回调表）：

- ✅ Legacy `SET_DISK_CONTROL_INTERFACE` 通过 memcpy 桥接到 EXT
- ✅ `GET_INPUT_MAX_USERS` + `GET_TARGET_SAMPLE_RATE`
- ✅ `SET_AUDIO_BUFFER_STATUS_CALLBACK` + `SET_MINIMUM_AUDIO_LATENCY`
- ✅ `--core-assets-dir` / `--playlist-dir` / `--file-browser-dir` CLI（对应三个 GET_*_DIRECTORY 回调）
- ✅ `SET_PERFORMANCE_LEVEL` 从 stub 升级为带 INFO 日志的实现

附加完成：loglevel-aware logger（`src/log.c`），统一替换 `fprintf(stderr,...)`，
并将 libretro `RETRO_ENVIRONMENT_GET_LOG_INTERFACE` 桥接到新 logger，
支持 `--log-level` 与 `PURERETRO_LOG` 环境变量。

---

## 8. 推荐下一轮立即执行的 Top 5 任务

延续 "最小代码变更换取最大兼容性收益" 的思路。这一轮所有候选都是几行到几十行的真实现，避开了需要新子系统（FF、subsystem、memory map）的大块工作：

1. **`GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT` (73)** — 没有这个回调，Vulkan 核心会假设我们只支持 v1 协商，但我们的 VK 后端已经能处理更高版本。一个 case 分支即可解锁现代 VK 核心的完整协商路径，**单项收益最大**。
2. **`GET_THROTTLE_STATE` + `GET_SAVESTATE_CONTEXT` + `GET_JIT_CAPABLE`** — 三个"返回常量"型回调（约 5–8 行/个）。大量核心据此选择默认行为：throttle 让核心判断要不要 frameskip；savestate context 决定是否能把指针写入存档；jit capable 决定是否启用动态重编译。三个一起做，复用同一段日志/测试套路。
3. **`GET_MESSAGE_INTERFACE_VERSION` + `SET_MESSAGE_EXT`** — 直接利用新 logger：`MESSAGE_TARGET_LOG` 按 `retro_message_ext::level` 走对应 `LOG_*`，`MESSAGE_TARGET_OSD`/`ALL` 临时也走 `LOG_INFO`（无 GUI）。把 stub `SET_MESSAGE` 一起对齐为同一路径。`GET_MESSAGE_INTERFACE_VERSION` 返回 1。
4. **`SET_SERIALIZATION_QUIRKS` + `SET_SUPPORT_ACHIEVEMENTS` + `SET_HW_SHARED_CONTEXT`** — 三个 "advertise / record / acknowledge" 类回调。各 <10 行；目的就是让核心初始化阶段不要因为 `default: false` 走降级路径。`SET_HW_SHARED_CONTEXT` 真正需要的后续工作（创建 GL 上下文时加 `SDL_GL_SHARE_WITH_CURRENT_CONTEXT`）也可以同步做掉。
5. **`--audio-rate <Hz>` + `--audio-buffer-ms <ms>` CLI** — 收尾 audio 路径：调试音频卡顿/重采样问题时可以从命令行强行覆盖。`--audio-rate` 覆盖 `g_av_info.timing.sample_rate`，`--audio-buffer-ms` 作为 `audio_set_minimum_latency` 的下限。无新模块，纯参数透传。

明确**这一轮不做**（理由附在 §4 注释里）：

- `SET_FASTFORWARDING_OVERRIDE`：需要先在 `run_loop` 实现 FF 状态机
- `SET_SUBSYSTEM_INFO` / `SET_CONTENT_INFO_OVERRIDE` / `GET_GAME_INFO_EXT`：需要 `--subsystem` CLI 和多内容加载支持
- `SET_MEMORY_MAPS`：当前没有 achievements / rewind 来消费它
- `SET_PROC_ADDRESS_CALLBACK`：frontend 暂未定义任何 core extension 函数符号
- `SET_ROTATION` / `SET_FRAME_TIME_CALLBACK`：HW 旋转和 TAS 计时与教学最小化定位不符
