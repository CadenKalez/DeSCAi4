// cGZDrawContextAsBuffer — a cIGZBuffer that draws through a draw context (66 functions incl. thunks at
// 0x100652e32..0x100653c68; class interfaces: the buffer at +0, a cRZRefCount at +8, and the draw-target view at +0x18 with
// iid 0xab300b2b). It is the hardware path's way of looking like a plain buffer: fills, pixels, lines and blits become
// context operations (each wrapped in PushTransform / PopTransform and followed by a Flush), colours are converted to the
// context's ARGB32, and everything else (transparency key, change counter, hardware cache, unknown interfaces) is
// forwarded to the real buffer it was initialised with.
// Layout: +0x20 context, +0x28 buffer, +0x30 buffer area, +0x40 lock count, +0x44 colour type, +0x48 locked surface (0x20
// bytes: pixels, pitch, width, height, format, bytes per pixel), +0x68 colour key set, +0x69 "suspend again on unlock".
// Quirks kept: Lock's flag test is `(int16)flags >= 0`, which is true for a read lock and false for the write flag; GetPixel
// locks the context's surface and never unlocks it; IsPixelTransparent's mask table is indexed with the surface format
// field, which is never changed from its initial 1, so the mask is always all ones; DrawLine thickens by shifting the line
// by half a pixel and drawing it as a one-segment polyline.
#pragma once
#include <cstdint>

#include "gz/Buffer.h"
#include "rz/cRZRefCount.h"

namespace gz {

constexpr std::uint32_t kLockContextFlags = 0x8010;

class cGZDrawContextAsBuffer : public cIGZBuffer, public cRZRefCount, public cIGZBufferDrawTarget {
public:
    cGZDrawContextAsBuffer();
    ~cGZDrawContextAsBuffer() override;

    // Both base interfaces share these (and cRZRefCount's counter backs them).
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x100652fee
    std::uint32_t AddRef() override { cRZRefCount::AddRef(); return RefCount(); }         // @ 0x100652fa2
    std::uint32_t Release() override { return static_cast<std::uint32_t>(cRZRefCount::Release()); }   // @ 0x100652fc8

    // cIGZBuffer
    bool Init(std::uint32_t, std::uint32_t, cGZBufferColorType) override { return false; }   // @ 0x1006533f2
    bool Shutdown() override { return true; }                                                  // @ 0x1006533fa
    bool Uninitialize() override { return false; }                                             // @ 0x100653402
    bool Lock(eLockFlags flags) override;
    bool Unlock(eLockFlags flags) override;
    bool IsLocked() const override { return lockCount_ != 0; }
    std::uint32_t Width() const override;
    std::uint32_t Height() const override;
    const cRZRect* GetBufferArea() const override { return &area_; }
    void GetBufferArea(cRZRect& out) const override { out = area_; }
    std::uint32_t GetMemoryUsage() const override { return 0; }
    cGZBufferColorType GetColorType() const override;
    void GetColorType(cGZBufferColorType& out) const override { out = GetColorType(); }
    std::uint32_t GetBitsPerPixel() const override { return surface_.bytesPerPixel << 3; }
    std::uint32_t GetBytesPerPixel() const override { return surface_.bytesPerPixel; }
    bool Fill(std::uint32_t color, const cRZRect& rect, const cRZRect* clip) override;
    bool Fill(std::uint32_t color, int l, int t, int r, int b, const cRZRect* clip) override;
    bool Fill(std::uint32_t color, const cRZRect* clip) override;
    std::uint32_t GetPixel(std::uint32_t x, std::uint32_t y) const override;
    void SetPixel(std::uint32_t x, std::uint32_t y, std::uint32_t color) override;
    void SetTransparency(std::uint32_t color) override;
    bool GetTransparency() const override { return hasKey_; }
    bool IsPixelTransparent(std::uint32_t x, std::uint32_t y) override;
    bool GetTransparentColor(std::uint32_t& color) const override { return buffer_->GetTransparentColor(color); }
    void RemoveTransparency() override;
    bool DrawLine(int x0, int y0, int x1, int y1, int color, std::uint32_t thickness) override;
    bool Blt(cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destination, const cRZRect* clip) override;
    std::uint32_t ConvertRGBValueToNative(std::uint8_t r, std::uint8_t g, std::uint8_t b) override;
    bool ConvertNativeValueToRGB(std::uint32_t native, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override;
    std::uint32_t ConvertRGBAValueToNative(std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) override { return 0; }
    bool ConvertNativeValueToRGBA(std::uint32_t, std::uint8_t&, std::uint8_t&, std::uint8_t&, std::uint8_t&) override { return false; }
    void* GetColorSurfaceBits() override { return surface_.pixels; }
    std::uint32_t GetColorSurfaceStride() override { return static_cast<std::uint32_t>(surface_.pitch); }
    std::uint32_t GetChangeCounter() override { return buffer_->GetChangeCounter(); }
    cIGZBufferHardwareCache* GetHardwareCache() override { return buffer_->GetHardwareCache(); }
    bool SetHardwareCache(cIGZBufferHardwareCache* cache) override { return buffer_->SetHardwareCache(cache); }
    bool IsReady() override { return true; }

    // cIGZBufferDrawTarget
    cIGZBuffer* AsIGZBuffer() override { return this; }
    cIGZDrawContext* GetDrawContext() override;
    cIGZDrawContext* LockContext() override;
    void UnlockContext() override;
    void Init(cIGZDrawContext* context, cIGZBuffer* buffer) override;
    void ResetSize() override;
    void SetBlitSource(cIGZBuffer* source) override;

    std::uint32_t NativeToARGB32(std::uint32_t native) const;   // @ 0x10065323e

private:
    cIGZDrawContext* context_ = nullptr;   // +0x20
    cIGZBuffer* buffer_ = nullptr;         // +0x28
    cRZRect area_{};                       // +0x30
    int lockCount_ = 0;                    // +0x40
    int colorType_ = 0;                    // +0x44
    cGZBufferSurfaceDesc surface_{};       // +0x48
    bool hasKey_ = false;                  // +0x68
    bool suspendOnUnlock_ = false;         // +0x69
};

}  // namespace gz
