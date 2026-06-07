# PureRetro — 未实现 libretro 接口分析与开发计划

> 本文档基于 `include/libretro.h` 与 `src/core.c` 的完整对照分析，列出所有未实现（或未完整实现）的 environment command、可配置项缺失，以及开发优先级建议。
> 供后续迭代开发时按优先级逐项实施。

---

## 1. 分析范围说明

- **已明确处理（有 `case` 分支）**：如下表所列，共 52 个 command。
- **完全未处理（走到 `default`）**：约 26 个 command。
- **已 stub 但值得升级为真实实现**：如 `SET_DISK_CONTROL_INTERFACE`（legacy）、`SET_MEMORY_MAPS` 等。
- **可配置项缺失**：大量路径、音频、输入参数仍硬编码。

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
| SET_PERFORMANCE_LEVEL | 🟡 Stub | 返回 false |
| GET_SYSTEM_DIRECTORY | ✅ | 返回配置的系统目录路径 |
| SET_PIXEL_FORMAT | ✅ | 存储协商格式 |
| SET_INPUT_DESCRIPTORS | 🟡 Stub | 返回 false |
| SET_KEYBOARD_CALLBACK | ✅ | 存储回调 |
| SET_DISK_CONTROL_INTERFACE | 🟡 Stub | 返回 false（legacy；EXT 已实现） |
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
| GET_LOG_INTERFACE | ✅ | 提供 log_stderr |
| GET_PERF_INTERFACE | 🟡 Stub | 返回 false（不计划实现） |
| GET_LOCATION_INTERFACE | 🟡 Stub | 返回 false（不计划实现） |
| GET_CORE_ASSETS_DIRECTORY | ✅ | 返回 NULL |
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
| GET_INPUT_MAX_USERS | ❌ Missing | 完全未处理 |
| SET_AUDIO_BUFFER_STATUS_CALLBACK | ❌ Missing | 完全未处理 |
| SET_MINIMUM_AUDIO_LATENCY | ❌ Missing | 完全未处理 |
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
| GET_PLAYLIST_DIRECTORY | ❌ Missing | 完全未处理 |
| GET_FILE_BROWSER_START_DIRECTORY | ❌ Missing | 完全未处理 |
| GET_TARGET_SAMPLE_RATE | ❌ Missing | 完全未处理 |
| GET_NETPLAY_CLIENT_INDEX | ❌ Missing | 完全未处理 |
| EXEC_MEM_ALLOC | ❌ Missing | 完全未处理 |
| EXEC_MEM_FREE | ❌ Missing | 完全未处理 |

---

## 3. 高优先级开发队列（P0–P1）

| 优先级 | Command / 配置项 | 当前状态 | 为什么重要 | 基本实现建议 |
|---|---|---|---|---|
| **P0** | `--config` 完整 key-mapping 格式文档 + 扩展 | CLI 已解析但无规格 | 用户无法配置按键。当前 `--config` 指向 keymap 文件，但格式未文档化。 | 编写 keymap 配置文件格式规格到 `docs/`；支持更多按键和 CDC 设备（analog）。 |
| **P1** | `SET_DISK_CONTROL_INTERFACE` (13, legacy) | 🟡 Stub | 旧核心仍使用 legacy 接口；现在 EXT 接口已实现，可以用同一组回调桥接 | 在 `g_frontend.disk_control` EXT 回调存在时，把 legacy 请求映射到 EXT 回调（结构前面 7 个字段完全一致）。 |
| **P1** | `GET_INPUT_MAX_USERS` (61) | ❌ Missing | 核心查询支持多少玩家。当前只支持 1P 键盘。 | `*(unsigned *)data = 1; return true;` |
| **P1** | `GET_TARGET_SAMPLE_RATE` (81) | ❌ Missing | 核心获取目标音频采样率做精确同步 | `*(float *)data = g_av_info.timing.sample_rate > 0 ? g_av_info.timing.sample_rate : FRONTEND_AUDIO_SAMPLE_RATE; return true;` |
| **P1** | `--core-assets-dir <path>` / `--playlist-dir <path>` / `--file-browser-dir <path>` CLI | 缺失 | `GET_CORE_ASSETS_DIRECTORY` / `GET_PLAYLIST_DIRECTORY` / `GET_FILE_BROWSER_START_DIRECTORY` 均返回 NULL | 添加对应 CLI 参数和 `g_frontend` 字段 |
| **P1** | `SET_AUDIO_BUFFER_STATUS_CALLBACK` (62) | ❌ Missing | 核心注册回调监听音频缓冲区状态 | 在 `audio_push` 中计算队列占用率变化时回调 |
| **P1** | `SET_MINIMUM_AUDIO_LATENCY` (63) | ❌ Missing | 核心请求最小音频延迟 | 在 `audio_init` 中参考此值调整缓冲区大小 |
| **P1** | `--audio-rate <Hz>` / `--audio-buffer-ms <ms>` | 缺失 | 音频采样率/缓冲区大小硬编码 | CLI 参数传入 `audio_init()` |
| **P1** | `SET_PERFORMANCE_LEVEL` (8) | 🟡 Stub | 核心给出性能等级提示 | 接收 `unsigned *`，记录并打印日志即可 |

