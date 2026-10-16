// cGZBarGraph — a bar chart on top of cGZGraph (94 functions 0x10037f0de..0x100387046). Data is a vector of series,
// each a vector of values (one per category); bars are either clustered (one bar per series inside each
// category, BarGraphType 1) or stacked (BarGraphType 0). Orientation 0 draws vertical bars with the categories
// along the horizontal axis.
//
// Layout (0x388 bytes): cGZGraph (0x320), +0x320 cIGZBarGraph view (iid 0x22cf2351), +0x328 type, +0x32c
// orientation, +0x330 data, +0x348 series infos, +0x360 category names, +0x378 bar width, +0x37c spacing between
// bars / categories, +0x380 bar width fraction (0.75).
//
// Quirks kept from the binary: RemoveCategoryName only clears the name, InsertCategoryName overwrites; the stacked
// GetRectOfBar accumulates along the category axis of one series (DrawAllBars stacks across series); the clustered
// DrawAllBars never swaps the rectangle for horizontal bars; the clustered series-count default resets all series.
#pragma once
#include <vector>

#include "gz/Graph.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZBarGraph = 0x22cf2351;
constexpr std::uint32_t kBarHitTestResult = 0x3e9;

enum BarGraphType : std::int32_t { kBarStacked = 0, kBarClustered = 1 };
enum GraphOrientation : std::int32_t { kOrientationVertical = 0, kOrientationHorizontal = 1 };
enum BarDirection : std::int32_t { kBarUp = 0, kBarDown = 1 };

// Per-series look (0x40 bytes). The pattern pointer is stored without a reference of its own, as in the binary.
struct BarGraphSeriesInfo {
    bool outline = true;                    // +0
    std::uint32_t outlineColor = 0;         // +4
    bool fill = true;                       // +8
    std::uint32_t fillColor = 0xffffffff;   // +0xc
    cIGZBuffer* pattern = nullptr;          // +0x10
    cRZString name;                         // +0x18
};

// cIGZBarGraph (iid 0x22cf2351): slot n at 8n.
class cIGZBarGraph : public cIGZUnknown {
public:
    virtual void SetBarGraphType(BarGraphType type) = 0;                                          // [3]
    virtual void SetGraphOrientation(GraphOrientation orientation) = 0;                           // [4]
    virtual void SetSeriesCount(std::uint32_t count) = 0;                                         // [5]
    virtual void InsertSeries(std::uint32_t index) = 0;                                           // [6]
    virtual void RemoveSeries(std::uint32_t index) = 0;                                           // [7]
    virtual void SetSeriesName(const cRZString& name, std::uint32_t series) = 0;                  // [8]
    virtual void SetSeriesInfo(BarGraphSeriesInfo& info, std::uint32_t series) = 0;               // [9]
    virtual void InsertSeriesInfo(BarGraphSeriesInfo& info, std::uint32_t series) = 0;            // [10]
    virtual void RemoveSeriesInfo(std::uint32_t series) = 0;                                      // [11]
    virtual void InsertCategoryName(const cRZString& name, std::uint32_t index) = 0;              // [12]
    virtual void RemoveCategoryName(std::uint32_t index) = 0;                                     // [13]
    virtual void SetBarColorUse(Usage outline, Usage fill, std::uint32_t series) = 0;             // [14]
    virtual void SetBarFillColor(std::uint32_t color, std::uint32_t series) = 0;                  // [15]
    virtual void SetBarOutlineColor(std::uint32_t color, std::uint32_t series) = 0;               // [16]
    virtual void SetBarPattern(cIGZBuffer* pattern, std::uint32_t series) = 0;                    // [17]
    virtual void SetBarWidthPercentage(float fraction) = 0;                                       // [18]
    virtual void InsertData(float value, std::uint32_t series, std::uint32_t position) = 0;       // [19]
    virtual void RemoveData(std::uint32_t position, std::uint32_t series) = 0;                    // [20]
    virtual void SetData(float value, std::uint32_t position, std::uint32_t series) = 0;          // [21]
    virtual void SetData(std::vector<float>& values, std::uint32_t series) = 0;                   // [22]
    virtual void AppendData(std::vector<float>& values, std::uint32_t series) = 0;                // [23]
    virtual int GetLeftOfBar(std::uint32_t category, std::uint32_t series) = 0;                   // [24]
    virtual int GetRightOfBar(std::uint32_t category, std::uint32_t series) = 0;                  // [25]
    virtual int GetCenterOfBar(std::uint32_t category, std::uint32_t series) = 0;                 // [26]
    virtual bool GetRectOfBar(std::uint32_t category, std::uint32_t series, cRZRect& rect) = 0;   // [27]
    virtual bool GetRectOfBarsInCategory(std::uint32_t category, cRZRect& rect) = 0;              // [28]
    virtual bool DoHitTest(cRZPoint& point, std::uint32_t& result) = 0;                           // [29]
    virtual void CompleteRedraw(cIGZBuffer* buffer) = 0;                                          // [30]
    virtual void DrawGraphData(cIGZBuffer* buffer) = 0;                                           // [31]
    virtual void DrawAllBars(cIGZBuffer* buffer) = 0;                                             // [32]
    virtual void DrawBar(cIGZBuffer* buffer, cRZRect& rect, std::uint32_t series, BarDirection direction) = 0;   // [33]
    virtual float GetHighestAxisValue(Axis axis) = 0;                                             // [34]
    virtual float GetLowestAxisValue(Axis axis) = 0;                                              // [35]
    virtual void CalculateBarWidthAndSpacing() = 0;                                               // [36]
    virtual int CalculateMaximumBarWidth() = 0;                                                   // [37]
    virtual cIGZWin* AsIGZWin() = 0;                                                              // [38]
    virtual cIGZGraph* AsIGZGraph() = 0;                                                          // [39]
};

