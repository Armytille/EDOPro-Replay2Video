#include "offscreen_capture.h"

#ifdef WITH_REPLAY2VIDEO

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <IrrlichtDevice.h>
#include <IVideoDriver.h>
#include <IImage.h>
#include <IRenderTarget.h>
#include "../../irrlicht/src/CImage.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}

namespace ygo {
namespace replay2video {

FrameCapture::FrameCapture() = default;

FrameCapture::~FrameCapture() {
    if (bgra_frame_) av_frame_free(&bgra_frame_);
    // fbo_texture_ and fbo_target_ are owned by the driver; driver_ cleans them up.
}

bool FrameCapture::Initialize(int width, int height, irr::video::IVideoDriver* /*driver*/) {
    width_ = width;
    height_ = height;

    bgra_frame_ = av_frame_alloc();
    if (!bgra_frame_) return false;
    bgra_frame_->width = width;
    bgra_frame_->height = height;
    bgra_frame_->format = AV_PIX_FMT_BGRA;
    if (av_frame_get_buffer(bgra_frame_, 32) < 0) {
        av_frame_free(&bgra_frame_);
        return false;
    }

    initialized_ = true;
    return true;
}

void FrameCapture::SetupFBO(irr::video::IVideoDriver* driver, int render_w, int render_h) {
    if (!driver || !initialized_) return;
    driver_ = driver;

    // FBO at the native render resolution (capped to desktop by DWM).
    // createScreenShot will read from this FBO; swscale upscales to output resolution.
    fbo_texture_ = driver->addRenderTargetTexture(
        irr::core::dimension2d<irr::u32>(render_w, render_h),
        "r2v_fbo", irr::video::ECF_A8R8G8B8);
    if (!fbo_texture_) {
        std::cerr << "[replay2video] addRenderTargetTexture() failed — falling back to window capture\n";
        return;
    }
    fbo_target_ = driver->addRenderTarget();
    if (!fbo_target_) {
        std::cerr << "[replay2video] addRenderTarget() failed — falling back to window capture\n";
        driver->removeTexture(fbo_texture_);
        fbo_texture_ = nullptr;
        return;
    }
    fbo_target_->setTexture({ fbo_texture_ }, nullptr);
    std::cout << "[replay2video] FBO render target created: " << render_w << "x" << render_h
              << (render_w != width_ || render_h != height_ ? " (swscale → " + std::to_string(width_) + "x" + std::to_string(height_) + ")" : "") << "\n";
}

void FrameCapture::BeginFrame() {
    if (!fbo_target_ || !driver_) return;
    // Redirect all rendering into the FBO. EDOPro's beginScene/endScene will operate
    // on this render target instead of the window's framebuffer.
    driver_->setRenderTargetEx(fbo_target_,
        irr::video::ECBF_COLOR | irr::video::ECBF_DEPTH,
        irr::video::SColor(0, 0, 0, 0));
}

void FrameCapture::EndFrame() {
    if (!fbo_target_ || !driver_) return;
    // Restore default framebuffer (window).
    driver_->setRenderTargetEx(nullptr, irr::video::ECBF_NONE);
}

AVFrame* FrameCapture::Capture(irr::video::IVideoDriver* driver) {
    if (!driver || !initialized_) return nullptr;

    // createScreenShot reads from GL_COLOR_ATTACHMENT0 when a FBO is active (patched),
    // or from GL_FRONT when using the window framebuffer.
    irr::video::IImage* screenshot = driver->createScreenShot();
    if (!screenshot) {
        std::cerr << "[replay2video] driver->createScreenShot() returned null\n";
        return nullptr;
    }

    const irr::core::dimension2d<irr::u32> dim = screenshot->getDimension();
    const int w = static_cast<int>(dim.Width);
    const int h = static_cast<int>(dim.Height);

    if (w == 0 || h == 0) {
        screenshot->drop();
        return nullptr;
    }

    if (!size_warned_) {
        size_warned_ = true;
        std::cout << "[replay2video] First screenshot: " << w << "x" << h
                  << " (target: " << width_ << "x" << height_ << ")\n";
        if (w != width_ || h != height_) {
            std::cerr << "[replay2video] WARNING: captured " << w << "x" << h
                      << " but output is " << width_ << "x" << height_
                      << " — swscale will upscale\n";
        }
    }

    // Ensure A8R8G8B8 (BGRA on little-endian).
    irr::video::IImage* src_img = screenshot;
    if (screenshot->getColorFormat() != irr::video::ECF_A8R8G8B8) {
        src_img = new irr::video::CImage(irr::video::ECF_A8R8G8B8, dim);
        screenshot->copyTo(src_img);
        screenshot->drop();
    }

    const uint8_t* src = reinterpret_cast<const uint8_t*>(src_img->lock());
    if (!src) {
        src_img->drop();
        return nullptr;
    }

    // Reallocate bgra_frame_ if captured size differs from expected (fallback path).
    if (w != bgra_frame_->width || h != bgra_frame_->height) {
        av_frame_unref(bgra_frame_);
        bgra_frame_->width = w;
        bgra_frame_->height = h;
        bgra_frame_->format = AV_PIX_FMT_BGRA;
        if (av_frame_get_buffer(bgra_frame_, 32) < 0) {
            src_img->unlock();
            src_img->drop();
            return nullptr;
        }
    }

    const int pitch = static_cast<int>(src_img->getPitch());
    const int dst_stride = bgra_frame_->linesize[0];
    uint8_t* dst = bgra_frame_->data[0];
    const int copy_stride = std::min(pitch, dst_stride);
    for (int y = 0; y < h; ++y) {
        std::memcpy(dst + y * dst_stride, src + y * pitch, copy_stride);
    }

    src_img->unlock();
    src_img->drop();

    return bgra_frame_;
}

void FrameCapture::SavePNG(AVFrame* frame, const std::string& path) {
    if (!frame) return;
    int w = frame->width;
    int h = frame->height;
    int stride = frame->linesize[0];
    const uint8_t* src = frame->data[0];

    std::FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "wb");
    if (!f) return;

