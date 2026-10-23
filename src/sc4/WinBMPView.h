// cWinBMPView — the zoom / scroll bitmap viewer window (59 symbols, 41 of them functions, 0x1003a2334..0x1003ca2d0;
// the other 18 are non-virtual thunks). It shows one cIGZBuffer scaled by a zoom factor and scrolled by an origin: a
// cGZWinBMP that draws the whole image stretched into an image rectangle instead of blitting it 1:1. The viewer part is
// the cIWinBMPView interface (iid 0x841471b9, vtable 0x100aa5158, 16 slots).
//
// Layout of the original (0x1d0 bytes): the 0x180-byte cGZWinBMP, +0x180 the cIWinBMPView vtable, then
//   +0x188 zooming enabled (true)     +0x189 scrolling enabled (true)    +0x18a fit image in view (false)
//   +0x18c zoom minimum (0.25)        +0x190 zoom maximum (4.0)          +0x194 padding (never written)
//   +0x198 alignment (0 = top left, everything else = centred)          +0x19c zoom (1.0)
//   +0x1a0 image rectangle (the scaled image's rect inside the window; rewritten by CalculateAllMetrics)
//   +0x1b0 origin x                   +0x1b4 origin y
//   +0x1b8 right-button-down x        +0x1bc right-button-down y
//   +0x1c0 time of the last scroll step (ms)                            +0x1c8 minimum ms between steps (0x14)
//
// Origin semantics (CalculateAllMetrics 0x1003c83e0): with the top-left alignment the origin is how many scaled image
// pixels are scrolled off to the left/top, clamped to [0, scaled size - window size]; with any other alignment it is
// the scaled image pixel that sits at the centre of the window, clamped to [window size / 2, scaled size - that]. The
// window size comes from the cGZWin virtuals (slots 0x148 / 0x150), the image size from GetImageWidth / GetImageHeight
// (slots 0x590 / 0x598 of the primary vtable, which forward to the cIGZBuffer).
//
// Quirks kept on purpose (do not "fix"):
//  * ZoomIn / ZoomOut never look at the zooming-enabled flag; only GZOnKeyDown (0x1003c8894) does.
//  * SetZoomRangeMinMax (0x1003c8a5c) does not check that the minimum is <= the maximum, and CalculateAllMetrics
//    clamps to the minimum first, so a minimum above the maximum wins.
//  * Scroll's "one step" is image size / 32 (0x1003c8fb7 / 0x1003c8ff6), not window size / 32, and at least 1.
//  * GetBMP(cIGZBuffer**) (0x1003a2340) stores the buffer without an AddRef.
//  * CenterOnImagePixel (0x1003c8cea) refuses to centre an image that is wider than the window but shorter than it.
//  * WriteBMPToFile (0x1003c9d7e) removes the *original* path when the extension it appends changes the name, then
//    writes the extended name back into the caller's string.
//  * Shutdown and the destructor both release the one file-static Gimex factory (mpGimexFactory).
//
// Not recovered (see the to-do row registered for this class): the fit-to-view downscale inside SetBMPFromFile
// (0x1003c938c..0x1003c9c60, a two-pass separable tent filter over 16 subpixel phases) is a host seam here.
#pragma once
#include <cstdint>

#include "gz/Buffer.h"
#include "gz/Win.h"
#include "gz/WinBMP.h"
#include "rz/cRZPoint.h"

class cGZGimexFactory;

namespace sc4 {

constexpr std::uint32_t kGZIID_cIWinBMPView = 0x841471b9;
// The message the view posts to itself while the right button drags it (GZOnMouseDownR 0x1003c8982, re-posted by
// DoMessage 0x1003ca16e as long as the window keeps the capture).
constexpr std::uint32_t kWinBMPViewScrollMsg = 0xfe342d2a;

// The viewer interface (vtable 0x100aa5158 at +0x180 of cWinBMPView). Order is the original's vtable order.
class cIWinBMPView {
public:
    // Only "zero / non-zero" is observable in the recovered code: 0 scrolled the origin from the top-left corner,
    // anything else treats the origin as the image pixel at the centre of the window.
    enum Alignment : std::int32_t { kAlignTopLeft = 0, kAlignCenter = 1 };

