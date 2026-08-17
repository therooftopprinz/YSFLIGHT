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

The same opaque program now evaluates exponential fog per vertex and
interpolates the resulting fog factor.  The previous shader evaluated `exp`
for every fragment of every overlapping map polygon.  Turning map fog off
entirely proved that work accounted for about 17% of live frame rate, but made
distant terrain visually wrong.  Vertex fog preserves the atmospheric blend
and averaged 12.77 versus 12.11 FPS across five interleaved live-flight pairs,
about a 5.4% gain.  The exact frozen cockpit scene remained visually equivalent
apart from normal simulation timing differences.

Disabling `GL_BLEND` around those opaque draws looked attractive on paper and
measured no better: interleaved blend enable/disable broke up the tiled render
pass and erased the win.  The `YSGL_OPAQUE_BLEND=1` switch remains for
profiling, but defaults off.  Set `YSGL_OPAQUE_FAST=0` to force the old
discard-bearing shaders.

#### Where a frame actually goes

Guessing at which renderer change would pay off produced a run of changes that
measured as noise, so the stages are now measured instead.  `YSGL_SKIP_MAP`,
`YSGL_SKIP_AIR`, `YSGL_SKIP_GNDMESH`, `YSGL_SKIP_GNDOBJ`, `YSGL_SKIP_FIELD`,
`YSGL_SKIP_HORIZON` and `YSGL_SKIP_CLOUD` each drop one stage, so the frame time
it accounts for is the difference against a run with nothing skipped.  Against
an ~79 ms baseline in live flight the maps cost about 19 ms, the aircraft and
their cockpits about 10 ms, the fallback ground mesh about 6 ms and the ground
objects about 5 ms; the horizon, the field objects and the clouds all came out
inside the noise.  `YSGL_GLINFO=1` prints the depth and stencil bit counts the
context was actually given.

Splitting the map stage showed the cost is entirely its color pass; the
color-masked depth pass that follows is free, because the driver turns it into a
depth-only pass.  That depth pass is still worth keeping: dropping it as well as
the color pass measured *slower* than dropping the color pass alone, since the
depth it leaves behind is what lets the GPU reject the aircraft and ground
objects drawn afterwards.

Shading coplanar map layers only once was tried on top of that cheap-depth
behavior and **removed**.  The idea was a color-masked prepass visiting map
objects and their cached primitives in reverse priority, handing each layer a
small depth bias, then a color pass in the original order testing `GL_EQUAL` so
that only the topmost layer shades, then a third pass restoring the real
geometric depth for aircraft and ground objects.

It cannot coexist with the primitive merging above.  Telling layers apart by
depth needs one bias per layer, but a merged draw carries one bias for all the
polygons inside it.  Those polygons rasterize the same plane from different
triangles, so their depths differ by a bit or two, and `GL_EQUAL` rejects
whichever one did not win the prepare pass.  At Aomori the whole airport arrives
as a single 252-vertex merged primitive, and the taxiway and apron pavement
disappeared, leaving the bare runway, which reads as the pavement z-fighting
with the ground.  Keeping one primitive per polygon for maps makes the frame
pixel-correct, but an interleaved three-pair A/B on the Hawaii benchmark then
measured 12.59 FPS against 13.36 without the whole scheme, a 5.8% loss: the
merging is worth more than the saved overdraw.  The 8% gain recorded during the
first attempt had been measured against the broken image.

Two things worth remembering from the attempt.  `SimDrawGroundMesh` is not
involved in any of this; it draws with `GL_ALWAYS` and `glDepthMask(GL_FALSE)`
and never writes depth under the map.  And a depth bias belongs in the vertex
shader's `zOffset`, not in `glDepthRangef`: the depth range scales, so its
separation shrinks to nothing as a surface approaches the near plane, which is
exactly where runway pavement sits during a takeoff roll, while a clip-space
offset stays constant at any distance.

A five-pass variant that isolated alpha-sensitive primitives was also tested,
but the extra traversal reversed the result to 12.14 versus 13.27 FPS.  Falling
back one plane group at a time was not safe either, because mixing the two depth
conventions changed priority where neighboring groups met.

Four attempts to cut that map cost were measured and reverted:

* **Front-to-back ordering of the map groups.**  Interleaved A/B runs put the
  cached order at 12.27 FPS and a near-to-far sort at 12.07.  Terrain maps are
  laid out side by side on a plane rather than stacked in depth, so they never
  occlude each other and there is nothing for an early depth test to reject.
* **Culling batches inside a map.**  About 40% of the cached batches fall
  outside the frustum, but they are off-screen geometry the GPU already discards
  cheaply, and the per-frame bounding-box tests cost as much as they saved
  (12.72 against 12.73 FPS).  It only helped a frozen camera, which reuses the
  previous frame's results.
* **Dropping the common ground texture sample.**  The small repeating tile is
  cache-friendly and not the bottleneck: textured maps averaged 13.57 FPS,
  while flat-colored maps averaged 13.23.
* **Masking the ground mesh where the maps already drew.**  The 2016 stencil
  optimisation this relies on has never run in the GLES2 build: SDL is asked for
  a 16-bit depth buffer and no stencil, so the context comes back with 0 stencil
  bits and `glStencilFunc(GL_EQUAL,0,255)` passes everywhere.  Asking for
  `SDL_GL_STENCIL_SIZE` 8 does grant a stencil buffer and does make the masking
  work, but carrying it cost more than the masking saved: 12.65 FPS with no
  stencil buffer, 12.14 once one is allocated, and 12.50 with the mesh masked
  off behind the maps.

Back-face culling for the scenery was also tried and reverted at 11.38 against
11.70 FPS; the maps are largely flat and up-facing, so little is back-facing to
begin with.

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

Optional quick start: put a one-line `QUICKFLIGHT` file in `$GAMEDIR` with
`Airplane Field StartPosition` and the launcher adds `-freeflight` for you. This
is a debugging aid that bypasses the title screen, so delete or comment out the
file when you are done, otherwise the port always boots straight into that
flight instead of the menu.

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