    int row_padded = (w * 3 + 3) & ~3;
    int dib_header_size = 40;
    int file_header_size = 14;
    int pixel_data_size = row_padded * h;
    int file_size = file_header_size + dib_header_size + pixel_data_size;

    unsigned char file_header[14] = {
        'B','M',
        0,0,0,0,
        0,0,0,0,
        (unsigned char)(file_header_size + dib_header_size),0,0,0
    };
    file_header[2] = file_size & 0xFF;
    file_header[3] = (file_size >> 8) & 0xFF;
    file_header[4] = (file_size >> 16) & 0xFF;
    file_header[5] = (file_size >> 24) & 0xFF;
    std::fwrite(file_header, 1, 14, f);

    unsigned char dib_header[40] = {};
    dib_header[0] = (unsigned char)dib_header_size;
    dib_header[4] = w & 0xFF;
    dib_header[5] = (w >> 8) & 0xFF;
    dib_header[6] = (w >> 16) & 0xFF;
    dib_header[7] = (w >> 24) & 0xFF;
    dib_header[8] = h & 0xFF;
    dib_header[9] = (h >> 8) & 0xFF;
    dib_header[10] = (h >> 16) & 0xFF;
    dib_header[11] = (h >> 24) & 0xFF;
    dib_header[12] = 1;
    dib_header[14] = 24;
    std::fwrite(dib_header, 1, 40, f);

    std::vector<uint8_t> row(row_padded, 0);
    for (int y = h - 1; y >= 0; --y) {
        std::fill(row.begin(), row.end(), 0);
        const uint8_t* src_row = src + y * stride;
        for (int x = 0; x < w; ++x) {
            row[x * 3 + 0] = src_row[x * 4 + 2]; // B
            row[x * 3 + 1] = src_row[x * 4 + 1]; // G
            row[x * 3 + 2] = src_row[x * 4 + 0]; // R
        }
        std::fwrite(row.data(), 1, row_padded, f);
    }
    std::fclose(f);
}

void FrameCapture::ReleaseFrame(AVFrame* /*frame*/) {
    // bgra_frame_ is owned by FrameCapture and reused; nothing to free here.
}

} // namespace replay2video
} // namespace ygo

#endif // WITH_REPLAY2VIDEO
