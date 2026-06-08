# PureRetro 代码审查报告

> 日期：2026-06-08 | 范围：25 文件 / ~7500 行 C99 | 审查方式：4 并行子代理 + 人工抽样验证

---

## 严重缺陷（Critical — 立即修复）

### C-1 `core_environment` 中所有 EXPERIMENTAL case 不可达

**位置**：`src/core.c:827` 全局剥离 `cmd &= ~RETRO_ENVIRONMENT_EXPERIMENTAL`，但下方 6 处 case 标签仍使用未剥离宏值，永远不会匹配。

| 行号 | callback | 后果 |
|------|----------|------|
| `core.c:1098` | `GET_INPUT_BITMASKS` | 返回 false → bitmask 失效 |
| `core.c:1260` | `GET_CURRENT_SOFTWARE_FRAMEBUFFER` | 零拷贝 SW 路径完全关闭 |
| `core.c:1287` | `GET_AUDIO_VIDEO_ENABLE` | 核心无法查询 audio/video 状态 |
| `core.c:1334` | `GET_FASTFORWARDING` | `--fastforward` 等同 noop |
| `core.c:1340` | `GET_TARGET_REFRESH_RATE` | 核心降级到默认 60Hz |
| `core.c:1408` | `GET_VFS_INTERFACE` | 完整 `vfs.c` 用不上 |

**对照证据**：`core.c:1306,1388,1394,1512,1531,1546,1616,1722` 这 8 个 case 都正确写了 `& ~RETRO_ENVIRONMENT_EXPERIMENTAL`。

**修复**：给上述 6 个 case 标签补上 `& ~RETRO_ENVIRONMENT_EXPERIMENTAL`，并加 CI 守卫（grep 检查 EXP 宏在 case 中是否带剥离）。

### C-2 `GET_AUDIO_VIDEO_ENABLE` 位定义与 libretro 规范不符 ✅

**位置**：`src/core.c:1293-1295`

```c
// 当前（错）：bit0=audio, bit1=video
*(int *)data = (g_frontend.no_audio ? 0 : 1) | (1 << 1);
// 规范：bit0=VIDEO (RETRO_AV_ENABLE_VIDEO), bit1=AUDIO (RETRO_AV_ENABLE_AUDIO)
```

bit0/bit1 互换。C-1 修了之后此 bug 立即导致核心推断"video 关闭、audio 开启"。

**修复**：`RETRO_AV_ENABLE_VIDEO | (g_frontend.no_audio ? 0 : RETRO_AV_ENABLE_AUDIO)`

### C-3 `vfs_seek` 返回值错误 ✅

**位置**：`src/vfs.c:105` — 返回 `fseek()`（成功 = 0），规范要求返回 seek 后绝对字节位置。

依赖 `seek(_, x, SEEK_CUR)` 探询位置的核心（FCEUmm、PicoDrive 等）会读到错误偏移。

**修复**：`if (fseek(...) != 0) return -1; return ftello(h->fp);`

### C-4 `vfs_read`/`vfs_write` 无法区分 EOF 与 I/O 错误 ✅

**位置**：`src/vfs.c:108-124` — `fread`/`fwrite` 短量时不查 `ferror()`，无法按规范返回 -1。

**修复**：加 `if (n < len && ferror(h->fp)) { clearerr(h->fp); return -1; }`

### C-5 Vulkan 三像素 / 二栅栏数据竞争

**位置**：`src/video_vk.c:421-428,612`

`VK_MAX_FRAMES_IN_FLIGHT = 2`，但 FIFO 三缓冲下 image_index ∈ {0,1,2}。`frame_index % 2` 作 fence 索引不保护 image 2 的 GPU 完成，间歇撕裂 / validation 报错。

**修复**：per-image fence 或 image-in-flight 追踪数组。

### C-6 `--config` CLI 分支重复，掩盖了缺失的 `--system-dir` ✅

**位置**：`src/main.c:161-168` 与 `184-191` 两个相同 `--config`。

`set_system_directory()` 注释写 "If --system-dir was explicitly given"，AGENTS.md 也列了 `--system-dir`。

**修复**：将第二分支改为 `--system-dir <path>`。

---

## 重要缺陷（Important — 合并前修复）

### I-1 `core_init` 失败路径触发未配对的 `retro_deinit`/`retro_unload_game`
**位置**：`core.c:401-412,449` — 引入 `g_core_initialized`/`g_game_loaded` 守卫。

