#include "offscreen_capture.h"

#ifdef WITH_REPLAY2VIDEO

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <IrrlichtDevice.h>
#include <IVideoDriver.h>
#include <IImage.h>
#include "../../irrlicht/src/CImage.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace ygo {
namespace replay2video {

FrameCapture::FrameCapture() = default;
FrameCapture::~FrameCapture() = default;

bool FrameCapture::Initialize(int width, int height) {
    width_ = width;
    height_ = height;
    initialized_ = true;
    return true;
}

AVFrame* FrameCapture::Capture(irr::video::IVideoDriver* driver) {
    if (!driver || !initialized_) return nullptr;

    // Use Irrlicht's built-in screenshot function. It captures the current framebuffer
    // reliably across OpenGL/DirectX backends without manual buffer management.
    irr::video::IImage* screenshot = driver->createScreenShot();
    if (!screenshot) {
        std::cerr << "[replay2video] driver->createScreenShot() returned null\n";
        return nullptr;
    }

    // Irrlicht screenshots are usually BGRA on Windows/OpenGL. Convert to RGBA.
    const irr::core::dimension2d<irr::u32> dim = screenshot->getDimension();
    const int w = dim.Width;
    const int h = dim.Height;

    if (w == 0 || h == 0) {
        screenshot->drop();
        return nullptr;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        screenshot->drop();
        return nullptr;
    }
    frame->width = w;
    frame->height = h;
    frame->format = AV_PIX_FMT_RGBA;

    if (av_frame_get_buffer(frame, 32) < 0) {
        av_frame_free(&frame);
        screenshot->drop();
        return nullptr;
    }

    // Lock image and copy pixel data row by row, handling BGRA->RGBA if needed.
    // For speed, we assume the screenshot format matches the driver's color format.
    // In practice EDOPro on Windows uses OpenGL/D3D with BGRA; we do a simple BGRA->RGBA swap.
    // Convert screenshot to A8R8G8B8 (Irrlicht's guaranteed 32-bit format) so we
    // always have a known 4-bytes-per-pixel layout regardless of the driver's native format.
    irr::video::IImage* rgba_img = nullptr;
    if(screenshot->getColorFormat() != irr::video::ECF_A8R8G8B8) {
        rgba_img = new irr::video::CImage(irr::video::ECF_A8R8G8B8, dim);
        screenshot->copyTo(rgba_img);
        screenshot->drop();
        screenshot = rgba_img;
    }

    uint8_t* dst = frame->data[0];
    const int dst_stride = frame->linesize[0];
    const int pitch = static_cast<int>(screenshot->getPitch());
    const uint8_t* src = reinterpret_cast<const uint8_t*>(screenshot->lock());

    if(!src) {
        screenshot->drop();
        av_frame_free(&frame);
        return nullptr;
    }

    // Irrlicht A8R8G8B8 is stored as BGRA in memory on little-endian (B=byte0, G=1, R=2, A=3).
    // FFmpeg AV_PIX_FMT_RGBA expects R=byte0, G=1, B=2, A=3. Swap R and B.
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = src + y * pitch;
        uint8_t* dst_row = dst + y * dst_stride;
        for (int x = 0; x < w; ++x) {
            dst_row[x * 4 + 0] = row[x * 4 + 2]; // R <- B
            dst_row[x * 4 + 1] = row[x * 4 + 1]; // G <- G
            dst_row[x * 4 + 2] = row[x * 4 + 0]; // B <- R
            dst_row[x * 4 + 3] = row[x * 4 + 3]; // A <- A
        }
    }
    screenshot->unlock();
    screenshot->drop();

    return frame;
}

void FrameCapture::SavePNG(AVFrame* frame, const std::string& path) {
    if (!frame) return;
    // Simple stb_image_write-based PNG save would go here, but for the deliverable
    // we implement a minimal PPM fallback if stb is unavailable, or we can use lodepng.
    // Since EDOPro already links libpng indirectly via irrlicht, we can write a raw
    // binary PPM as a guaranteed fallback, or implement a simple BMP.
    // Here we emit an uncompressed BMP which every image viewer can open.
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
        file_header_size + dib_header_size,0,0,0
    };
    file_header[2] = file_size & 0xFF;
    file_header[3] = (file_size >> 8) & 0xFF;
    file_header[4] = (file_size >> 16) & 0xFF;
    file_header[5] = (file_size >> 24) & 0xFF;
    std::fwrite(file_header, 1, 14, f);

    unsigned char dib_header[40] = {};
    dib_header[0] = dib_header_size;
    dib_header[4] = w & 0xFF;
    dib_header[5] = (w >> 8) & 0xFF;
    dib_header[6] = (w >> 16) & 0xFF;
    dib_header[7] = (w >> 24) & 0xFF;
    dib_header[8] = h & 0xFF;
    dib_header[9] = (h >> 8) & 0xFF;
    dib_header[10] = (h >> 16) & 0xFF;
    dib_header[11] = (h >> 24) & 0xFF;
    dib_header[12] = 1; // planes
    dib_header[14] = 24; // bpp
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

void FrameCapture::ReleaseFrame(AVFrame* frame) {
    if (frame) {
        av_frame_free(&frame);
    }
}

} // namespace replay2video
} // namespace ygo

#endif // WITH_REPLAY2VIDEO
