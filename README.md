# PureRetro

A minimal [libretro](https://www.libretro.com/) frontend written in Pure C with SDL3.

Inspired by [sdlarch](https://github.com/heuripedes/sdlarch), PureRetro aims to be a small, clean, and educational reference implementation. It supports software rendering as well as hardware-rendered OpenGL and Vulkan contexts.

## Features

- Pure C + SDL3
- CMake-based build
- Cross-platform: Windows, Linux, macOS
- Software rendering via SDL3 textures
- Hardware rendering: OpenGL and Vulkan
- Keyboard-to-RetroPad input mapping
- No external dependencies from libretro-common (only `libretro.h`)

## Dependencies

| Dependency    | Version | Required | Notes                                    |
|---------------|---------|----------|------------------------------------------|
| CMake         | >= 3.16 | Yes      | Build system                             |
| SDL3          | >= 3.0  | Yes      | Video, audio, input, windowing           |
| C Compiler    | C99+    | Yes      | GCC, Clang, or MSVC                      |
| Vulkan SDK    | >= 1.3  | No       | Only if building with Vulkan support     |

### Platform-Specific Notes

**Linux**
```bash
# Debian / Ubuntu
sudo apt install cmake libsdl3-dev libvulkan-dev build-essential

# Fedora
sudo dnf install cmake SDL3-devel vulkan-loader-devel gcc make
```

**macOS**
```bash
# Using Homebrew
brew install cmake sdl3

# Vulkan is optional; install the Vulkan SDK if you need Vulkan support.
```

**Windows**
- Install [Visual Studio 2022](https://visualstudio.microsoft.com/) (Community edition is fine) or MinGW-w64.
- Download the [SDL3 development library](https://libsdl.org/) and make it available to CMake.
- Install the [Vulkan SDK](https://vulkan.lunarg.com/) if you need Vulkan support.

## Building

```bash
# Clone the repository
git clone https://github.com/yourusername/pureretro.git
cd pureretro

# Configure
mkdir build && cd build
cmake ..

# Build
cmake --build .
```

### Build Options

| Option                    | Default | Description                          |
|---------------------------|---------|--------------------------------------|
| `PURERETRO_ENABLE_VULKAN` | `ON`    | Enable Vulkan hardware renderer      |

To disable Vulkan support:
```bash
cmake .. -DPURERETRO_ENABLE_VULKAN=OFF
```

## Usage

```bash
# Software-rendered core
./pureretro <core_path> <rom_path> [options]

# Example: NES core
./pureretro ./nestopia_libretro.so ./game.nes

# Example: prefer Vulkan, 3x window, named player, Japanese locale
./pureretro ./mupen64plus_next_libretro.so ./game.n64 \
    --render vk --scale 3 --username Player1 --lang ja
```

### Command-Line Options

| Flag | Argument | Purpose |
|------|----------|---------|
| `--fullscreen`, `-f` | — | Start in fullscreen mode. |
| `--render <api>` | `vk` / `gl` / `sw` | Hint preferred renderer (core may override). |
| `--scale <N>` | 1–16 | Integer window scale relative to the core's base resolution. |
| `--no-audio` | — | Disable audio output (also reflected in `GET_AUDIO_VIDEO_ENABLE`). |
| `--variable <k=v>` | `key=value` | Override a core option. Highest priority; never persisted. |
| `--portable` | — | Use `./system/` for the system directory instead of the user data directory. |
| `--config <path>` | file path | Load a keymap configuration file. |
| `--disk-index <N>` | 0–255 | Initial disc index for multi-disc content. |
| `--lang <code>` | locale code | Language reported via `GET_LANGUAGE`. 30+ codes mapped (`en`, `ja`, `zh_cn`, `de`, ...). |
| `--username <name>` | string | Player name reported via `GET_USERNAME`. |
| `--subsystem <ident>` | string | Load content through `retro_load_game_special` (e.g. `sgb`, `bsx`). |
| `--core-assets-dir <path>` | dir path | Directory reported via `GET_CORE_ASSETS_DIRECTORY`. |
| `--playlist-dir <path>` | dir path | Directory reported via `GET_PLAYLIST_DIRECTORY`. |
| `--file-browser-dir <path>` | dir path | Directory reported via `GET_FILE_BROWSER_START_DIRECTORY`. |
| `--audio-rate <Hz>` | 4000–384000 | Override audio sample rate (default: core's reported rate). |
| `--audio-buffer-ms <ms>` | 1–5000 | Override minimum audio buffer latency (default: 64 ms). |
| `--log-level <lvl>` | `debug`/`info`/`warn`/`error` | Log threshold (also `PURERETRO_LOG` env var). Default `info`. |

### Default Keyboard Mapping

| Keyboard Key  | RetroPad Button |
|---------------|-----------------|
| Arrow Keys    | D-Pad           |
| Z             | B               |
| X             | A               |
| A             | Y               |
| S             | X               |
| Enter         | Start           |
| Right Shift   | Select          |
| Q / W         | L / R           |

Override the default mapping with `--config <path>` pointing to a file with `<scancode> <button>` lines (one per line; e.g. `SPACE A`).

### Window Controls

| Key  | Action            |
|------|-------------------|
| F11  | Toggle Fullscreen |
| Esc  | Quit              |

## License

PureRetro is released under the [MIT License](LICENSE.md).

The bundled `libretro.h` header retains its own MIT-style license from the libretro project.
