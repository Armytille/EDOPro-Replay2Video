#pragma once

#ifdef WITH_REPLAY2VIDEO

#include <cstdint>
#include <string>

struct AVFrame;

namespace irr {
namespace video {
class IVideoDriver;
class IImage;
}
}

namespace ygo {
namespace replay2video {

class FrameCapture {
public:
    FrameCapture();
    ~FrameCapture();
    bool Initialize(int width, int height);
    AVFrame* Capture(irr::video::IVideoDriver* driver);
    void SavePNG(AVFrame* frame, const std::string& path);
    void ReleaseFrame(AVFrame* frame);

private:
    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
};

} // namespace replay2video
} // namespace ygo

#endif // WITH_REPLAY2VIDEO
