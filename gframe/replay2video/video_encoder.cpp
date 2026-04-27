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

    stream_ = avformat_new_stream(fmt_ctx_, codec);
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
    codec_ctx_->max_b_frames = 2;

    if (cfg.bitrate_kbps > 0) {
        codec_ctx_->bit_rate = cfg.bitrate_kbps * 1000LL;
        codec_ctx_->rc_buffer_size = codec_ctx_->bit_rate;
    } else {
        // CRF mode: set via x264/x265 private options
        if (cfg.codec == "libx264" || cfg.codec == "libx265") {
            av_opt_set(codec_ctx_->priv_data, "crf", std::to_string(cfg.crf).c_str(), 0);
            av_opt_set(codec_ctx_->priv_data, "preset", cfg.preset.c_str(), 0);
        }
    }

    // Hardware encoder tuning
    if (cfg.codec == "h264_nvenc") {
        av_opt_set(codec_ctx_->priv_data, "preset", "p4", 0); // p4 = fast, p1 = slow
        av_opt_set(codec_ctx_->priv_data, "tune", "hq", 0);
        av_opt_set(codec_ctx_->priv_data, "profile", "high", 0);
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

    // Prepare YUV420P frame and swscale context
    sws_ctx_ = sws_getContext(width_, height_, AV_PIX_FMT_RGBA, width_, height_, AV_PIX_FMT_YUV420P,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        std::cerr << "[replay2video] sws_getContext failed\n";
        return false;
    }

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

    initialized_ = true;
    std::cout << "[replay2video] Encoder initialized: " << width_ << "x" << height_
              << " @ " << fps_ << "fps, codec=" << cfg.codec << "\n";
    return true;
}

bool VideoEncoder::EncodeFrame(AVFrame* rgba_frame) {
    if (!initialized_ || !rgba_frame) return false;

    // Convert RGBA -> YUV420P
    sws_scale(sws_ctx_, rgba_frame->data, rgba_frame->linesize, 0, height_,
              yuv_frame_->data, yuv_frame_->linesize);

    yuv_frame_->pts = frame_pts_++;

    int ret = avcodec_send_frame(codec_ctx_, yuv_frame_);
    if (ret < 0) {
        std::cerr << "[replay2video] avcodec_send_frame failed: " << av_err(ret) << "\n";
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx_, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            std::cerr << "[replay2video] avcodec_receive_packet failed: " << av_err(ret) << "\n";
            av_packet_free(&pkt);
            return false;
        }

        av_packet_rescale_ts(pkt, codec_ctx_->time_base, stream_->time_base);
        pkt->stream_index = stream_->id;

        ret = av_interleaved_write_frame(fmt_ctx_, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            std::cerr << "[replay2video] av_interleaved_write_frame failed: " << av_err(ret) << "\n";
            av_packet_free(&pkt);
            return false;
        }
    }
    av_packet_free(&pkt);
    return true;
}

void VideoEncoder::Finalize() {
    if (!initialized_) return;

    // Flush encoder
    if (codec_ctx_) {
        int ret = avcodec_send_frame(codec_ctx_, nullptr);
        AVPacket* pkt = av_packet_alloc();
        while (ret >= 0) {
            ret = avcodec_receive_packet(codec_ctx_, pkt);
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) break;
            if (ret < 0) break;
            av_packet_rescale_ts(pkt, codec_ctx_->time_base, stream_->time_base);
            pkt->stream_index = stream_->id;
            av_interleaved_write_frame(fmt_ctx_, pkt);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
    }

    if (fmt_ctx_) {
        av_write_trailer(fmt_ctx_);
        if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&fmt_ctx_->pb);
        }
    }

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
