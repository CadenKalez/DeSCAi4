// cGZLineGraph — a line chart on top of cGZGraph (86 functions 0x1002e2d20..0x100387020). Data is a vector of
// series (one value per category point); each series can draw a line through its points and a symbol character
// (from the symbol font) at every point. Orientation 0 runs the categories along the horizontal axis.
//
// Layout (0x388 bytes): cGZGraph (0x320), +0x320 cIGZLineGraph view (iid 0x12cf2351), +0x328 orientation, +0x330
// data, +0x348 series infos, +0x360 category names, +0x378 symbol font, +0x380 its colour.
//
// Quirks kept from the binary: the horizontal-orientation segment is built as {y1, x0, y0, x1} (not a transpose),
// the symbol loop uses the length of the last line-drawn series for every series, RemoveCategoryName only clears
// the text, and the symbol font names "windings" (sic) then "dingbats".
#pragma once
#include <vector>

#include "gz/BarGraph.h"
#include "gz/Graph.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZLineGraph = 0x12cf2351;

// Per-series look (0x40 bytes).
struct LineGraphSeriesInfo {
    bool symbolUse = true;                  // +0
    char symbol = 'A';                      // +1
    std::uint32_t symbolSize = 12;          // +4
    bool lineUse = true;                    // +8
    std::uint32_t color = 0;                // +0xc
    std::uint32_t thickness = 1;            // +0x10
    cRZString name;                         // +0x18
};

// cIGZLineGraph (iid 0x12cf2351): slot n at 8n.
class cIGZLineGraph : public cIGZUnknown {
public:
    virtual void SetGraphOrientation(GraphOrientation orientation) = 0;                            // [3]
    virtual void SetSymbolFont(cIGZFont* font, std::uint32_t color) = 0;                           // [4]
    virtual void SetSeriesCount(std::uint32_t count) = 0;                                          // [5]
    virtual void InsertSeries(std::uint32_t index) = 0;                                            // [6]
    virtual void RemoveSeries(std::uint32_t index) = 0;                                            // [7]
    virtual void RemoveAllSeries() = 0;                                                            // [8]
    virtual void SetSymbolUse(Usage symbol, Usage line, std::uint32_t series) = 0;                 // [9]
    virtual void SetSymbolCharacter(char symbol, std::uint32_t series) = 0;                        // [10]
    virtual void SetSymbolSize(std::uint32_t size, std::uint32_t series) = 0;                      // [11]
    virtual void SetSeriesColor(std::uint32_t color, std::uint32_t series) = 0;                    // [12]
    virtual void SetSeriesName(const cRZString& name, std::uint32_t series) = 0;                   // [13]
    virtual void SetSeriesInfo(LineGraphSeriesInfo& info, std::uint32_t series) = 0;               // [14]
    virtual void InsertSeriesInfo(LineGraphSeriesInfo& info, std::uint32_t series) = 0;            // [15]
    virtual void RemoveSeriesInfo(std::uint32_t series) = 0;                                       // [16]
    virtual void InsertCategoryName(const cRZString& name, std::uint32_t index) = 0;               // [17]
    virtual void RemoveCategoryName(std::uint32_t index) = 0;                                      // [18]
    virtual void RemoveCategoryNames() = 0;                                                        // [19]
    virtual void InsertData(float value, std::uint32_t series, std::uint32_t position) = 0;        // [20]
    virtual void RemoveData(std::uint32_t position, std::uint32_t series) = 0;                     // [21]
    virtual void SetData(float value, std::uint32_t position, std::uint32_t series) = 0;           // [22]
    virtual void SetData(std::vector<float>& values, std::uint32_t series) = 0;                    // [23]
    virtual void AppendData(std::vector<float>& values, std::uint32_t series) = 0;                 // [24]
    virtual float GetHighestAxisValue(Axis axis) = 0;                                              // [25]
    virtual float GetLowestAxisValue(Axis axis) = 0;                                               // [26]
    virtual void CompleteRedraw(cIGZBuffer* buffer) = 0;                                           // [27]
    virtual void DrawGraphData(cIGZBuffer* buffer) = 0;                                            // [28]
    virtual void LoadDefaultFonts() = 0;                                                           // [29]
    virtual int GetCenterOfDataPoint(std::uint32_t index) = 0;                                     // [30]
    virtual void DrawSymbol(cIGZBuffer* buffer, cRZPoint& point, std::uint32_t series) = 0;        // [31]
    virtual void DrawLine(cIGZBuffer* buffer, cRZRect& segment, std::uint32_t series) = 0;         // [32]
    virtual cIGZWin* AsIGZWin() = 0;                                                               // [33]
    virtual cIGZGraph* AsIGZGraph() = 0;                                                           // [34]
};

