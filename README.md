# EDOPro Replay2Video

Convert EDOPro `.yrpX` replay files into `.mp4` / `.mkv` videos using the actual EDOPro rendering engine for 100% visual fidelity.

## Architecture

The integration uses a **headless EDOPro + framebuffer capture + FFmpeg encoding** pipeline:

```
.yrpX  ->  ygo::Replay  ->  ygo::ReplayMode (auto-advance)
  ->  Irrlicht hidden window  ->  driver->endScene()
  ->  glReadPixels / createScreenShot  ->  libswscale (BGRA->YUV420P)
  ->  [libplacebo via Vulkan GPU — optional]
  ->  libavcodec (H.264 / H.265 / AV1, CPU or NVENC)  ->  .mp4 / .mkv
```

All rendering code is native EDOPro/Irrlicht. No custom renderer. No screenshot hacks.

## Prerequisites

- **Windows 10/11** (x64)
- **Visual Studio 2019+** with C++17 workload, **or** MinGW-w64
- **Git** (to clone EDOPro with submodules)
- **7-Zip** (for extracting FFmpeg archives)
- **FFmpeg dev libraries** (downloaded automatically by build script)

## Quick Start

1. **Clone and build** (run in PowerShell or CMD):
   ```batch
   cd replay2video
   build_windows.bat
   ```

2. **Get a test replay** (optional, `build_windows.bat` also downloads it):
   ```batch
   powershell -Command "Invoke-WebRequest -Uri 'https://www.quicksendfile.com/download/tcrtv' -OutFile 'test_replay.yrpX'"
   ```

3. **Render a replay**:
   ```batch
   edopro\bin\x64\release\ygopro.exe --render-replay duel.yrpX --output duel.mp4 --resolution 1920x1080 --fps 60 --workdir D:\EDOPro
   ```

   GPU-accelerated example (NVIDIA):
   ```batch
   ygopro.exe --render-replay duel.yrpX --output duel.mkv --resolution 1920x1080 --fps 60 ^
     --codec hevc_nvenc --crf 6 --preset p7 --hwaccel vulkan ^
     --vf "libplacebo=upscaler=ewa_lanczos:downscaler=ewa_lanczos:antiringing=1:deband=1" ^
     --workdir D:\EDOPro
   ```

4. **Run smoke tests**:
   ```batch
   test_smoke.bat
   ```

## CLI Arguments

| Argument | Description | Default |
|----------|-------------|---------|
| `--input`, `--render-replay` | Path to `.yrpX` replay file | *(required)* |
| `--output`, `--render-output` | Output video path (`.mp4` or `.mkv`) | `output.mp4` |
| `--resolution`, `-r` | `WxH` (e.g. `1920x1080`) | `1920x1080` |
| `--fps` | Frame rate | `60` |
| `--crf` | Quality level — CRF for libx264/libx265 (`0`–`51`), CQ for NVENC (`0`–`51`) | `23` |
| `--preset` | Encoding preset — `ultrafast`…`veryslow` for CPU codecs, `p1`–`p7` for NVENC | `veryfast` |
| `--codec` | `libx264`, `libx265`, `h264_nvenc`, `hevc_nvenc`, `av1_nvenc` | `libx264` |
| `--bitrate` | Override CRF/CQ with fixed kbps | *(none)* |
| `--scale-filter` | Scaling algorithm (`bilinear`, `lanczos`) | `bilinear` |
| `--tune` | Codec tune — x264: `film`/`animation`/`grain`; x265: `grain`/`zerolatency`/etc. | *(none)* |
| `--vf` | FFmpeg video filter string (e.g., `libplacebo=upscaler=ewa_lanczos:deband=1`) | *(none)* |
| `--hwaccel` | Hardware acceleration for filtergraph — `vulkan` enables GPU filters (libplacebo) | *(none)* |
| `--hwaccel-device` | HW device index (e.g., `0`) | `0` |
| `--speed` | Playback speed multiplier (e.g., `1.3` = 30% faster) | `1.0` |
| `--workdir` | EDOPro installation dir (containing `pics/`, `textures/`…) | `.` |
| `--dry-run` | Render 10 frames, save frame #5 as PNG, exit | *(disabled)* |

### Codec guide

| Use case | Recommended command |
|----------|-------------------|
| CPU, best compatibility | `--codec libx264 --crf 23 --preset veryfast` |
| CPU, high quality | `--codec libx265 --crf 18 --preset medium` |
| GPU, fast + good quality | `--codec hevc_nvenc --crf 18 --preset p4` |
| GPU, maximum quality | `--codec hevc_nvenc --crf 6 --preset p7 --hwaccel vulkan --vf "libplacebo=upscaler=ewa_lanczos:deband=1"` |
| GPU, AV1 (best compression) | `--codec av1_nvenc --crf 6 --preset p7 --hwaccel vulkan --vf "libplacebo=upscaler=ewa_lanczos:deband=1"` |