---

## 4. 中优先级开发队列（P2）

| 优先级 | Command / 配置项 | 当前状态 | 为什么重要 | 基本实现建议 |
|---|---|---|---|---|
| **P2** | `SET_MEMORY_MAPS` (36) | ❌ Missing | 用于 achievements、rewind、调试 | 接收 `retro_memory_map`，保存到全局变量 |
| **P2** | `SET_PROC_ADDRESS_CALLBACK` (33) | ❌ Missing | 某些 GL 核心需要额外符号 | 用 `SDL_GL_GetProcAddress` 包装进 `retro_get_proc_address_interface` |
| **P2** | `SET_SUBSYSTEM_INFO` (34) | ❌ Missing | Sufami Turbo、BS-X 等特殊子系统 | 接收并记录 `retro_subsystem_info` 数组 |
| **P2** | `GET_MESSAGE_INTERFACE_VERSION` (59) + `SET_MESSAGE_EXT` (60) | ❌ Missing | 增强的消息 API，提供分类和优先级 | 实现新版本消息接口，`SET_MESSAGE_EXT` 支持 `retro_message_ext` |
| **P2** | `SET_FASTFORWARDING_OVERRIDE` (64) | ❌ Missing | 核心请求快进/退出 | 在 `run_loop` 中跳过帧等待逻辑 |
| **P2** | `SET_CONTENT_INFO_OVERRIDE` (65) | ❌ Missing | 核心覆盖内容文件扩展名检测 | 接收并保存 `retro_content_info_override` 数组 |
| **P2** | `GET_GAME_INFO_EXT` (66) | ❌ Missing | 丰富内容元数据 | 提供带 meta 字段的扩展信息 |
| **P2** | `GET_THROTTLE_STATE` (71) | ❌ Missing | 查询节流状态 | 返回 `{ RETRO_THROTTLE_NONE, 1.0f }` |
| **P2** | `GET_SAVESTATE_CONTEXT` (72) | ❌ Missing | 查询 savestate 上下文 | 返回 `RETRO_SAVESTATE_CONTEXT_NORMAL` |
| **P2** | `GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT` (73) | ❌ Missing | 查询 HW 协商支持 | VK 返回 true，其余 false |
| **P2** | `GET_JIT_CAPABLE` (74) | ❌ Missing | 查询 JIT 是否允许 | Linux/macOS 返回 true（若 `mprotect` 可用） |
| **P2** | `SET_SERIALIZATION_QUIRKS` (44) | ❌ Missing | 影响 savestate 序列化 | 记录到全局状态 |
| **P2** | `SET_HW_SHARED_CONTEXT` (44\|EXPERIMENTAL) | ❌ Missing | GL 上下文共享 | 对 GL 后端设置共享标志 |
| **P2** | `SET_SUPPORT_ACHIEVEMENTS` (42) | ❌ Missing | 声明 achievements 支持 | 返回 true 并记录日志 |
| **P2** | `SET_VARIABLE` (70) | ✅ | 运行时字段覆盖 | 已实现 |

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

## 7. 推荐立即执行的 Top 5 任务

若选择下一步 immediate 工作，建议按此顺序：

1. **Legacy `SET_DISK_CONTROL_INTERFACE` 桥接到 EXT** — 实现最简单（C 结构前 7 字段完全一致，memcpy 即可），但显著提升旧核心磁盘兼容性。
2. **`GET_INPUT_MAX_USERS` + `GET_TARGET_SAMPLE_RATE`** — 两行代码的真实现，大量核心依赖此信息做初始化决策。
3. **`SET_AUDIO_BUFFER_STATUS_CALLBACK` + `SET_MINIMUM_AUDIO_LATENCY`** — 完善音频路径，让核心能动态适配音频策略。
4. **`--core-assets-dir` + `--playlist-dir` + `--file-browser-dir` CLI** — 系统目录类接口的补齐，低风险且一劳永逸。
5. **`SET_PERFORMANCE_LEVEL`** — 接收并记录，几行代码即可从 stub 升级为有意义的实现。

这样以最小的代码变更换取最大兼容性收益，符合 "minimal by design" 哲学。
