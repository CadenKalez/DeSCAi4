// EARLY TEST / EXPERIMENTAL HOST — not recovered game code. See README_EARLY_TEST.md.
// The graphics seams the window tree needs, backed by a CPU buffer drawn with the recovered software engine.
#pragma once
#include <cstdint>
#include <memory>

#include "gz/Buffer.h"
#include "gz/DrawContext.h"
#include "gz/DrawContextAsBuffer.h"
#include "gz/DrawContextRenderToBuffer.h"
#include "gz/Win.h"

namespace exphost {

// An IWinBuffer over a real cGZBuffer (XRGB8888) with its own cGZDrawContext.
class HostBuffer final : public IWinBuffer {
public:
    HostBuffer(int width, int height);
    ~HostBuffer();
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override { return --refs_; }

    const cRZRect* GetBounds() const override { return buffer_->GetBufferArea(); }
    std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { return buffer_->ConvertRGBValueToNative(r, g, b); }
    void NativeToRGB(std::uint32_t native, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override { buffer_->ConvertNativeValueToRGB(native, r, g, b); }
    void* GetDrawContext() override { return asBuffer_->GetDrawContext(); }   // attaches the buffer as the context target when needed
    bool SetFadeAlpha(std::uint8_t) override { return false; }
    bool Fill(std::uint32_t color, const cRZRect& rect) override { return buffer_->Fill(color, rect, nullptr); }
    gz::IDrawTargetBuffer* AsDrawSource() override { return buffer_; }
    // a private buffer shown on its parent (cGZWin::PlotPresent without a draw target): the software blit
    bool Blt(IWinBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect) override {
        return buffer_->Blt(static_cast<HostBuffer*>(source)->buffer_, sourceRect, destRect, nullptr);
    }

    // What the game's draw system hands windows: a buffer that draws through its context (cGZDrawContextAsBuffer). Fonts
    // draw onto this one so the glyphs follow the context's origin.
    cIGZBuffer* AsBuffer() { return asBuffer_; }
    cIGZBufferDrawTarget* QueryDrawTarget() override { return asBuffer_; }
    cGZBuffer* Buffer() { return buffer_; }
    const std::uint32_t* Pixels() { return static_cast<const std::uint32_t*>(buffer_->GetColorSurfaceBits()); }
    std::uint32_t Stride() { return buffer_->GetColorSurfaceStride(); }
    int Width() const { return int(buffer_->Width()); }
    int Height() const { return int(buffer_->Height()); }

private:
    int refs_ = 1;
    cGZBuffer* buffer_ = nullptr;
    gz::cGZDrawContext* context_ = nullptr;
    gz::cGZDrawContextAsBuffer* asBuffer_ = nullptr;
};

class HostGraphics final : public IWinGraphics, public gz::IDrawContextHost, public IBufferHost {
public:
    explicit HostGraphics(int width, int height);
    ~HostGraphics();
    void Install();   // registers the three seams

    HostBuffer* Primary() { return primary_.get(); }
    std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { return primary_->MakeNativeColor(r, g, b); }
    void NativeToRGB(std::uint32_t n, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override { primary_->NativeToRGB(n, r, g, b); }
    IWinBuffer* PrimaryBuffer() override { return primary_.get(); }
    bool DrawSystemActive() override { return true; }   // windows draw through contexts with an origin offset, as in the game
    // windows with a private buffer (winflag_pbuff) get a buffer of their own size, drawn like the primary one
    IWinBuffer* DrawSystemCreateBuffer(int width, int height) override { return new HostBuffer(width, height); }
    IWinBuffer* GraphicsCreateBuffer() override { return nullptr; }

    gz::cIGZDrawEngine* CreateEngine(gz::IDrawTargetBuffer* target) override;

    bool HasGraphicsSystem() override { return true; }
    cGZBufferColorType DisplayColorType() override { return {kBufferColorXRGB8888, 32}; }
    bool QueryTextureBitCount(std::uint32_t& bits) override { bits = 32; return true; }

private:
    std::unique_ptr<HostBuffer> primary_;
};

}  // namespace exphost
