# Eglantine on macOS 13 (Ventura), Apple Silicon

This fork's `macos13` branch builds and runs Eglantine on macOS 13 on Apple Silicon. Upstream's
macOS builds target macOS 15 and are built on a macOS 26 runner. Tested on a MacBook with an M1,
16 GB of RAM, and macOS 13.5.2.

No game files, keys or firmware are part of this repository. Switch 2 games run from `.nup`
containers or loose dumps. Those are already decrypted, so no keys are needed. No firmware is
needed either, because system archives and fonts are synthesized.

## Building

Requirements:

- Xcode 15 (only for the macOS SDK and linker; it does not need to be selected with `xcode-select`)
- Homebrew with `llvm`, `cmake`, `ninja`, `meson`, `nasm`, `boost`, `libusb`, `lz4`, `zstd`,
  `openssl@3`, `pkgconf`, `autoconf`, `automake`, `libtool`
- About 15 GB of free disk space for the build tree

```sh
tools/macos13/build.sh            # builds into build/macos13 (eden-cli + eglantine.app)
tools/macos13/package.sh          # build/macos13/package/Eglantine.app and a zip
```

The first build also builds two tools into `build/macos13/_tools`:

- `glslangValidator`: Homebrew has no macOS 13 package of it.
- A static FFmpeg with dav1d: the bundled macOS FFmpeg has no AV1 decoder (see below).

How the toolchain is set up and why:

- **Compiler.** Homebrew LLVM, with its libc++ linked statically. The tree needs a newer C++
  library than macOS 13 ships (`std::jthread`, among others). Linking it statically keeps the app
  free of dylibs from outside the system.
- **Exports.** The executables export only `main` (`tools/macos13/exported_symbols.txt`). If they
  exported their weak C++ definitions, dyld would coalesce them with the system libc++ that Apple's
  frameworks load. Objects made by one copy of libc++ would then be destroyed by the other, which
  crashed on the first iostream.
- **Bundled libraries.** The project's bundled packages are used for Qt 6.11 (built for macOS
  13.0), SDL3, OpenSSL and MoltenVK.

## Running

Open `Eglantine.app`, or start a game directly:

```sh
Eglantine.app/Contents/MacOS/eglantine -g "/path/to/game.nup"
Eglantine.app/Contents/MacOS/eden-cli -g "/path/to/game.nup"     # SDL frontend, no game list
```

For Ocarina of Time, the Qt app asks whether to use recommended settings. Answering Yes is a
reasonable choice. On an M1, lowering the resolution (Graphics → Resolution) is the setting that
helps most.

### Interface

The default theme, "Ryujinx", follows Ryujinx's layout (sizes and colours taken from Ryujinx's
MIT-licensed `ApplicationListView.axaml`, `MainMenuBarView.axaml` and `Themes.xaml`): menus in the window, a toolbar with
list/grid buttons, an icon size slider, sorting and search, games as cards (icon, title, file, version; title ID and type; last
played, time played, size), menus File / Options / Actions / View / Help, and Ryujinx's status bar
(game count and total play time while idle; docked mode and volume in game) plus **Controls**. The other themes are under
Settings → General → UI.

**Controls** on the status bar, or Emulation → Controls..., opens the button mapping directly
(keyboard or controller, per player).

### Speed options (Options menu)

Switch 2 games run at the speed they present frames: at 8 fps, a game made for 60 runs at about
an eighth of its speed (a 45-second cutscene takes minutes). **Options → Frame Skip** lets the
game keep running while the GPU draws only some of its frames: the picture is choppier, but the
game itself gets closer to normal speed. It takes effect immediately and also runs a frame ahead,
except in Ocarina of Time (see below). **Run a Frame Ahead** on its own overlaps the game's CPU
work with the GPU's.

**Ocarina of Time runs in real time** below 60 fps with a built-in patch ported from upstream
Eglantine (c6d2acd37a): the game normally steps a fixed 1/60 s per frame, and the patch makes
it step by the measured frame time instead. The status bar shows "Game speed: N%". Turn it off in
the game's Add-Ons ("Real-time speed (built-in)") or with `EDEN_NX2_OOT_REAL_TIME=0`. Upstream's
Max Resolution patch is not included: on a slow GPU the game's dynamic resolution helps.

**Options → Ocarina of Time Speed** sets the largest step one frame may take: Normal (4/60 s, full
speed down to 15 fps; below that the game slows down), Faster (8/60 s, down to 7.5 fps), Full
speed down to 5 fps (12/60 s) or down to 4 fps (15/60 s); the last two are experimental. Walking,
the camera, ladders and running into a tree were checked at 6, 8 and 12, and walking, swimming and
climbing at 15; fast action (combat, long jumps) has not been. `EDEN_NX2_OOT_MAX_DELTA` does the
same for `eden-cli`. The step follows a smoothed frame time; a frame under half the average (the
first after a load) pulls it down at once, so the game doesn't run up to twice full speed for a
second after each load.

