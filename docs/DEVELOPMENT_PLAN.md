# PureRetro — 未实现 libretro 接口分析与开发计划

> 本文档基于 `include/libretro.h` 与 `src/core.c` 的完整对照分析，列出所有未实现（或未完整实现）的 environment command、可配置项缺失，以及开发优先级建议。
> 供后续迭代开发时按优先级逐项实施。

---

## 1. 分析范围说明

- **已明确处理（有 `case` 分支）**：约 28 个 command，其中部分为 stub（直接返回 `false`）。
- **完全未处理（走到 `default`）**：约 40 个 command。
- **已 stub 但值得升级为真实实现**：如 `SET_DISK_CONTROL_EXT_INTERFACE`、`SET_MEMORY_MAPS` 等。
- **可配置项缺失**：目前 CLI 支持 `--fullscreen`、`-f`、`--render`、`--scale`、`--no-audio`、`--variable`、`--portable`、`--system-dir`、`--save-dir`、`--config`；大量路径、音频、输入、行为参数仍硬编码。

---

## 2. 高优先级开发队列（P0–P1）

> P0 = 核心兼容性瓶颈，大量核心依赖；P1 = 特定功能类别（输入、音频、选项、配置）的标配接口。

| 优先级 | Command / 配置项 | 当前状态 | 为什么重要 | 基本实现建议 |
|:---:|---|:---:|---|---|
| **P0** | `SET_CONTROLLER_INFO` (35) | 完全未处理 | **多端口核心**（如 N64、PS1 模拟器）都会调用，用于报告各端口支持的设备类型。虽然 minimal frontend 无 GUI，但返回 `true` 并记录信息可避免核心报错。 | 接收 `retro_controller_info` 数组，遍历存下各端口的设备类型信息。可先只记录到 stderr 并返回 `true`。 |
| **P1** | `SET_DISK_CONTROL_EXT_INTERFACE` (58) | 完全未处理 | 多盘游戏（如 PS1、PC Engine CD）的换盘功能。当前只有旧版 `SET_DISK_CONTROL_INTERFACE` stub。 | 实现基础磁盘控制回调：存下 `retro_disk_control_ext_callback`，提供插入/弹出/获取镜像数量/获取镜像标签的能力。CLI 可加 `--disk-index <N>` 来切换。 |
| **P1** | `GET_DISK_CONTROL_INTERFACE_VERSION` (57) | 完全未处理 | 核心先查询版本再决定用旧版还是 EXT 接口。 | `*(unsigned *)data = 1; return true;` |
| **P1** | `GET_CURRENT_SOFTWARE_FRAMEBUFFER` (40) | 完全未处理 | **性能优化**：允许软件渲染核心直接写 frontend 的 framebuffer，避免一次内存拷贝。对软件渲染路径很有价值。 | 在 `video_sw.c` 中实现：若当前为软件后端，返回指向当前 SDL 纹理像素缓冲区的指针。需要配合 `SDL_LockTexture` / `SDL_UnlockTexture` 的生命周期。 |
| **P1** | `GET_AUDIO_VIDEO_ENABLE` (47) | 完全未处理 | 核心查询音视频是否启用，用于**选择性跳过渲染/混音**以提升性能。例如 `--no-audio` 后核心可以停止生成音频。 | `*(int *)data` 按位设置：`bit0 = !g_frontend.no_audio`，`bit1 = 1`（视频始终启用）。返回 `true`。 |
| **P1** | `GET_FASTFORWARDING` (49) | 完全未处理 | 核心查询是否在快进，用于**跳过特效/降质音频**以提升快进体验。 | 当前无快进逻辑，先 `*(bool *)data = false; return true;`。为后续快进功能预留。 |
| **P1** | `GET_TARGET_REFRESH_RATE` (50) | 完全未处理 | 核心获取显示器刷新率以做**自适应同步**（如 G-Sync/FreeSync 感知的帧间隔）。 | 用 `SDL_GetDisplayMode` 获取当前显示器刷新率，写入 `*(float *)data`。 |
| **P1** | `SET_AUDIO_BUFFER_STATUS_CALLBACK` (62) | 完全未处理 | 核心注册回调以监听音频缓冲区下溢/上溢，从而**动态调整音频生成策略**。 | 在 `audio.c` 中调用 `SDL_GetAudioStreamQueued` 计算队列占用率，当状态变化时调用 `retro_audio_buffer_status_callback`。 |
| **P1** | `SET_MINIMUM_AUDIO_LATENCY` (63) | 完全未处理 | 核心请求最小音频延迟，用于**低延迟模式**。 | 在 `audio.c` 初始化时参考此值调整缓冲区大小。当前固定 64ms，可按此值重新计算。 |
| **P1** | `GET_USERNAME` (38) | 完全未处理 | 联机/排行榜相关核心会查询玩家名称。 | 添加 `--username <name>` CLI 参数，存于 `g_frontend.username`。无配置时返回 `NULL`。 |
| **P1** | `SET_MEMORY_MAPS` (36) | 完全未处理 | 用于 achievements、rewind、调试。一些核心会主动设置内存映射。 | 接收 `retro_memory_map`，简单保存到全局变量。可先只记录日志并返回 `true`，为将来 achievements / rewind 打基础。 |
| **P1** | `--lang <code>` CLI 参数 | 缺失 | `GET_LANGUAGE` 目前固定返回 `ENGLISH`。 | 添加 CLI 参数映射到 `retro_language` 枚举，存入 `g_frontend.language`。 |

