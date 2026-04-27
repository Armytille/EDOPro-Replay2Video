#include "video_encoder.h"

#ifdef WITH_REPLAY2VIDEO

#include <iostream>
#include <cstring>
#include <string>

#include "replay2video_integration.h"

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/dict.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace ygo {
namespace replay2video {

VideoEncoder::VideoEncoder() = default;
VideoEncoder::~VideoEncoder() {
    Finalize();
}

static std::string av_err(int errnum) {
    char buf[256] = {};
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

// Build a filtergraph:
//   GPU path: buffersrc → hwupload → <vf> → hwdownload → format=yuv420p → buffersink
//   CPU path: buffersrc → <vf>,format=yuv420p → buffersink
//
// All GPU-side filters are created with avfilter_graph_alloc_filter + hw_device_ctx set
// before avfilter_init_str, as required for AVFILTER_FLAG_HWDEVICE filters.
// The buffersink is unconstrained; the explicit format filter provides the yuv420p guarantee.
bool VideoEncoder::InitFilterGraph(const RenderConfig& cfg) {
    int ret;

    if (!cfg.vf.empty() && cfg.hwaccel == "vulkan") {
        std::string device_idx = cfg.hwaccel_device.empty() ? "0" : cfg.hwaccel_device;
        // linear_images=0: use staging buffers instead of VK_IMAGE_TILING_LINEAR for
        // CPU→GPU upload. NVIDIA does not support linear images beyond small sizes,
        // causing VK_ERROR_OUT_OF_DEVICE_MEMORY on 1920×1080 BGRA frames.
        AVDictionary* vk_opts = nullptr;
        av_dict_set(&vk_opts, "linear_images", "0", 0);
        ret = av_hwdevice_ctx_create(&hw_device_ctx_, AV_HWDEVICE_TYPE_VULKAN,
                                     device_idx.c_str(), vk_opts, 0);
        av_dict_free(&vk_opts);
        if (ret < 0) {
            std::cerr << "[replay2video] Vulkan device init failed: " << av_err(ret)
                      << " — falling back to CPU filtergraph\n";
            av_buffer_unref(&hw_device_ctx_);
        } else {
            std::cout << "[replay2video] Vulkan device initialized (device " << device_idx << ")\n";
        }
    }

    filter_graph_ = avfilter_graph_alloc();
    if (!filter_graph_) return false;

    // With Vulkan, use yuv420p as buffersrc input format: libplacebo handles
    // the GPU upload internally from yuv420p (3MB/frame) without needing hwupload.
    // BGRA (8.3MB/frame) saturates libplacebo's Vulkan slab pool on NVIDIA.
    AVPixelFormat src_fmt = hw_device_ctx_ ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_BGRA;
    filt_input_yuv_ = (src_fmt == AV_PIX_FMT_YUV420P);

    char src_args[512];
    snprintf(src_args, sizeof(src_args),
             "video_size=%dx%d:pix_fmt=%d:time_base=1/%d:pixel_aspect=1/1",
             width_, height_, (int)src_fmt, fps_);

    const AVFilter* buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");
    if (!buffersrc || !buffersink) {
        std::cerr << "[replay2video] buffer/buffersink filter not found\n";
        return false;
    }

    ret = avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "in",
                                       src_args, nullptr, filter_graph_);
    if (ret < 0) { std::cerr << "[replay2video] buffersrc failed: " << av_err(ret) << "\n"; return false; }

    ret = avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                       nullptr, nullptr, filter_graph_);
    if (ret < 0) { std::cerr << "[replay2video] buffersink failed: " << av_err(ret) << "\n"; return false; }

    std::string filter_chain;

    {
        // GPU path (hw_device_ctx_ set) and CPU path both use parse_ptr.
        //
        // For GPU (--hwaccel vulkan): buffersrc receives yuv420p frames directly.
        // libplacebo accepts SW yuv420p input and manages its own Vulkan upload
        // internally. This avoids hwupload entirely — hwupload on NVIDIA with Vulkan
        // requires VK_IMAGE_TILING_LINEAR which is unsupported at 1920×1080 BGRA,
        // causing VK_ERROR_OUT_OF_DEVICE_MEMORY on every frame.
        // The hw_device_ctx is attached to the filtergraph filters so libplacebo
        // can pick it up during graph config.
        filter_chain = cfg.vf + ",format=yuv420p";

        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs  = avfilter_inout_alloc();
        if (!outputs || !inputs) { avfilter_inout_free(&outputs); avfilter_inout_free(&inputs); return false; }
        outputs->name = av_strdup("in");  outputs->filter_ctx = buffersrc_ctx_;  outputs->pad_idx = 0; outputs->next = nullptr;
        inputs->name  = av_strdup("out"); inputs->filter_ctx  = buffersink_ctx_; inputs->pad_idx  = 0; inputs->next  = nullptr;
        ret = avfilter_graph_parse_ptr(filter_graph_, filter_chain.c_str(), &inputs, &outputs, nullptr);
        avfilter_inout_free(&outputs); avfilter_inout_free(&inputs);
        if (ret < 0) { std::cerr << "[replay2video] avfilter_graph_parse_ptr failed: " << av_err(ret) << "\n"; return false; }

        // Attach hw_device_ctx to all parsed filters so libplacebo finds the Vulkan device.
        if (hw_device_ctx_) {
            for (unsigned i = 0; i < filter_graph_->nb_filters; i++) {
                if (!filter_graph_->filters[i]->hw_device_ctx)
                    filter_graph_->filters[i]->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
            }
        }
    }

    ret = avfilter_graph_config(filter_graph_, nullptr);
    if (ret < 0) {
        std::cerr << "[replay2video] avfilter_graph_config failed: " << av_err(ret) << "\n";
        return false;
    }

    filt_frame_ = av_frame_alloc();
    if (!filt_frame_) return false;

    std::cout << "[replay2video] Filtergraph: " << filter_chain << "\n";
    return true;
}

bool VideoEncoder::Initialize(const RenderConfig& cfg) {
    output_path_ = cfg.output_video;
    width_ = cfg.width;
    height_ = cfg.height;
    fps_ = cfg.fps;
    frame_pts_ = 0;

    avformat_alloc_output_context2(&fmt_ctx_, nullptr, nullptr, output_path_.c_str());
    if (!fmt_ctx_) {
        std::cerr << "[replay2video] avformat_alloc_output_context2 failed\n";
        return false;
    }

    const AVCodec* codec = avcodec_find_encoder_by_name(cfg.codec.c_str());
    if (!codec) {
        std::cerr << "[replay2video] Codec not found: " << cfg.codec << "\n";
        return false;
    }

    stream_ = avformat_new_stream(fmt_ctx_, nullptr);
    if (!stream_) {
        std::cerr << "[replay2video] avformat_new_stream failed\n";
        return false;
    }
    stream_->id = 0;

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        std::cerr << "[replay2video] avcodec_alloc_context3 failed\n";
        return false;
    }

    codec_ctx_->width = width_;
    codec_ctx_->height = height_;
    codec_ctx_->time_base = AVRational{ 1, fps_ };
    codec_ctx_->framerate = AVRational{ fps_, 1 };
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx_->gop_size = fps_;
    codec_ctx_->max_b_frames = 0;
    if (fmt_ctx_->oformat->flags & AVFMT_GLOBALHEADER)
        codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (cfg.bitrate_kbps > 0) {
        codec_ctx_->bit_rate = cfg.bitrate_kbps * 1000LL;
        codec_ctx_->rc_buffer_size = codec_ctx_->bit_rate;
    } else {
        if (cfg.codec == "libx264" || cfg.codec == "libx265") {
            av_opt_set(codec_ctx_->priv_data, "crf", std::to_string(cfg.crf).c_str(), 0);
            av_opt_set(codec_ctx_->priv_data, "preset", cfg.preset.c_str(), 0);
            // tune=film/animation/grain are x264-only; x265 uses grain/animation/zerolatency/etc.
            // Let the user pass whatever tune is valid for the chosen codec; ignore if rejected.
            if (!cfg.tune.empty()) {
                int tune_ret = av_opt_set(codec_ctx_->priv_data, "tune", cfg.tune.c_str(), 0);
                if (tune_ret < 0)
                    std::cerr << "[replay2video] Warning: tune=" << cfg.tune
                              << " not valid for " << cfg.codec << ", ignored\n";
            }
        }
    }

    if (cfg.codec == "h264_nvenc" || cfg.codec == "hevc_nvenc" || cfg.codec == "av1_nvenc") {
        // NVENC preset: p1 (fastest) to p7 (slowest/best). Use p4 (balanced) unless user specified.
        std::string nvenc_preset = cfg.preset;
        if (nvenc_preset.empty() || (nvenc_preset != "p1" && nvenc_preset != "p2" &&
            nvenc_preset != "p3" && nvenc_preset != "p4" && nvenc_preset != "p5" &&
            nvenc_preset != "p6" && nvenc_preset != "p7"))
            nvenc_preset = "p4";
        av_opt_set(codec_ctx_->priv_data, "preset", nvenc_preset.c_str(), 0);
        av_opt_set(codec_ctx_->priv_data, "tune", "hq", 0);
        if (cfg.codec == "h264_nvenc")
            av_opt_set(codec_ctx_->priv_data, "profile", "high", 0);
        // CRF → CQ mapping: NVENC uses rc=vbr + cq for constant-quality mode
        if (cfg.bitrate_kbps <= 0) {
            av_opt_set(codec_ctx_->priv_data, "rc", "vbr", 0);
            av_opt_set(codec_ctx_->priv_data, "cq", std::to_string(cfg.crf).c_str(), 0);
            codec_ctx_->bit_rate = 0;
        }
        // Quality enhancements: AQ
        av_opt_set(codec_ctx_->priv_data, "spatial-aq", "1", 0);
        av_opt_set(codec_ctx_->priv_data, "temporal-aq", "1", 0);
        av_opt_set(codec_ctx_->priv_data, "aq-strength", "8", 0);
        // rc-lookahead disabled: causes frame duplication/freeze with av1_nvenc
        if (cfg.codec != "av1_nvenc")
            av_opt_set(codec_ctx_->priv_data, "rc-lookahead", "32", 0);
    }

    int ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        std::cerr << "[replay2video] avcodec_open2 failed: " << av_err(ret) << "\n";
        return false;
    }

    ret = avcodec_parameters_from_context(stream_->codecpar, codec_ctx_);
    if (ret < 0) {
        std::cerr << "[replay2video] avcodec_parameters_from_context failed: " << av_err(ret) << "\n";
        return false;
    }
    // Reset codec_tag so the muxer picks the correct one for the container.
    // NVENC encoders set a tag valid for MP4 but rejected by MKV's muxer.
    stream_->codecpar->codec_tag = 0;

    stream_->time_base = codec_ctx_->time_base;

    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmt_ctx_->pb, output_path_.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            std::cerr << "[replay2video] avio_open failed: " << av_err(ret) << "\n";
            return false;
        }
    }

    ret = avformat_write_header(fmt_ctx_, nullptr);
    if (ret < 0) {
        std::cerr << "[replay2video] avformat_write_header failed: " << av_err(ret) << "\n";
        return false;
    }

    if (!cfg.vf.empty()) {
        if (!InitFilterGraph(cfg)) return false;
        // filt_frame_ is used as the encode source; no swscale/yuv_frame_ needed
    } else {
        // Plain swscale path
        sws_ctx_ = nullptr;
        yuv_frame_ = av_frame_alloc();
        if (!yuv_frame_) {
            std::cerr << "[replay2video] av_frame_alloc failed\n";
            return false;
        }
        yuv_frame_->width = width_;
        yuv_frame_->height = height_;
        yuv_frame_->format = AV_PIX_FMT_YUV420P;
        ret = av_frame_get_buffer(yuv_frame_, 32);
        if (ret < 0) {
            std::cerr << "[replay2video] av_frame_get_buffer failed\n";
            return false;
        }
    }

    pkt_ = av_packet_alloc();
    if (!pkt_) {
        std::cerr << "[replay2video] av_packet_alloc failed\n";
        return false;
    }

    initialized_ = true;
    std::cout << "[replay2video] Encoder initialized: " << width_ << "x" << height_
              << " @ " << fps_ << "fps, codec=" << cfg.codec;
    if (!cfg.vf.empty()) std::cout << ", vf=" << cfg.vf;
    if (!cfg.hwaccel.empty()) std::cout << ", hwaccel=" << cfg.hwaccel;
    std::cout << "\n";
    return true;
}