### I-2 Vulkan 交换链不在 OUT_OF_DATE/SUBOPTIMAL 时重建
**位置**：`video_vk.c:621-625` — 标记 `swapchain_dirty=true`，下次 present 重建。

### I-3 Vulkan swapchain 重建中途失败留下损坏 context
**位置**：`video_vk.c:243-250` — 新对象存临时变量，全部成功后再 swap。

### I-4 GL FBO resize 不通知核心 context_destroy/reset
**位置**：`video_gl.c:307-319` — resize 时通知核心或原地 reallocate 纹理。

### I-5 Vulkan 不清除 letterbox/pillarbox 区域
**位置**：`video_vk.c:654` — blit 前插 `vkCmdClearColorImage`。

### I-6 ESC/F11 硬编码拦截与 SET_KEYBOARD_CALLBACK 冲突
**位置**：`main.c:472-478` — 交互 callback 活跃时禁用快捷退出。

### I-7 HiDPI/显示缩放变化不触发 backend resize
**位置**：`main.c:481-484` — 追加 DISPLAY_SCALE_CHANGED；最小化时暂停 run loop。

### I-8 `input_process_event` 重复调用 `build_retro_key_table`
**位置**：`src/input.c:308-312` — 删除重复调用。

### I-9 SRAM 加载时静默截断
**位置**：`core.c:608-615` — 文件 > slot 时拒绝加载。

### I-10 SERIALIZATION_QUIRKS 与 HW_SHARED_CONTEXT 共用 case 存在 NULL 解引用风险
**位置**：`core.c:1593-1613` — 消除全局 EXP 剥离或增加 per-case 守卫。

### I-11 Fast-forward 时音频继续按原速推送
**位置**：`audio.c:103-105` — ff 时 `audio_push` 直接 return。

### I-12 `SET_VARIABLES` 数组上限 64 < 规范 128
**位置**：`core.c:998` — 64 改为 `RETRO_NUM_CORE_OPTION_VALUES_MAX`。

### I-13 VFS 函数指针缺 `RETRO_CALLCONV` ✅
**位置**：`vfs.c` 全部 vfs_* 函数 — 加 `RETRO_CALLCONV` 标注。

---

## 次要缺陷（Minor — 可积压）

| 编号 | 位置 | 描述 |
|------|------|------|
| M-1 | `core.c:862` | `SET_MESSAGE` 源标签 `[CORE]` 与 `LOG_INFO` 的 `[FRONTEND]` 重复 |
| M-2 | `core_variables.c:190,207` | 强制 cast 抹掉 const，应暴露 `_get_mutable` |
| M-3 | `core.c:1444-1462` | `SET_CORE_OPTIONS_INTL` 忽略 `local` 但返回 true |
| M-4 | `core_variables_parse.c:21-23` | `parse_default` 对无 `;` 的 v0 字符串行为不一致 |
| M-5 | `core_variables.c:60-61` | 遇重复 key 整表清空，应 WARN 后跳过 |
| M-6 | `main.c:390-401` | `SDL_GetPrefPath` 用 `const char*` + cast，应用 `char*` |
| M-7 | `main.c:439-445` | `SDL_strdup` 混用 `free`/`SDL_free`，统一为 `SDL_free` |
| M-8 | `main.c:130` | `argv[2][0] != '-'` 拒绝 `-` 开头的内容路径 |
| M-9 | `vfs.c:71-84` | `vfs_size` 在 `fseek` 失败时 stream 已被偏移到 EOF ✅ |
| M-10 | `main.c:460-461` | `g_av_info.timing.fps` 未上限校验，过高 fps 可导致忙等 |
| M-11 | `audio.c:144` | Audio occupancy 未含设备硬件缓冲，underrun 误报 |
| M-12 | `input.c:331-336` | `enum retro_mod` 用 `|=`，Clang `-Wassign-enum` 警告 |

---

## 可读性与可维护性问题

### 巨型函数

| 函数 | 位置 | 行数 | 问题 |
|------|------|------|------|
| `core_environment` | `core.c:820-1839` | 1020 | 一 switch 占半文件 |
| `core_init` | `core.c:394-517` | 124 | 混合 init/load/AV/subsystem/context_reset |
| `vk_swapchain_create` | `video_vk.c:228-434` | ~206 | caps/format/mode/extent/images/views/cmd/sync |
| `video_vk_init` | `video_vk.c` | ~250+ | instance/surface/device/queues |
| `parse_args` | `main.c:120-330` | ~210 | 长 if-else 链 |
| `game_info_ext_populate` | `core.c:97-173` | 77 | 路径栈 |

