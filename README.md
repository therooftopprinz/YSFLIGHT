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

### Native GLES2 build (experimental)

The GL2 renderer can be built directly against EGL/GLES2, without gl4es:

```sh
cmake -S src -B build-gles2 \
  -DYSFLIGHT_NATIVE_GLES2=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-gles2 --target ysflight64_gl2 --parallel
```

This mode enables the SDL backend automatically, requests an OpenGL ES 2.0
context, excludes the desktop GL1 targets, and produces
`build-gles2/main/ysflight64_gl2`. It requires Linux SDL2, EGL, and GLES2
development libraries. Do not put gl4es on `LD_LIBRARY_PATH` when launching
this binary.

In this mode the GLSL renderers route their program binds, buffer binds and
vertex attribute array enables through a shadow state cache
(`ysgl/src/ysglstatecache.[ch]`), which drops redundant calls and defers
attribute enables until a draw needs them. Profiling on the R36S showed the
Mali driver consuming more than half of the frame budget servicing that
traffic; the cache cuts per-frame attribute enables by roughly 97% and is
worth 1.1x-1.5x frames per second depending on the scene. If a new renderer
starts calling those entry points, include the header so it goes through the
cache too, otherwise the shadow copy will drift from the driver's state.

#### Fragment cost on the Mali-G31

With the state cache in place the GLES2 renderer was still three times slower
than GL1 through gl4es, and measuring utilization on a frozen scene explained
why: GLES2 sat at 86% GPU with the process only 24% busy, while GL1 sat at 14%
GPU and 91% CPU. The renderer was fragment-bound, not draw-call-bound, so
batching draws bought nothing. Three changes to the shading path fixed that:

- `YsCalculateStandardLighting` skips lights whose `lightEnabled` is zero.
  YSFlight only ever enables light 0, so the loop was paying for seven extra
  `normalize`/`pow` pairs per fragment.
- The GLES2 build shades per vertex instead of per pixel
  (`YsGLSLSetPerPixRendering`). Set `YSGL_PERPIX=1` to compare against the
  per-pixel path.
- Untextured draws no longer sample the texture, and fog math is skipped when
  `fogDensity` is zero. Both were previously computed and then multiplied out.

On the frozen benchmark scene that took the R36S from 3.9 to about 13.5 frames
per second, roughly 1.4x faster than the GL1/gl4es path it replaces.

#### Opaque geometry without `discard`

Every shared 3D fragment shader ended with an alpha cutoff that always called
`discard`.  On Mali that forces late-Z and disables early depth rejection, which
hurts once opaque scenery starts overdrawing itself.  The GLES2 build now keeps
a second per-vertex shaded program compiled with `YSGLSL_OPAQUE`, which omits
the cutoff entirely, and routes fully opaque scenery through it:

- cached 2D maps without a user texture (vertex alpha is always 1, and the
  optional common ground tile is an opaque grayscale image)
- elevation-grid mesh and side walls when the grid has no own texture
- the shell renderer's `solidShadedPosNomColHd` buffer, which is already
  separated from transparent aircraft and ground-object polygons

Alpha-sensitive draws (user textures, particles, flashes, runway lights, clouds)
stay on the original shaders.  That change alone raised the frozen-scene rate
from about 14.2 to 16.7 fps and live free-flight from 10.5 to 12.2 fps.
Routing solid shaded shell geometry through the same program added another
7% in the frozen scene and 9% in live flight.  The separate solid-unshaded
buffer was tested and reverted: it did not improve the frozen scene and made
live flight about 6% slower.  Set `YSGL_OPAQUE_SHELL=0` to disable only the
successful shaded-shell path.

Because the program is shared, its texture state has to be managed like any
other renderer's.  The scenery sets it to tile the common ground texture, so it
is now reset alongside `Flat`/`MonoColorShaded`/`VariColorShaded`, and the shell
path sets `YSGLSL_TEX_TYPE_NONE` before drawing its untextured buffer.  Without
that, aircraft, ground objects and the joystick models sampled the ground tile
with no texture bound and came out solid black.