static int send_packet(AVCodecContext* codec_ctx, AVFormatContext* fmt_ctx,
                       AVStream* stream, AVPacket* pkt, AVFrame* frame) {
    int ret = avcodec_send_frame(codec_ctx, frame);
    if (ret < 0) {
        std::cerr << "[replay2video] avcodec_send_frame failed: " << av_err(ret) << "\n";
        return ret;
    }
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            std::cerr << "[replay2video] avcodec_receive_packet failed: " << av_err(ret) << "\n";
            return ret;
        }
        av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);
        pkt->stream_index = stream->id;
        ret = av_interleaved_write_frame(fmt_ctx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            std::cerr << "[replay2video] av_interleaved_write_frame failed: " << av_err(ret) << "\n";
            return ret;
        }
    }
    return 0;
}

bool VideoEncoder::SendFrameThroughFilter(AVFrame* bgra_frame) {
    AVFrame* src_frame = bgra_frame;

    // GPU path: filtergraph expects yuv420p input. Convert BGRA→yuv420p via swscale first.
    if (filt_input_yuv_) {
        const int src_w = bgra_frame->width;
        const int src_h = bgra_frame->height;
        filt_sws_ctx_ = sws_getCachedContext(filt_sws_ctx_,
            src_w, src_h, AV_PIX_FMT_BGRA,
            width_, height_, AV_PIX_FMT_YUV420P,
            src_w == width_ && src_h == height_ ? SWS_FAST_BILINEAR : SWS_BILINEAR,
            nullptr, nullptr, nullptr);
        if (!filt_sws_ctx_) return false;

        if (!yuv_pre_frame_) {
            yuv_pre_frame_ = av_frame_alloc();
            if (!yuv_pre_frame_) return false;
            yuv_pre_frame_->width = width_;
            yuv_pre_frame_->height = height_;
            yuv_pre_frame_->format = AV_PIX_FMT_YUV420P;
            if (av_frame_get_buffer(yuv_pre_frame_, 32) < 0) return false;
        }
        sws_scale(filt_sws_ctx_, bgra_frame->data, bgra_frame->linesize, 0, src_h,
                  yuv_pre_frame_->data, yuv_pre_frame_->linesize);
        src_frame = yuv_pre_frame_;
    }

    // Push frame into the filtergraph
    int ret = av_buffersrc_add_frame_flags(buffersrc_ctx_, src_frame,
                                           AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
        std::cerr << "[replay2video] av_buffersrc_add_frame_flags failed: " << av_err(ret) << "\n";
        return false;
    }

    // Pull filtered YUV420P frames and encode each
    while (true) {
        ret = av_buffersink_get_frame(buffersink_ctx_, filt_frame_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            std::cerr << "[replay2video] av_buffersink_get_frame failed: " << av_err(ret) << "\n";
            return false;
        }
        filt_frame_->pts = frame_pts_++;
        if (send_packet(codec_ctx_, fmt_ctx_, stream_, pkt_, filt_frame_) < 0) {
            av_frame_unref(filt_frame_);
            return false;
        }
        av_frame_unref(filt_frame_);
    }
    return true;
}

