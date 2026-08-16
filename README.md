# YS Flight Simulator

by CaptainYS  
http://www.ysflight.com

This repository is a fork of [captainys/YSFLIGHT](https://github.com/captainys/YSFLIGHT) with changes aimed at handheld Linux devices (notably the R36S / RK3326), plus a few flight-UX fixes that help on a gamepad.

Upstream introduction and history are unchanged: YSFLIGHT is a casual flight simulator by Soji Yamakawa. Please see the original project and [ysflight.com](http://www.ysflight.com) for the full story.

**Companion fork:** the SDL2 window backend lives in [therooftopprinz/captainys_public](https://github.com/therooftopprinz/captainys_public). You need both forks to build the handheld port.

---

## What this fork changes

### SDL2 window backend (via `public`)

Stock Linux YSFLIGHT opens an X11/GLX window. On the R36S Mali stack that path is not useful for GPU rendering: the blob exposes EGL on GBM, not GLX. This fork’s sibling `public` tree adds an SDL2 `fssimplewindow` backend so the game can run fullscreen through SDL (typically KMSDRM) and gl4es, with no X server.

Build with `-DYSFLIGHT_USE_SDL=ON` (see below).

### Handheld / gamepad UX

| Change | Why |
|--|--|
| **Auto trim is a mode** (toggle), not a one-shot `trim += elevator` | On a handheld, reaching Select releases the stick first, so the old capture almost always read zero. While the mode is on, trim walks toward held elevator (~0.5× deflection per second). `A/TRIM` shows on the HUD. It pauses in manoeuvres (hard elevator, aileron/rudder, >30° bank, or on ground). |
| **HUD cycle** (`TOGGLEHUD`, key `9` / Select+D-pad Left in the R36S map) | Cycles HUD+panel → panel only → HUD only (forces a HUD on aircraft that only ship an instrument panel). |
| **Spoiler steps** | Extend/retract by 20% per press (was 25%). |
| **Select+Start quit path** (SDL backend) | Quits cleanly back to EmulationStation / PortMaster without hunting File → Exit. |
| **Optional `YSFPSLOG`** | When set in the environment, prints `YSFPS xx.xx` about twice a second for remote sampling. |

R36S pad mapping (cursor vs flight modes, R1-gated incremental throttle, Select chords, on-screen keyboard) is implemented in the SDL wrapper and a dedicated `ctlassign.cfg`. Do **not** deploy the stock `runtime/config/ctlassign.cfg` from a vanilla build to the handheld — that mapping puts absolute throttle on the right stick and feels like the sticks are swapped.

---

## Build (desktop / upstream-style)

Prerequisite: a C/C++ compiler and CMake.

```sh
git clone https://github.com/therooftopprinz/captainys_public.git
git clone https://github.com/therooftopprinz/YSFLIGHT.git
cd YSFLIGHT
mkdir build && cd build
cmake ../src
cmake --build . --config Release --parallel
```

That is the classic X11/GLX Linux build (or the platform default on Windows / macOS).

CMake looks for a sibling directory named `captainys_public` or `public`.

---

## Build (SDL / R36S-style)

Use this when you want the handheld SDL2 backend.

### Dependencies (Debian/Ubuntu aarch64 example)

- `build-essential`, `cmake`
- `libsdl2-dev`
- OpenGL / X11 headers still needed for some of the public libraries (`libgl-dev`, `libglu1-mesa-dev`, `libx11-dev`)
- On device or for the device binary: [gl4es](https://github.com/ptitSeb/gl4es) providing `libGL.so.1` that the launcher puts on `LD_LIBRARY_PATH`

Clone **this** fork of captainys’ public libraries next to `YSFLIGHT` (same parent directory). CMake accepts either folder name `captainys_public` or `public`.

```sh
git clone https://github.com/therooftopprinz/captainys_public.git
git clone https://github.com/therooftopprinz/YSFLIGHT.git
cd YSFLIGHT
mkdir build-sdl && cd build-sdl
cmake -DYSFLIGHT_USE_SDL=ON -DCMAKE_BUILD_TYPE=Release ../src
cmake --build . --config Release --parallel
```

Binary (64-bit OpenGL1 path used on R36S):

```text
build-sdl/main/ysflight64_gl1
```

Runtime data (aircraft, scenery, default configs) is staged under `build-sdl/main/` next to the binary.

---

## Deploy (R36S / PortMaster)

Target layout used by this port:

```text
/roms/ports/YSFlight.sh          # PortMaster launcher
/roms/ports/ysflight/            # game directory ($GAMEDIR)
  ysflight64_gl1
  config/ctlassign.cfg           # R36S mapping (not stock)
  config/flight.cfg              # kept across deploys; FPS counter optional
  language/en.uitxt              # menu strings must match the binary
  gl4es/                         # gl4es libGL for Mali/EGL
  … aircraft, scenery, etc.
```

### 1. Install / refresh the port tree once

Copy a full `build-sdl/main/` runtime (or an existing port package) to `/roms/ports/ysflight/`, plus:

- Launcher script as `/roms/ports/YSFlight.sh` (PortMaster calls this; see notes below)
- gl4es libraries under `$GAMEDIR/gl4es/`

### 2. Update the binary and mapping after a rebuild

From a machine that can SSH to the handheld (replace host as needed):

```sh
HOST=ark@192.168.253.119
GAMEDIR=/roms/ports/ysflight
BIN=build-sdl/main/ysflight64_gl1

ssh "$HOST" "pkill -9 ysflight64_gl1 2>/dev/null; mkdir -p $GAMEDIR/config" || true
scp "$BIN" "$HOST:$GAMEDIR/ysflight64_gl1"
scp path/to/r36s-ctlassign.cfg "$HOST:$GAMEDIR/config/ctlassign.cfg"
scp build-sdl/main/language/en.uitxt "$HOST:$GAMEDIR/language/en.uitxt"
ssh "$HOST" "chmod +x $GAMEDIR/ysflight64_gl1"
```

**Critical:** always pass `-configdir` to the binary so it reads `$GAMEDIR/config`. Without it, YSFLIGHT stores config under `$HOME/Documents/YSFLIGHT.COM/YSFLIGHT/config` and falls back to the built-in stick assignment (absolute throttle on right X).

Launcher essentials:

```sh
export LIBGL_ES=2 LIBGL_GL=21 LIBGL_FB=4
export LD_LIBRARY_PATH="$GAMEDIR/gl4es:$LD_LIBRARY_PATH"
unset DISPLAY SDL_VIDEODRIVER
./ysflight64_gl1 -configdir "$GAMEDIR/config"
```

Optional quick start: put a one-line `QUICKFLIGHT` file in `$GAMEDIR` with `Airplane Field StartPosition`, then launch with `-freeflight` as well.

### 3. Sanity checks after deploy

- `ctlassign.cfg` must **not** contain `AXS 0 2 THROTTLE` or `TRG` lines (those mark the stock upstream mapping).
- R36S mapping uses `AXS 0 2 RUDDER` and leaves axis 3 unassigned so the SDL backend can drive incremental throttle while R1 is held.
- Confirm md5 of `ysflight64_gl1` matches the local build if you are iterating quickly.

### Controls (short)

| Input | Effect |
|--|--|
| Select (quick tap) | MENU/CURSOR ↔ FLIGHT |
| Select + Y (flight) | Auto trim mode on/off |
| Select + Start | Quit to EmulationStation |
| Select + D-pad Left (flight) | Cycle HUD / panel |
| Right stick Y + hold R1 | Incremental throttle |

Full pad tables live in the port’s `CONTROLS.md` when shipped with the handheld package.

---

## Upstream notes (unchanged)

The first line of code was written in 1998. Much of the codebase is intentionally C-like C++, and the public library overlaps with what the C++ STL provides today. Experimental iOS support was abandoned and likely no longer builds.

You may find `CMakeFiles` directories in the source tree; they were kept deliberately so an accidental in-tree `cmake` invocation would not contaminate the sources.

---

## Credits

* Soji Yamakawa — YSFlight (BSD-3-Clause)
* ptitSeb — gl4es (Mali / GLES translation used on device)
