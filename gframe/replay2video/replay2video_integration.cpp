#include "replay2video_integration.h"

#ifdef WITH_REPLAY2VIDEO

#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

#include <unordered_set>

#include "video_encoder.h"
#include "offscreen_capture.h"
#include "../utils.h"
#include "../game.h"
#include "../deck_con.h"
#include "../deck.h"
#include "../data_manager.h"

namespace ygo {
namespace replay2video {

BatchState g_batch;
Integration g_integration;

bool RenderConfig::ParseArgs(int argc, wchar_t* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        auto next = [&](std::string& out) -> bool {
            if (i + 1 >= argc) return false;
            wchar_t* w = argv[++i];
            int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            out.resize(len);
            WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
            if (!out.empty() && out.back() == '\0') out.pop_back();
            return true;
        };
        auto next_int = [&](int& out) -> bool {
            std::string s;
            if (!next(s)) return false;
            out = std::stoi(s);
            return true;
        };

        if (arg == L"--render-replay" || arg == L"--input") {
            std::string s;
            if (!next(s)) return false;
            input_replay = s;
        } else if (arg == L"--render-deck") {
            std::string s;
            if (!next(s)) return false;
            input_deck = s;
        } else if (arg == L"--card-duration-ms") {
            next_int(card_duration_ms);
        } else if (arg == L"--render-output" || arg == L"--output") {
            std::string s;
            if (!next(s)) return false;
            output_video = s;
        } else if (arg == L"--render-width") {
            next_int(width);
        } else if (arg == L"--render-height") {
            next_int(height);
        } else if (arg == L"--render-fps") {
            next_int(fps_cli);
        } else if (arg == L"--resolution" || arg == L"-r") {
            std::string s;
            if (!next(s)) return false;
            auto x = s.find('x');
            if (x != std::string::npos) {
                width = std::stoi(s.substr(0, x));
                height = std::stoi(s.substr(x + 1));
            }
        } else if (arg == L"--fps") {
            next_int(fps_cli);
        } else if (arg == L"--sim-fps") {
            next_int(sim_fps);
        } else if (arg == L"--crf") {
            next_int(crf);
        } else if (arg == L"--preset") {
            std::string s;
            if (!next(s)) return false;
            preset = s;
        } else if (arg == L"--codec") {
            std::string s;
            if (!next(s)) return false;
            codec = s;
        } else if (arg == L"--bitrate") {
            next_int(bitrate_kbps);
        } else if (arg == L"--workdir") {
            std::string s;
            if (!next(s)) return false;
            workdir = s;
        } else if (arg == L"--speed") {
            std::string s;
            if (!next(s)) return false;
            speed = std::stof(s);
        } else if (arg == L"--scale-filter") {
            std::string s;
            if (!next(s)) return false;
            scale_filter = s;
        } else if (arg == L"--tune") {
            std::string s;
            if (!next(s)) return false;
            tune = s;
        } else if (arg == L"--vf") {
            std::string s;
            if (!next(s)) return false;
            vf = s;
        } else if (arg == L"--hwaccel") {
            std::string s;
            if (!next(s)) return false;
            hwaccel = s;
        } else if (arg == L"--hwaccel-device") {
            std::string s;
            if (!next(s)) return false;
            hwaccel_device = s;
        } else if (arg == L"--dry-run") {
            dry_run = true;
        } else if (arg == L"--margin") {
            std::string s;
            if (!next(s)) return false;
            margin = std::stof(s);
        } else if (arg == L"--cam-offset-y") {
            std::string s;
            if (!next(s)) return false;
            cam_offset_y = std::stof(s);
        } else if (arg == L"--player") {
            next_int(pov_player);
        } else if (arg == L"--film-grain") {
            next_int(film_grain);
        } else if (arg == L"--topdown") {
            topdown_view = true;
        }
    }
    return true;
}

bool RenderConfig::LoadIni(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return false;
    std::string line;
    while (std::getline(ifs, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == '#') continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t\r\n");
            size_t b = s.find_last_not_of(" \t\r\n");
            if (a == std::string::npos) s.clear();
            else s = s.substr(a, b - a + 1);
        };
        trim(key); trim(val);
        try {
            if (key == "resolution") {
                auto x = val.find('x');
                if (x != std::string::npos) {
                    width = std::stoi(val.substr(0, x));
                    height = std::stoi(val.substr(x + 1));
                }
            } else if (key == "fps") fps = std::stoi(val);
            else if (key == "sim_fps") sim_fps = std::stoi(val);
            else if (key == "crf") crf = std::stoi(val);
            else if (key == "preset") preset = val;
            else if (key == "codec") codec = val;
            else if (key == "bitrate") bitrate_kbps = std::stoi(val);
            else if (key == "workdir") workdir = val;
            else if (key == "scale_filter") scale_filter = val;
            else if (key == "tune") tune = val;
            else if (key == "vf") vf = val;
            else if (key == "hwaccel") hwaccel = val;
            else if (key == "hwaccel_device") hwaccel_device = val;
            else if (key == "speed") speed = std::stof(val);
            else if (key == "margin") margin = std::stof(val);
            else if (key == "cam_offset_y") cam_offset_y = std::stof(val);
            else if (key == "player") pov_player = std::stoi(val);
            else if (key == "film_grain") film_grain = std::stoi(val);
            else if (key == "topdown_view") topdown_view = (val == "true" || val == "1");
            else if (key == "input_deck") input_deck = val;
            else if (key == "card_duration_ms") card_duration_ms = std::stoi(val);
        } catch (...) {
            // Ignore malformed values
        }
    }
    return true;
}

