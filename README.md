# EDOPro Replay2Video

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B17-lightgrey)
![License](https://img.shields.io/badge/license-see%20notes-orange)

Convert EDOPro `.yrpX` replay files into `.mp4` / `.mkv` videos using the **actual EDOPro rendering engine** — no screen capture, no emulation. Full visual fidelity, frame-perfect output.

---

## Features

- **100% faithful rendering** — uses the real EDOPro/Irrlicht engine in a hidden window
- **6 codecs** — libx264, libx265, libsvtav1 (CPU) + h264_nvenc, hevc_nvenc, av1_nvenc (NVIDIA GPU)
- **GPU-accelerated filtering** — libplacebo via Vulkan (debanding, high-quality upscale/downscale)
- **Deterministic output** — virtual timer ensures frame-perfect, clock-independent encoding
- **22 ready-to-use presets** — low / medium / high / max for every codec
- **Config file + CLI** — all options available via `config.ini` or command-line flags
- **Dry-run mode** — render 10 frames and save a PNG for quick diagnostics
- **Camera control** — adjustable margin, vertical offset, and player POV

---

## Prerequisites

- **Windows 10/11** x64
- **EDOPro installation** with `pics/`, `textures/`, `skin/`, `fonts/`, `cards.cdb` (the `--workdir`)
- **Visual Studio 2022 Build Tools** (for building from source)
- **NVIDIA GPU** — only required for `h264_nvenc`, `hevc_nvenc`
- **NVIDIA RTX 40xx or newer** — required for `av1_nvenc`

---

## Quick Start

### 1. Build

```bat
build_windows.bat
```

One-shot script: clones EDOPro, downloads FFmpeg dev libs, applies patches, builds. Output: `edopro\bin\x64\release\ygopro.exe`. Copy `ffmpeg-dev\bin\*.dll` next to the exe once after building.

### 2. Render a replay

**CPU (compatible everywhere):**
```bat
ygopro.exe --render-replay duel.yrpX --output duel.mp4 --workdir D:\EDOPro
```

**NVIDIA GPU + libplacebo (maximum quality):**
```bat
ygopro.exe --render-replay duel.yrpX --output duel.mkv --resolution 1920x1080 --fps 60 ^
  --codec hevc_nvenc --crf 6 --preset p7 --hwaccel vulkan ^
  --vf "libplacebo=upscaler=ewa_lanczos:downscaler=ewa_lanczos:antiringing=1:deband=1:deband_iterations=4:deband_radius=24:deband_threshold=4:chroma_location=left" ^
  --workdir D:\EDOPro
```

### 3. Smoke test

```bat
test_smoke.bat
```

Runs a full 720p/30fps render, a dry-run (10 frames + PNG), black-frame luminance check, and FFprobe H.264/duration validation.

---

## CLI Reference

### Input / Output

| Flag | Description | Default |
|------|-------------|---------|
| `--input`, `--render-replay` | Path to `.yrpX` replay file | *(required)* |
| `--output`, `--render-output` | Output path (`.mp4` or `.mkv`) | `output.mp4` |

### Video

| Flag | Description | Default |
|------|-------------|---------|
| `--resolution`, `-r` | Output resolution `WxH` | `1920x1080` |
| `--fps` | Output frame rate | `60` |
| `--sim-fps` | Simulation FPS (0 = same as `--fps`). Decouples game logic tick rate from output FPS. | `0` |
| `--speed` | Playback speed multiplier (`1.3` = 30% faster) | `1.0` |

### Encoding

| Flag | Description | Default |
|------|-------------|---------|
| `--codec` | Video codec (see [Codec Guide](#codec-guide)) | `libx264` |
| `--crf` | Quality — CRF for CPU codecs, CQ for NVENC (`0`–`51`) | `23` |
| `--preset` | Encoding preset (see [Codec Guide](#codec-guide)) | `veryfast` |
| `--bitrate` | Fixed bitrate in kbps — overrides `--crf` if set | *(none)* |
| `--tune` | Codec tune — x264: `film`/`animation`/`grain`; x265: `grain`/`zerolatency` | *(none)* |
| `--film-grain` | SVT-AV1 film grain synthesis (`0`–`50`) | `0` |

### Filters & Hardware Acceleration

| Flag | Description | Default |
|------|-------------|---------|
| `--vf` | FFmpeg video filter string (e.g. `libplacebo=upscaler=ewa_lanczos:deband=1`) | *(none)* |
| `--hwaccel` | Hardware acceleration for filters — `vulkan` enables GPU filtergraph (libplacebo) | *(none)* |
| `--hwaccel-device` | GPU device index for multi-GPU systems | `0` |
| `--scale-filter` | Scaling algorithm: `bilinear` or `lanczos` | `bilinear` |

### Camera & Framing

| Flag | Description | Default |
|------|-------------|---------|
| `--margin` | Background margin as fraction of width (`0.0`–`0.45`). Expands the camera frustum to reveal more background. | `0.10` |
| `--cam-offset-y` | Vertical camera target offset. Controls the balance between both players' hands. | `0.7` |
| `--player` | Point-of-view: `0` = Player 1 (bottom), `1` = Player 2 (top, swaps field) | `0` |

### Misc

| Flag | Description | Default |
|------|-------------|---------|
| `--workdir` | EDOPro installation directory (must contain `pics/`, `textures/`, `skin/`, `fonts/`, `cards.cdb`) | `.` |
| `--dry-run` | Render 10 frames, save frame #5 as `dry_run_frame.png`, exit without encoding | *(disabled)* |

---

## Codec Guide

| Codec | Type | Hardware | Preset range | Notes |
|-------|------|----------|--------------|-------|
| `libx264` | H.264 | CPU | `ultrafast` → `veryslow` | Most compatible, 8-bit |
| `libx265` | H.265 | CPU | `ultrafast` → `veryslow` | Smaller files, slower encode |
| `libsvtav1` | AV1 | CPU | `0` (best) → `13` (fastest) | 10-bit output, film grain support |
| `h264_nvenc` | H.264 | NVIDIA GPU | `p1` (fastest) → `p7` (best) | GTX 10xx+ |
| `hevc_nvenc` | H.265 | NVIDIA GPU | `p1` → `p7` | GTX 10xx+ |
| `av1_nvenc` | AV1 | NVIDIA GPU | `p1` → `p7` | **RTX 40xx+ only**, 10-bit output |

**Quality presets (recommended starting points):**

| Use case | Command |
|----------|---------|
| CPU, maximum compatibility | `--codec libx264 --crf 23 --preset veryfast` |
| CPU, high quality H.265 | `--codec libx265 --crf 18 --preset medium` |
| CPU, AV1 balanced | `--codec libsvtav1 --crf 16 --preset 4` |
| GPU, fast + good quality | `--codec hevc_nvenc --crf 18 --preset p4` |
| GPU, maximum quality | `--codec hevc_nvenc --crf 6 --preset p7 --hwaccel vulkan --vf "libplacebo=..."` |
| GPU, best compression (AV1) | `--codec av1_nvenc --crf 12 --preset p7 --hwaccel vulkan --vf "libplacebo=..."` |

> Use `.mkv` output for best compatibility across all codecs (especially H.265 and AV1).

---

## Ready-to-Use Presets

22 preset files are available in [`presets/`](presets/). Copy the one matching your setup and rename it to `config.ini`:

| Preset file | Codec | Quality tier | GPU required |
|-------------|-------|-------------|--------------|
| `cpu_h264_low.ini.example` | libx264 | 720p/30fps | No |
| `cpu_h264_medium.ini.example` | libx264 | 1080p/60fps | No |
| `cpu_h264_high.ini.example` | libx264 | 1080p/60fps + libplacebo | No |
| `cpu_h264_max.ini.example` | libx264 | Max quality + libplacebo | No |
| `cpu_h265_low.ini.example` | libx265 | 720p/30fps | No |
| `cpu_h265_medium.ini.example` | libx265 | 1080p/60fps | No |
| `cpu_h265_high.ini.example` | libx265 | 1080p/60fps + libplacebo | No |
| `cpu_h265_max.ini.example` | libx265 | Max quality + libplacebo | No |
| `cpu_av1_svt_low.ini.example` | libsvtav1 | 720p/30fps | No |
| `cpu_av1_svt_medium.ini.example` | libsvtav1 | 1080p/60fps | No |
| `cpu_av1_svt_high.ini.example` | libsvtav1 | 1080p/60fps + libplacebo | No |
| `cpu_av1_svt_max.ini.example` | libsvtav1 | Max quality + libplacebo + film grain | No |
| `gpu_h264_nvenc_low.ini.example` | h264_nvenc | 720p/30fps | GTX 10xx+ |
| `gpu_h264_nvenc_medium.ini.example` | h264_nvenc | 1080p/60fps | GTX 10xx+ |
| `gpu_h264_nvenc_high.ini.example` | h264_nvenc | 1080p/60fps + libplacebo | GTX 10xx+ |
| `gpu_h264_nvenc_max.ini.example` | h264_nvenc | Max quality + libplacebo + Lanczos | GTX 10xx+ |
| `gpu_hevc_nvenc_low.ini.example` | hevc_nvenc | 720p/30fps | GTX 10xx+ |
| `gpu_hevc_nvenc_medium.ini.example` | hevc_nvenc | 1080p/60fps | GTX 10xx+ |
| `gpu_hevc_nvenc_high.ini.example` | hevc_nvenc | 1080p/60fps + libplacebo | GTX 10xx+ |
| `gpu_hevc_nvenc_max.ini.example` | hevc_nvenc | Max quality + libplacebo + Lanczos | GTX 10xx+ |
| `gpu_av1_nvenc_medium.ini.example` | av1_nvenc | 1080p/60fps + libplacebo | RTX 40xx+ |
| `gpu_av1_nvenc_max.ini.example` | av1_nvenc | Max quality + libplacebo + Lanczos | RTX 40xx+ |

---

## Configuration File

Create `config.ini` next to `ygopro.exe` (or in `--workdir`). CLI flags override config values when both are set.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `workdir` | string | `.` | Path to EDOPro installation |
| `resolution` | string | `1920x1080` | Output resolution (`WxH`) |
| `fps` | int | `60` | Output frame rate |
| `sim_fps` | int | `0` | Simulation FPS (0 = same as fps) |
| `codec` | string | `libx264` | Video codec |
| `crf` | int | `23` | Quality (0–51) |
| `preset` | string | `veryfast` | Encoding preset |
| `bitrate` | int | `0` | Fixed bitrate in kbps (0 = use crf) |
| `tune` | string | *(none)* | Codec tune parameter |
| `film_grain` | int | `0` | SVT-AV1 film grain (0–50) |
| `scale_filter` | string | `bilinear` | Scaling: `bilinear` or `lanczos` |
| `vf` | string | *(none)* | FFmpeg video filter string |
| `hwaccel` | string | *(none)* | Hardware acceleration (`vulkan`) |
| `hwaccel_device` | string | `0` | GPU device index |
| `speed` | float | `1.0` | Playback speed multiplier |
| `margin` | float | `0.10` | Background margin (0.0–0.45) |
| `cam_offset_y` | float | `0.7` | Camera vertical target offset |
| `player` | int | `0` | POV player (0 or 1) |

See [`config.ini.example`](config.ini.example) for a fully annotated template.

---

## Camera & Framing

### `--margin`

Expands the camera frustum outward to reveal more background. Value is a fraction of the field width:
- `0.0` — tight crop, field fills the frame
- `0.10` — 10% black margin on each side (default)
- `0.45` — maximum margin

### `--cam-offset-y`

Shifts the camera's vertical target to balance the view between both players' hands. Default `0.7` centers the field with a slight bias toward Player 1's side. Increase to show more of Player 2's hand.

### `--player`

Sets the point of view:
- `0` — Player 1 perspective (bottom of field)
- `1` — Player 2 perspective (top of field — the camera and field orientation are swapped)

### `--sim-fps`

Decouples the game simulation tick rate from the output frame rate. By default (`0`), simulation and output run at the same FPS. Set `--sim-fps 30 --fps 60` to run simulation at 30 ticks/s while outputting 60fps (each game frame is duplicated — smoother motion, same game speed). Useful when CPU is the bottleneck.

---

## How It Works

### Virtual Timer

`Game::MainLoop()` replaces the real system clock with a **virtual timer** that advances by exactly `1000 / fps` milliseconds per frame. This ensures animations (card moves, summons, attacks) play at the correct speed regardless of wall-clock time, producing deterministic frame-perfect output across runs.

### Replay Auto-Advance

`ReplayMode::ReplayAnalyze()` is patched to skip all user-input waits in batch mode: no pause on interactive messages, no end-of-replay popup, no blocking. The replay thread processes packets at maximum speed while the render thread captures frames.

### Frame Capture

Each rendered frame is read via `IVideoDriver::createScreenShot()` (Irrlicht's cross-platform framebuffer read), converted BGRA→RGBA, then fed to `libswscale` for color space conversion (YUV420P or YUV420P10LE depending on codec) before H.264/H.265/AV1 encoding via `libavcodec`.

### Hidden Window

The Irrlicht device is created normally, then `ShowWindow(hwnd, SW_HIDE)` hides it before it ever appears on screen. The OpenGL context remains fully functional for rendering — no display adapter or monitor required beyond what Windows needs for context creation.

### GPU Filtergraph (libplacebo)

When `--hwaccel vulkan` and `--vf` are set, the encoder builds an FFmpeg filter graph with a Vulkan device. Frames are uploaded to the GPU as `yuv420p` via `buffersrc`, processed through libplacebo (debanding, high-quality scaling), then pulled back via `buffersink` in the target pixel format. This path enables high-quality filters at near-zero CPU cost.

---

## Build

### First-time setup

```bat
build_windows.bat
```

Clones EDOPro with submodules, downloads FFmpeg dev libs and Premake5, applies all patches, copies integration sources, generates VS solution, compiles. Do not re-run on an existing `edopro/` clone without deleting it first.

**Output:** `edopro/bin/x64/release/ygopro.exe`. After building, copy `ffmpeg-dev/bin/*.dll` next to the exe.

### Iterative development

1. Edit sources in `gframe/replay2video/` (canonical sources in this repo)
2. Copy into the EDOPro clone:
   ```bat
   xcopy /Y gframe\replay2video\* edopro\gframe\replay2video\
   ```
3. Recompile:
   ```powershell
   $env:FFMPEG_DEV = "D:\ProjectIgnis\replay2video\ffmpeg-dev"
   Set-Location "D:\ProjectIgnis\replay2video\edopro"
   & "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" `
       build\ygo.sln /p:Configuration=Release /p:Platform=x64 /m /t:ygopro /nologo /v:quiet
   ```

### Regenerate patches

After editing any patched file:

```bat
cd edopro
for %f in (gframe/game.cpp gframe/gframe.cpp gframe/image_manager.cpp gframe/image_manager.h gframe/replay_mode.cpp gframe/replay_mode.h gframe/utils_gui.cpp gframe/premake5.lua irrlicht/premake5.lua) do git diff HEAD -- %f > ..\patches\%~nxf.patch
```

(`irrlicht/premake5.lua` → output file: `irrlicht_premake5.lua.patch`)

### Regenerate VS solution

Only needed if `premake5.lua` changed:

```powershell
$env:FFMPEG_DEV = "D:\ProjectIgnis\replay2video\ffmpeg-dev"
Set-Location "D:\ProjectIgnis\replay2video\edopro"
& "..\premake5\premake5.exe" vs2022 --with-replay2video --no-direct3d `
    --vcpkg-root="D:\ProjectIgnis\replay2video\vcpkg" `
    --vcpkg-triplet=-windows-static --architecture=x64
```

### Deploy

```bat
deploy_package.bat
```

Assembles a distributable package in `dist/` with the exe and all required FFmpeg DLLs.

---

## Troubleshooting

### "Failed to open replay"
- Ensure the file uses the `.yrpX` format (magic bytes `79 72 70 58`)
- Old replays created before the `.yrpX` format may not be supported

### Black frames / blank output
- Verify `--workdir` points to a valid EDOPro installation with `pics/`, `textures/`, `skin/`, `fonts/`, and `cards.cdb`
- Run `--dry-run` and inspect `dry_run_frame.png` to confirm rendering is working
- Irrlicht needs a display adapter — purely headless servers without a GPU/display may fail

### NVENC encoder not found
- Ensure an NVIDIA GPU is present and drivers are up to date
- Try `ffmpeg -encoders | findstr nvenc` to confirm FFmpeg can see NVENC
- `av1_nvenc` requires RTX 40xx (Ada Lovelace) or newer; RTX 30xx only supports `h264_nvenc` and `hevc_nvenc`

### FFmpeg link errors at build time
- Verify `FFMPEG_DEV` points to a directory with `include/` and `lib/` subdirectories
- Ensure you are using x64 import libs (`.lib`), not x86

### Runtime DLL errors
- Copy all `ffmpeg-dev/bin/*.dll` next to `ygopro.exe`
- `avdevice-*.dll` is not required (audio device capture is not used)

---

## Architecture

```
.yrpX → ReplayMode::InitBatch → Game::Initialize (hidden window, SW_HIDE) →
gframe.cpp batch block: OpenReplay → InitBatch → hide wMainMenu/mTopMenu →
Game::MainLoop (virtual timer, StepBatch per frame) →
Integration::ProcessFrame → FrameCapture::Capture (Irrlicht screenshot) →
VideoEncoder::EncodeFrame (BGRA→YUV420P via libswscale → codec via libavcodec) →
.mp4 / .mkv
```

| Module | File(s) | Role |
|--------|---------|------|
| Integration | `replay2video_integration.h/cpp` | Config/CLI parsing, `BatchState`, virtual timer hook |
| Encoder | `video_encoder.h/cpp` | FFmpeg libav* setup, CRF/bitrate/NVENC modes, libplacebo filtergraph |
| Capture | `offscreen_capture.h/cpp` | Irrlicht `createScreenShot()`, BGRA↔RGBA conversion |

Patches (in `patches/`): `game.cpp`, `gframe.cpp`, `replay_mode.cpp`, `replay_mode.h`, `image_manager.cpp`, `image_manager.h`, `utils_gui.cpp`, `drawing.cpp`, `gframe/premake5.lua`, `irrlicht/premake5.lua`.

---

## Technical Decisions

**Why not FBO / render-to-texture?**
EDOPro's rendering pipeline is tightly coupled to the default framebuffer (GUI coordinates, viewport, post-processing). Redirecting rendering to an offscreen texture would require invasive changes to Irrlicht's device setup and all of EDOPro's draw calls. A hidden window with `createScreenShot()` achieves the same result with zero rendering pipeline changes.

**Why virtual timer instead of replacing `ITimer`?**
Irrlicht's `ITimer` is an interface, but EDOPro caches the pointer early in initialization and uses it pervasively. Replacing it requires modifying device creation in the Irrlicht fork. Forcing `delta_time` in `MainLoop()` is a single targeted change that controls all downstream animation timing.

**Why patch `ReplayMode` instead of a custom packet parser?**
The `.yrpX` format is partially compressed and versioned. Reimplementing the parser would duplicate thousands of lines from `Replay.cpp` and break every time EDOPro updates the format. Reusing `ReplayMode` guarantees compatibility with every replay version EDOPro itself supports.

---

## License Notes

The integration code is provided as-is for EDOPro modding purposes.

FFmpeg libraries (libavcodec, libavformat, etc.) are LGPL/GPL depending on build configuration. The `gyan.dev` **full shared** build used by `build_windows.bat` is **GPL-licensed** (includes x264/x265). For a proprietary-friendly build, use the **LGPL** variant from BtbN or compile FFmpeg with `--disable-gpl`.
