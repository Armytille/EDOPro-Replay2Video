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

#include "video_encoder.h"
#include "offscreen_capture.h"

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
        } else if (arg == L"--render-output" || arg == L"--output") {
            std::string s;
            if (!next(s)) return false;
            output_video = s;
        } else if (arg == L"--render-width") {
            next_int(width);
        } else if (arg == L"--render-height") {
            next_int(height);
        } else if (arg == L"--render-fps") {
            next_int(fps);
        } else if (arg == L"--resolution" || arg == L"-r") {
            std::string s;
            if (!next(s)) return false;
            auto x = s.find('x');
            if (x != std::string::npos) {
                width = std::stoi(s.substr(0, x));
                height = std::stoi(s.substr(x + 1));
            }
        } else if (arg == L"--fps") {
            next_int(fps);
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
        } catch (...) {
            // Ignore malformed values
        }
    }
    return true;
}

void RenderConfig::Normalize() {
    if (width < 640) width = 640;
    if (height < 480) height = 480;

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
        encoder->Finalize();
        encoder.reset();
    }
    capture.reset();
}

bool Integration::ProcessFrame(irr::video::IVideoDriver* driver) {
    if (!capture || !encoder) return false;
    
    auto frame = capture->Capture(driver);
    if (!frame) {
        std::cerr << "[replay2video] Frame capture failed\n";
        return false;
    }
    
    if (g_batch.config.dry_run) {
        uint64_t fc = g_batch.frame_count.load();
        if (fc == static_cast<uint64_t>(g_batch.config.dry_run_save_frame)) {
            capture->SavePNG(frame, "dry_run_frame.png");
        }
        capture->ReleaseFrame(frame);
        return true;
    }
    
    // Write each simulated frame frames_per_sim times (frame doubling for higher output FPS).
    bool ok = true;
    for (int i = 0; i < g_batch.frames_per_sim && ok; ++i) {
        ok = encoder->EncodeFrame(frame);
    }
    capture->ReleaseFrame(frame);
    return ok;
}

bool Integration::IsFinished() const {
    if (g_batch.request_stop) return true;
    if (g_batch.config.dry_run && g_batch.frame_count >= static_cast<uint64_t>(g_batch.config.dry_run_frames))
        return true;
    if (g_batch.replay_finished && g_batch.frame_count > 0)
        return true;
    return false;
}

} // namespace replay2video
} // namespace ygo

#endif // WITH_REPLAY2VIDEO
