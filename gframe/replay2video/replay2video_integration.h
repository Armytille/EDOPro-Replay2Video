#pragma once

#include <cstdint>
#include <string>
#include <atomic>
#include <memory>

#ifdef WITH_REPLAY2VIDEO

struct AVFrame;
struct SwsContext;

namespace irr {
namespace video {
class IVideoDriver;
}
}

namespace ygo {
namespace replay2video {

struct RenderConfig {
    std::string input_replay;
    std::string output_video;
    std::string workdir;
    int width = 1920;         // output video resolution
    int height = 1080;
    int render_width = 0;    // Irrlicht window size (0 = auto-detect from desktop, capped to output res)
    int render_height = 0;
    int fps = 60;            // output video FPS
    int sim_fps = 0;         // simulation FPS (0 = same as fps); each sim frame is duplicated fps/sim_fps times
    int crf = 23;
    std::string preset = "veryfast";
    std::string codec = "libx264";
    int bitrate_kbps = 0; // 0 = use CRF
    std::string scale_filter = "bilinear"; // scaling algorithm: "bilinear" or "lanczos"
    std::string tune = ""; // x264/x265 tune parameter: "film", "animation", "grain", etc. (empty = none)
    std::string vf = ""; // video filter string (e.g., "libplacebo=upscaler=ewa_lanczos...")
    std::string hwaccel = ""; // hardware acceleration (e.g., "vulkan")
    std::string hwaccel_device = ""; // hwaccel device (e.g., "0")
    float speed = 1.0f; // playback speed multiplier: 1.0=normal, 1.3=30% faster
    float margin = 0.10f; // background margin: 0=field fills screen, 0.10=10% background each side
    float cam_offset_x = 4.0f; // camera X position and target X (4.0 centers left/right content)
    float cam_offset_y = 0.7f; // vertical camera target Y offset to balance both players' hands
    // Camera frustum computed by Normalize() for batch centering; 0 = use EDOPro defaults
    float cam_left = 0.0f;
    float cam_right = 0.0f;
    float cam_top = 0.0f;
    float cam_bottom = 0.0f;
    int pov_player = 0; // 0 = player 1 (default), 1 = player 2 (swap field)
    int film_grain = 0; // SVT-AV1 film grain synthesis (0=off, 1-50)
    bool topdown_view = false;
    bool dry_run = false;
    int dry_run_frames = 10;
    int dry_run_save_frame = 5;

    bool ParseArgs(int argc, wchar_t* argv[]);
    bool LoadIni(const std::string& path);
    void Normalize();
};

struct BatchState {
    std::atomic<bool> active{false};
    std::atomic<bool> request_stop{false};
    std::atomic<bool> replay_finished{false};
    std::atomic<bool> replay_started{false};
    std::atomic<uint64_t> frame_count{0};
    std::atomic<uint64_t> virtual_time_ms{0};
    int target_fps = 60;
    int frame_time_ms = 16;
    int frames_per_sim = 1;  // output frames written per simulated frame (fps / sim_fps)
    uint64_t max_frames = 0; // 0 = unlimited
    RenderConfig config;

    void Start(const RenderConfig& cfg);
    void Stop();
    uint64_t AdvanceVirtualTime();
};

extern BatchState g_batch;

class VideoEncoder;
class FrameCapture;

struct Integration {
    std::unique_ptr<VideoEncoder> encoder;
    std::unique_ptr<FrameCapture> capture;

    bool Initialize();
    void SetupFBO(irr::video::IVideoDriver* driver);
    void Shutdown();
    void BeginFrame();
    void EndFrame();
    bool ProcessFrame(irr::video::IVideoDriver* driver);
    bool IsFinished() const;
};

extern Integration g_integration;

} // namespace replay2video
} // namespace ygo

#endif // WITH_REPLAY2VIDEO
