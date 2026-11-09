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

The default theme, "Ryujinx", follows Ryujinx's layout: menus in the window, a toolbar with
list/grid buttons, an icon size slider, sorting and search, games as cards, and a short status bar
(refresh, game count, **Controls**, docked mode, volume). The other themes are under
Settings → General → UI.

**Controls** on the status bar, or Emulation → Controls..., opens the button mapping directly
(keyboard or controller, per player).

## What works (tested with The Legend of Zelda: Ocarina of Time, `0400486024DEC000`)

- The NUP loads with no keys and no firmware.
- The title screen runs at 60 fps, including its AV1 video background.
- The AV1 intro cutscene plays, with subtitles.
- New Game reaches the first 3D scene (the forest near the Deku Tree), which renders.
- Saves load; Link's house renders with its door (a mesh shader draw, see below).
- A 15-minute run stayed up with no crash or device loss.

## Performance on an M1 (8-core GPU, 16 GB)

- Title screen: 60 fps.
- Gameplay at 1x: about 6 fps (about 160 ms per frame, inside Link's house). The GPU is ~94% busy:
  the game is GPU-bound. FXAA, the scaling filter and post-processing effects cost almost nothing;
  the game's own shaders are the cost (its main lit-material shader alone is ~20 ms per frame).
- Gameplay at 0.5x: about 8 fps.
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
| `assert page size 0x4000 is incompatible with 4K paging` | 16K host pages skip fastmem cleanly |
| `eden-cli` could not find MoltenVK | Look next to the executable and in a sibling `eglantine.app` |
| AV1 movies black (title background, intro) | FFmpeg with dav1d (`tools/macos13/build-ffmpeg-av1.sh`) |
| Metal validation: uniform bound near the end of the stream buffer could read past it | Pad the stream buffer by 64 KiB |
| Blending enabled on integer render targets | Leave blending off there (Vulkan ignores it; Metal does not) |
| Device lost (`Internal Error 00000106`) in the first 3D scene | Clamp framebuffer size to `maxFramebufferWidth/Height`: attachment-less passes were sized 32768x32768 from the guest's surface clip |
| Null texture fallback had 2D views for every texture type (on a 1D image) | One null image per dimension, and a view of each declared type |
| No `VK_EXT_mesh_shader`: mesh draws skipped (OOT: the door of Link's house, ~30 draws a frame) | Task and mesh programs run as compute shaders that write a vertex buffer, drawn by a generated vertex shader (`vk_mesh_emulation.h`) |
| Every draw sampling the bound depth buffer ended the render pass (~95 per OOT frame) | End the pass only if it wrote depth or stencil (185 → 160 ms per frame on an M1) |

## Known issues

- **Mesh shaders** are emulated with compute shaders (see above). Indirect mesh draws are still
  skipped, and many OOT mesh draws arrive with a zero count (upstream reads a GPU-written count
  stale), so some mesh geometry may still be missing. `EDEN_NX2_NO_MESH_EMULATION=1` turns the
  emulation off.
- **Metal compiler crashes.** macOS 13's Metal compiler crashes ("Compiler encountered an internal
  error") on two of the game's compute shaders, so those dispatches are skipped.
- **Washed-out gameplay picture.** This is an upstream issue on every platform (see
  `.claude/skills/nx2-lab-automation/references/ocarina-of-time.md`), not specific to macOS.
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
- `EDEN_UI_SNAPSHOT=<png>` with `QT_QPA_PLATFORM=offscreen` saves the main window (and with
  `EDEN_UI_SNAPSHOT_CONTROLS=<png>` the controls settings) without a display.
