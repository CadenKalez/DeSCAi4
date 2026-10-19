// cGZBuffer — the GZ software pixel surface (class id 0xc470d325, interface 0x20732180; 60 functions at
// 0x1004118bc..0x100620cec). A buffer owns a width x height block of 8/16/24/32-bit pixels (the original also
// reached DirectDraw for the display buffer through cGZScreenBuffer; the plain buffer here is pure memory), with
// an optional colour key, shade colour and hardware cache hook, a change counter and a lock count.
//
// cGZBufferColorType is {type, bitsPerPixel}: 1/3 = "display format" (resolved when the surface is created),
// 4 = XRGB1555, 5 = RGB565, 6 = ARGB1555, 7 = ARGB4444, 8 = XRGB8888, 9 = ARGB8888.
//
// Layout: +0x10 ready, +0x14 colour type, +0x1c area (0,0,w,h), +0x2c colour key, +0x30 flags (bit 1 key valid),
// +0x34 refcount, +0x38 change counter, +0x3c shade colour (0xffffffff = none), +0x40 lock count (16 bit),
// +0x42 bytes per pixel, +0x48 pixels, +0x50 stride (multiple of 4), +0x54 lock flags, +0x58 hardware cache.
#pragma once
#include <cstdint>
#include <memory>

#include "gz/DrawContext.h"
#include "gz/SoftBlit.h"
#include "gz/StreamOps.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZPoint.h"

constexpr std::uint32_t kGZCLSID_cGZBuffer = 0xc470d325;
constexpr std::uint32_t kGZIID_cIGZBuffer = 0x20732180;
constexpr std::uint32_t kGZIID_cIGZBufferShade = 0x86d72b57;

struct cGZBufferColorType {
    std::int32_t type = 3;
    std::uint32_t bitsPerPixel = 0x10;
};
enum : std::int32_t {
    kBufferColorDisplay = 1, kBufferColorDefault = 3, kBufferColorXRGB1555 = 4, kBufferColorRGB565 = 5,
    kBufferColorARGB1555 = 6, kBufferColorARGB4444 = 7, kBufferColorXRGB8888 = 8, kBufferColorARGB8888 = 9,
};

class cIGZBuffer;

// The shade colour view (iid 0x86d72b57) the windows' fade effect and RZDrawBltEdge ask a buffer for.
class cIGZBufferShade : public cIGZUnknown {
public:
    virtual bool SetShadeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) = 0;   // +0x18
    virtual bool GetShadeColor(std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a) const = 0;   // +0x20
};

// A hardware (texture) copy of a buffer.
class cIGZBufferHardwareCache : public cIGZUnknown {
public:
    virtual bool Attach(cIGZBuffer* buffer) = 0;                 // +0x18
    virtual void Slot4() {}
    virtual void Slot5() {}
    virtual void Flush() {}                                      // +0x30 (the draw system drops the texture copy)
    virtual void Slot7() {}
    virtual void Invalidate() = 0;                               // +0x40 (colour key changed)
    virtual bool IsResident() { return false; }                  // +0x48 (holds a texture copy)
    virtual void Slot10() {}
    virtual std::uint32_t GetLastUsedClock() { return 0; }       // +0x58 (draw system clock of the last use)
};

class cIGZBuffer : public gz::IDrawTargetBuffer {
public:
    enum eLockFlags : std::uint32_t { kLockRead = 1, kLockWrite = 0x8000 };

