// IBackingStore — the Metal driver service that replaces cSC43DRender's raw OpenGL framebuffer object (the "backing store" of
// the city view: a colour + depth copy of the last fully drawn static scene). Rectangles are in pixels with the origin at the
// top-left of the frame; source and destination blocks have the same size. Colour and depth are both copied, unfiltered.
// The frame is the persistent render target of the driver (contents survive between frames).
#pragma once
#include <cstdint>

namespace gd {

class IBackingStore {
public:
    virtual ~IBackingStore() = default;
    virtual bool Create(std::uint32_t width, std::uint32_t height) = 0;   // false when the storage cannot be made
    virtual void Destroy() = 0;
    virtual bool Exists() const = 0;
    // frame -> store
    virtual void SaveFromFrame(int srcX, int srcY, int width, int height, int dstX, int dstY) = 0;
    // store -> frame
    virtual void RestoreToFrame(int srcX, int srcY, int width, int height, int dstX, int dstY) = 0;
};

}  // namespace gd
