# DOOM 1.10 - Linux & macOS Port

Original source code released by id Software in 1997. This port fixes compilation and runtime issues on modern 64-bit systems and adds native macOS support via SDL2.

## Platforms

- **Linux**: Runs on modern 64-bit Linux with X11 (original backend) or SDL2 (cross-platform backend)
- **macOS**: Runs natively on both Intel and Apple Silicon Macs via SDL2

## Requirements

### macOS

- macOS 10.15+ (Catalina or later)
- Xcode Command Line Tools or Xcode
- Homebrew
- CMake
- SDL2
- DOOM WAD data file (`doom1.wad`, `doom.wad`, `doom2.wad`, etc.)

### Linux

- 64-bit Linux (Ubuntu 22.04+ / Fedora 38+ or equivalent)
- GCC
- Either:
  - X11 development libraries (for X11 backend)
  - SDL2 (for SDL2 backend)
- DOOM WAD data file

## Build Instructions

### macOS (SDL2 - default)

```bash
# Install dependencies
brew install sdl2 cmake pkg-config

# Configure and build
PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig" cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# The binary is at build/linuxdoom-1.10/doom
```

### Linux with X11 (original backend)

```bash
# Install dependencies
# Ubuntu / Debian
sudo apt install gcc libx11-dev libxext-dev

# Fedora / RHEL
sudo dnf install gcc libX11-devel libXext-devel

# Build with Makefile
cd linuxdoom-1.10
mkdir -p linux
make

# The binary is at linuxdoom-1.10/linux/linuxxdoom
```

### Linux with SDL2 (optional)

```bash
# Install dependencies
# Ubuntu / Debian
sudo apt install gcc cmake libsdl2-dev pkg-config

# Fedora / RHEL
sudo dnf install gcc cmake SDL2-devel pkg-config

# Configure and build
cmake -B build -DUSE_SDL2=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build

# The binary is at build/linuxdoom-1.10/doom
```

## Prepare the WAD File

DOOM requires an original data file to run. Copy the WAD to your working directory with a lowercase filename:

| Edition | Filename |
|---------|----------|
| Shareware (free demo) | `doom1.wad` |
| Registered (full episode 1) | `doom.wad` |
| Ultimate DOOM | `doomu.wad` |
| DOOM II | `doom2.wad` |

```bash
# macOS or Linux with CMake build
cp /path/to/DOOM1.WAD build/linuxdoom-1.10/doom1.wad

# Linux with Makefile build
cp /path/to/DOOM1.WAD linuxdoom-1.10/doom1.wad
```

## Run

### macOS

```bash
cd build/linuxdoom-1.10
./doom
```

### Linux (X11 backend)

```bash
cd linuxdoom-1.10
./linux/linuxxdoom
```

### Linux (SDL2 backend)

```bash
cd build/linuxdoom-1.10
./doom
```

## Resolution Options

Default is 2x (640×400). Use flags to adjust:

| Flag | Resolution |
|------|------------|
| (default) | 640×400 |
| `-2` | 640×400 |
| `-3` | 960×600 |
| `-4` | 1280×800 |

```bash
# Example: run at 3x resolution
./doom -3
```

## Additional Options

- `-grabmouse`: Capture mouse cursor (bypasses OS mouse acceleration)
- `-iwad <file>`: Specify WAD file explicitly (e.g., `-iwad doom2.wad`)

## Troubleshooting

### macOS

**`dyld: Library not loaded: libSDL2`**
- SDL2 not installed or not in library path
- Solution: `brew install sdl2`

**`Permission denied` when running**
- Binary not executable
- Solution: `chmod +x doom`

**Black screen or no window**
- Missing WAD file
- Solution: Ensure `doom1.wad` (or other WAD) is in the same directory as the executable

### Linux (X11 backend)

**`Error: xdoom: requires a TrueColor display`**
- The X server does not support TrueColor (extremely rare on modern systems)

**`Error: W_InitFiles: no files found`**
- WAD file is missing or has the wrong filename case
- Confirm the WAD is in the correct directory and the filename is all lowercase

**`Could not start sound server [sndserver]`**
- The sound server was not included in the source release; this is normal and does not affect gameplay

**`Demo is from a different game version!`**
- Normal message; the bundled demo was recorded with a different version and is skipped

### SDL2 backend (macOS & Linux)

**`Could not initialize SDL`**
- SDL2 not installed properly
- macOS: `brew install sdl2`
- Linux: `sudo apt install libsdl2-dev` or `sudo dnf install SDL2-devel`

**Audio crackling or distortion**
- Buffer size issues (rare with SDL2)
- SDL2 audio driver may need configuration