void RenderConfig::Normalize() {
    if (width < 640) width = 640;
    if (height < 480) height = 480;

    // FPS selection priority: CLI flag > deck-mode default > ini value > 60.
    if (fps_cli > 0) {
        fps = fps_cli;
    } else if (!input_deck.empty()) {
        // Deck export is near-static — only the highlighted card changes every
        // card_duration_ms. Output at 10 fps with sim at 10 fps too: the engine
        // already has near-zero per-frame cost (deck builder doesn't run game
        // simulation), and sim_fps must stay high enough that EDOPro's async
        // texture loader has time to fetch card thumbs between captures.
        fps = 10;
        if (sim_fps == 0) sim_fps = 10;
    } else if (fps == 0) {
        fps = 60;
    }

    // Deck export defaults: GPU encode (h264_nvenc) when codec wasn't explicitly
    // chosen — RTX-class hardware churns through the static frames in seconds.
    if (!input_deck.empty() && (codec.empty() || codec == "libx264")) {
        codec = "h264_nvenc";
    }
    // Deck export: use lanczos in swscale (sharper than bilinear, the default)
    // since the source content is dense card art with fine text.
    if (!input_deck.empty() && (scale_filter.empty() || scale_filter == "bilinear")) {
        scale_filter = "lanczos";
    }

    // Determine Irrlicht window size. OpenGL backbuffers on Windows are capped to the
    // desktop resolution by DWM — rendering beyond the monitor size produces black pixels.
    // If render_width/height are not set, auto-detect the desktop resolution and clamp.
    if (render_width <= 0 || render_height <= 0) {
#ifdef _WIN32
        int desk_w = GetSystemMetrics(SM_CXSCREEN);
        int desk_h = GetSystemMetrics(SM_CYSCREEN);
        if (desk_w > 0 && desk_h > 0) {
            render_width  = std::min(width,  desk_w);
            render_height = std::min(height, desk_h);
        } else {
            render_width  = width;
            render_height = height;
        }
#else
        render_width  = width;
        render_height = height;
#endif
        if (render_width != width || render_height != height) {
            std::cout << "[replay2video] Desktop is " << render_width << "x" << render_height
                      << " — rendering at native res, upscaling to "
                      << width << "x" << height << " via swscale\n";
        }
    }

    if (fps < 1) fps = 60;
    if (fps > 240) fps = 240;
    // sim_fps=0 means "same as fps" (no frame doubling)
    if (sim_fps <= 0) sim_fps = fps;
    if (sim_fps > fps) sim_fps = fps; // can't simulate faster than output
    // ensure fps is an exact multiple of sim_fps for clean frame doubling
    if (fps % sim_fps != 0) {
        // round sim_fps down to nearest divisor of fps
        for (int d = sim_fps; d >= 1; --d) {
            if (fps % d == 0) { sim_fps = d; break; }
        }
    }
    if (crf < 0) crf = 0;
    if (crf > 51) crf = 51;
    if (preset.empty()) preset = "veryfast";
    if (codec.empty()) codec = "libx264";
    if (scale_filter.empty()) scale_filter = "bilinear";
    if (speed <= 0.0f) speed = 1.0f;

    // Force even output dimensions (required for YUV420P).
    width  = (width  / 2) * 2;
    height = (height / 2) * 2;

    // Compute a centered camera frustum for batch mode.
    //
    // EDOPro's default frustum (CAMERA_LEFT=-0.90, CAMERA_RIGHT=0.45) is asymmetric:
    // the field is pushed left to leave room for the UI panels on the right.
    // In batch/video mode there are no UI panels, so we center the frustum.
    //
    // Default frustum half-width  = (0.45 - (-0.90)) / 2 = 0.675
    // Default frustum half-height = (0.42 - (-0.42)) / 2 = 0.42
    //
    // The default frustum was designed for 1024x640 (ratio 1.6). For a different
    // output ratio we scale the horizontal half accordingly so pixels stay square.
    //
    // `margin` (default 0.10) expands the frustum outward so the background is
    // visible around the field. The field always fits fully — margin only adds space.
    if (margin < -0.45f) margin = -0.45f;
    if (margin > 0.45f) margin = 0.45f;
    {
        // Analytically tight frustum that fits the full field + hands with no cropping,
        // centered on cam_offset_y=0.7 (balanced between player 1 and player 2 hands).
        // Derived by projecting the bounding box of all field content through the camera.
        // Camera: pos=(4.2, 8.0, 7.8), target=(4.2, 0.7, 0), up=(0,0,1), near=1.
        //   tight half_h = 0.2564  (balances ndc_p1=+0.611 vs ndc_p2=-0.605)
        //   tight half_w = 0.4491  (extra deck left is the widest content)
        // margin expands the frustum outward to reveal background around the field.
        const float tight_half_h = 0.2850f; // includes card half-height at hand positions
        // Derive half_w from half_h * pixel aspect ratio so cards render square at any resolution.
        const float pixel_ratio = (height > 0) ? (float)width / (float)height : (16.0f / 9.0f);
        const float half_h = tight_half_h * (1.0f + 2.0f * margin);
        const float half_w = half_h * pixel_ratio;
        cam_left   = -half_w;
        cam_right  =  half_w;
        cam_bottom = -half_h;
        cam_top    =  half_h;
    }
    std::cout << "[replay2video] margin=" << (int)(margin * 100)
              << "% -> frustum [" << cam_left << ", " << cam_right
              << "] x [" << cam_bottom << ", " << cam_top << "]\n";
}