class cGZLineGraph : public cGZGraph, public cIGZLineGraph {
public:
    explicit cGZLineGraph(cIGZWinMgr* mgr);
    virtual ~cGZLineGraph();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    bool Init() override { return cGZGraph::Init(); }
    bool Shutdown() override;

    void SetGraphOrientation(GraphOrientation orientation) override { orientation_ = orientation; }
    void SetSymbolFont(cIGZFont* font, std::uint32_t color) override;
    void SetSeriesCount(std::uint32_t count) override;
    void InsertSeries(std::uint32_t index) override;
    void RemoveSeries(std::uint32_t index) override;
    void RemoveAllSeries() override;
    void SetSymbolUse(Usage symbol, Usage line, std::uint32_t series) override;
    void SetSymbolCharacter(char symbol, std::uint32_t series) override;
    void SetSymbolSize(std::uint32_t size, std::uint32_t series) override;
    void SetSeriesColor(std::uint32_t color, std::uint32_t series) override;
    void SetSeriesName(const cRZString& name, std::uint32_t series) override;
    void SetSeriesInfo(LineGraphSeriesInfo& info, std::uint32_t series) override;
    void InsertSeriesInfo(LineGraphSeriesInfo& info, std::uint32_t series) override;
    void RemoveSeriesInfo(std::uint32_t series) override;
    void InsertCategoryName(const cRZString& name, std::uint32_t index) override;
    void RemoveCategoryName(std::uint32_t index) override;
    void RemoveCategoryNames() override { categoryNames_.clear(); }
    void InsertData(float value, std::uint32_t series, std::uint32_t position) override;
    void RemoveData(std::uint32_t position, std::uint32_t series) override;
    void SetData(float value, std::uint32_t position, std::uint32_t series) override;
    void SetData(std::vector<float>& values, std::uint32_t series) override;
    void AppendData(std::vector<float>& values, std::uint32_t series) override;
    float GetHighestAxisValue(Axis axis) override;
    float GetLowestAxisValue(Axis axis) override;
    void CompleteRedraw(cIGZBuffer* buffer) override { cGZGraph::CompleteRedraw(buffer); }
    void DrawGraphData(cIGZBuffer* buffer) override;
    void LoadDefaultFonts() override;
    int GetCenterOfDataPoint(std::uint32_t index) override;
    void DrawSymbol(cIGZBuffer* buffer, cRZPoint& point, std::uint32_t series) override;
    void DrawLine(cIGZBuffer* buffer, cRZRect& segment, std::uint32_t series) override;
    bool DrawLine(cIGZBuffer* buffer, int x0, int y0, int x1, int y1, int color, std::uint32_t thickness) override {
        return cGZGraph::DrawLine(buffer, x0, y0, x1, y1, color, thickness);
    }
    cIGZWin* AsIGZWin() override { return this; }
    cIGZGraph* AsIGZGraph() override { return static_cast<cGZGraph*>(this); }

    void DrawTicks(cIGZBuffer* buffer, std::uint32_t axisMask) override;
    void DrawTickText(cIGZBuffer* buffer, std::uint32_t axisMask) override;

    // for tests
    std::size_t SeriesCount() const { return data_.size(); }
    const std::vector<float>& Series(std::size_t index) const { return data_[index]; }
    const LineGraphSeriesInfo& SeriesInfo(std::size_t index) const { return series_[index]; }
    const std::vector<cRZString>& CategoryNames() const { return categoryNames_; }
    cIGZFont* SymbolFont() const { return symbolFont_; }
    std::uint32_t SymbolFontColor() const { return symbolFontColor_; }

private:
    std::uint32_t OuterIndex(std::uint32_t series) const;

    std::int32_t orientation_ = 0;                 // +0x328
    std::vector<std::vector<float>> data_;         // +0x330
    std::vector<LineGraphSeriesInfo> series_;      // +0x348
    std::vector<cRZString> categoryNames_;         // +0x360
    cIGZFont* symbolFont_ = nullptr;               // +0x378
    std::uint32_t symbolFontColor_ = kAutoColor;   // +0x380
};

}  // namespace gz
