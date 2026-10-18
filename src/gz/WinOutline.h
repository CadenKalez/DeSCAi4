// cGZWinOutline — a window that draws an outline (flat, raised or sunken edges, or the edges of an arbitrary polygon) and
// may fill its shape (0x1004dc8e6..0x100516200, 29 functions, iid 0xe303e5ae). The object lays out as the cIGZWinOutline
// interface first and the 0x150-byte cGZWin sub-object at +8.
//
// Flags (0x158): 1 = edges both dark ("flat" outline), 2 = dark top-left / light bottom-right, 4 = light top-left / dark
// bottom-right, 0x10 = polygon mode (set by SetShapePoly), 0x20 = fill the shape. Colours: 0 = fill, 1 = dark edge,
// 2 = light edge (stored as 0xffRRGGBB). In polygon mode the vertices are rotated so the top-most, left-most one comes
// first, the shape is scan-converted into per-row lists of x crossings (even-odd rule; a vertex counts once, horizontal
// edges only at local extrema) and edges are drawn light when they run left-to-right / upwards.
#pragma once
#include <cstdint>
#include <list>
#include <vector>

#include "gz/Win.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinOutline = 0xe303e5ae;
constexpr std::uint32_t kOutlineFlat = 1, kOutlineSunken = 2, kOutlineRaised = 4, kOutlinePolygon = 0x10, kOutlineFilled = 0x20;

class cIGZWinOutline : public cIGZUnknown {
public:
    virtual void SetFlags(std::uint32_t flags) = 0;                                                  // [3]
    virtual std::uint32_t GetFlags() = 0;                                                            // [4]
    virtual void SetColor(int which, std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;            // [5]
    virtual void GetColor(int which, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) = 0;         // [6]
    virtual std::uint32_t GetFillColor() = 0;                                                        // [7]
    virtual void SetFillColor(std::uint32_t native) = 0;                                             // [8]
    virtual void SetDarkOutlineColor(std::uint32_t native) = 0;                                      // [9]
    virtual std::uint32_t GetDarkOutlineColor() = 0;                                                 // [10]
    virtual std::uint32_t GetLightOutlineColor() = 0;                                                // [11]
    virtual void SetLightOutlineColor(std::uint32_t native) = 0;                                     // [12]
    virtual bool SetShapePoly(int count, const cRZPoint* points, bool resize, bool resizeToBounds) = 0;   // [13]
    virtual void ResizeToFitVertexBounds(bool keepLarger) = 0;                                       // [14]
    virtual void GetVertexBounds(cRZRect& out) const = 0;                                            // [15]
    virtual int GetPolyCount() const = 0;                                                            // [16]
    virtual bool GetShapePoly(cRZPoint* out) const = 0;                                              // [17]
    virtual cIGZWin* AsIGZWin() = 0;                                                                 // [18]
};

class cGZWinOutline : public cIGZWinOutline, public cGZWin {
public:
    explicit cGZWinOutline(cIGZWinMgr* mgr);                       // @ 0x1005150e0
    ~cGZWinOutline() override = default;                           // @ 0x100515266
    static cGZWin* CreateNewObject(cIGZWinMgr* mgr);               // @ 0x1004dc8e6

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x100515378
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }   // @ 0x1005153c0
    std::uint32_t Release() override { return cGZWin::Release(); } // @ 0x1005153d8
    bool DoMessage(cGZMessage& message) override;                  // @ 0x1005159ca
    bool GZPaint() override;                                       // @ 0x100515a54
    void SetArea(int l, int t, int r, int b) override;             // @ 0x100515db0
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    using cGZWin::SetFillColor;
    using cGZWin::GetFillColor;

    void SetFlags(std::uint32_t flags) override { flags_outline_ = flags; }      // @ 0x1005160b2
    std::uint32_t GetFlags() override { return flags_outline_; }                 // @ 0x1005160be
    void SetColor(int which, std::uint8_t r, std::uint8_t g, std::uint8_t b) override;   // @ 0x100515e8c
    void GetColor(int which, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override;   // @ 0x100515ef4
    std::uint32_t GetFillColor() override { return cGZWin::GetFillColor(); }     // @ 0x100515f4c
    void SetFillColor(std::uint32_t native) override { cGZWin::SetFillColor(native); }   // @ 0x100515f5a
    void SetDarkOutlineColor(std::uint32_t native) override;                     // @ 0x100515f72
    std::uint32_t GetDarkOutlineColor() override;                                // @ 0x100516046
    std::uint32_t GetLightOutlineColor() override;                               // @ 0x10051607c
    void SetLightOutlineColor(std::uint32_t native) override;                    // @ 0x100515fdc
    bool SetShapePoly(int count, const cRZPoint* points, bool resize, bool resizeToBounds) override;   // @ 0x1005153f0
    void ResizeToFitVertexBounds(bool keepLarger) override;                      // @ 0x10051587a
    void GetVertexBounds(cRZRect& out) const override { out = bounds_; }         // @ 0x100515928
    int GetPolyCount() const override { return (flags_outline_ & kOutlinePolygon) ? count_ : 4; }   // @ 0x100515944
    bool GetShapePoly(cRZPoint* out) const override;                             // @ 0x10051595e
    cIGZWin* AsIGZWin() override { return this; }                                // @ 0x1005160ca
    void BuildScanlines();                                                       // @ 0x100515626
    bool ScanlinesValid() const { return scanlinesValid_; }
    const std::vector<std::list<int>>& Scanlines() const { return scanlines_; }

private:
    std::uint32_t flags_outline_ = 1;      // +0x158
    std::uint32_t darkColor_ = 0, lightColor_ = 0xffffffffu;   // +0x15c / +0x160
    int count_ = 0;                        // +0x164
    std::vector<cRZPoint> points_;         // +0x168
    bool scanlinesValid_ = false;          // +0x170
    cRZRect bounds_{};                     // +0x174
    std::vector<std::list<int>> scanlines_;   // +0x188
};

}  // namespace gz