bool VideoEncoder::EncodeFrame(AVFrame* rgba_frame) {
    if (!initialized_ || !rgba_frame) return false;

    if (filter_graph_) {
        return SendFrameThroughFilter(rgba_frame);
    }

    // Plain swscale path (no --vf)
    const int src_w = rgba_frame->width;
    const int src_h = rgba_frame->height;
    sws_ctx_ = sws_getCachedContext(sws_ctx_,
        src_w, src_h, AV_PIX_FMT_BGRA,
        width_, height_, AV_PIX_FMT_YUV420P,
        src_w == width_ && src_h == height_ ? SWS_FAST_BILINEAR : SWS_BILINEAR,
        nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        std::cerr << "[replay2video] sws_getCachedContext failed\n";
        return false;
    }

    sws_scale(sws_ctx_, rgba_frame->data, rgba_frame->linesize, 0, src_h,
              yuv_frame_->data, yuv_frame_->linesize);
    yuv_frame_->pts = frame_pts_++;

    return send_packet(codec_ctx_, fmt_ctx_, stream_, pkt_, yuv_frame_) == 0;
}

void VideoEncoder::Finalize() {
    if (!initialized_) return;

    // Flush encoder
    if (codec_ctx_ && pkt_) {
        int ret = avcodec_send_frame(codec_ctx_, nullptr);
        while (ret >= 0) {
            ret = avcodec_receive_packet(codec_ctx_, pkt_);
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) break;
            if (ret < 0) break;
            av_packet_rescale_ts(pkt_, codec_ctx_->time_base, stream_->time_base);
            pkt_->stream_index = stream_->id;
            av_interleaved_write_frame(fmt_ctx_, pkt_);
            av_packet_unref(pkt_);
        }
        av_packet_free(&pkt_);
    }

    if (fmt_ctx_) {
        av_write_trailer(fmt_ctx_);
        if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&fmt_ctx_->pb);
        }
    }

    avfilter_graph_free(&filter_graph_);
    buffersrc_ctx_ = nullptr;
    buffersink_ctx_ = nullptr;
    av_frame_free(&filt_frame_);
    av_frame_free(&yuv_pre_frame_);
    sws_freeContext(filt_sws_ctx_);
    filt_sws_ctx_ = nullptr;
    av_buffer_unref(&hw_device_ctx_);

    sws_freeContext(sws_ctx_);
    sws_ctx_ = nullptr;

    av_frame_free(&yuv_frame_);
    avcodec_free_context(&codec_ctx_);
    avformat_free_context(fmt_ctx_);
    fmt_ctx_ = nullptr;
    stream_ = nullptr;

    initialized_ = false;
    std::cout << "[replay2video] Encoder finalized, output: " << output_path_ << "\n";
}

} // namespace replay2video
} // namespace ygo

#endif // WITH_REPLAY2VIDEO