> **Note:** NVENC codecs (`h264_nvenc`, `hevc_nvenc`, `av1_nvenc`) require an NVIDIA GPU. AV1 requires RTX 4000+. Use `.mkv` output for best compatibility with all codecs.

## Configuration File

Create `config.ini` in the working directory for default settings:

```ini
# Example config.ini
resolution=1920x1080
fps=60
crf=23
preset=veryfast
codec=libx264
scale_filter=bilinear
# tune=film
# bitrate=8000
workdir=C:\Games\EDOPro
```

(See `config.ini.example` for a full annotated template.)

## How It Works

### Virtual Timer

In batch mode, `Game::MainLoop()` replaces the real system timer with a **virtual timer** that advances by exactly `1000 / fps` milliseconds per rendered frame. This ensures:

- Animations (card moves, summons, attacks) play at normal speed in the output video
- No dependency on wall-clock time or vsync
- Deterministic, frame-perfect reproduction across runs

### Replay Auto-Advance

`ReplayMode::ReplayAnalyze()` is modified to skip all user-input waits when batch mode is active:

- No pause on interactive messages
- No end-of-replay popup blocking
- The replay thread processes packets at maximum speed while the render thread captures frames

### Frame Capture

Frames are captured via `IVideoDriver::createScreenShot()` (Irrlicht's built-in cross-platform framebuffer read), converted from BGRA to RGBA, then fed through `libswscale` to YUV420P for H.264 encoding.

### Window Handling

The Irrlicht device is created normally, then `ShowWindow(hwnd, SW_HIDE)` hides it before it becomes visible. The OpenGL/D3D context remains fully functional for offscreen rendering.

## Build System Integration

The `premake5.lua` patch adds:

- A `--with-replay2video` option
- Automatic inclusion of `replay2video/*.cpp` sources
- FFmpeg `include`/`lib` paths and link directives (`avcodec`, `avformat`, `avutil`, `avfilter`, `swscale`, `swresample`)

## Files Added/Modified

### New files
- `gframe/replay2video/replay2video_integration.h/.cpp` — batch loop API, virtual timer, CLI/config parsing
- `gframe/replay2video/offscreen_capture.h/.cpp` — framebuffer read via Irrlicht screenshot
- `gframe/replay2video/video_encoder.h/.cpp` — FFmpeg encoding (H.264/H.265/AV1, CPU + NVENC, libplacebo GPU filtergraph)

### Patched files
- `gframe/game.cpp` — hidden window, virtual timer, frame capture hook
- `gframe/replay_mode.cpp` — auto-advance, skip waits
- `gframe/edopro_main.cpp` — CLI parsing, batch launch path
- `gframe/premake5.lua` — build option, source files, FFmpeg linkage

## Troubleshooting

### "Failed to open replay"
- Ensure the replay file uses the `.yrpX` format (magic bytes `79 72 70 58`)
- If the replay was created with an old EDOPro version, try the `.yrp` path instead

### Black frames / blank output
- Ensure `--workdir` points to a valid EDOPro installation with `pics/`, `textures/`, `skin/`, `fonts/`, and `cards.cdb`
- Run `--dry-run` and inspect `dry_run_frame.png` to verify rendering is working
- Check that the Irrlicht device initializes successfully (hidden window creation can fail on headless servers without a display adapter)

### FFmpeg link errors
- Verify `FFMPEG_DEV` environment variable or the downloaded FFmpeg dev archive contains `include/` and `lib/` directories
- For MSVC, ensure you link against the correct architecture (x64)

## License Notes

- The integration code is provided as-is for EDOPro modding purposes.
- FFmpeg libraries used (libavcodec, libavformat, etc.) are under LGPL/GPL depending on build configuration. The gyan.dev **full shared** build used by `build_windows.bat` is GPL-licensed (includes x264/x265). If you need a proprietary-friendly build, switch to the **LGPL** variant from BtbN or compile FFmpeg with `--disable-gpl`.

## Technical Decisions

**Why not use an FBO / render-to-texture?**
> EDOPro's rendering pipeline is deeply tied to the default framebuffer (GUI coordinates, viewport, post-processing). Redirecting all rendering to an offscreen texture would require invasive changes to Irrlicht's device setup and EDOPro's draw calls. A hidden window with `createScreenShot()` achieves the same result with minimal, maintainable changes.

**Why virtual timer instead of hooking Irrlicht's `ITimer`?**
> Irrlicht's timer is an interface, but EDOPro caches the pointer early in initialization and uses it pervasively. Replacing it with a custom implementation would require modifying the device creation code in the Irrlicht fork. Forcing `delta_time` in `MainLoop()` is a single-line change that controls all downstream animation timing.

**Why `ReplayMode` auto-advance rather than a custom packet parser?**
> The `.yrpX` format is partially compressed and versioned. Rewriting the parser would duplicate thousands of lines from `Replay.cpp` and would break every time EDOPro updates the format. Reusing `ReplayMode` guarantees compatibility with all replay versions EDOPro itself supports.
