#include "replay2video_integration.h"

#ifdef WITH_REPLAY2VIDEO

#include <algorithm>
#include <fstream>
#include <sstream>
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
        } else if (arg == L"--dry-run") {
            dry_run = true;
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
            else if (key == "crf") crf = std::stoi(val);
            else if (key == "preset") preset = val;
            else if (key == "codec") codec = val;
            else if (key == "bitrate") bitrate_kbps = std::stoi(val);
            else if (key == "workdir") workdir = val;
            else if (key == "speed") speed = std::stof(val);
        } catch (...) {
            // Ignore malformed values
        }
    }
    return true;
}

void RenderConfig::Normalize() {
    if (width < 640) width = 640;
    if (height < 480) height = 480;
    if (fps < 1) fps = 60;
    if (fps > 240) fps = 240;
    if (crf < 0) crf = 0;
    if (crf > 51) crf = 51;
    if (preset.empty()) preset = "veryfast";
    if (codec.empty()) codec = "libx264";
    if (speed <= 0.0f) speed = 1.0f;
}

void BatchState::Start(const RenderConfig& cfg) {
    config = cfg;
    target_fps = cfg.fps;
    frame_time_ms = 1000 / target_fps;
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
    if (!capture->Initialize(g_batch.config.width, g_batch.config.height)) {
        std::cerr << "[replay2video] FrameCapture init failed\n";
        return false;
    }
    encoder = std::make_unique<VideoEncoder>();
    if (!encoder->Initialize(g_batch.config)) {
        std::cerr << "[replay2video] VideoEncoder init failed\n";
        return false;
    }
    return true;
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
    
    bool ok = encoder->EncodeFrame(frame);
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
