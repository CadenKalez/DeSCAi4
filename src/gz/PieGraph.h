// cGZPieGraph — a pie chart on top of cGZGraph (74 functions 0x100384614..0x1003870f4). Each series is one slice:
// the data is a vector of values (one per series, default 1) normalised to fractions, the series info carries the
// slice colour and the percentage-text colour. The flat view (0) fills an ellipse and flood-fills each slice; the 3D
// view (1) blits a pair of mask buffers (face / side) and stacks the disk `thickness` times.
//
// Layout (0x3b8 bytes): cGZGraph (0x320), +0x320 cIGZPieGraph view (iid 0x42cf2351), +0x328 view type, +0x32c show
// percentage text (default true), +0x330 values, +0x348 text decimals, +0x350 series infos, +0x368 minimum
// fraction to label, +0x370 percentage font, +0x378 its colour, +0x380 / +0x388 face / side masks, +0x390 disk
// rectangle, +0x3a0 mask bounds, +0x3b0 3D thickness.
//
// Quirks kept from the binary: InsertData with an index overwrites instead of inserting, the minimum-percentage
// test is skipped in the 3D view, and the slice separators of the flat view are redrawn black after the outline.
#pragma once
#include <vector>

#include "gz/BarGraph.h"
#include "gz/Graph.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZPieGraph = 0x42cf2351;

enum PieViewType : std::int32_t { kPieFlat = 0, kPie3D = 1 };

// Per-slice look (0x30 bytes).
struct PieGraphSeriesInfo {
    std::uint32_t color = 0;                 // +0
    std::uint32_t textColor = kAutoColor;    // +4
    cRZString name;                          // +8
};

// The drawing primitives of the pie chart that live in other (still unrecovered) modules.
class IPieDrawing {
public:
    virtual ~IPieDrawing() = default;
    virtual void FillEllipse(cIGZBuffer* buffer, int left, int top, int right, int bottom, std::uint32_t color) = 0;   // 0x1002b3eaf
    virtual void FloodFill(cIGZBuffer* buffer, int x, int y, std::uint32_t color) = 0;                                  // RZDrawFloodFill
    virtual void DrawCircle(cIGZBuffer* buffer, int left, int top, int right, int bottom, std::uint32_t color, std::uint32_t thickness) = 0;   // RZDrawDrawCircle
    virtual void SaturationMaskBlt(cIGZBuffer* dest, cIGZBuffer* mask, const cRZRect& source, const cRZPoint& at) = 0;   // 0x100386c71
};
IPieDrawing* PieDrawing();
void SetPieDrawing(IPieDrawing* drawing);

// cIGZPieGraph (iid 0x42cf2351): slot n at 8n.
class cIGZPieGraph : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                                       // [3]
    virtual bool Shutdown() = 0;                                                                   // [4]
    virtual void SetViewType(PieViewType type) = 0;                                                // [5]
    virtual void Set3DViewMaskBuffers(cIGZBuffer* face, cIGZBuffer* side) = 0;                     // [6]
    virtual void GetLineFromCenterOfDiskToEdge(float fraction, cRZPoint& center, cRZPoint& edge) = 0;   // [7]
    virtual void GetPositionForPercentageText(std::uint32_t series, cRZPoint& out) = 0;            // [8]
    virtual void GetPositionForPercentageText(float start, float end, cRZPoint& out) = 0;          // [9]
    virtual void MakeNormalizedDataArray(std::vector<float>& in, std::vector<float>& out) = 0;     // [10]
    virtual void CompleteRedraw(cIGZBuffer* buffer) = 0;                                           // [11]
    virtual void DrawGraphData(cIGZBuffer* buffer) = 0;                                            // [12]
    virtual void SetGraphArea(const cRZRect& area) = 0;                                            // [13]
    virtual void SetPercentageTextDecimalPlaces(std::uint32_t places) = 0;                         // [14]
    virtual void SetPercentageTextFont(cIGZFont* font, std::uint32_t color) = 0;                   // [15]
    virtual void SetMinPercentageToDisplay(float percentage) = 0;                                  // [16]
    virtual void SetSeriesCount(std::uint32_t count) = 0;                                          // [17]
    virtual void InsertSeries(std::uint32_t index) = 0;                                            // [18]
    virtual void RemoveSeries(std::uint32_t index) = 0;                                            // [19]
    virtual void RemoveAllSeries() = 0;                                                            // [20]
    virtual void SetSeriesColor(std::uint32_t color, std::uint32_t series, std::uint32_t textColor) = 0;   // [21]
    virtual void SetSeriesName(const cRZString& name, std::uint32_t series) = 0;                   // [22]
    virtual void SetSeriesInfo(PieGraphSeriesInfo& info, std::uint32_t series) = 0;                // [23]
    virtual void InsertSeriesInfo(PieGraphSeriesInfo& info, std::uint32_t series) = 0;             // [24]
    virtual void RemoveSeriesInfo(std::uint32_t series) = 0;                                       // [25]
    virtual void InsertData(float value, std::uint32_t index) = 0;                                 // [26]
    virtual void RemoveData(std::uint32_t index) = 0;                                              // [27]
    virtual void SetData(float value, std::uint32_t index) = 0;                                    // [28]
    virtual cIGZWin* AsIGZWin() = 0;                                                               // [29]
    virtual cIGZGraph* AsIGZGraph() = 0;                                                           // [30]
};