    virtual bool Init(std::uint32_t width, std::uint32_t height, cGZBufferColorType type) = 0;     // [5]
    virtual bool Shutdown() = 0;                                                                   // [6]
    virtual bool Uninitialize() = 0;                                                               // [7]
    virtual bool Lock(eLockFlags flags) = 0;                                                       // [8]
    virtual bool Unlock(eLockFlags flags) = 0;                                                     // [9]
    virtual bool IsLocked() const = 0;                                                             // [10]
    virtual std::uint32_t Width() const = 0;                                                       // [11]
    virtual std::uint32_t Height() const = 0;                                                      // [12]
    const cRZRect* GetBufferArea() const override = 0;                                             // [13]
    virtual void GetBufferArea(cRZRect& out) const = 0;                                            // [14]
    virtual std::uint32_t GetMemoryUsage() const = 0;                                              // [15]
    virtual cGZBufferColorType GetColorType() const = 0;                                           // [16]
    virtual void GetColorType(cGZBufferColorType& out) const = 0;                                  // [17]
    virtual std::uint32_t GetBitsPerPixel() const = 0;                                             // [18]
    virtual std::uint32_t GetBytesPerPixel() const = 0;                                            // [19]
    virtual bool Fill(std::uint32_t color, const cRZRect& rect, const cRZRect* clip) = 0;          // [20]
    virtual bool Fill(std::uint32_t color, int l, int t, int r, int b, const cRZRect* clip) = 0;   // [21]
    virtual bool Fill(std::uint32_t color, const cRZRect* clip) = 0;                               // [22]
    virtual std::uint32_t GetPixel(std::uint32_t x, std::uint32_t y) const = 0;                    // [23]
    virtual void SetPixel(std::uint32_t x, std::uint32_t y, std::uint32_t color) = 0;              // [24]
    virtual void SetTransparency(std::uint32_t color) = 0;                                         // [25]
    virtual bool GetTransparency() const = 0;                                                      // [26]
    virtual bool IsPixelTransparent(std::uint32_t x, std::uint32_t y) = 0;                         // [27]
    virtual bool GetTransparentColor(std::uint32_t& color) const = 0;                              // [28]
    virtual void RemoveTransparency() = 0;                                                         // [29]
    virtual bool DrawLine(int x0, int y0, int x1, int y1, int color, std::uint32_t thickness) = 0; // [30]
    virtual bool Blt(cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destination, const cRZRect* clip) = 0;   // [31] (cGZBuffer only reads the destination's top-left corner)
    std::uint32_t ConvertRGBValueToNative(std::uint8_t r, std::uint8_t g, std::uint8_t b) override = 0;     // [32]
    virtual bool ConvertNativeValueToRGB(std::uint32_t native, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) = 0;   // [33]
    virtual std::uint32_t ConvertRGBAValueToNative(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) = 0;   // [34]
    virtual bool ConvertNativeValueToRGBA(std::uint32_t native, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a) = 0;   // [35]
    virtual void* GetColorSurfaceBits() = 0;                                                       // [36]
    virtual std::uint32_t GetColorSurfaceStride() = 0;                                             // [37]
    virtual std::uint32_t GetChangeCounter() = 0;                                                  // [38]
    virtual cIGZBufferHardwareCache* GetHardwareCache() = 0;                                       // [39]
    virtual bool SetHardwareCache(cIGZBufferHardwareCache* cache) = 0;                             // [40]
    virtual bool IsReady() = 0;                                                                    // [41]
};

// Buffers that can be drawn into through a draw context (iid 0xab300b2b): the hardware path of RZDrawBltTiled /
// RZDrawBltStretch.
constexpr std::uint32_t kGZIID_cIGZBufferDrawTarget = 0xab300b2b;
class cIGZBufferDrawTarget : public cIGZUnknown {
public:
    virtual cIGZBuffer* AsIGZBuffer() = 0;                                   // +0x18
    virtual gz::cIGZDrawContext* GetDrawContext() = 0;                       // +0x20 (attaches the buffer as the target when needed)
    virtual gz::cIGZDrawContext* LockContext() = 0;                          // +0x28
    virtual void UnlockContext() = 0;                                        // +0x30
    virtual void Init(gz::cIGZDrawContext* context, cIGZBuffer* buffer) = 0;  // +0x38
    virtual void ResetSize() = 0;                                            // +0x40
    virtual void SetBlitSource(cIGZBuffer* source) = 0;                      // +0x48
};

// What DeterminePixelFormat asks the graphics system (RZ::GS()).
class IBufferHost {
public:
    virtual ~IBufferHost() = default;
    virtual bool HasGraphicsSystem() = 0;
    virtual cGZBufferColorType DisplayColorType() = 0;                       // GS vslot 0x48
    virtual bool QueryTextureBitCount(std::uint32_t& bitCount) = 0;          // interface 0xc47b747c, vslot 0x78
};
IBufferHost* BufferHost();
void SetBufferHost(IBufferHost* host);

// The BltRect blitter for a destination / source colour type pair (gTexConvVector).
using PixelBlitterFn = void (*)(std::uint32_t width, std::uint32_t height, nGZDrawSoftRender::cGZBufferSurfaceDesc dst,
                                nGZDrawSoftRender::cGZBufferSurfaceDesc src, nGZDrawSoftRender::cBltInfo info);
PixelBlitterFn GetPixelBlitter(const cGZBufferColorType& dst, const cGZBufferColorType& src);
PixelBlitterFn GetPixelBlitterColorKey(const cGZBufferColorType& dst, const cGZBufferColorType& src);

