# DOOM 1.10 - Linux 64-bit Build Guide

Original source code released by id Software in 1997. This branch fixes compilation and runtime issues on modern 64-bit Linux.

## Requirements

- 64-bit Linux (Ubuntu 22.04+ / Fedora 38+ or equivalent)
- GCC
- X11 development libraries
- DOOM WAD data file (`doom1.wad`, `doom.wad`, `doom2.wad`, etc.)

## Install Dependencies

```bash
# Ubuntu / Debian
sudo apt install gcc libx11-dev libxext-dev

# Fedora / RHEL
sudo dnf install gcc libX11-devel libXext-devel
```

## Build

```bash
cd linuxdoom-1.10
mkdir -p linux
make
```

The binary is produced at `linuxdoom-1.10/linux/linuxxdoom`.

## Prepare the WAD File

DOOM requires an original data file to run. Copy the WAD to the `linuxdoom-1.10/` directory with a lowercase filename:

| Edition | Filename |
|---------|----------|
| Shareware (free demo) | `doom1.wad` |
| Registered (full episode 1) | `doom.wad` |
| Ultimate DOOM | `doomu.wad` |
| DOOM II | `doom2.wad` |

```bash
cp /path/to/DOOM1.WAD linuxdoom-1.10/doom1.wad
```

## Run

The engine uses TrueColor X11 and runs directly on any modern Linux desktop (X11 or XWayland).

```bash
cd linuxdoom-1.10
./linux/linuxxdoom
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
./linux/linuxxdoom -3
```

## Troubleshooting

**`Error: xdoom: requires a TrueColor display`**
- The X server does not support TrueColor (extremely rare on modern systems)

**`Error: W_InitFiles: no files found`**
- WAD file is missing or has the wrong filename case
- Confirm the WAD is in `linuxdoom-1.10/` and the filename is all lowercase

**`Could not start sound server [sndserver]`**
- The sound server was not included in the source release; this is normal and does not affect gameplay

**`Demo is from a different game version!`**
- Normal message; the bundled demo was recorded with a different version and is skipped

## Porting Notes

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
| Engine requires 8-bit PseudoColor X display (modern X servers don't support it) | Switched to TrueColor; palette converted to 32-bit at blit time |
