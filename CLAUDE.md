# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Project Does

EDOPro Replay2Video converts `.yrpX` EDOPro replay files into `.mp4` videos with full visual fidelity by running the actual EDOPro rendering engine headlessly and encoding each rendered frame with FFmpeg. It is Windows-only and works by applying surgical patches to EDOPro's source tree rather than being a standalone binary.

## Build

### First-time setup

`build_windows.bat` is a one-shot script: clones EDOPro with submodules, downloads FFmpeg dev libs and Premake5, applies all patches, copies integration sources into the EDOPro tree, generates VS build files, and compiles. Do not re-run on an existing `edopro/` clone without deleting it first.

**Output:** `edopro/bin/x64/release/replay2video.exe`. Copy `ffmpeg-dev/bin/*.dll` next to the exe once after the first build.

### Iterative development (after initial setup)

1. Edit sources in `gframe/replay2video/` (the canonical sources in this repo)
2. Copy changed files into the EDOPro clone:
   ```bat
   xcopy /Y gframe\replay2video\* edopro\gframe\replay2video\
   ```
3. Recompile (PowerShell — no premake needed unless `premake5.lua` changed):
   ```powershell
   $env:FFMPEG_DEV = "D:\ProjectIgnis\replay2video\ffmpeg-dev"
   Set-Location "D:\ProjectIgnis\replay2video\edopro"
   & "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" `
       build\ygo.sln /p:Configuration=Release /p:Platform=x64 /m /t:ygopro /nologo /v:quiet
   ```
4. When done, regenerate patches to keep them in sync:
   ```bat
   cd edopro
   for %f in (gframe/game.cpp gframe/gframe.cpp gframe/image_manager.cpp gframe/image_manager.h gframe/replay_mode.cpp gframe/replay_mode.h gframe/utils_gui.cpp gframe/premake5.lua irrlicht/premake5.lua) do git diff HEAD -- %f > ..\patches\%~nxf.patch
   ```
   For `irrlicht/premake5.lua` the output file should be `irrlicht_premake5.lua.patch`.

### Regenerate VS solution (only if premake5.lua changed)

Run from `edopro/` root:

```powershell
$env:FFMPEG_DEV = "D:\ProjectIgnis\replay2video\ffmpeg-dev"
Set-Location "D:\ProjectIgnis\replay2video\edopro"
& "..\premake5\premake5.exe" vs2022 --with-replay2video --no-direct3d `
    --vcpkg-root="D:\ProjectIgnis\replay2video\vcpkg" `
    --vcpkg-triplet=-windows-static --architecture=x64