Ocarina of Time a frame ahead renders each frame with data the game already wrote for the next
one (one-frame white or overexposed flashes). With frame skip those frames were the only ones shown,
so the 3D picture stayed black or blown out while the HUD updated: frame skip no longer runs it a
frame ahead (`EDEN_NX2_FRAMES_AHEAD=2` is frame skip's mode for `eden-cli`).

## What works (tested with The Legend of Zelda: Ocarina of Time, `0400486024DEC000`)

- The NUP loads with no keys and no firmware.
- The title screen runs at 60 fps, including its AV1 video background.
- The AV1 intro cutscene plays, with subtitles.
- New Game reaches the first 3D scene (the forest near the Deku Tree), which renders.
- Saves load; Link's house renders with its door (a mesh shader draw, see below).
- A 15-minute run stayed up with no crash or device loss.

## Performance on an M1 (8-core GPU, 16 GB)

Kokiri Forest gameplay, handheld, 0.5x, `eden-cli` with a hidden window (60 fps-equivalent =
full speed):

| Setting | Frames per second | Frames shown | Game speed |
|---|---|---|---|
| Normal | ~5.5 | ~5.5 | ~21 fps-equivalent (35%) |
| Draw every 3rd frame | ~10.5 | ~3.5 | ~41 (68%) |
| OoT Speed: full speed down to 5 fps | ~5.5 | ~5.5 | ~60 (100%) |

In the app (docked, 0.5x) frames take 210-240 ms: full speed down to 5 fps showed "Game speed:
80-100%", down to 4 fps 100% in five of six readings. Docked 0.75x took ~280 ms and 1x ~340 ms
(60%). Handheld 0.75x takes ~200-210 ms (the game does less work handheld than docked), about the
same as docked 0.5x, with a 1080x607 scene and a full 1440x810 frame instead of 960x540. The
coloured speckle noise that covered 0.75x is fixed (see below). A lighter strip runs down the right edge of the 3D picture (about 3% of its width).

The GPU is the limit: ~93% busy during gameplay. Frame time barely follows the resolution (0.25x
~157 ms, 0.5x ~180 ms, 1x ~235 ms): most of it is per-draw and geometry work (about 11,400 draws a
frame, 9,300 of them with one simple position-only vertex program) plus one lighting pass whose
fragment program reads memory through ~112 global-memory lookups per pixel (~16 ms, not scaled by
the resolution setting).

Measured without gain (Nov 2026): GPU fence behaviour Immediate or Balanced; the asynchronous acquire
submitting instead of waiting while suspended; polling the timeline semaphore instead of sleeping;
skipping the waits for GPU-written command lists altogether (wrong, ~5%). The GPU thread waits
mostly on other channels' semaphore releases and on the command lists the game's culling writes,
which are real dependencies.

Upstream's hot-page and GPU translation cache commits (f27522bfb9, b1e650849e) were tried and
reverted: no measurable gain on the M1, and 2 of 13 boots with them went bad (a GPU hang on a
semaphore that read 0, and a crash on garbage GPU commands) against 0 of 7 without.

- Title screen: 60 fps.
- At 1x the GPU is ~94% busy. A Metal System Trace puts ~55 ms of every frame in one pass, the main
  1440x810 scene pass, mostly three of the game's material shaders. FXAA, the scaling filter and
  post-processing effects cost almost nothing.
- Running a frame ahead overlaps the game's CPU work with the GPU's; it's opt-in because the
  game rewrites per-frame data the emulator hasn't read yet (fog flicker).
- Memory: the process settles around 12 GB (about 5 GB is the game's own RAM). With other apps
  open, a 16 GB Mac swaps. Close what you can.

Upstream's own measurements on desktop GPUs are around 50 ms of GPU time per gameplay frame. A
base M1 is several times slower than that, so expect slow gameplay on this class of hardware.
M1 Pro, Max or Ultra and later chips should do proportionally better.

## Changes in this fork

| Problem on macOS 13 / Apple Silicon | Fix |
|---|---|
| Upstream builds need macOS 15 and a newer libc++ | `tools/macos13/build.sh`: deployment target 13.0, Homebrew LLVM with static libc++ |
| Heap corruption at startup (two libc++ copies) | Export only `main` from the executables |
| `assert page size 0x4000 is incompatible with 4K paging` | 16K host pages skip fastmem. Fastmem with 16K host pages (a host page mapped while its four guest pages are consecutive backing on a 16K boundary, other accesses falling back to the page table) is opt-in, `EDEN_FASTMEM_16K=1`: it faults millions of times while a game loads, and OoT corrupted its memory around loads in about a quarter of boots with it (never without it; cause not found yet) |
| The game's cores spent ~39% of their busy time spinning on dynarmic's global exclusive-monitor lock | The monitor locks per address (64 stripes), with the old semantics (`EDEN_NX2_SERIALIZE_ATOMICS=1` takes the global lock too). Compare-and-swap without a lock (`Unsafe_IgnoreGlobalMonitor`, `DYNARMIC_LOCAL_MONITOR=1`) misses ABA and is opt-in |
| A fault on a 128-bit ordered store in fastmem crashed ("Segfault wasn't at a fastmem patch location") | The patch location is the store, not the barrier before it |
| Indirect branches always went through the dispatcher's hash lookup | Fast dispatch table for the ARM64 backend (the x64 backend had one) |
| Every guest exclusive store was a call into the emulator | Opt-in (`DYNARMIC_INLINE_EXCLUSIVES=1`): an inline compare-and-swap (`CASAL`) on the fastmem address. Off by default while memory corruption seen during loading is investigated |
| JIT bookkeeping took ~5 GB (hash maps per translated block, six cores) | Compact per-block lists: the process went from ~20 GB to ~15 GB |
| A slow GPU slows the whole game (its speed follows its frame rate) | Frame skip: skipped frames run the game's commands but drop draws, clears and large compute dispatches, and aren't shown. Only busy 3D frames are skipped (most of the last 32 with 300+ draws), so loading screens and menus run in full |
| `eden-cli` could not find MoltenVK | Look next to the executable and in a sibling `eglantine.app` |
| AV1 movies black (title background, intro) | FFmpeg with dav1d (`tools/macos13/build-ffmpeg-av1.sh`) |
| Metal validation: uniform bound near the end of the stream buffer could read past it | Pad the stream buffer by 64 KiB |
| Coloured speckle noise over the 3D picture at 0.75x | OOT's own upscaler writes its images through R32_UINT views, and they are scaled down for that compute pass and up again every frame. The bilinear scale blits mixed the bytes of the packed words wherever samples fell between texels (0.75x, not 0.5x). Images with an integer view are scaled unfiltered |
| The upscaler's full 1440x810 output (handheld) was shrunk to the scaled size and stretched back | A 2D blit from a full-size source into a rescaled destination that lands 1:1 reads the source directly (`EDEN_NX2_BLIT_KEEP_SRC=0` restores the old path). Not when the blit would shrink it: docked, 1920x1080 into 1280x720, that tinted the picture |
| Blending enabled on integer render targets | Leave blending off there (Vulkan ignores it; Metal does not) |
| Device lost (`Internal Error 00000106`) in the first 3D scene | Clamp framebuffer size to `maxFramebufferWidth/Height`: attachment-less passes were sized 32768x32768 from the guest's surface clip |
| Null texture fallback had 2D views for every texture type (on a 1D image) | One null image per dimension, and a view of each declared type |
| No `VK_EXT_mesh_shader`: mesh draws skipped (OOT: the door of Link's house, ~30 draws a frame) | Task and mesh programs run as compute shaders that write a vertex buffer, drawn by a generated vertex shader (`vk_mesh_emulation.h`) |
| Every draw sampling the bound depth buffer ended the render pass (~95 per OOT frame) | End the pass only if it wrote depth or stencil (185 → 160 ms per frame on an M1) |
| Washed-out, white picture with pink outlines | The volumetric fog compute program is miscompiled by Metal into NaNs and infinities, which the bloom spreads over the frame. That program's float results are sanitized on MoltenVK, and render target and image outputs are made finite as NVIDIA's conversions give |
| CPU and GPU never overlap (each present waits for its own frame) | Opt-in: Options → Run a Frame Ahead (`EDEN_NX2_FRAMES_AHEAD=1`), faster but OOT's fog flickers |

## Known issues

- **Frame skip** makes the picture choppier; while the camera moves the game's temporal
  anti-aliasing smears a little (its history is several game frames old), and now and then falling
  leaves or a light shaft show white or purple for a frame.

- **Mesh shaders** are emulated with compute shaders (see above). Indirect mesh draws are still
  skipped, and many OOT mesh draws arrive with a zero count (upstream reads a GPU-written count
  stale), so some mesh geometry may still be missing. `EDEN_NX2_NO_MESH_EMULATION=1` turns the
  emulation off.
- **Metal compiler crashes.** macOS 13's Metal compiler crashes ("Compiler encountered an internal
  error") on two of the game's compute shaders, so those dispatches are skipped.
- **Lost video frames.** Video frames are dropped when the game draws more slowly than the movie
  plays. This is deliberate, to keep the picture in sync with the sound.
- **Signing.** The app is ad-hoc signed, not notarized.

## Debug switches added here

- `EDEN_NX2_LOG_PROGRAMS=1` logs each native program as it is lowered (address, key and hash),
  and the first draw of each program pair and target, with target size, depth, surface clip and
  scissor. Pair it with `EDEN_NX2_REJECT_HASH` to bisect a crash that follows new programs.
- `EDEN_NX2_SKIP_NO_COLOR=1|2|3` drops draws that have no colour target: all of them, only
  attachment-less ones, or only depth-only ones.
- `EDEN_NX2_GPU_TIMING=1` counts render passes and dispatches per second by target size and
  program, how render passes switch, and the call sites that end them. (MoltenVK on Apple GPUs
  timestamps per command buffer, so the per-pass times read zero.)
- `EDEN_NX2_LOG_FPS=1` (eden-cli) logs the frame rate and frame time every 5 s.
- `EDEN_NX2_GPU_TIMING=2` labels render passes (`RP#n`) and dispatches for
  `xcrun xctrace record --template 'Metal System Trace'`; `=3` gives every draw its own pass.
- `EDEN_NX2_IR_DUMP=<hash>,<dir>` writes a program's IR (graphics programs also as `.spv`; `all`
  for every graphics program); `EDEN_NX2_NAN_SANITIZE=<hash>:<ops>`
  replaces NaN/infinite results of chosen IR operations with 0 (finding NaN sources).
- `EDEN_NX2_FRAME_SKIP=<n>` draws one frame in n + 1 (a path instead of a number: the file holding
  n, re-read every 30 frames). `EDEN_NX2_FRAME_SKIP_WORK` (bits: 1 draws, 2 clears, 4 compute)
  chooses what skipped frames drop; compute dispatches of at most `EDEN_NX2_FRAME_SKIP_COMPUTE_MAX`
  threads (4096) still run, as do programs listed in `EDEN_NX2_FRAME_SKIP_KEEP=<hash>,...`;
  `EDEN_NX2_FRAME_SKIP_PREP=1` runs all compute on the frame before a drawn one.
- `EDEN_NX2_SERIALIZE_ATOMICS=1` (always) or `=a` (outside busy 3D frames) makes the guest's atomics
  also take one global lock.
- `EDEN_NX2_PENDING_ACQUIRE=1` lets a semaphore acquire on the GPU thread pass on a release already
  queued to the host GPU instead of waiting for it. Experimental: in OoT the game corrupted its
  memory with it (as with GPU accuracy Low), so it is off.
- `EDEN_NX2_PENDING_ACQUIRE_ONLY=<device address>,...` limits the above to these semaphores.
  `EDEN_NX2_ACQUIRE_STATS=1` logs, every 10 s, each semaphore the GPU thread waited on: waits per
  second, time waited and whether a GPU release met it. In OoT the waits are releases from other
  channels' later submissions (two semaphores, ~5 and ~17 a second), so passing on pending releases
  does not apply; with all of them the boot hangs.
- `EDEN_NX2_LOG_GPU_LISTS=1` logs the words of command lists GPU work wrote (OoT: 2-3 word entries,
  mostly zeros). `EDEN_NX2_LOG_BLITS=1` logs 2D blits between rescaled and full-size images.
- `EDEN_NX2_THREAD_DUMP=1` (`eden-cli`, with `EDEN_NX2_LOG_FPS`) logs every guest thread's state,
  wait reason, registers and frame-pointer backtrace once no frame has been presented for 15 s
  (from 40 s after boot); `=a` also dumps 45 s after boot, to compare with a good boot.
- `EDEN_NX2_FRAME_SKIP_MIN_DRAWS=<n>` (300) is the draws a frame needs to count as busy.
  Skipped frames still run draws into colour targets of at least 256x256 that none of the last
  four drawn frames used (HUD, minimap and dialog layers redrawn only when they change;
  `EDEN_NX2_FRAME_SKIP_NEW_TARGETS=0` none, `=2` any size), and compute programs those frames
  didn't run (`EDEN_NX2_FRAME_SKIP_NEW_PROGRAMS=0` off). `EDEN_NX2_FRAME_SKIP_KEEP_BUFFERS=1`
  keeps compute that writes buffers (it turned OoT's lighting purple).
- `EDEN_FASTMEM_16K_STATS=1` logs how much guest memory fastmem maps directly with 16K pages.
  `DYNARMIC_INLINE_EXCLUSIVES=1` makes exclusive stores inline compare-and-swaps.
- `EDEN_JIT_MAP=<dir>` writes each core's JIT block map (host address, guest PC) every 10 s and the
  guest code once, to attribute `sample` profiles of JIT code to the game's code.
  `EDEN_NX2_CNTPCT_STATS=1` counts the guest's counter reads (busy-wait loops).
- `EDEN_UI_SNAPSHOT=<png>` with `QT_QPA_PLATFORM=offscreen` saves the main window (and with
  `EDEN_UI_SNAPSHOT_CONTROLS=<png>` the controls settings) without a display.