void BatchState::Start(const RenderConfig& cfg) {
    config = cfg;
    // Simulation runs at sim_fps; each simulated frame is written fps/sim_fps times to the video.
    target_fps = cfg.sim_fps;
    frame_time_ms = 1000 / target_fps;
    frames_per_sim = cfg.fps / cfg.sim_fps; // e.g. 60/30=2 — frame doubling factor
    virtual_time_ms = 0;
    frame_count = 0;
    replay_finished = false;
    request_stop = false;
    replay_started = false;
    deck_mode = !cfg.input_deck.empty();
    deck_index_pos = 0;
    deck_index_seq = -1;
    deck_current_code = 0;
    deck_info_layout = false;
    // Reset Integration deck state too — re-launching a deck export within the
    // same process otherwise reuses the previous deck's sequence.
    g_integration.deck_sequence.clear();
    g_integration.deck_sequence_built = false;
    g_integration.deck_last_encoded_idx = -1;
    g_integration.deck_current_idx = -1;
    g_integration.deck_frames_per_card = 0;
    // Deck mode warmup: gives EDOPro's async texture loader time to upload
    // every thumb + art (worker processes one per main-loop tick). 30 frames
    // covers ≤30 unique cards; we lazily extend the warmup in TickDeck when
    // the actual deck size is larger.
    deck_warmup_frames = deck_mode ? 30 : 0;
    active = true;
    if (cfg.dry_run) {
        max_frames = cfg.dry_run_frames;
    } else {
        max_frames = 0;
    }
}