---

## 3. 中优先级开发队列（P2）

> 受众较窄或依赖其他前置功能，但在特定场景下有价值。

| 优先级 | Command / 配置项 | 当前状态 | 为什么重要 | 基本实现建议 |
|:---:|---|:---:|---|---|
| **P2** | `SET_SUBSYSTEM_INFO` (34) | 完全未处理 | 用于 Sufami Turbo、BS-X 等特殊子系统。只有特定 SNES 核心使用。 | 接收 `retro_subsystem_info` 数组，记录子系统名称和识别符。返回 `true`。CLI 可扩展为 `pureretro <core> <subsystem> <content>`。 |
| **P2** | `SET_SERIALIZATION_QUIRKS` (44) / `SET_HW_SHARED_CONTEXT` (45) | 完全未处理 | 前者影响 savestate 序列化行为；后者影响 GL 上下文共享。**与 rewind/savestate 相关**，但 PureRetro 目前没有这些功能。 | 前者记录 `retro_serialization_quirks` 到全局状态；后者对 GL 后端设置 `SDL_GL_SHARE_WITH_CURRENT_CONTEXT` 标志。 |
| **P2** | `SET_PROC_ADDRESS_CALLBACK` (33) | 完全未处理 | 某些 OpenGL core 需要加载 HW render 之外的额外符号。 | 在 `video_gl.c` 中，把 `SDL_GL_GetProcAddress` 包装进 `retro_get_proc_address_interface`，返回给核心。 |
| **P2** | `SET_CONTENT_INFO_OVERRIDE` (65) | 完全未处理 | 较新的 API，允许核心覆盖内容文件的扩展名/需求检测。 | 接收并保存 `retro_content_info_override` 数组，返回 `true`。影响 `retro_load_game` 的扩展名检查逻辑。 |
| **P2** | `GET_GAME_INFO_EXT` (66) | 完全未处理 | 较新的 API，提供比 `retro_game_info` 更丰富的内容元数据。 | 在 `core_init` 中若核心查询此接口，返回带 `meta` 字段的扩展信息。需根据加载的文件提取元数据（如 CRC32）。 |
| **P2** | `SET_FASTFORWARDING_OVERRIDE` (64) | 完全未处理 | 核心可临时请求进入/退出快进状态。 | 在 `g_frontend` 中添加 `ff_override` 标志。若核心请求快进，在 `run_loop` 中跳过 `SDL_DelayNS` 的帧等待逻辑。 |
| **P2** | `GET_THROTTLE_STATE` (71) | 完全未处理 | 核心查询当前是否被节流（慢动作、帧步进等）。 | `*(retro_throttle_state *)data = { RETRO_THROTTLE_NONE, 1.0f }; return true;`。为未来慢动作/帧步进功能预留。 |
| **P2** | `GET_SAVESTATE_CONTEXT` (72) | 完全未处理 | 核心查询当前是否在运行/回放 savestate，以决定是否保存/恢复额外状态。 | 当前无 savestate 功能，`*(retro_savestate_context *)data = RETRO_SAVESTATE_CONTEXT_NORMAL; return true;` |
| **P2** | `GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT` (73) | 完全未处理 | 核心查询 frontend 是否支持 HW render 上下文协商（Vulkan 专用）。 | 检查 `g_frontend.video.renderer`，若为 Vulkan 则 `*(bool *)data = true`，否则 `false`。 |
| **P2** | `GET_JIT_CAPABLE` (74) | 完全未处理 | 某些模拟器核心（如 Dolphin、Citra）查询是否允许 JIT 重编译。 | Linux/macOS 返回 `true`（通常允许 `mprotect(...PROT_EXEC)`）；Windows 返回 `true`。若平台受限返回 `false`。 |
| **P2** | `GET_INPUT_MAX_USERS` (61) | 完全未处理 | 核心查询支持的最大玩家数。 | `*(unsigned *)data = 1; return true;`（当前只支持键盘映射到 1P）。若未来添加手柄支持可调高。 |
| **P2** | `--core-assets-dir <path>` / `--playlist-dir <path>` / `--file-browser-dir <path>` | 缺失 | `GET_CORE_ASSETS_DIRECTORY` / `GET_PLAYLIST_DIRECTORY` / `GET_FILE_BROWSER_START_DIRECTORY` 均返回 NULL。 | 添加对应的 CLI 参数和 `g_frontend` 字段。无配置时返回 NULL。 |
| **P2** | 音频采样率/缓冲区 CLI 配置 | 缺失 | `FRONTEND_AUDIO_SAMPLE_RATE` (48000) 和 `FRONTEND_AUDIO_BUFFER_MS` (64) 硬编码。 | 添加 `--audio-rate <Hz>` 和 `--audio-buffer-ms <ms>` CLI 参数，传入 `audio_init()`。 |
| **P2** | `SET_SUPPORT_ACHIEVEMENTS` (42) | 完全未处理 | 核心声明是否支持 achievements。PureRetro 暂无 achievement 系统。 | 返回 `true` 并记录日志，为未来集成 RetroAchievements 做准备。 |
| **P2** | `SET_PERFORMANCE_LEVEL` (8) | Stub | 目前只是 stub。核心会给出性能等级提示，可用于 frontend 警告。 | 接收 `unsigned *`，记录到 `g_frontend.performance_level` 并打印日志：`fprintf(stderr, "Core performance level: %u\n", level); return true;`。 |
| **P2** | `SET_VARIABLE` (70) | Stub | 用于核心在运行时更新单个变量的值。当前 stub 可能导致某些核心的动态选项失效。 | 调用 `variable_table_set` 更新 `g_frontend.variables` 中对应 key 的默认值部分。需要解析 "description; value\|opt1\|..." 格式并替换当前值。 |
| **P2** | `GET_TARGET_SAMPLE_RATE` (81) | 完全未处理 | 核心获取目标音频采样率以做精确同步。 | `*(float *)data = g_av_info.timing.sample_rate > 0 ? g_av_info.timing.sample_rate : FRONTEND_AUDIO_SAMPLE_RATE; return true;`。 |

