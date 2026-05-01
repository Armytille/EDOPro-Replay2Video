# EDOPro Replay2Video

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)
![License](https://img.shields.io/badge/license-see%20notes-orange)

Convert EDOPro `.yrpX` replay files into `.mp4` / `.mkv` videos using the actual EDOPro rendering engine — full visual fidelity, no screen capture.

---

## Download

**[→ Latest Release](https://github.com/Armytille/EDOPro-Replay2Video/releases/latest)**

Download the ZIP, extract anywhere. Use the GUI or the CLI.

---

## Requirements

- Windows 10/11 x64
- A working EDOPro installation (needs `pics/`, `textures/`, `skin/`, `fonts/`, `cards.cdb`)
- NVIDIA GPU — only for `h264_nvenc` / `hevc_nvenc` / `av1_nvenc` codecs

---

## Setup

1. Extract the ZIP anywhere
2. **GUI:** double-click `replay2videoGUI.exe` — no configuration needed to start
3. **CLI:** copy `config.ini.example` → `config.ini` and set `workdir` to your EDOPro folder:
   ```ini
   workdir=C:\EDOPro
   ```
4. Optionally pick a preset from `presets/` that matches your hardware and copy it as `config.ini`

---

## GUI Launcher

`replay2videoGUI.exe` is a standalone GUI — no Python or other runtime required.

- Browse for your `.yrpX` replay file, set an output path and workdir
- Load a quality preset with one click (codec, CRF, resolution, FPS all filled in)
- Advanced settings (codec, CRF slider, tune, hardware acceleration) in a collapsible panel
- Live frame counter during encoding, log output, and an "Open folder" button on completion
- Dry run mode (10 frames + PNG) for a quick render check

Settings are saved to `config.ini` automatically after each encode.

---

## Usage (CLI)

### Minimal

```bat
replay2video.exe --render-replay "C:\replays\duel.yrpX" --output "C:\replays\duel.mp4" --workdir "C:\EDOPro"
```

> `--render-replay`, `--output`, and `--workdir` all require full paths.

### With a config file (recommended)

```bat
replay2video.exe --render-replay "C:\replays\duel.yrpX" --output "C:\replays\duel.mp4"
```
*(reads `config.ini` from the same folder — set `workdir` there)*

### Higher quality, slower encode

```bat
replay2video.exe --render-replay duel.yrpX --output duel.mp4 ^
  --codec libx265 --crf 18 --preset medium
```

### AV1 (SVT-AV1) — best quality/size ratio ⭐ recommended

CPU-only, slower encode but significantly smaller files at equal or better quality than H.264/H.265.

```bat
replay2video.exe --render-replay "C:\replays\duel.yrpX" --output "C:\replays\duel.mp4" ^
  --codec libsvtav1 --crf 16 --preset 4 --workdir "C:\EDOPro"
```

### NVIDIA GPU — fast

```bat
replay2video.exe --render-replay "C:\replays\duel.yrpX" --output "C:\replays\duel.mkv" ^
  --codec hevc_nvenc --crf 18 --preset p4 --workdir "C:\EDOPro"
```

### NVIDIA GPU — maximum quality (libplacebo debanding)

```bat
replay2video.exe --render-replay "C:\replays\duel.yrpX" --output "C:\replays\duel.mkv" ^
  --codec hevc_nvenc --crf 6 --preset p7 --hwaccel vulkan --workdir "C:\EDOPro" ^
  --vf "libplacebo=upscaler=ewa_lanczos:downscaler=ewa_lanczos:antiringing=1:deband=1:deband_iterations=4:deband_radius=24:deband_threshold=4:chroma_location=left"
```

### Dry run — check rendering without encoding

```bat
replay2video.exe --render-replay duel.yrpX --dry-run
```
Renders 10 frames and saves `dry_run_frame.png` for inspection.

---

## Presets

Copy any file from `presets/` as `config.ini`. Each preset is a ready-to-use configuration.

> **Recommended:** `cpu_av1_svt_medium` or `cpu_av1_svt_high` — best quality/size ratio of all CPU codecs. Files are significantly lighter than H.264/H.265 at equal or better quality. The trade-off is encode time (CPU-only, slower than NVENC).

| File | Codec | Quality | GPU |
|------|-------|---------|-----|
| `cpu_h264_low` | H.264 | 720p / 30fps | — |
| `cpu_h264_medium` | H.264 | 1080p / 60fps | — |
| `cpu_h264_high` | H.264 | 1080p / 60fps + libplacebo | — |
| `cpu_h264_max` | H.264 | Max quality + libplacebo | — |
| `cpu_h265_low` | H.265 | 720p / 30fps | — |
| `cpu_h265_medium` | H.265 | 1080p / 60fps | — |
| `cpu_h265_high` | H.265 | 1080p / 60fps + libplacebo | — |
| `cpu_h265_max` | H.265 | Max quality + libplacebo | — |
| `cpu_av1_svt_low` | AV1 | 720p / 30fps | — |
| `cpu_av1_svt_medium` | AV1 | 1080p / 60fps | — |
| `cpu_av1_svt_high` | AV1 | 1080p / 60fps + libplacebo | — |
| `cpu_av1_svt_max` | AV1 | Max quality + film grain | — |
| `gpu_h264_nvenc_low` | H.264 NVENC | 720p / 30fps | GTX 10xx+ |
| `gpu_h264_nvenc_medium` | H.264 NVENC | 1080p / 60fps | GTX 10xx+ |
| `gpu_h264_nvenc_high` | H.264 NVENC | 1080p + libplacebo | GTX 10xx+ |
| `gpu_h264_nvenc_max` | H.264 NVENC | Max quality + libplacebo | GTX 10xx+ |
| `gpu_hevc_nvenc_low` | HEVC NVENC | 720p / 30fps | GTX 10xx+ |
| `gpu_hevc_nvenc_medium` | HEVC NVENC | 1080p / 60fps | GTX 10xx+ |
| `gpu_hevc_nvenc_high` | HEVC NVENC | 1080p + libplacebo | GTX 10xx+ |
| `gpu_hevc_nvenc_max` | HEVC NVENC | Max quality + libplacebo | GTX 10xx+ |
| `gpu_av1_nvenc_medium` | AV1 NVENC | 1080p + libplacebo | RTX 40xx+ |
| `gpu_av1_nvenc_max` | AV1 NVENC | Max quality + libplacebo | RTX 40xx+ |

---

## All CLI Options

### Input / Output
| Flag | Description | Default |
|------|-------------|---------|
| `--render-replay` | Path to `.yrpX` replay file | *(required)* |
| `--output` | Output path (`.mp4` or `.mkv`) | `output.mp4` |
| `--workdir` | Path to EDOPro installation | `.` |

### Video
| Flag | Description | Default |
|------|-------------|---------|
| `--resolution` | `WxH` e.g. `1920x1080` | `1920x1080` |
| `--fps` | Output frame rate | `60` |
| `--speed` | Playback speed multiplier (`1.5` = 50% faster) | `1.0` |
| `--player` | POV: `0` = Player 1, `1` = Player 2 | `0` |

### Encoding
| Flag | Description | Default |
|------|-------------|---------|
| `--codec` | `libx264` `libx265` `libsvtav1` `h264_nvenc` `hevc_nvenc` `av1_nvenc` | `libx264` |
| `--crf` | Quality: 0 (best) → 51 (worst) | `23` |
| `--preset` | CPU: `ultrafast`…`veryslow` · NVENC: `p1`…`p7` · SVT-AV1: `0`…`13` | `veryfast` |
| `--bitrate` | Fixed bitrate in kbps (overrides `--crf`) | — |
| `--tune` | x264: `film` `animation` `grain` · x265: `grain` `zerolatency` | — |
| `--film-grain` | SVT-AV1 grain synthesis `0`–`50` | `0` |

### Filters & GPU
| Flag | Description | Default |
|------|-------------|---------|
| `--vf` | FFmpeg filter string | — |
| `--hwaccel` | `vulkan` to enable GPU filtergraph (libplacebo) | — |
| `--hwaccel-device` | GPU index (multi-GPU) | `0` |
| `--scale-filter` | `bilinear` or `lanczos` | `bilinear` |

### Camera
| Flag | Description | Default |
|------|-------------|---------|
| `--margin` | Background margin `0.0`–`0.45` | `0.10` |
| `--cam-offset-y` | Camera vertical offset | `0.7` |

### Diagnostics
| Flag | Description |
|------|-------------|
| `--dry-run` | Render 10 frames, save `dry_run_frame.png`, exit |

---

## config.ini Keys

All CLI options are also available in `config.ini`. CLI always wins over config.

```ini
workdir=C:\EDOPro
resolution=1920x1080
fps=60
codec=libx264
crf=23
preset=veryfast
scale_filter=bilinear
speed=1.0
player=0
margin=0.10
# cam_offset_y=0.7
# sim_fps=0
# tune=film
# bitrate=8000
# film_grain=0
# vf=libplacebo=upscaler=ewa_lanczos:deband=1
# hwaccel=vulkan
# hwaccel_device=0
```

---

## Troubleshooting

**Black / blank output**
→ Check `--workdir` has `pics/`, `textures/`, `skin/`, `fonts/`, `cards.cdb`
→ Run `--dry-run` and inspect `dry_run_frame.png`

**"Failed to open replay"**
→ Make sure the file is `.yrpX` format (not `.yrp`)

**NVENC not found**
→ Update NVIDIA drivers
→ `av1_nvenc` requires RTX 40xx — RTX 30xx only supports `h264_nvenc` / `hevc_nvenc`

**Missing DLL at startup**
→ Keep all `.dll` files in the same folder as `replay2video.exe`

---

## How It Works

EDOPro runs in a hidden window (no display needed), driven by a virtual timer for frame-perfect output. Each frame is captured via Irrlicht's framebuffer, converted to YUV, and encoded with FFmpeg. The replay auto-advances without any user interaction.

Optional: GPU filtergraph via Vulkan + libplacebo for high-quality debanding and scaling.

---

## Building from Source

Requires Visual Studio 2022 Build Tools (C++17) and Git.

```bat
git clone https://github.com/Armytille/EDOPro-Replay2Video.git
cd EDOPro-Replay2Video
build_windows.bat
```

`build_windows.bat` clones EDOPro with submodules, downloads FFmpeg dev libs, applies all patches, and compiles. Output: `edopro\bin\x64\release\replay2video.exe`. Copy `ffmpeg-dev\bin\*.dll` next to the exe once after building.

### Building the GUI launcher

Requires Python 3.11 (3.14+ does not have a `pythonnet` wheel).

```bat
cd gui
build_launcher.bat
```

Produces `dist\replay2videoGUI.exe` (standalone, ~25 MB, no Python required for end users). Then run `deploy_package.bat` to assemble the full distribution in `dist\`.

---

## License

FFmpeg libraries bundled in the release are GPL-licensed (gyan.dev full build, includes x264/x265). For LGPL, use the BtbN build with `--disable-gpl`.