void BatchState::Stop() {
    request_stop = true;
    active = false;
}

uint64_t BatchState::AdvanceVirtualTime() {
    virtual_time_ms += frame_time_ms;
    return ++frame_count;
}

bool Integration::Initialize() {
    capture = std::make_unique<FrameCapture>();
    // FBO driver is set later via SetupFBO() once the Irrlicht device is ready.
    if (!capture->Initialize(g_batch.config.width, g_batch.config.height, nullptr)) {
        std::cerr << "[replay2video] FrameCapture init failed\n";
        return false;
    }
    if (!g_batch.config.dry_run) {
        encoder = std::make_unique<VideoEncoder>();
        if (!encoder->Initialize(g_batch.config)) {
            std::cerr << "[replay2video] VideoEncoder init failed\n";
            return false;
        }
    }
    if (g_batch.frames_per_sim > 1) {
        std::cout << "[replay2video] Frame doubling: sim=" << g_batch.config.sim_fps
                  << "fps output=" << g_batch.config.fps
                  << "fps (x" << g_batch.frames_per_sim << " per frame)\n";
    }
    return true;
}

void Integration::SetupFBO(irr::video::IVideoDriver* driver) {
    if (capture) capture->SetupFBO(driver, g_batch.config.render_width, g_batch.config.render_height);
}

void Integration::BeginFrame() {
    if (capture) capture->BeginFrame();
}

void Integration::EndFrame() {
    if (capture) capture->EndFrame();
}

void Integration::Shutdown() {
    if (encoder) {
        // Deck export: pad the final card so it stays visible for its full
        // card_duration_ms. The last EncodeFrame() landed at pts = (N-1) *
        // frames_per_card, so pad to N * frames_per_card to close the interval.
        if (g_batch.deck_mode && deck_last_encoded_idx >= 0 && deck_frames_per_card > 0) {
            const int64_t end_pts = (int64_t)(deck_last_encoded_idx + 1) * deck_frames_per_card;
            encoder->PadLastFrame(end_pts);
        }
        encoder->Finalize();
        encoder.reset();
    }
    capture.reset();
    // Clear deck traversal state so a subsequent BatchState::Start() begins clean.
    deck_sequence.clear();
    deck_sequence_built = false;
    deck_last_encoded_idx = -1;
    deck_current_idx = -1;
    deck_frames_per_card = 0;
}

bool Integration::ProcessFrame(irr::video::IVideoDriver* driver) {
    if (!capture) return false;
    if (!encoder && !g_batch.config.dry_run) return false;

    auto frame = capture->Capture(driver);
    if (!frame) {
        std::cerr << "[replay2video] Frame capture failed\n";
        return false;
    }

    if (g_batch.config.dry_run) {
        uint64_t fc = g_batch.frame_count.load();
        if (fc == static_cast<uint64_t>(g_batch.config.dry_run_save_frame)) {
            std::string png_path = ygo::Utils::ToUTF8IfNeeded(ygo::Utils::GetExeFolder()) + "dry_run_frame.png";
            capture->SavePNG(frame, png_path);
        }
        capture->ReleaseFrame(frame);
        return true;
    }

    bool ok = true;
    if (g_batch.deck_mode) {
        // Deck export: encode 1 frame per unique card with a deterministic pts.
        // Anchoring pts = idx * frames_per_card guarantees the first card lands
        // at pts=0 and every card occupies exactly card_duration_ms on the
        // timeline, regardless of how warmup or main-loop pacing nudges
        // frame_count. PadLastFrame() in Shutdown closes the trailing interval.
        if (deck_current_idx >= 0 && deck_current_idx != deck_last_encoded_idx) {
            const int64_t pts = (int64_t)deck_current_idx * deck_frames_per_card;
            ok = encoder->EncodeFrame(frame, pts);
            deck_last_encoded_idx = deck_current_idx;
        }
    } else {
        // Replay mode: write each simulated frame frames_per_sim times
        // (frame doubling for higher output FPS).
        for (int i = 0; i < g_batch.frames_per_sim && ok; ++i) {
            ok = encoder->EncodeFrame(frame);
        }
    }
    capture->ReleaseFrame(frame);

    // Emit progress for the GUI launcher (every 10 sim-frames to avoid stdout saturation).
    uint64_t fc = g_batch.frame_count.load();
    if (fc % 10 == 0) {
        printf("[r2v:frame] %llu\n", (unsigned long long)fc);
        fflush(stdout);
    }

    return ok;
}

