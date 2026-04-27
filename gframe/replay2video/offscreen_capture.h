#pragma once

#ifdef WITH_REPLAY2VIDEO

#include <cstdint>
#include <string>

struct AVFrame;

namespace irr {
namespace video {
class IVideoDriver;
class IImage;
class IRenderTarget;
class ITexture;
}
}

namespace ygo {
namespace replay2video {

class FrameCapture {
public:
    FrameCapture();
    ~FrameCapture();
    bool Initialize(int width, int height, irr::video::IVideoDriver* driver = nullptr);
    // Creates the FBO at render_w×render_h (capped to desktop). swscale upscales to output res.
    void SetupFBO(irr::video::IVideoDriver* driver, int render_w, int render_h);
    // Called before beginScene — redirects rendering into the FBO (no-op if no FBO).
    void BeginFrame();
    // Called after endScene / ProcessFrame — restores default framebuffer.
    void EndFrame();
    AVFrame* Capture(irr::video::IVideoDriver* driver);
    void SavePNG(AVFrame* frame, const std::string& path);
    void ReleaseFrame(AVFrame* frame);

private:
    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
    bool size_warned_ = false;
    AVFrame* bgra_frame_ = nullptr;

    // FBO render target — when non-null, EDOPro renders into this instead of the window.
    // Allows native off-screen rendering at resolutions larger than the desktop.
    irr::video::IRenderTarget* fbo_target_ = nullptr;
    irr::video::ITexture*      fbo_texture_ = nullptr;
    irr::video::IVideoDriver*  driver_ = nullptr;
};

} // namespace replay2video
} // namespace ygo

#endif // WITH_REPLAY2VIDEO