---

## 4. 低优先级 / 边缘功能（P3）

| 优先级 | Command / 配置项 | 当前状态 | 说明 | 建议 |
|:---:|---|:---:|---|---|
| **P3** | `SET_FRAME_TIME_CALLBACK` (21) | Stub | 核心注册帧时间回调，用于精准计时（如 TAS）。 | 在 `run_loop` 每帧调用 `retro_frame_time_callback` 并传入纳秒级帧时间。实现简单但受众窄。 |
| **P3** | `SET_AUDIO_CALLBACK` (22) | Stub | 核心接管音频输出（而非推送 sample）。一些特殊核心使用。 | 在 `audio.c` 中改用回调模式：核心注册回调后，`run_loop` 在适当时机调用 `retro_audio_callback` 请求音频数据。 |
| **P3** | `SET_INPUT_DESCRIPTORS` (11) | Stub | 为 frontend GUI 提供输入标签描述。PureRetro 无 GUI，实现价值低。 | 接收并打印到 stderr 即可，`return true;`。 |
| **P3** | `SET_ROTATION` (1) | Stub | 屏幕旋转。只有极少数竖屏街机核心需要。 | 若 SDL3 支持旋转（或通过调整 viewport 矩形），实现 90°/180°/270° 旋转。 |
| **P3** | `GET_OVERSCAN` (2) | 已实现 | 已硬编码返回 `false`。这是 deprecated 的 API，现代核心用 core options 管理 overscan。 | 保持现状即可，无需改动。 |

