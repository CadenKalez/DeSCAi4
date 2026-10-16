// cGZScatterGraph — an x/y scatter chart on top of cGZGraph (77 functions 0x10038232a..0x1003870b2). Every series
// is a list of (x, y) points drawn as a symbol character from the symbol font, optionally joined by lines.
//
// Layout (0x370 bytes): cGZGraph (0x320), +0x320 cIGZScatterGraph view (iid 0x32cf2351), +0x328 shared value range
// (both axes use one range, default true), +0x330 data, +0x348 series infos, +0x360 symbol font, +0x368 its colour.
//
// Quirks kept from the binary: the automatic plot area is squared off, every series is drawn with as many points
// as the first series has, and the symbol font is used without a null check.
#pragma once
#include <vector>

#include "gz/BarGraph.h"
#include "gz/Graph.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZScatterGraph = 0x32cf2351;

struct cRZ2DPointFloat32 {
    float x = 0.0f, y = 0.0f;
};

// Per-series look (0x40 bytes).
struct ScatterGraphSeriesInfo {
    char symbol = 'A';                      // +0
    std::uint32_t symbolSize = 12;          // +4
    std::uint32_t color = 0;                // +8
    cRZString name;                         // +0x10
    bool drawLines = false;                 // +0x38
};

// cIGZScatterGraph (iid 0x32cf2351): slot n at 8n.
class cIGZScatterGraph : public cIGZUnknown {
public:
    virtual void SetSymbolFont(cIGZFont* font, std::uint32_t color) = 0;                                    // [3]
    virtual void SetSeriesCount(std::uint32_t count) = 0;                                                    // [4]
    virtual void InsertSeries(std::uint32_t index) = 0;                                                      // [5]
    virtual void RemoveSeries(std::uint32_t index) = 0;                                                      // [6]
    virtual void SetSymbolCharacter(char symbol, std::uint32_t series) = 0;                                  // [7]
    virtual void SetSymbolSize(std::uint32_t size, std::uint32_t series) = 0;                                // [8]
    virtual void SetSeriesColor(std::uint32_t color, std::uint32_t series) = 0;                              // [9]
    virtual void SetSeriesName(const cRZString& name, std::uint32_t series) = 0;                             // [10]
    virtual void SetSeriesLineDrawing(bool lines, std::uint32_t series) = 0;                                 // [11]
    virtual void SetSeriesInfo(ScatterGraphSeriesInfo& info, std::uint32_t series) = 0;                      // [12]
    virtual void InsertSeriesInfo(ScatterGraphSeriesInfo& info, std::uint32_t series) = 0;                   // [13]
    virtual void RemoveSeriesInfo(std::uint32_t series) = 0;                                                 // [14]
    virtual void InsertData(const cRZ2DPointFloat32& point, std::uint32_t series, std::uint32_t position) = 0;   // [15]
    virtual void InsertData(float x, float y, std::uint32_t series, std::uint32_t position) = 0;             // [16]
    virtual void RemoveData(std::uint32_t position, std::uint32_t series) = 0;                               // [17]
    virtual void SetData(const cRZ2DPointFloat32& point, std::uint32_t position, std::uint32_t series) = 0;  // [18]
    virtual void SetData(float x, float y, std::uint32_t position, std::uint32_t series) = 0;                // [19]
    virtual void SetData(const std::vector<cRZ2DPointFloat32>& points, std::uint32_t series) = 0;            // [20]
    virtual void AppendData(const std::vector<cRZ2DPointFloat32>& points, std::uint32_t series) = 0;         // [21]
    virtual void GetAutomaticGraphAreaRect(cRZRect& out) = 0;                                                // [22]
    virtual void GetAutomaticAxisMinValue(Axis axis, float& out) = 0;                                        // [23]
    virtual void GetAutomaticAxisMaxValue(Axis axis, float& out) = 0;                                        // [24]
    virtual float GetHighestAxisValue(Axis axis) = 0;                                                        // [25]
    virtual float GetLowestAxisValue(Axis axis) = 0;                                                         // [26]
    virtual void CompleteRedraw(cIGZBuffer* buffer) = 0;                                                     // [27]
    virtual void DrawGraphData(cIGZBuffer* buffer) = 0;                                                      // [28]
    virtual void LoadDefaultFonts() = 0;                                                                     // [29]
    virtual void DrawSymbol(cIGZBuffer* buffer, cRZPoint& point, std::uint32_t series) = 0;                  // [30]
    virtual cIGZWin* AsIGZWin() = 0;                                                                         // [31]
    virtual cIGZGraph* AsIGZGraph() = 0;                                                                     // [32]
};

