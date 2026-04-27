#pragma once

#ifdef WITH_REPLAY2VIDEO

#include <string>
#include <cstdint>

struct AVFrame;
struct AVPacket;
struct AVCodecContext;
struct AVFormatContext;
struct AVStream;
struct AVFilterGraph;
struct AVFilterContext;
struct AVBufferRef;
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
    bool InitFilterGraph(const RenderConfig& cfg);
    bool SendFrameThroughFilter(AVFrame* bgra_frame);

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
    AVPacket* pkt_ = nullptr;

    // filtergraph (used when cfg.vf is non-empty)
    AVFilterGraph* filter_graph_ = nullptr;
    AVFilterContext* buffersrc_ctx_ = nullptr;
    AVFilterContext* buffersink_ctx_ = nullptr;
    AVBufferRef* hw_device_ctx_ = nullptr;
    AVFrame* filt_frame_ = nullptr;
    AVFrame* yuv_pre_frame_ = nullptr;  // intermediate yuv420p frame for GPU path pre-conversion
    SwsContext* filt_sws_ctx_ = nullptr; // swscale for BGRA→yuv420p before filtergraph
    bool filt_input_yuv_ = false;        // true when filtergraph expects yuv420p input

    std::string output_path_;
};

} // namespace replay2video
} // namespace ygo

#endif // WITH_REPLAY2VIDEO