class cGZBarGraph : public cGZGraph, public cIGZBarGraph {
public:
    explicit cGZBarGraph(cIGZWinMgr* mgr);
    virtual ~cGZBarGraph();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    bool Init() override { return cGZGraph::Init(); }
    bool Shutdown() override { return cGZGraph::Shutdown(); }

    void SetBarGraphType(BarGraphType type) override { type_ = type; }
    void SetGraphOrientation(GraphOrientation orientation) override { orientation_ = orientation; }
    void SetSeriesCount(std::uint32_t count) override;
    void InsertSeries(std::uint32_t index) override;
    void RemoveSeries(std::uint32_t index) override;
    void SetSeriesName(const cRZString& name, std::uint32_t series) override;
    void SetSeriesInfo(BarGraphSeriesInfo& info, std::uint32_t series) override;
    void InsertSeriesInfo(BarGraphSeriesInfo& info, std::uint32_t series) override;
    void RemoveSeriesInfo(std::uint32_t series) override;
    void InsertCategoryName(const cRZString& name, std::uint32_t index) override;
    void RemoveCategoryName(std::uint32_t index) override;
    void SetBarColorUse(Usage outline, Usage fill, std::uint32_t series) override;
    void SetBarFillColor(std::uint32_t color, std::uint32_t series) override;
    void SetBarOutlineColor(std::uint32_t color, std::uint32_t series) override;
    void SetBarPattern(cIGZBuffer* pattern, std::uint32_t series) override;
    void SetBarWidthPercentage(float fraction) override;
    void InsertData(float value, std::uint32_t series, std::uint32_t position) override;
    void RemoveData(std::uint32_t position, std::uint32_t series) override;
    void SetData(float value, std::uint32_t position, std::uint32_t series) override;
    void SetData(std::vector<float>& values, std::uint32_t series) override;
    void AppendData(std::vector<float>& values, std::uint32_t series) override;
    int GetLeftOfBar(std::uint32_t category, std::uint32_t series) override;
    int GetRightOfBar(std::uint32_t category, std::uint32_t series) override;
    int GetCenterOfBar(std::uint32_t category, std::uint32_t series) override;
    bool GetRectOfBar(std::uint32_t category, std::uint32_t series, cRZRect& rect) override;
    bool GetRectOfBarsInCategory(std::uint32_t category, cRZRect& rect) override;
    bool DoHitTest(cRZPoint& point, std::uint32_t& result) override;
    void CompleteRedraw(cIGZBuffer* buffer) override;
    void DrawGraphData(cIGZBuffer* buffer) override { DrawAllBars(buffer); }
    void DrawAllBars(cIGZBuffer* buffer) override;
    void DrawBar(cIGZBuffer* buffer, cRZRect& rect, std::uint32_t series, BarDirection direction) override;
    float GetHighestAxisValue(Axis axis) override;
    float GetLowestAxisValue(Axis axis) override;
    void CalculateBarWidthAndSpacing() override;
    int CalculateMaximumBarWidth() override;
    cIGZWin* AsIGZWin() override { return this; }
    cIGZGraph* AsIGZGraph() override { return static_cast<cGZGraph*>(this); }

    void DrawTicks(cIGZBuffer* buffer, std::uint32_t axisMask) override;
    void DrawTickText(cIGZBuffer* buffer, std::uint32_t axisMask) override;

    // for tests
    float BarWidth() const { return barWidth_; }
    float BarSpacing() const { return spacing_; }
    float WidthFraction() const { return widthFraction_; }
    std::size_t SeriesCount() const { return data_.size(); }
    const std::vector<float>& Series(std::size_t index) const { return data_[index]; }
    const BarGraphSeriesInfo& SeriesInfo(std::size_t index) const { return series_[index]; }
    const std::vector<cRZString>& CategoryNames() const { return categoryNames_; }

private:
    std::uint32_t OuterIndex(std::uint32_t series) const;   // the "single series, unspecified" convention

    std::int32_t type_ = kBarStacked;                  // +0x328
    std::int32_t orientation_ = kOrientationVertical;  // +0x32c
    std::vector<std::vector<float>> data_;             // +0x330
    std::vector<BarGraphSeriesInfo> series_;           // +0x348
    std::vector<cRZString> categoryNames_;             // +0x360
    float barWidth_ = 1.0f;                            // +0x378
    float spacing_ = 1.0f;                             // +0x37c
    float widthFraction_ = 0.75f;                      // +0x380
};

}  // namespace gz