    virtual bool SetZoom(float zoom) = 0;                                          // [0] 0x1003c8ab6
    virtual void ZoomIn(float factor) = 0;                                         // [1] 0x1003c8be0
    virtual bool ZoomOut(float factor) = 0;                                        // [2] 0x1003c8c1c
    virtual bool SetZoomRangeMinMax(float minZoom, float maxZoom) = 0;             // [3] 0x1003c8a5c
    virtual bool SetOrigin(int x, int y) = 0;                                      // [4] 0x1003c8c84
    virtual bool CenterOnImagePixel(int x, int y) = 0;                             // [5] 0x1003c8cea
    virtual void CenterOnWindowPixel(int x, int y) = 0;                            // [6] 0x1003c8e3c
    virtual bool WindowPixelToImagePixel(int windowX, int windowY, int& imageX, int& imageY) = 0;   // [7] 0x1003c8eda
    virtual bool Scroll(int dx, int dy) = 0;                                       // [8] 0x1003c8f88
    virtual void SetMinMsPerScroll(std::uint32_t ms) = 0;                          // [9] 0x1003a2334
    virtual void GetBMP(cIGZBuffer** out) = 0;                                     // [10] 0x1003a2340
    virtual void SetBMP(cIGZBuffer* buffer) = 0;                                   // [11] 0x1003c9098
    virtual bool SetBMPFromFile(const cIGZString& path) = 0;                       // [12] 0x1003c9148
    virtual void FreeMemory(void* memory) = 0;                                     // [13] 0x1003ca144
    virtual int GetImageWidth() = 0;                                               // [14] 0x1003a2350
    virtual int GetImageHeight() = 0;                                              // [15] 0x1003a236c

protected:
    ~cIWinBMPView() = default;   // the interface is not refcounted and has no virtual destructor in the original
};

// What the view reaches for outside itself.
class IWinBMPViewHost {
public:
    virtual ~IWinBMPViewHost() = default;
    // The window's draw target (cGZWin::drawBuffer_, +0x98) as the GZ buffer the original painted into
    // (`*(cIGZBuffer**)(this + 0x98)` in GZPaint). The port's windows hold the smaller IWinBuffer seam in that field,
    // so a host supplies the matching cIGZBuffer; null means "do not paint".
    virtual cIGZBuffer* DrawBuffer(cGZWin& window) = 0;
    // One cGZGimexFactory (COM class 0xa2ffb5d3, interface 0x02ffbb60) owned by the view: created by Init
    // (0x1003c83ba) when the file-static factory is null, released by Shutdown / the destructor.
    virtual cGZGimexFactory* CreateGimexFactory() = 0;
    // The fit-to-view downscale of SetBMPFromFile (original body 0x1003c938c..0x1003c9c60): a window-sized ARGB8888
    // buffer from the graphic system (service 0xc416025c / 0x73283c, vslot 0x20; Init vslot 0x28 with colour type 9)
    // filled by a two-pass separable tent filter (16 subpixel phases, 1/16384 weights, 4/scale support). Not recovered:
    // a host may return the scaled buffer (a new reference), or null to keep the image unscaled.
    virtual cIGZBuffer* DownscaleImageToFit(cIGZBuffer* image, int width, int height) {
        (void)image; (void)width; (void)height; return nullptr;
    }
};
IWinBMPViewHost* WinBMPViewHost();
void SetWinBMPViewHost(IWinBMPViewHost* host);

// cRZCallbackTimer::GetCurrentSystemMillisecondTime (0x1006375f6), the clock the drag repeat compares against.
using WinBMPViewClockFn = std::uint64_t (*)();
void SetWinBMPViewClock(WinBMPViewClockFn clock);

class cWinBMPView : public gz::cGZWinBMP, public cIWinBMPView {
public:
    explicit cWinBMPView(cIGZWinMgr* mgr);      // @ 0x1003c8230
    ~cWinBMPView() override;                    // @ 0x1003c82d8 (+ 0x1003c8354 / 0x1003c835e)
    using cGZWinBMP::GetBMP;                    // the 1-argument GetBMP below would hide the (iid, out) one

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1003ca280 (+ thunk 0x1003ca2a4)