class cGZScatterGraph : public cGZGraph, public cIGZScatterGraph {
public:
    explicit cGZScatterGraph(cIGZWinMgr* mgr);
    virtual ~cGZScatterGraph();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    bool Init() override { return cGZGraph::Init(); }
    bool Shutdown() override;

    void SetSymbolFont(cIGZFont* font, std::uint32_t color) override;
    void SetSeriesCount(std::uint32_t count) override;
    void InsertSeries(std::uint32_t index) override;
    void RemoveSeries(std::uint32_t index) override;
    void SetSymbolCharacter(char symbol, std::uint32_t series) override;
    void SetSymbolSize(std::uint32_t size, std::uint32_t series) override;
    void SetSeriesColor(std::uint32_t color, std::uint32_t series) override;
    void SetSeriesName(const cRZString& name, std::uint32_t series) override;
    void SetSeriesLineDrawing(bool lines, std::uint32_t series) override;
    void SetSeriesInfo(ScatterGraphSeriesInfo& info, std::uint32_t series) override;
    void InsertSeriesInfo(ScatterGraphSeriesInfo& info, std::uint32_t series) override;
    void RemoveSeriesInfo(std::uint32_t series) override;
    void InsertData(const cRZ2DPointFloat32& point, std::uint32_t series, std::uint32_t position) override;
    void InsertData(float x, float y, std::uint32_t series, std::uint32_t position) override { InsertData(cRZ2DPointFloat32{x, y}, series, position); }
    void RemoveData(std::uint32_t position, std::uint32_t series) override;
    void SetData(const cRZ2DPointFloat32& point, std::uint32_t position, std::uint32_t series) override;
    void SetData(float x, float y, std::uint32_t position, std::uint32_t series) override { SetData(cRZ2DPointFloat32{x, y}, position, series); }
    void SetData(const std::vector<cRZ2DPointFloat32>& points, std::uint32_t series) override;
    void AppendData(const std::vector<cRZ2DPointFloat32>& points, std::uint32_t series) override;
    void GetAutomaticGraphAreaRect(cRZRect& out) override;
    void GetAutomaticAxisMinValue(Axis axis, float& out) override;
    void GetAutomaticAxisMaxValue(Axis axis, float& out) override;
    float GetHighestAxisValue(Axis axis) override;
    float GetLowestAxisValue(Axis axis) override;
    void CompleteRedraw(cIGZBuffer* buffer) override { cGZGraph::CompleteRedraw(buffer); }
    void DrawGraphData(cIGZBuffer* buffer) override;
    void LoadDefaultFonts() override;
    void DrawSymbol(cIGZBuffer* buffer, cRZPoint& point, std::uint32_t series) override;
    cIGZWin* AsIGZWin() override { return this; }
    cIGZGraph* AsIGZGraph() override { return static_cast<cGZGraph*>(this); }

    // for tests
    std::size_t SeriesCount() const { return data_.size(); }
    const std::vector<cRZ2DPointFloat32>& Series(std::size_t index) const { return data_[index]; }
    const ScatterGraphSeriesInfo& SeriesInfo(std::size_t index) const { return series_[index]; }
    cIGZFont* SymbolFont() const { return symbolFont_; }
    std::uint32_t SymbolFontColor() const { return symbolFontColor_; }
    void SetSharedRange(bool shared) { sharedRange_ = shared; }

private:
    std::uint32_t OuterIndex(std::uint32_t series) const;

    bool sharedRange_ = true;                      // +0x328
    std::vector<std::vector<cRZ2DPointFloat32>> data_;   // +0x330
    std::vector<ScatterGraphSeriesInfo> series_;   // +0x348
    cIGZFont* symbolFont_ = nullptr;               // +0x360
    std::uint32_t symbolFontColor_ = kAutoColor;   // +0x368
};

}  // namespace gz
