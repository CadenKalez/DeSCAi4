// Two small decorative windows of the city UI: cSC4WinAuraBar (21 functions at 0x1005c4194..0x1005c45fc, view iid
// 0x4a5d1208) and cSC4WinAlertBorder (19 functions at 0x1005c3d04..0x1005c4126, view iid 0xca5d3290). Both are a cGZWin plus an
// interface view at +0x160 that sets the image they draw; the queries answer the interface for iid 1 (not the window).
//
// Aura bar: a value in [0, 1] (default 0.5). Without an image it paints the fill colour over the left `value` part of its
// area (rounded to the pixel); with an image it paints a one-pixel-high strip of the image, picked at the row
// `value` of the image height and centred on the window's width, repeated down the window.
// Alert border: draws the image as a nine-slice frame (tiles are a third of the image, the centre is left empty) when
// the render properties say alert borders are on; setting an image makes magenta its colour key.
#pragma once
#include "gz/Buffer.h"
#include "gz/RZDraw.h"
#include "gz/Win.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4WinAuraBar = 0x4a5d1208, kGZIID_cISC4WinAlertBorder = 0xca5d3290;

class cISC4WinAuraBar : public cIGZUnknown {
public:
    virtual void SetImage(cIGZBuffer* image) = 0;     // [3]
    virtual void SetValue(double value) = 0;          // [4]
    virtual double GetValue() = 0;                    // [5]
};
class cISC4WinAlertBorder : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                  // [3]
    virtual void SetImage(cIGZBuffer* image) = 0;     // [4]
};

// SL::spRenderProperties: the alert border asks whether borders are switched on (flag at +0x8b8 of its state record).
class IRenderProperties : public cIGZUnknown {
public:
    virtual bool AlertBordersEnabled() = 0;
};
IRenderProperties* WinRenderProperties();
void SetWinRenderProperties(IRenderProperties* properties);

class cSC4WinAuraBar : public cGZWin, public cISC4WinAuraBar {
public:
    explicit cSC4WinAuraBar(cIGZWinMgr* mgr) : cGZWin(mgr) {}           // @ 0x1005c4194
    ~cSC4WinAuraBar() override;                                           // @ 0x1005c422c

    bool QueryInterface(std::uint32_t iid, void** out) override;         // @ 0x1005c436a
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }         // @ 0x1005c4316
    std::uint32_t Release() override { return cGZWin::Release(); }       // @ 0x1005c4340
    bool GZPaint() override;                                              // @ 0x1005c43b2
    void SetImage(cIGZBuffer* image) override;                            // @ 0x1005c4516
    void SetValue(double value) override;                                 // @ 0x1005c457e
    double GetValue() override { return value_; }                         // @ 0x1005c45ee

private:
    double value_ = 0.5;                     // +0x168
    cIGZBuffer* image_ = nullptr;            // +0x170
};

class cSC4WinAlertBorder : public cGZWin, public cISC4WinAlertBorder {
public:
    explicit cSC4WinAlertBorder(cIGZWinMgr* mgr);                        // @ 0x1005c3d04
    ~cSC4WinAlertBorder() override;                                      // @ 0x1005c3dfc

    bool QueryInterface(std::uint32_t iid, void** out) override;         // @ 0x1005c3f64
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }         // @ 0x1005c3f10
    std::uint32_t Release() override { return cGZWin::Release(); }       // @ 0x1005c3f3a
    bool GZPaint() override;                                              // @ 0x1005c3fac
    cIGZWin* AsIGZWin() override { return this; }                         // @ 0x1005c411c
    void SetImage(cIGZBuffer* image) override;                            // @ 0x1005c4078

private:
    cIGZBuffer* image_ = nullptr;            // +0x168
    IRenderProperties* properties_ = nullptr;   // +0x170 (a reference)
};

}  // namespace sc4
