// cSC4WinTrendBar — the little trend gauge on the city toolbar (19 functions at 0x1004653c0..0x100465d38, object 0x1b8+ bytes).
// It shows one history-warehouse series as a vertical bar made of a one-pixel-row strip of a fill image (picked by the
// current value) with a marker image beside it that grows with the speed of change. The series value goes through a
// response curve into [0, 1]; the shown value glides towards the target, one hundredth of the change per day tick.
//
// Object layout (the original's view order; the port puts the cGZWin first):
//   +0x000 view cISC4WinTrendBar (iid 0xca5c2f84, also answered for iid 1)   +0x008 cGZWin (iid 0x22ba0121)
//   +0x158 view cIGZMessageTarget2 (iid 0x452294aa)    +0x160 u32 series id   +0x164 float current (0.5 after SetDataSource)
//   +0x168 float target   +0x16c float delta (target - current)   +0x170 cIGZBuffer* fill image   +0x178 cIGZBuffer* marker image
//   +0x180 cSC4ResponseCurve curve
//
// Messages: the history warehouse's store message 0x89efa536 {data1 = series, data2 = value} for the shown series sets the
// target (UpdateTrend); the day tick 0x66956814 glides the current value (InterpolateValue(0.01)).
//
// Painting (fill image F, marker image M; M is six equal columns: three "falling" ones left, three "rising" ones right):
//   rise level r = 3 / 2 / 1 / 0 for delta > 0.02 / > 0.01 / > 0.00001 / otherwise, fall level f = 3 / 2 / 1 / 0 for
//   delta < -0.02 / -0.01 / -0.00001 / otherwise (NaN gives 0 for both);
//   the window's buffer is locked with 0x8010; with no lock nothing is drawn but the paint still answers true;
//   a fall level draws the left f columns' worth of M ending at the centre; a rise level draws r columns from column 3 on,
//   ending at the right edge; both are centred vertically; then one row of F (row (F.height-1) - int(current*(F.height-1)+0.5))
//   is tiled over the window's whole height, centred horizontally; without both images nothing is drawn and the paint answers false.
//
// Quirks kept:
//  * SetArtwork makes magenta (255, 0, 255) the colour key of the marker image only.
//  * DoMessage builds the simulator date for a store message and then ignores it; it always answers false.
//  * A paint with a fall level reads the marker's width/height and the fill image's width several times, as the original does.
#pragma once
#include <cstdint>

#include "gz/Win.h"
#include "gz/cGZDate.h"
#include "gz/cIGZMessage2.h"
#include "sc4/cSC4ResponseCurve.h"

class cIGZBuffer;

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4WinTrendBar = 0xca5c2f84;
constexpr std::uint32_t kTrendBarStoreMessage = 0x89efa536;   // the history warehouse's store message
constexpr std::uint32_t kTrendBarDayMessage = 0x66956814;
constexpr std::uint32_t kTrendBarLockFlags = 0x8010;
constexpr float kTrendBarGlide = 0.01f;

class cISC4WinTrendBar : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                                   // [3] 0x18
    virtual void SetArtwork(cIGZBuffer* fill, cIGZBuffer* marker) = 0;                // [4] 0x20
    virtual void SetDataSource(std::uint32_t series, float low, float high) = 0;      // [5] 0x28
    virtual void SetDataSource(std::uint32_t series, const cSC4ResponseCurve& curve) = 0;   // [6] 0x30
};

// SL::spMS2, the history warehouse (SL::spCity vslot 0x100, GetPoint vslot 0x40), the simulator's day number (vslot 0x40)
// and the two buffer blits the paint makes on the window's buffer.
class ITrendBarHost {
public:
    virtual ~ITrendBarHost() = default;
    virtual cIGZMessageServer2* MessageServer() = 0;
    virtual bool HistoryPoint(std::uint32_t series, const cIGZDate& date, float& value) = 0;
    virtual std::uint32_t SimulatorDayNumber() = 0;
    // target vslot 0xf8: Blt(source, sourceRect, destRect, no clip).
    virtual bool Blt(IWinBuffer* target, cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect) = 0;
    // RZDrawBltTiled(target, source, sourceRect, destRect, 0, 0, no clip) @ 0x1006a3062.
    virtual bool BltTiled(IWinBuffer* target, cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect) = 0;
};
void SetTrendBarHost(ITrendBarHost* host);

class cSC4WinTrendBar : public cGZWin, public cISC4WinTrendBar, public cIGZMessageTarget2 {
public:
    explicit cSC4WinTrendBar(cIGZWinMgr* mgr);                                          // @ 0x1004653c0
    ~cSC4WinTrendBar() override;                                                       // @ 0x1004654a6 / 0x10046557e

    bool QueryInterface(std::uint32_t iid, void** out) override;                       // @ 0x1004655ba
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }                       // @ 0x100465624
    std::uint32_t Release() override { return cGZWin::Release(); }                     // @ 0x10046564e
    cIGZWin* AsIGZWin() override { return this; }                                       // @ 0x100465678
    bool Init() override;                                                               // @ 0x100465682
    bool Shutdown() override;                                                           // @ 0x100465704
    void SetArtwork(cIGZBuffer* fill, cIGZBuffer* marker) override;                    // @ 0x100465786
    void SetDataSource(std::uint32_t series, float low, float high) override;          // @ 0x100465852
    void SetDataSource(std::uint32_t series, const cSC4ResponseCurve& curve) override; // @ 0x100465a4e
    bool DoMessage(cIGZMessage2* message) override;                                     // @ 0x100465b18
    bool GZPaint() override;                                                            // @ 0x100465d38

    void UpdateValue(float value);        // @ 0x1004659b6: takes the series value now, no glide
    void UpdateTrend(float value);        // @ 0x100465bda: sets the target and glides one step
    void InterpolateValue(float step);    // @ 0x100465c92

    float Current() const { return current_; }
    float Target() const { return target_; }
    float Delta() const { return delta_; }
    std::uint32_t Series() const { return series_; }

private:
    void RefreshFromHistory();
    void RepaintIfVisible();

    std::uint32_t series_ = 0;       // +0x160
    float current_ = 0;              // +0x164
    float target_ = 0;               // +0x168
    float delta_ = 0;                // +0x16c
    cIGZBuffer* fill_ = nullptr;     // +0x170 (a reference)
    cIGZBuffer* marker_ = nullptr;   // +0x178 (a reference)
    cSC4ResponseCurve curve_;        // +0x180
};

}  // namespace sc4
