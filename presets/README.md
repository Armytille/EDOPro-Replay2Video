# Presets

Rename the chosen file to `config.ini` and place it next to `ygopro.exe`.

| File | Codec | GPU required | Quality | Speed |
|------|-------|-------------|---------|-------|
| `cpu_h264_low.ini.example` | H.264 (CPU) | No | Low | Fast |
| `cpu_h264_medium.ini.example` | H.264 (CPU) | No | Medium | Medium |
| `cpu_h264_high.ini.example` | H.264 (CPU) | No | High + libplacebo | Very slow |
| `cpu_h265_medium.ini.example` | H.265 (CPU) | No | Medium | Slow |
| `cpu_h265_high.ini.example` | H.265 (CPU) | No | High + libplacebo | Extremely slow |
| `gpu_h264_nvenc_low.ini.example` | H.264 NVENC | NVIDIA | Low | Very fast |
| `gpu_h264_nvenc_medium.ini.example` | H.264 NVENC | NVIDIA | Medium | Very fast |
| `gpu_h264_nvenc_high.ini.example` | H.264 NVENC | NVIDIA + Vulkan | High + libplacebo | Very fast |
| `gpu_hevc_nvenc_medium.ini.example` | HEVC NVENC | NVIDIA GTX 10xx+ | Medium | Very fast |
| `gpu_hevc_nvenc_high.ini.example` | HEVC NVENC | NVIDIA GTX 10xx+ + Vulkan | High + libplacebo | Very fast |
| `gpu_av1_nvenc_medium.ini.example` | AV1 NVENC | NVIDIA RTX 40xx + Vulkan | Medium + libplacebo | Fast |
| `gpu_av1_nvenc_max.ini.example` | AV1 NVENC | NVIDIA RTX 40xx + Vulkan | Max + libplacebo | Fast |

**Quick pick:**
- Sharing with anyone → `cpu_h264_medium` or `gpu_h264_nvenc_medium`
- Best quality, NVIDIA GPU → `gpu_av1_nvenc_max`
- No GPU, archival → `cpu_h264_high`

All presets default to `workdir=D:\ProjectIgnis` — adjust to your EDOPro install path.