```

### MSBuild location on this machine

```
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe
```

### Notes

- `--no-direct3d` uses OpenGL, avoiding the DirectX SDK requirement.
- `vcpkg integrate install` injects includes automatically for VS builds — no manual include paths needed.
- Irrlicht uses its bundled `bzip2/jpeglib/libpng/zlib`; platform test files are excluded in `irrlicht/premake5.lua`.
- FFmpeg `.lib` files are x64 MSVC import libs; matching DLLs must be next to the exe at runtime.

## Deploy

After building, run `deploy_package.bat` from the repo root to assemble a distributable package in `dist/`:

```bat
deploy_package.bat
```

The script copies `replay2video.exe`, the 6 required FFmpeg DLLs from `ffmpeg-dev/bin/`, `config.ini.example`, `presets/`, and (if already built) `replay2videoGUI.exe` into `dist/`. The exe and DLLs must remain in the same directory (Windows DLL search order).

**DLL note:** `avdevice-62.dll` is not included — it handles audio device capture which this project does not use.

### Building the GUI launcher (optional, before deploy)

Requires Python 3.11 (`pythonnet` has no wheel for Python 3.14+):

```bat
cd gui
build_launcher.bat
```

This installs `pywebview` + `pyinstaller` into the Python 3.11 environment, compiles `gui/launcher.py` + `gui/ui.html` into a standalone `replay2videoGUI.exe` (~25 MB), and copies it to `dist/`. End users need no Python installed., and copies it to `dist/`. End users need no Python installed.

**GUI source files:**

| File | Role |
|------|------|
| `gui/launcher.py` | Python backend: HTTP server, SSE progress stream, subprocess management, pywebview JS API for file dialogs |
| `gui/ui.html` | Single-file frontend: HTML/CSS/JS UI served on localhost, communicates with backend via fetch + EventSource |
| `gui/build_launcher.bat` | PyInstaller build script (uses `py -3.11`) |
| `gui/requirements.txt` | `pywebview>=5.0`, `pyinstaller>=6.0` |

**Architecture:** `replay2videoGUI.exe` starts a free-port HTTP server in a background thread, then opens the URL in a pywebview (WebView2/WinForms) window. File/folder dialogs are exposed as `window.pywebview.api.*` methods so they execute on the main thread as required by WebView2. Encode progress is streamed from the subprocess stdout via SSE (`/api/progress`).

**Usage from any directory:**

```bat
replay2video.exe --render-replay duel.yrpX --output duel.mp4 --workdir D:\ProjectIgnis
```

`--workdir` must point to a working EDOPro installation (contains `config/strings.conf`, `pics/`, `textures/`, `skin/`, `fonts/`). `replay2video.exe` coexists alongside the stock `EDOPro.exe` without conflict.

## Prerequisites

`--workdir` must point to a working EDOPro installation. On this machine: `D:\ProjectIgnis`. That directory must contain `pics/`, `textures/`, `skin/`, `fonts/`, and `cards.cdb`. Without these assets the renderer will crash or produce blank output.

## Run

```bash
replay2video.exe --render-replay duel.yrpX --output duel.mp4 --resolution 1920x1080 --fps 60 --workdir D:\ProjectIgnis
```

Key flags: `--crf`, `--preset`, `--codec` (libx264/h264_nvenc/libx265), `--bitrate`, `--dry-run`, `--player 0|1` (POV: 0=player 1 default, 1=player 2).

A `config.ini` in the working directory is also supported — see `config.ini.example` for all keys.

## Test

```bash
test_smoke.bat
```

Runs a full 720p/30fps render, a dry-run (10 frames + PNG), black-frame luminance check, and FFprobe H.264/duration validation.

## Architecture

The integration lives entirely in `gframe/replay2video/` (three files) and five patches. All integration sources are copied into the EDOPro source tree by the build script before compilation.

**Data flow:**
```
.yrpX → ReplayMode::InitBatch → Game::Initialize (hidden window, SW_HIDE) →
gframe.cpp batch block: OpenReplay → InitBatch → hide wMainMenu/mTopMenu →
Game::MainLoop (virtual timer, StepBatch per frame) →
Integration::ProcessFrame → FrameCapture::Capture (Irrlicht screenshot) →
VideoEncoder::EncodeFrame (BGRA→YUV420P via libswscale → H.264 via libavcodec) →
.mp4
```

**Integration modules:**

| File | Role |
|------|------|
| `replay2video_integration.h/cpp` | Config parsing, CLI arg handling, global `BatchState` and `Integration` objects. Emits `[r2v:frame] N` on stdout every 10 frames for GUI progress. |
| `video_encoder.h/cpp` | FFmpeg libav* setup, CRF/bitrate encoding modes, frame encoding, finalization |
| `offscreen_capture.h/cpp` | Irrlicht `createScreenShot()` capture, BGRA↔RGBA conversion |

**Patches** (in `patches/`): 9 patches covering `gframe.cpp`, `game.cpp`, `replay_mode.cpp`, `replay_mode.h`, `image_manager.cpp`, `image_manager.h`, `utils_gui.cpp`, `gframe/premake5.lua`, `irrlicht/premake5.lua`.

**Key design decisions and why they were made:**

- **Virtual timer over ITimer replacement**: `Game::MainLoop()` forces `delta_time = frame_time_ms` when batch mode is active. Simpler than replacing ITimer (which EDOPro caches early in startup).
- **Hidden window over FBO/offscreen texture**: EDOPro tightly couples all rendering to the default framebuffer. `ShowWindow(hwnd, SW_HIDE)` keeps the OpenGL/D3D context functional with zero rendering pipeline changes.
- **Replay auto-advance via patching `ReplayAnalyze()`**: Reuses all of EDOPro's replay format parsing including versioning and compression. Adding custom packet parsing would duplicate that and break on replay format changes.
