#pragma once

#include <cstdint>
#include <string>
#include <atomic>
#include <memory>
#include <vector>

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
    std::string input_deck;   // .ydk path for deck export mode
    int card_duration_ms = 3000; // deck mode: time spent on each unique card
    std::string output_video;
    std::string workdir;
    int width = 1920;         // output video resolution
    int height = 1080;
    int render_width = 0;    // Irrlicht window size (0 = auto-detect from desktop, capped to output res)
    int render_height = 0;
    int fps = 0;             // 0 = not set; Normalize() picks 60 for replay, 5 for deck mode
    int fps_cli = 0;         // set by CLI --fps / --render-fps; trumps ini and deck-mode default
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
    // Deck export mode: drives DeckBuilder rendering instead of replay playback.
    bool deck_mode = false;
    // Current highlighted card in the decklist. Updated by Integration::TickDeck().
    // (deck_index_pos: 1=main, 2=extra, 3=side; deck_index_seq: index within that section)
    int deck_index_pos = 0;
    int deck_index_seq = -1;
    uint32_t deck_current_code = 0;
    // Deck mode warmup: render N frames without capturing, so async texture
    // GPU upload completes before the first encoded frame.
    int deck_warmup_frames = 0;
    // Deck mode: signals RefreshCardInfoTextPositions() to push stInfo etc.
    // below the (taller, wordwrappable) stName instead of overwriting at y=37.
    bool deck_info_layout = false;
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

    // Deck export: unique-card traversal, built once on first TickDeck() call.
    // Lives on the Integration (not in static locals) so a second export within
    // the same process — possible via repeat BatchState::Start() — sees a clean
    // slate. Cleared in Shutdown().
    struct DeckSlot { int pos; int seq; uint32_t code; };
    std::vector<DeckSlot> deck_sequence;
    bool deck_sequence_built = false;
    // Index of the card encoded most recently (-1 = none yet). Used by
    // ProcessFrame() to skip already-encoded cards. Reset in Shutdown().
    int deck_last_encoded_idx = -1;
    // frames_per_card = card_duration_ms * fps / 1000. Cached at the moment
    // the sequence is built so ProcessFrame can derive the pts deterministically
    // (idx * frames_per_card) instead of sampling g_batch.frame_count.
    int deck_frames_per_card = 0;

    bool Initialize();
    void SetupFBO(irr::video::IVideoDriver* driver);
    void Shutdown();
    void BeginFrame();
    void EndFrame();
    bool ProcessFrame(irr::video::IVideoDriver* driver);
    bool IsFinished() const;
    // Deck mode: advance the highlight cursor based on g_batch.frame_count.
    // Returns true while there are still cards to show; false once the sequence is done.
    bool TickDeck();
    // Deck mode: current slot index (0-based) into deck_sequence, or -1 if no
    // active card. Set by TickDeck(); read by ProcessFrame() to compute the pts.
    int deck_current_idx = -1;
};

extern Integration g_integration;

} // namespace replay2video
} // namespace ygo

#endif // WITH_REPLAY2VIDEO