---

## 5. 过于冷门 / 本项目不计划实现

以下接口要么需要特殊硬件（震动、光枪、摄像头、传感器），要么与 PureRetro "minimal educational frontend" 的定位严重不符（联机、MIDI、LED、麦克风、GPS 定位等），**建议明确标记为 "Not Planned"**。

| Command | 名称 | 不实现原因 |
|---|---|---|
| `GET_RUMBLE_INTERFACE` (23) | 震动反馈 | 需要物理手柄/震动硬件支持。minimal frontend 可跳过。 |
| `GET_SENSOR_INTERFACE` (25) | 传感器（加速度计/陀螺仪） | 只有体感/移动设备核心使用，PC 环境下极罕见。 |
| `GET_CAMERA_INTERFACE` (26) | 摄像头 | 几乎没有核心使用。 |
| `GET_PERF_INTERFACE` (28) | 性能计数器 | RetroArch 特有功能，用于性能剖析。 |
| `GET_LOCATION_INTERFACE` (29) | GPS 定位 | 无实际核心使用。 |
| `GET_LED_INTERFACE` (46) | LED 灯控制 | 仅某些复古主机（如 Commodore 64）有键盘 LED 模拟需求。 |
| `GET_MIDI_INTERFACE` (48) | MIDI 接口 | 只有音乐制作相关核心（如 DOSBox 的 MPU-401）可能使用，需要外部 MIDI 设备或软合成器。 |
| `GET_MICROPHONE_INTERFACE` (75) | 麦克风 | 极罕见，无主流核心使用。 |
| `GET_DEVICE_POWER` (77) | 设备电量 | 仅移动平台有意义，PC/Linux 环境下几乎不用。 |
| `SET_NETPACKET_INTERFACE` (78) | 网络包接口 | 联机对战需要完整网络栈和同步逻辑，远超 minimal frontend 范围。 |
| `GET_NETPLAY_CLIENT_INDEX` (82) | 联机客户端索引 | 同上，依赖 netplay 基础设施。 |
| `EXEC_MEM_ALLOC` (83) / `EXEC_MEM_FREE` (84) | 执行内存分配 | WiiU / 3DS 等特定平台需要，通用桌面平台不需要。 |

---

## 6. 推荐立即执行的 Top 5 任务

若选择下一步 immediate 工作，建议按此顺序：

1. **`SET_CONTROLLER_INFO`** — 多端口核心兼容性基础，提升 N64/PS1 模拟器支持度。
2. **`SET_DISK_CONTROL_EXT_INTERFACE` + `GET_DISK_CONTROL_INTERFACE_VERSION`** — 多盘游戏（PS1 CD、PC Engine）换盘功能。
3. **`GET_CURRENT_SOFTWARE_FRAMEBUFFER`** — 软件渲染路径性能优化，零拷贝渲染。
4. **`GET_AUDIO_VIDEO_ENABLE` + `GET_FASTFORWARDING` + `GET_TARGET_REFRESH_RATE`** — 现代核心的标准查询接口，实现简单但兼容性收益大。
5. **`--lang` CLI 参数 + `GET_USERNAME`** — 国际化与个性化配置基础。

这样既能显著提升核心兼容性，又保持了 "minimal by design" 的哲学。
