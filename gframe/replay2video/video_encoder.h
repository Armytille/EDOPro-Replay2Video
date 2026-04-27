#pragma once

#ifdef WITH_REPLAY2VIDEO

#include <string>
#include <cstdint>

struct AVFrame;
struct AVCodecContext;
struct AVFormatContext;
struct AVStream;
struct SwsContext;

namespace ygo {
namespace replay2video {

struct RenderConfig;

class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    bool Initialize(const RenderConfig& cfg);
    bool EncodeFrame(AVFrame* rgba_frame);
    void Finalize();

    bool IsInitialized() const { return initialized_; }

private:
    bool initialized_ = false;
    int width_ = 0;
    int height_ = 0;
    int fps_ = 60;
    int64_t frame_pts_ = 0;

    AVFormatContext* fmt_ctx_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    AVStream* stream_ = nullptr;
    SwsContext* sws_ctx_ = nullptr;
    AVFrame* yuv_frame_ = nullptr;
    std::string output_path_;
};

} // namespace replay2video
} // namespace ygo

#endif // WITH_REPLAY2VIDEO