bool Integration::IsFinished() const {
    if (g_batch.request_stop) return true;
    if (g_batch.config.dry_run && g_batch.frame_count >= static_cast<uint64_t>(g_batch.config.dry_run_frames))
        return true;
    // In replay mode, gate on frame_count>0 so we don't exit before MSG_START has
    // produced any visible output. Deck mode has no such pump — TickDeck may
    // signal finished with frame_count still at 0 (empty deck, all warmup), so
    // skip that gate there.
    if (g_batch.replay_finished && (g_batch.deck_mode || g_batch.frame_count > 0))
        return true;
    return false;
}

// Deck mode driver: builds a deduplicated traversal of (main, extra, side) on the
// first call, then advances the highlight cursor based on the elapsed virtual time.
// Updates g_batch.deck_index_{pos,seq} so DrawDeckBd can outline the active card,
// and calls Game::ShowCardInfo() so the left-side image+text panel follows along.
// Returns false when the sequence is exhausted (signals MainLoop to stop).
bool Integration::TickDeck() {
    if (!mainGame) return false;

    // Build the traversal once per Integration lifetime. State lives on the
    // Integration (not in function statics) so a fresh BatchState::Start() —
    // which resets these fields — sees a clean slate.
    if (!deck_sequence_built) {
        deck_sequence.clear();
        std::unordered_set<uint32_t> seen;
        const auto& deck = mainGame->deckBuilder.GetCurrentDeck();
        int dropped = 0;
        auto emit = [&](int pos, const Deck::Vector& v) {
            for (size_t i = 0; i < v.size(); ++i) {
                uint32_t code = v[i] ? v[i]->code : 0;
                if (code == 0) continue;
                // Skip cards unknown to the data manager — they would render as
                // tUnknown and produce a silently degraded slot in the timeline.
                if (!gDataManager || !gDataManager->GetCardData(code)) {
                    ++dropped;
                    continue;
                }
                if (seen.insert(code).second) {
                    deck_sequence.push_back({ pos, (int)i, code });
                }
            }
        };
        emit(1, deck.main);
        emit(2, deck.extra);
        emit(3, deck.side);
        deck_sequence_built = true;
        // Cache frames_per_card so ProcessFrame() can compute a deterministic
        // pts (idx * frames_per_card) instead of sampling g_batch.frame_count
        // (which would have a 1-tick offset on the first encoded card).
        const int per_ms = std::max(1, g_batch.config.card_duration_ms);
        deck_frames_per_card = std::max(1, per_ms * g_batch.target_fps / 1000);
        std::cout << "[replay2video] Deck export: " << deck_sequence.size()
                  << " unique cards, " << g_batch.config.card_duration_ms
                  << " ms each";
        if (dropped > 0) std::cout << " (" << dropped << " unknown codes skipped)";
        std::cout << "\n";
        // Extend warmup if the deck has more unique cards than the default budget.
        // Each main-loop tick the worker advances ~1 texture; we need roughly
        // (unique_count + a couple spare) ticks for everything to be GPU-resident.
        const int needed = (int)deck_sequence.size() + 5;
        if (needed > g_batch.deck_warmup_frames) {
            g_batch.deck_warmup_frames = needed;
        }
    }

    if (deck_sequence.empty()) return false;

    const uint64_t t_ms = g_batch.virtual_time_ms.load();
    const uint64_t per = std::max(1, g_batch.config.card_duration_ms);
    const uint64_t idx = t_ms / per;
    if (idx >= deck_sequence.size()) {
        return false;
    }

    const DeckSlot& slot = deck_sequence[idx];
    deck_current_idx = (int)idx;
    if (slot.code != g_batch.deck_current_code) {
        g_batch.deck_current_code = slot.code;
        g_batch.deck_index_pos = slot.pos;
        g_batch.deck_index_seq = slot.seq;
        mainGame->ShowCardInfo(slot.code);
    }
    return true;
}

} // namespace replay2video
} // namespace ygo

#endif // WITH_REPLAY2VIDEO
