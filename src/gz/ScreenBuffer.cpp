#include "gz/ScreenBuffer.h"

bool cGZScreenBuffer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZScreenBuffer) {
        *out = static_cast<cIGZScreenBuffer*>(this);
    } else if (iid == 0x86d72b57) {
        *out = static_cast<cIGZBufferShade*>(this);
    } else if (iid == 1 || iid == kGZCLSID_cGZScreenBuffer) {
        *out = static_cast<cIGZBuffer*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cGZScreenBuffer::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

// Sets up the screen of `width` x `height`; an already initialised screen answers true, a zero size false.
bool cGZScreenBuffer::Init(std::uint32_t width, std::uint32_t height, bool, cGZBufferColorType type) {
    if (screenInitialized_) return true;
    if (ready_) return false;
    if (width == 0 || height == 0) return false;
    bufferArea_ = {0, 0, static_cast<int>(width), static_cast<int>(height)};
    appWindow_ = bufferArea_;
    area_ = {0, 0, static_cast<int>(width), static_cast<int>(height)};
    colorType_ = type;
    DeterminePixelFormat();
    if (!overlay_ && graphics_) {                      // a 4 x 4 scratch buffer filled with the overlay colour
        if (cIGZBuffer* scratch = graphics_->CreateBuffer()) {
            overlay_ = scratch;
            if (!overlay_->Init(4, 4, colorType_)) {
                overlay_ = nullptr;
            } else if (overlay_->Lock(static_cast<eLockFlags>(0x8001))) {
                overlay_->Fill(overlayColor_, nullptr);
                overlay_->Unlock(static_cast<eLockFlags>(0x8001));
            }
        }
    }
    ready_ = true;
    screenInitialized_ = true;
    if (createActiveDrawBuffer) activeDrawBuffer_ = createActiveDrawBuffer(*this);
    return true;
}

bool cGZScreenBuffer::Shutdown() {
    activeDrawBuffer_ = nullptr;
    if (overlay_) {
        overlay_->Shutdown();
        overlay_ = nullptr;
    }
    ready_ = false;
    screenInitialized_ = false;
    return true;
}

bool cGZScreenBuffer::Uninitialize() {
    if (screenInitialized_) ready_ = false;
    return screenInitialized_;
}

cIGZBuffer* cGZScreenBuffer::ActiveDrawBuffer() {
    cIGZBuffer* buffer = activeDrawBuffer_ ? activeDrawBuffer_.get() : static_cast<cIGZBuffer*>(this);
    buffer->AddRef();
    buffer->Release();                                  // (the original takes and drops a reference; no net effect)
    return buffer;
}

// Only the flags 0x1 / 0x8000 (and their combination) are lockable; a write lock marks the buffer dirty.
bool cGZScreenBuffer::Lock(eLockFlags flags) {
    if ((flags & 0xfe0) != 0) return false;
    if (static_cast<std::int16_t>(flags) < 0) SetDirty();
    lockFlags_ |= flags;
    ++lockCount_;
    return true;
}

bool cGZScreenBuffer::Unlock(eLockFlags flags) {
    if ((flags & 0xfe0) != 0) return false;
    return cGZBuffer::Unlock(flags);
}

bool cGZScreenBuffer::ModifyAppWindow(cRZRect& window) {
    appWindow_ = window;
    area_.right = window.right - window.left;
    area_.bottom = window.bottom - window.top;
    bufferArea_ = {area_.left, area_.top, area_.right, area_.bottom};
    return true;
}

bool cGZScreenBuffer::Swap() {
    bool ready = IsReady();
    if (ready && graphics_) {
        graphics_->RenderBufferToScreen(this, area_, area_);
        graphics_->LazyFlush();
    }
    return ready;
}

void cGZScreenBuffer::AdjustClientRect(cRZRect& rect) {
    rect.left += appWindow_.left;
    rect.top += appWindow_.top;
    rect.right += appWindow_.left;
    rect.bottom += appWindow_.top;
}

void cGZScreenBuffer::AdjustClientPoint(cRZPoint& point) {
    point.x += appWindow_.left;
    point.y += appWindow_.top;
}

// A software buffer blits normally; otherwise a non-empty destination is drawn by the graphic system.
bool cGZScreenBuffer::Blt(cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destination, const cRZRect* clip) {
    if (bits_) return cGZBuffer::Blt(source, sourceRect, destination, clip);
    if (destination.left < destination.right && destination.top < destination.bottom && graphics_) {
        graphics_->RenderBufferToScreen(source, sourceRect, destination);
        graphics_->LazyFlush();
    }
    return true;
}

// Clears the clipped rectangle with the driver (the viewport is set to the rectangle, bottom-left origin).
bool cGZScreenBuffer::Fill(std::uint32_t color, const cRZRect& rect, const cRZRect* clip) {
    cRZRect request{0, 0, rect.right - rect.left, rect.bottom - rect.top};
    cRZRect target{rect.left, rect.top, rect.right, rect.bottom};
    cRZRect clipped;
    if (!GenericBufferClip(this, clipped, target, clip)) return false;
    if (graphics_) {
        if (gd::cIGZGDriver* driver = graphics_->GetGDriver()) {
            std::uint32_t saved[4];
            driver->GetViewport(saved);
            driver->SetViewport(static_cast<std::uint32_t>(rect.left), static_cast<std::uint32_t>(area_.bottom - rect.bottom),
                                static_cast<std::uint32_t>(rect.right - rect.left), static_cast<std::uint32_t>(rect.bottom - rect.top));
            std::uint8_t r = 0, g = 0, b = 0;
            ConvertNativeValueToRGB(color, r, g, b);
            driver->ClearColor(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, 1.0f);
            driver->Clear(0x4000);
            driver->SetViewport(saved[0], saved[1], saved[2] - saved[0], saved[3] - saved[1]);
        }
    }
    (void)request;
    return true;
}

bool cGZScreenBuffer::Fill(std::uint32_t color, int l, int t, int r, int b, const cRZRect* clip) {
    cRZRect rect{l, t, r, b};
    return Fill(color, rect, clip);
}

bool cGZScreenBuffer::Fill(std::uint32_t color, const cRZRect* clip) { return Fill(color, *GetBufferArea(), clip); }