class cGZBuffer : public cIGZBuffer, public cIGZBufferShade {
public:
    cGZBuffer() = default;
    virtual ~cGZBuffer();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    bool Init(std::uint32_t width, std::uint32_t height, cGZBufferColorType type) override;
    bool Shutdown() override;
    bool Uninitialize() override;
    bool Lock(eLockFlags flags) override;
    bool Unlock(eLockFlags flags) override;
    bool IsLocked() const override { return lockCount_ != 0; }
    std::uint32_t Width() const override { return area_.right; }
    std::uint32_t Height() const override { return area_.bottom; }
    const cRZRect* GetBufferArea() const override { return &area_; }
    void GetBufferArea(cRZRect& out) const override { out = area_; }
    std::uint32_t GetMemoryUsage() const override;
    cGZBufferColorType GetColorType() const override { return colorType_; }
    void GetColorType(cGZBufferColorType& out) const override { out = colorType_; }
    std::uint32_t GetBitsPerPixel() const override { return colorType_.bitsPerPixel; }
    std::uint32_t GetBytesPerPixel() const override { return colorType_.bitsPerPixel >> 3; }
    bool Fill(std::uint32_t color, const cRZRect& rect, const cRZRect* clip) override;
    bool Fill(std::uint32_t color, int l, int t, int r, int b, const cRZRect* clip) override;
    bool Fill(std::uint32_t color, const cRZRect* clip) override;
    std::uint32_t GetPixel(std::uint32_t x, std::uint32_t y) const override;
    void SetPixel(std::uint32_t x, std::uint32_t y, std::uint32_t color) override;
    void SetTransparency(std::uint32_t color) override;
    bool GetTransparency() const override { return (flags_ & 2) != 0; }
    bool IsPixelTransparent(std::uint32_t x, std::uint32_t y) override;
    bool GetTransparentColor(std::uint32_t& color) const override;
    void RemoveTransparency() override;
    bool DrawLine(int x0, int y0, int x1, int y1, int color, std::uint32_t thickness) override;
    bool Blt(cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destination, const cRZRect* clip) override;
    std::uint32_t ConvertRGBValueToNative(std::uint8_t r, std::uint8_t g, std::uint8_t b) override;
    bool ConvertNativeValueToRGB(std::uint32_t native, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override;
    std::uint32_t ConvertRGBAValueToNative(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override;
    bool ConvertNativeValueToRGBA(std::uint32_t native, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a) override;
    void* GetColorSurfaceBits() override { return bits_.get(); }
    std::uint32_t GetColorSurfaceStride() override { return stride_; }
    std::uint32_t GetChangeCounter() override { return changeCounter_; }
    cIGZBufferHardwareCache* GetHardwareCache() override { return hardwareCache_; }
    bool SetHardwareCache(cIGZBufferHardwareCache* cache) override;
    bool IsReady() override { return ready_; }

    bool SetShadeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override;
    bool GetShadeColor(std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a) const override;

    // overridable surface management (the display buffer replaces these)
    virtual bool CreateSurfaces();
    virtual bool InitializeSurfaces();
    virtual bool DestroySurfaces();
    virtual void DeterminePixelFormat();
    virtual bool GenericBufferClip(cIGZBuffer* buffer, cRZRect& adjusted, cRZRect& clipped, const cRZRect* clip);
    virtual bool BltNoClip(cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect);
    void SetDirty() { ++changeCounter_; }

    // Cohen-Sutherland clipping of a segment to the inclusive rectangle (minX,minY)-(maxX,maxY); false when
    // the segment is entirely outside.
    static bool ClipRect(int* x0, int* y0, int* x1, int* y1, int minX, int minY, int maxX, int maxY);

protected:
    bool ready_ = false;                                // +0x10
    cGZBufferColorType colorType_;                      // +0x14
    cRZRect area_;                                      // +0x1c
    std::uint32_t transparentColor_ = 0;                // +0x2c
    std::uint8_t flags_ = 0;                            // +0x30
    std::uint32_t refCount_ = 0;                        // +0x34
    std::uint32_t changeCounter_ = 0;                   // +0x38
    std::uint32_t shadeColor_ = 0xffffffff;             // +0x3c
    std::int16_t lockCount_ = 0;                        // +0x40
    std::uint8_t bytesPerPixel_ = 0;                    // +0x42
    std::unique_ptr<std::uint8_t[]> bits_;              // +0x48
    std::uint32_t stride_ = 0;                          // +0x50
    std::uint32_t lockFlags_ = 0;                       // +0x54
    cIGZBufferHardwareCache* hardwareCache_ = nullptr;  // +0x58
};