Disabling `GL_BLEND` around those opaque draws looked attractive on paper and
measured no better: interleaved blend enable/disable broke up the tiled render
pass and erased the win.  The `YSGL_OPAQUE_BLEND=1` switch remains for
profiling, but defaults off.  `GL_STENCIL_TEST` is still required to mask the
fallback ground mesh behind the scenery map, so it stays enabled.  Set
`YSGL_OPAQUE_FAST=0` to force the old discard-bearing shaders.

#### Benchmarking on device

`tools/device_bench*.sh` run a fixed scene unattended. They drive the game with
`-script` (`tools/bench_static.txt`), which dismisses the startup dialogs and
the "center the joystick" prompt — that prompt waits for a button press and
will otherwise hang any headless run — and then issues
`BTNFUNC:PAUSESIMULATION` so every binary renders the identical frozen frame.
Free-running gameplay numbers come from `tools/bench_freeflight.txt`.

---

## Deploy (R36S / PortMaster)

Target layout used by this port:

```text
/roms/ports/YSFlight.sh          # PortMaster launcher (native GLES2)
/roms/ports/ysflight/            # game directory ($GAMEDIR)
  ysflight64_gles2               # what the launcher runs
  ysflight64_gl1                 # gl4es build, kept for comparison only
  config/ctlassign.cfg           # R36S mapping (not stock)
  config/flight.cfg              # kept across deploys; FPS counter optional
  language/en.uitxt              # menu strings must match the binary
  gl4es/                         # gl4es libGL, only used by ysflight64_gl1
  … aircraft, scenery, etc.
```

The port ships a single entry running the native GLES2 binary, which is about
1.3x faster in flight than the GL1/gl4es build it replaced. The gl4es binary
and libraries stay on the card so the two paths can still be compared, but
nothing in the menu launches them.

### 1. Install / refresh the port tree once

Copy a full runtime (or an existing port package) to `/roms/ports/ysflight/`, plus:

- Launcher script as `/roms/ports/YSFlight.sh` (PortMaster calls this; see notes below)

### 2. Update the binary and mapping after a rebuild

From a machine that can SSH to the handheld (replace host as needed):

```sh
HOST=ark@192.168.253.10
GAMEDIR=/roms/ports/ysflight
BIN=build-gles2/main/ysflight64_gl2

ssh "$HOST" "pkill -9 ysflight64_gles2 2>/dev/null; mkdir -p $GAMEDIR/config" || true
scp "$BIN" "$HOST:$GAMEDIR/ysflight64_gles2"
scp path/to/r36s-ctlassign.cfg "$HOST:$GAMEDIR/config/ctlassign.cfg"
scp build-gles2/main/language/en.uitxt "$HOST:$GAMEDIR/language/en.uitxt"
ssh "$HOST" "chmod +x $GAMEDIR/ysflight64_gles2"
```

**Critical:** always pass `-configdir` to the binary so it reads `$GAMEDIR/config`. Without it, YSFLIGHT stores config under `$HOME/Documents/YSFLIGHT.COM/YSFLIGHT/config` and falls back to the built-in stick assignment (absolute throttle on right X).

Launcher essentials:

```sh
export SDL_VIDEODRIVER=kmsdrm
export SDL_VIDEO_GL_DRIVER=libGLESv2.so
export SDL_VIDEO_EGL_DRIVER=libEGL.so
unset LIBGL_ES LIBGL_GL LIBGL_FB DISPLAY
./ysflight64_gles2 -configdir "$GAMEDIR/config"
```

gl4es must stay off `LD_LIBRARY_PATH` here: its `libGL.so.1` would shadow the
Mali driver. The launcher switches the CPU governor to `performance` while the
game runs and restores the previous one on exit.

Drop a file named `PERPIXEL` in `$GAMEDIR` to force per-pixel shading, which
looks slightly better and roughly halves the frame rate.

Optional quick start: put a one-line `QUICKFLIGHT` file in `$GAMEDIR` with `Airplane Field StartPosition`, then launch with `-freeflight` as well.

### 3. Sanity checks after deploy

- `ctlassign.cfg` must **not** contain `AXS 0 2 THROTTLE` or `TRG` lines (those mark the stock upstream mapping).
- R36S mapping uses `AXS 0 2 RUDDER` and leaves axis 3 unassigned so the SDL backend can drive incremental throttle while R1 is held.
- Confirm md5 of `ysflight64_gles2` matches the local build if you are iterating quickly.

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