## Architecture

### Graphics Backends

| Backend | Platform | Description |
|---------|----------|-------------|
| X11 (original) | Linux only | Direct X11 with TrueColor and XShm support |
| SDL2 | macOS & Linux | Cross-platform with hardware acceleration |

The SDL2 backend uses:
- `SDL_CreateWindow` / `SDL_CreateRenderer` for display
- `SDL_Texture` for framebuffer (hardware-accelerated)
- 8-bit indexed color → 32-bit RGBA conversion (same as X11 backend)
- Native scancode-based input handling

### Audio Backends

| Backend | Platform | Description |
|---------|----------|-------------|
| OSS (original) | Linux only | `/dev/dsp` device with `ioctl()` configuration |
| SDL2 | macOS & Linux | Cross-platform callback-based audio |

The SDL2 audio backend:
- Preserves the original 8-channel mixing algorithm exactly (from `i_sound.c` lines 540-655)
- Uses 11025 Hz, 16-bit stereo output (matching original)
- Audio callback pulls mixed data from game thread

## Porting Notes

### Original 64-bit Linux Fixes

The original source targets 32-bit Linux from 1997. The following changes were required for modern 64-bit systems:

| Issue | Fix |
|-------|-----|
| `false`/`true` are C23 keywords | Added `-std=gnu89` to Makefile |
| `<errnos.h>` no longer exists | Changed to `<errno.h>` |
| `extern int errno` conflicts with glibc TLS | Removed; use `#include <errno.h>` instead |
| `default_t` stored pointers in `int` (truncated) | Changed to `intptr_t` |
| `maptexture_t` `void**` field broke struct alignment | Changed to `int` to match WAD binary layout |
| Pointer array allocations hardcoded `*4` | Changed to `sizeof(*ptr)` |
| Pointer alignment arithmetic used `(int)ptr` | Changed to `(intptr_t)ptr` |
| Colormap not installed, causing wrong colors | Added `XInstallColormap()` |
| `audio_fd` defaults to 0 (stdin) in SNDSERV mode, causing SIGPIPE | Initialized `audio_fd = -1`; added guard in `I_SubmitSound` |
| Engine requires 8-bit PseudoColor X display (not supported on modern X servers) | Switched to TrueColor; palette converted to 32-bit at blit time |

### macOS Port Changes

Additional changes for macOS support:

| Issue | Fix |
|-------|-----|
| X11 not available on macOS by default | Added SDL2 graphics backend (`i_video_sdl.c`) |
| Linux OSS audio (`/dev/dsp`) not available on macOS | Added SDL2 audio backend (`i_sound_sdl.c`) |
| `<values.h>` not available on macOS | Use `<limits.h>` with `INT_MAX`/`INT_MIN` on Apple platforms |
| `<malloc.h>` not available on macOS | Use `<stdlib.h>` (standard location for `malloc`) |
| `<alloca.h>` not available on macOS | Use `<stdlib.h>` (alloca is built-in) |
| SDL2 MMX headers incompatible with ARM64 | Added `SDL_DISABLE_IMMINTRIN_H` compile definition |
| Build system is Linux-specific Makefile | Migrated to CMake with platform detection |
| SNDSERV external sound server conflicts with SDL2 | Disabled SNDSERV when `USE_SDL2` is defined |

## File Structure

```
DOOM/
├── CMakeLists.txt                    # Root CMake configuration
├── README.md                         # This file
└── linuxdoom-1.10/
    ├── CMakeLists.txt                # Project CMake configuration
    ├── Makefile                      # Original Linux Makefile (still works)
    ├── i_video.c                     # X11 graphics backend (Linux)
    ├── i_video_sdl.c                 # SDL2 graphics backend (macOS & Linux)
    ├── i_sound.c                     # OSS audio backend (Linux)
    ├── i_sound_sdl.c                 # SDL2 audio backend (macOS & Linux)
    ├── i_system.c                    # POSIX system interface (cross-platform)
    ├── i_net.c                       # Berkeley sockets networking (cross-platform)
    ├── i_main.c                      # Platform entry point
    ├── d_*.c, p_*.c, r_*.c, s_*.c    # Game logic (platform-independent)
    └── ... (other game source files)
```

## License

DOOM source code is licensed under the GNU GPL v2. See LICENSE file for details.

Original copyright (C) 1993-1996 id Software, Inc.

## Credits

- **Original DOOM**: id Software (John Carmack, John Romero, et al.)
- **64-bit Linux port**: Based on linuxdoom-1.10 with modern fixes
- **macOS port**: SDL2 backend implementation (2026)