class cGZPieGraph : public cGZGraph, public cIGZPieGraph {
public:
    explicit cGZPieGraph(cIGZWinMgr* mgr);
    virtual ~cGZPieGraph();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    bool Init() override { return cGZGraph::Init(); }
    bool Shutdown() override;

    void SetViewType(PieViewType type) override { viewType_ = type; }
    void Set3DViewMaskBuffers(cIGZBuffer* face, cIGZBuffer* side) override;
    void GetLineFromCenterOfDiskToEdge(float fraction, cRZPoint& center, cRZPoint& edge) override;
    void GetPositionForPercentageText(std::uint32_t series, cRZPoint& out) override;
    void GetPositionForPercentageText(float start, float end, cRZPoint& out) override;
    void MakeNormalizedDataArray(std::vector<float>& in, std::vector<float>& out) override;
    void CompleteRedraw(cIGZBuffer* buffer) override { cGZGraph::CompleteRedraw(buffer); }
    void DrawGraphData(cIGZBuffer* buffer) override;
    void SetGraphArea(const cRZRect& area) override;
    void SetPercentageTextDecimalPlaces(std::uint32_t places) override { decimals_ = places < 9 ? places : 9; }
    void SetPercentageTextFont(cIGZFont* font, std::uint32_t color) override;
    void SetMinPercentageToDisplay(float percentage) override;
    void SetSeriesCount(std::uint32_t count) override;
    void InsertSeries(std::uint32_t index) override;
    void RemoveSeries(std::uint32_t index) override;
    void RemoveAllSeries() override;
    void SetSeriesColor(std::uint32_t color, std::uint32_t series, std::uint32_t textColor) override;
    void SetSeriesName(const cRZString& name, std::uint32_t series) override;
    void SetSeriesInfo(PieGraphSeriesInfo& info, std::uint32_t series) override;
    void InsertSeriesInfo(PieGraphSeriesInfo& info, std::uint32_t series) override;
    void RemoveSeriesInfo(std::uint32_t series) override;
    void InsertData(float value, std::uint32_t index) override;
    void RemoveData(std::uint32_t index) override;
    void SetData(float value, std::uint32_t index) override;
    void LoadDefaultFonts() override;
    cIGZWin* AsIGZWin() override { return this; }
    cIGZGraph* AsIGZGraph() override { return static_cast<cGZGraph*>(this); }

    // for tests
    std::size_t SeriesCount() const { return series_.size(); }
    const std::vector<float>& Values() const { return values_; }
    const PieGraphSeriesInfo& SeriesInfo(std::size_t index) const { return series_[index]; }
    cIGZFont* PercentageFont() const { return percentageFont_; }
    float MinFraction() const { return minFraction_; }
    std::uint32_t Decimals() const { return decimals_; }
    int Thickness() const { return thickness_; }
    cRZRect Disk() const { return disk_; }
    cRZRect MaskBounds() const { return maskBounds_; }
    PieViewType ViewType() const { return static_cast<PieViewType>(viewType_); }
    void SetShowPercentages(bool show) { showPercentages_ = show; }

private:
    std::uint32_t SingleIndex(std::uint32_t index) const { return (values_.size() == 1 && index == 0xffffffffu) ? 0 : index; }
    void DrawPercentage(cIGZBuffer* buffer, std::size_t slice, float start, float fraction, bool checkMinimum,
                        const std::vector<float>& normalized);

    std::int32_t viewType_ = kPieFlat;             // +0x328
    bool showPercentages_ = true;                  // +0x32c
    std::vector<float> values_;                    // +0x330
    std::uint32_t decimals_ = 0;                   // +0x348
    std::vector<PieGraphSeriesInfo> series_;       // +0x350
    float minFraction_ = 0.0f;                     // +0x368
    cIGZFont* percentageFont_ = nullptr;           // +0x370
    std::uint32_t percentageColor_ = kAutoColor;   // +0x378
    cIGZBuffer* faceMask_ = nullptr;               // +0x380
    cIGZBuffer* sideMask_ = nullptr;               // +0x388
    cRZRect disk_{kAutoRect, kAutoRect, INT32_MIN, INT32_MIN};   // +0x390
    cRZRect maskBounds_{0, 0, 0, 0};               // +0x3a0
    std::int32_t thickness_ = 0;                   // +0x3b0
};

}  // namespace gz
