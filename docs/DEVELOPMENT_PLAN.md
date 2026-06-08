# PureRetro — 未实现 libretro 接口分析与开发计划

> 本文档基于 `include/libretro.h` 与 `src/core.c` 的完整对照分析，列出所有未实现（或未完整实现）的 environment command、可配置项缺失，以及开发优先级建议。
> 供后续迭代开发时按优先级逐项实施。

---

## 1. 分析范围说明

- **已明确处理（有 `case` 分支）**：如下表所列，共 67 个 command。
- **完全未处理（走到 `default`）**：约 11 个 command。
- **可配置项缺失**：无（音频采样率/缓冲区已可 CLI 覆盖）。

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
| SET_SUPPORT_ACHIEVEMENTS | ✅ | 存储到 g_frontend.core_supports_achievements + INFO 日志 |
| SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE | ✅ | VK 设备创建协商 |
| SET_SERIALIZATION_QUIRKS | ✅ | 保留核心写入值 + INFO 日志 |
| SET_HW_SHARED_CONTEXT | ✅ | 记录标志，下次 GL init 用 SDL_GL_SHARE_WITH_CURRENT_CONTEXT |
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
| GET_MESSAGE_INTERFACE_VERSION | ✅ | 返回 1 |
| SET_MESSAGE_EXT | ✅ | TARGET_LOG 按消息级别路由，OSD/ALL 走 LOG_INFO |
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
| GET_THROTTLE_STATE | ✅ | 返回 { RETRO_THROTTLE_NONE, core_fps } |
| GET_SAVESTATE_CONTEXT | ✅ | 返回 RETRO_SAVESTATE_CONTEXT_NORMAL |
| GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT | ✅ | VK 返回 interface_version=2，其余 0 |
| GET_JIT_CAPABLE | ✅ | 返回 true（桌面平台） |
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

所有原 P0/P1 项已完成。下一轮推荐见 §8。

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

### 第一轮（已完成）

- ✅ Legacy `SET_DISK_CONTROL_INTERFACE` 通过 memcpy 桥接到 EXT
- ✅ `GET_INPUT_MAX_USERS` + `GET_TARGET_SAMPLE_RATE`
- ✅ `SET_AUDIO_BUFFER_STATUS_CALLBACK` + `SET_MINIMUM_AUDIO_LATENCY`
- ✅ `--core-assets-dir` / `--playlist-dir` / `--file-browser-dir` CLI（对应三个 GET_*_DIRECTORY 回调）
- ✅ `SET_PERFORMANCE_LEVEL` 从 stub 升级为带 INFO 日志的实现

附加完成：loglevel-aware logger（`src/log.c`），统一替换 `fprintf(stderr,...)`，
并将 libretro `RETRO_ENVIRONMENT_GET_LOG_INTERFACE` 桥接到新 logger，
支持 `--log-level` 与 `PURERETRO_LOG` 环境变量。

### 第二轮（已完成）

- ✅ `GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT` — VK 核心 v2 协商路径解锁
- ✅ `GET_THROTTLE_STATE` — 核心可判断 throttle/frameskip 状态
- ✅ `GET_SAVESTATE_CONTEXT` — 核心可正确处理 savestate 指针
- ✅ `GET_JIT_CAPABLE` — 核心可启用动态重编译
- ✅ `GET_MESSAGE_INTERFACE_VERSION` + `SET_MESSAGE_EXT` — 分级/分类消息路由到 logger
- ✅ `SET_SERIALIZATION_QUIRKS` — 记录核心 savestate 怪癖
- ✅ `SET_SUPPORT_ACHIEVEMENTS` — 记录核心成就支持声明
- ✅ `SET_HW_SHARED_CONTEXT` — GL 共享上下文请求，下次 GL init 时 honored
- ✅ `--audio-rate <Hz>` + `--audio-buffer-ms <ms>` CLI — 音频参数可从命令行覆盖

---

## 8. 推荐下一轮立即执行的 Top 5 任务

延续 "最小代码变更换取最大兼容性收益" 的思路。这一轮进入 P2 领域，每项需要小规模新状态或子系统，但依然可控：

1. **`SET_MEMORY_MAPS` (36)** — 接收 `retro_memory_map` 并保存到全局变量。核心（如 Beetle PSX）使用此声明内存布局；虽然我们暂不消费它，但未来 achievements/rewind 会用到。实现约 20 行（接收 + deep-copy region 数组 + 日志）。

2. **`SET_PROC_ADDRESS_CALLBACK` (33)** — 接收并存储 `retro_get_proc_address_t`，记录 INFO 日志。Frontend 暂不定义任何 core extension 符号，所以存储即可。约 10 行。

3. **`SET_SUBSYSTEM_INFO` (34) + `--subsystem <ident>` CLI** — 接收并 deep-copy `retro_subsystem_info` 数组（与 SET_CONTROLLER_INFO 同模式），新增 `--subsystem` CLI 用以选择子系统类型。这是解锁 Sufami Turbo / BS-X 等特殊内容的前提。约 50–80 行。

4. **`SET_FASTFORWARDING_OVERRIDE` (64)** — 增加 `g_frontend.fast_forward` 布尔状态；在 `run_loop` 跳过帧延迟逻辑；实现回调让核心可请求强制快进/退出快进。约 30 行核心逻辑 + run_loop 3 行。

5. **`SET_CONTENT_INFO_OVERRIDE` (65) + `GET_GAME_INFO_EXT` (66)** — 两个配套回调：前者让核心声明"我接受哪些扩展名"，后者在 load_game 成功后提供丰富元数据。两者一起实现，约 40–60 行。

明确**这一轮不做**：

- `SET_ROTATION` / `SET_FRAME_TIME_CALLBACK` / `SET_AUDIO_CALLBACK`：P3 级别，教学最小化定位不符
- `SET_INPUT_DESCRIPTORS` / `SET_CORE_OPTIONS_DISPLAY`：无 GUI 价值低
- `GET_DEVICE_POWER` / `SET_NETPACKET_INTERFACE` / `GET_NETPLAY_CLIENT_INDEX` / `EXEC_MEM_*`：Not Planned