    bool Init() override;                       // @ 0x1003c838c
    bool Shutdown() override;                   // @ 0x1003c8678
    bool DoMessage(cGZMessage& message) override;   // @ 0x1003ca16e
    bool GZPaint() override;                    // @ 0x1003c86ac
    void SetArea(int l, int t, int r, int b) override;   // @ 0x1003c8878
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t flags) override;     // @ 0x1003c8894
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t flags) override;       // @ 0x1003c8960
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;       // @ 0x1003c8968
    bool GZOnMouseDownR(int x, int y, std::uint32_t flags) override;       // @ 0x1003c8982
    bool GZOnMouseUpR(int x, int y, std::uint32_t flags) override;         // @ 0x1003c89d4
    bool GZOnMouseMove(int x, int y, std::uint32_t flags) override;        // @ 0x1003c8a14
    bool GZOnMouseWheel(int x, int y, std::uint32_t flags, int delta) override;   // @ 0x1003c8a1c

    // --- cIWinBMPView (vtable 0x100aa5158) ---
    bool SetZoom(float zoom) override;                     // [0] 0x1003c8ab6
    void ZoomIn(float factor) override;                    // [1] 0x1003c8be0
    bool ZoomOut(float factor) override;                   // [2] 0x1003c8c1c
    bool SetZoomRangeMinMax(float minZoom, float maxZoom) override;   // [3] 0x1003c8a5c
    bool SetOrigin(int x, int y) override;                 // [4] 0x1003c8c84
    bool CenterOnImagePixel(int x, int y) override;        // [5] 0x1003c8cea
    void CenterOnWindowPixel(int x, int y) override;       // [6] 0x1003c8e3c
    bool WindowPixelToImagePixel(int windowX, int windowY, int& imageX, int& imageY) override;   // [7] 0x1003c8eda
    bool Scroll(int dx, int dy) override;                  // [8] 0x1003c8f88
    void SetMinMsPerScroll(std::uint32_t ms) override { minMsPerScroll_ = ms; }   // [9] 0x1003a2334
    void GetBMP(cIGZBuffer** out) override { *out = bmp_; }   // [10] 0x1003a2340 (no AddRef, as in the original)
    void SetBMP(cIGZBuffer* buffer) override;              // [11] 0x1003c9098
    bool SetBMPFromFile(const cIGZString& path) override;  // [12] 0x1003c9148
    void FreeMemory(void* memory) override;                // [13] 0x1003ca144
    int GetImageWidth() override;                          // [14] 0x1003a2350
    int GetImageHeight() override;                         // [15] 0x1003a236c

    // --- declared on the class itself in the original (primary vtable slots 0x508..0x578) ---
    virtual void SetAlignment(Alignment alignment);        // @ 0x1003c8a24 (primary slot 0x508)
    virtual void SetShouldFitImageInView(bool fit);        // @ 0x1003c884e (0x530)
    virtual void EnableZooming(bool enable);               // @ 0x1003c885c (0x538)
    virtual void EnableScrolling(bool enable);             // @ 0x1003c886a (0x570)
    virtual void ClearBMP();                               // @ 0x1003c9084 (0x578)

    // Static helpers (the original has them as members; 0x1003c9ce4 is the file loader used by SetBMPFromFile).
    static bool SetBMPFromFile(cIGZBuffer** out, const cIGZString& path);   // @ 0x1003c9ce4
    // format 0/2 = .jpg, 1 = .png, 3 = .gif, 4 = .bmp; the original's declared signature also has a 4th and 5th
    // parameter (unsigned int, cRZString const&) that the body never reads. Flags 100 go to the Gimex writer.
    static bool WriteBMPToFile(cIGZBuffer* buffer, std::uint32_t format, const cIGZString& path);   // @ 0x1003c9d7e

    void CalculateAllMetrics();                            // @ 0x1003c83e0

    // test access
    float Zoom() const { return zoom_; }
    float ZoomMin() const { return zoomMin_; }
    float ZoomMax() const { return zoomMax_; }
    Alignment GetAlignment() const { return alignment_; }
    int OriginX() const { return originX_; }
    int OriginY() const { return originY_; }
    const cRZRect& ImageDest() const { return imageDest_; }
    bool ZoomingEnabled() const { return zoomingEnabled_; }
    bool ScrollingEnabled() const { return scrollingEnabled_; }
    bool ShouldFitImageInView() const { return fitImageInView_; }
    std::uint32_t MinMsPerScroll() const { return minMsPerScroll_; }
    void SetLastScrollTime(std::uint64_t time) { lastScrollMs_ = time; }
    std::uint64_t LastScrollTime() const { return lastScrollMs_; }
    int AnchorX() const { return anchorX_; }
    int AnchorY() const { return anchorY_; }

private:
    bool zoomingEnabled_ = true;       // +0x188
    bool scrollingEnabled_ = true;     // +0x189
    bool fitImageInView_ = false;      // +0x18a
    float zoomMin_ = 0.25f;            // +0x18c
    float zoomMax_ = 4.0f;             // +0x190
    Alignment alignment_ = kAlignTopLeft;   // +0x198 (the 4 bytes at +0x194 stay unused)
    float zoom_ = 1.0f;                // +0x19c
    cRZRect imageDest_;                // +0x1a0
    int originX_ = 0;                  // +0x1b0
    int originY_ = 0;                  // +0x1b4
    int anchorX_ = 0;                  // +0x1b8
    int anchorY_ = 0;                  // +0x1bc
    std::uint64_t lastScrollMs_ = 0;   // +0x1c0
    std::uint32_t minMsPerScroll_ = 0x14;   // +0x1c8
};

}  // namespace sc4
