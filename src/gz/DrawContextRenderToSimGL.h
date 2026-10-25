// cGZDrawContextRenderToSimGL — the hardware half of the UI draw context (class vtable 0x100af02e0, 0x1007b38ec..0x1007b4e58). In the
// original it derives from cGZDrawContextRenderToHardware and implements its ten pure virtuals (HWInit, HWShutdown,
// HWUpdateState, HWSetViewport, HWSetTransform, HWSetImage, HWGetImageFragments, HWSetTexture, HWDrawTriangleList,
// HWDrawQuadList) plus Suspend/Resume/Draw on top of the engine-level driver cIGZGDriver. This port keeps the engine as it is and
// installs this class through the IHardwareRenderHost seam, so it is driven by whichever cIGZGDriver the graphic system holds -
// the native Metal driver (render/gd/MetalGDriver) in the shipping build. Nothing here is translated or emulated: it is the
// original's sequence of driver calls.
//
// What the original does that is not here:
//   * Draw() has a second path for op 3 (a blit) that hands the source's pixels to the driver's StretchBlt family. It is only
//     taken when the driver's video-mode record reports a blit capability (byte +0x2e of sGDMode, copied to +0x11c in HWInit);
//     the Metal driver reports none, so the engine's vertex path (which this class implements) is the one that runs - exactly
//     what the original does with a driver that cannot blit.
//
// Layout of the original after cGZDrawContextRenderToHardware (0xf0): +0xf0 buffer cache, +0xf8 tile source, +0x100 driver,
// +0x108 draw system (cRZSysServPtr), +0x114 float alpha threshold (0.5), +0x118 first update pending, +0x119 driver state
// modified, +0x11a translucency allowed (1), +0x11b something drawn, +0x11c blit capable.
#pragma once
#include <cstdint>

#include "gz/BufferSimGLCache.h"
#include "gz/DrawContextRenderToHardware.h"

// The graphics-system and draw-system services the class reaches through RZ::GS() (interface 0xc47b747c) and its cRZSysServPtr.
class ISimGLDrawServices : public IGLCacheHost {
public:
    // GS group 0xc47b747c, slot 12 (+0x60): the draw context has changed driver state; the system resets what it caches about it.
    virtual void DriverStateChanged() = 0;
    // slot 16 (+0x80): something was drawn through the driver; the screen needs a flush.
    virtual void ScreenDirty() = 0;
    // slot 19 (+0x98) / slot 20 (+0xa0): false when the system forces translucency off (SetForceNoTranslucency), in which case
    // alpha blending is replaced by an alpha test against this threshold.
    virtual bool TranslucencyAllowed() = 0;
    virtual float TranslucencyThreshold() = 0;
    // draw system (service 0xeb2ea3cb) slot 0x50: GetBufferCacheClock.
    virtual std::uint32_t BufferCacheClock() = 0;
};

class cGZDrawContextRenderToSimGL final : public IHardwareRenderHost {
public:
    // `engine` is the cGZDrawContextRenderToHardware this class is the hardware half of (its blit source and blend flags are
    // read in HWUpdateState; the original reads them from itself).
    cGZDrawContextRenderToSimGL(cGZDrawContextRenderToHardware* engine, ISimGLDrawServices* services);   // 0x1007b38ec
    ~cGZDrawContextRenderToSimGL() override;                                                           // 0x1007b39c4

    bool Init() override;                                          // HWInit          0x1007b4588
    void Shutdown() override;                                      // HWShutdown      0x1007b466a
    void UpdateState() override;                                   // HWUpdateState   0x1007b4724
    void SetViewport(const cRZRect& viewport) override;            // HWSetViewport   0x1007b4950
    void SetTransform(const float* matrix) override;               // HWSetTransform  0x1007b498a
    bool SetImage(cIGZBuffer* image) override;                     // HWSetImage      0x1007b49ce
    void GetImageFragments(std::vector<cGZDrawImageTexture>& textures,
                           std::vector<cGZDrawImageFragment>& fragments) override;   // 0x1007b4b6c
    void SetTexture(std::uint32_t texture, bool scaled, bool tiled) override;        // HWSetTexture 0x1007b4cf6
    void DrawTriangleList(cGZDrawVertex* vertices, int count) override;              // 0x1007b4de2
    void DrawQuadList(cGZDrawVertex* vertices, int count) override;                  // 0x1007b4e58
    void Suspend() override;                                       // 0x1007b3abe (after the engine's own)
    void Resume() override;                                        // 0x1007b3b5e

    // --- inspection (tests) ---
    bool DriverStateModified() const { return modified_; }
    bool SomethingDrawn() const { return drawn_; }
    bool TranslucencyOn() const { return translucency_; }
    float AlphaThreshold() const { return threshold_; }

private:
    void ResetDriverStates();   // the shared sequence of Resume and the first UpdateState

    cGZDrawContextRenderToHardware* engine_;
    ISimGLDrawServices* services_;
    gd::cIGZGDriver* driver_ = nullptr;             // +0x100 (counted reference)
    cIGZBufferHardwareCache* cache_ = nullptr;      // +0xf0
    cIGZBufferTileSource* tiles_ = nullptr;         // +0xf8
    float threshold_ = 0.5f;                        // +0x114
    bool firstUpdate_ = false;                      // +0x118
    bool modified_ = false;                         // +0x119
    bool translucency_ = true;                      // +0x11a
    bool drawn_ = false;                            // +0x11b
};
