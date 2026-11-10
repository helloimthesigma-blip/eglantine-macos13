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
game itself gets much closer to normal speed. It takes effect immediately and also runs a frame
ahead. **Run a Frame Ahead** on its own overlaps the game's CPU work with the GPU's.

## What works (tested with The Legend of Zelda: Ocarina of Time, `0400486024DEC000`)

- The NUP loads with no keys and no firmware.
- The title screen runs at 60 fps, including its AV1 video background.
- The AV1 intro cutscene plays, with subtitles.
- New Game reaches the first 3D scene (the forest near the Deku Tree), which renders.
- Saves load; Link's house renders with its door (a mesh shader draw, see below).
- A 15-minute run stayed up with no crash or device loss.

## Performance on an M1 (8-core GPU, 16 GB)

Game speed in Kokiri Forest at 0.5x (frames the game runs per second, of 60), with frame skip
(running a frame ahead):

| Frame skip | Game speed |
|---|---|
| Off | ~8 fps |
| Draw every 2nd frame | ~20-30 fps |
| Draw every 3rd frame | ~28-30 fps |
| Nothing drawn (the CPU limit) | ~43 fps |

Before the CPU work below, the CPU limit was ~21 fps.

- Title screen: 60 fps.
- Gameplay at 0.5x: about 8-9 fps indoors (~115 ms per frame), ~7 fps outdoors. 0.5x still looks
  clean and is the setting to use on a base M1. Options → "Run a Frame Ahead" makes it ~20-30%
  faster (0.5x indoors ~83 ms per frame) but OOT's fog then flickers outdoors.
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
| `assert page size 0x4000 is incompatible with 4K paging` | Fastmem with 16K host pages: a host page is mapped while its four guest pages are consecutive backing on a 16K boundary; other accesses fall back to the page table (`EDEN_FASTMEM_16K=0` turns it off) |
| The game's cores spent ~39% of their busy time spinning on dynarmic's global exclusive-monitor lock | The ARM64 backend now honours `Unsafe_IgnoreGlobalMonitor` (the Auto CPU accuracy sets it): exclusive stores compare and swap, as the x64 backend does |
| A fault on a 128-bit ordered store in fastmem crashed ("Segfault wasn't at a fastmem patch location") | The patch location is the store, not the barrier before it |
| Indirect branches always went through the dispatcher's hash lookup | Fast dispatch table for the ARM64 backend (the x64 backend had one) |
| A slow GPU slows the whole game (its speed follows its frame rate) | Frame skip: skipped frames run the game's commands but drop draws, clears and large compute dispatches, and aren't shown |
| `eden-cli` could not find MoltenVK | Look next to the executable and in a sibling `eglantine.app` |
| AV1 movies black (title background, intro) | FFmpeg with dav1d (`tools/macos13/build-ffmpeg-av1.sh`) |
| Metal validation: uniform bound near the end of the stream buffer could read past it | Pad the stream buffer by 64 KiB |
| Blending enabled on integer render targets | Leave blending off there (Vulkan ignores it; Metal does not) |
| Device lost (`Internal Error 00000106`) in the first 3D scene | Clamp framebuffer size to `maxFramebufferWidth/Height`: attachment-less passes were sized 32768x32768 from the guest's surface clip |
| Null texture fallback had 2D views for every texture type (on a 1D image) | One null image per dimension, and a view of each declared type |
| No `VK_EXT_mesh_shader`: mesh draws skipped (OOT: the door of Link's house, ~30 draws a frame) | Task and mesh programs run as compute shaders that write a vertex buffer, drawn by a generated vertex shader (`vk_mesh_emulation.h`) |
| Every draw sampling the bound depth buffer ended the render pass (~95 per OOT frame) | End the pass only if it wrote depth or stencil (185 → 160 ms per frame on an M1) |
| Washed-out, white picture with pink outlines | The volumetric fog compute program is miscompiled by Metal into NaNs and infinities, which the bloom spreads over the frame. That program's float results are sanitized on MoltenVK, and render target and image outputs are made finite as NVIDIA's conversions give |
| CPU and GPU never overlap (each present waits for its own frame) | Opt-in: Options → Run a Frame Ahead (`EDEN_NX2_FRAMES_AHEAD=1`), faster but OOT's fog flickers |

## Known issues

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
- `EDEN_NX2_IR_DUMP=<hash>,<dir>` writes a compute program's IR; `EDEN_NX2_NAN_SANITIZE=<hash>:<ops>`
  replaces NaN/infinite results of chosen IR operations with 0 (finding NaN sources).
- `EDEN_NX2_FRAME_SKIP=<n>` draws one frame in n + 1 (a path instead of a number: the file holding
  n, re-read every 30 frames). `EDEN_NX2_FRAME_SKIP_WORK` (bits: 1 draws, 2 clears, 4 compute)
  chooses what skipped frames drop; compute dispatches of at most `EDEN_NX2_FRAME_SKIP_COMPUTE_MAX`
  threads (4096) still run, as do programs listed in `EDEN_NX2_FRAME_SKIP_KEEP=<hash>,...`.
- `EDEN_JIT_MAP=<dir>` writes each core's JIT block map (host address, guest PC) every 10 s and the
  guest code once, to attribute `sample` profiles of JIT code to the game's code.
  `EDEN_NX2_CNTPCT_STATS=1` counts the guest's counter reads (busy-wait loops).
- `EDEN_UI_SNAPSHOT=<png>` with `QT_QPA_PLATFORM=offscreen` saves the main window (and with
  `EDEN_UI_SNAPSHOT_CONTROLS=<png>` the controls settings) without a display.