### 头部耦合
- `frontend.h` 329 行，含全部子系统状态，god-struct 中央枢纽，任何字段改动触发全量重编译。
- `video.h` 暴露 backend 内部状态给其他模块。

### 复制粘贴残留
- `main.c:184-191` 重复 `--config`（C-6）
- `input.c:308-312` 重复 `build_retro_key_table`（I-8）

---

## 可扩展性弱点

| 维度 | 当前阻力 |
|------|----------|
| 新增 env callback | 必须在 1020 行 switch 中找位置；需手动处理 EXP 标志 |
| 新增 video backend | vtable 已够，但 `g_frontend.video` 字段渗漏私有状态 |
| 新增 CLI flag | 长 if-else，每加一个都要触碰到所有分支 |
| 新增 audio backend | 直接调 SDL，无抽象层 |
| 多核心热切换 | `g_frontend` 全局状态硬假设单一核心生命周期 |
| 结构化日志 | `log.c` 写死 stderr + 文本格式 |

---

## Top 5 重构建议（按 ROI 排序）

### R1. 拆分 `core_environment`
**动机**：1020 行 switch + EXP 标志陷阱是项目最大单点。
**做法**：按类别抽 static handler（set*/get*/video/audio）；删除全局 `cmd &= ~EXP`；用 dispatch helper 表。
**风险**：低。**工作量**：M（~2 天）。
**收益**：C-1、I-10 物理消失，新增 callback 变成加一行表项。

### R2. Vulkan 三像素/二栅栏修复 + swapchain 生命周期统一
**动机**：C-5、I-2、I-3、I-5 都是 Vulkan 同步/生命周期问题。
**做法**：per-image fence tracking；抽 `swapchain_recreate_if_needed()`，所有 OOD/SUBOPTIMAL 路径统一调；swapchain 重建用临时变量 atomic swap。
**风险**：中（Vulkan 同步，需 validation layer 验证）。**工作量**：M（~3 天）。

### R3. VFS 层全面修复
**动机**：C-3、C-4、I-13 + M-9 同属 VFS 层，批量修复便于统一测试。
**做法**：`vfs_seek` 返回 `ftello`；`read/write` 检查 `ferror`；`size` 修复 stream 损坏；全加 `RETRO_CALLCONV`。
**风险**：低（VFS 函数独立）。**工作量**：S（@半 天）。

### R4. `input.c` / `main.c` 复制粘贴清理 + 小缺陷批量修复
**动机**：C-6、I-8、I-6、I-7、M-6、M-7、M-8、M-12 都是几天小修补。
**做法**：删重复代码；统一 `SDL_free`；`--system-dir` 真实现；ESC 快捷退出受 keyboard_callback 保护；追加 DISPLAY_SCALE_CHANGED。
**风险**：极低。**工作量**：S（@1 天）。

### R5. 巨型函数重构
**动机**：`core_environment`（R1 已覆盖）外，`core_init`（124 行）和 `parse_args`（210 行）是次要目标。
**做法**：`core_init` 抽 `core_load_game`/`core_init_hw_render`；`parse_args` 抽 flag 到 struct `cli_option_handlers[]` 表（或至少 if-else 按功能分组加注释边界）。
**风险**：低。**工作量**：S（~0.5 天）。

---

## 代码亮点

1. **split-renderer 架构**：`video_backend.h` vtable + 三份独立实现（sw/gl/vk），新增 backend 只需实现 6 个接口函数，设计干净。
2. **控制反转干净**：`core_environment` 虽大，但每个 case 返回 bool、日志完整、无过早 return 泄漏资源。
3. **Core options 归一化**：v0/v1/v2/intl 四种定义格式统一进 `core_options_table`，后继核心选项处理逻辑只面向一种数据结构。
4. **VFS 层所有权管理**：`vfs_close` 释放方式正确（先 fclose、再 free 路径、再 free 句柄），错误时全路径返回 -1。

---

*共计：6 Critical / 13 Important / 12 Minor / 5 重构建议 / 4 代码亮点*