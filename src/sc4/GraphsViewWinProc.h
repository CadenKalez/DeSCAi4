// cSC4GraphsViewWinProc — the graphs panel window procedure (21 class symbols 0x1002dfbf6..0x1002e29b2 plus the four
// window helpers 0x1002e2a7a..0x1002e2b86 and the static SetupGraphButtons 0x1002e0341).
//
// The panel is one window inside a frame; its parent holds two sibling panels, the collapsed one (0x8a8b5b72, shown
// with the "graphs expanded" toggle off) and the expanded one (0x8a8b5b71).  The procedure owns the graph control that
// lives in the expanded panel and rebuilds it from a graph exemplar whenever the selected option changes:
//
//   exemplar key   {0x6534284a, 0xca4ad545, option}          (spRM vslot 0x18, iid 0xa52160f5)
//     property 0xaa4c0d1b  graph style: 1 line, 2 bar, 3 pie (anything else: no graph)
//     property 0x8a416a99  localized title (SC4LoadExemplarString); falls back to property 0x20
//   line plots (style 1)                                    bar plots (style 2)
//     0x6a4aee40  value series id                            0x6a4aee40  value series id
//     0x6a4aee5f  operand series id (for the combine op)     0x6a4aeedc  category name (string id)
//     0x6a4aee68  combine op (1 add 2 sub 3 mul 4 div 5 log) 0x6a4aeee2  value scale (float)
//     0x6a4aeedc  legend name (string id)                    0x6a4aeee3  bar series index
//     0x6a4aeedf  legend colour (3 bytes RGB)                0x6a4aeee4  bar fill colour (3 bytes RGB)
//     0x6a4aeee2  value scale (float)                        0x6a4aeee5  bar outline colour (3 bytes RGB)
//                                                            0x6a4aee68  combine op
//   both styles: 0x6a4aeded axis max override (float, sentinel -FLT_MAX), 0xea4aedc6 axis min override (float,
//   sentinel +FLT_MAX) and 0x6a4aedef "use the computed major tick spacing" (float, 0 = the auto spacing).
//
// The comparison dates come from the simulator: vslot 0x40 is the simulated day number (the bar chart samples it
// directly, the line chart starts four days back and rounds it down to a whole time-scale period), and vslot 0x28 is
// the simulation start date (samples at or before it read as zero).  Every series value comes from the history
// warehouse (spCity vslot 0x100 = SL::spHistoryWarehouse, vslot 0x40 = GetPoint).
//
// Messages handled (all only while the panel window is visible):
//   command (3) sender 0x887113a3 / id 0xea4a7cf2   the time-scale slider moved -> SetTimeScale(value)
//   command (3) sender 0x287259f6 / id 0xa8b7e00    collapse (show the collapsed panel)
//   command (3) sender 0x287259f6 / id 0xa8b7e01    expand   (show the expanded panel)
//   command (3) sender 0x287259f6 / id 0xca8b82f9   toggle expand / collapse
//   command (3) sender 0x287259f6 / id 0x4000..0x4fff  graph option (low byte) -> SelectOption/ShowGraph
//   command (3) sender 0x287259f6 / id 0x4000000..0x41ffffff  per-series legend toggle -> RefreshGraph
// The "month" message 0x66956816 (subscribed to in Init) rebuilds the data (DoMessage).
//
// Layout (0x90 bytes): +0 the cIGZMessageTarget2 view, +8 the cIGZWinProc view, +0x10 reference count, +0x14
// initialised, +0x18 graph style, +0x1c current option, +0x20 frame window (referenced), +0x28 collapsed panel
// (referenced), +0x30 expanded panel (referenced), +0x38 graph control (referenced), +0x40 visible, +0x41 expanded,
// +0x48 std::vector<LinePlotInfo>, +0x60 std::map<uint32_t,bool> legend on/off by value-id, +0x78 plot count,
// +0x7c bar series count, +0x80 time scale, +0x84 axis max override, +0x88 axis min override, +0x8c auto tick
// spacing flag.
//
// Quirks kept from the binary:
//  * the graph is destroyed and rebuilt even when the option did not change (ShowWindow always calls ShowGraph) and
//    ShowGraph(0) is the way HideWindow drops the control, so a missing exemplar instance 0 leaves no graph;
//  * the plot is only rebuilt when the style property differs from the current one, but the vector is cleared and
//    the graph recreated whenever it does differ (so the exemplar is re-read on every option change);
//  * the legend colour of a bar chart (the swatch of a category) was never stored by the bar path in the original
//    either: the label-colour bytes at +0x10 stay 0 (black);
//  * axis 0's minimum is reset to 0 only by the line-chart tail, not by the bar-chart tail.
// The port guards the window / panel pointers the original always has (it would fault on a half-built panel) and the
// time-scale table index the original reads unbounded.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gz/LineGraph.h"
#include "gz/WinProc.h"
#include "gz/cGZDate.h"
#include "gz/cIGZMessage2.h"
#include "rz/cRZString.h"

class cIGZBuffer;
class cIGZFont;
namespace gz { class cGZBarGraph; class cGZGraph; class cGZPieGraph; }
namespace sc4 { class cISCPropertyHolder; class cISCResExemplar; }

namespace sc4 {

// One graph plot definition (0x48 bytes). The original builds the same record for the line and the bar chart; each
// style fills the fields it needs (the line chart leaves `series` 0, the bar chart leaves `operandId` 0).
struct LinePlotInfo {
    std::uint32_t valueId = 0;        // +0x00 history series read for the plot value
    std::uint32_t operandId = 0;      // +0x04 second history series (line chart, combine != 0)
    std::uint32_t series = 0;         // +0x08 bar-chart series this plot feeds
    std::uint32_t combine = 0;        // +0x0c 0 none, 1 add, 2 subtract, 3 multiply, 4 divide, 5 logarithmic
    std::uint8_t red = 0;             // +0x10
    std::uint8_t green = 0;           // +0x11
    std::uint8_t blue = 0;            // +0x12
    float scale = 1.0f;               // +0x14 value multiplier
    cRZString name;                   // +0x18 legend label (localized string id 0x6a231eaa)
    bool enabled = true;              // +0x40 legend toggle state
};

// The draw primitive only the SC4 line chart overrides: DrawLineAntialiased @ 0x100390498 (free function, not
// recovered). Without a host the SC4 line chart falls back to the plain cGZGraph line.
class IGraphLineDrawing {
public:
    virtual ~IGraphLineDrawing() = default;
    virtual void DrawLineAntialiased(cIGZBuffer* buffer, int x0, int y0, int x1, int y1, int color,
                                     std::uint32_t thickness) = 0;
};
IGraphLineDrawing* GraphLineDrawing();
void SetGraphLineDrawing(IGraphLineDrawing* drawing);

// cSC4LineGraph — the line chart the graph panel builds: cGZLineGraph with both DrawLine overloads overridden. The
// (buffer, rect, series) overload just forwards to the base (quirk kept); the segment overload draws antialiased.
class cSC4LineGraph final : public gz::cGZLineGraph {
public:
    explicit cSC4LineGraph(cIGZWinMgr* mgr) : gz::cGZLineGraph(mgr) {}
    ~cSC4LineGraph() override = default;   // @ 0x1002e2cb0 / 0x1002e2cba
    bool DrawLine(cIGZBuffer* buffer, int x0, int y0, int x1, int y1, int color,
                  std::uint32_t thickness) override;                                        // @ 0x1002e2cf6
    void DrawLine(cIGZBuffer* buffer, cRZRect& segment, std::uint32_t series) override;    // @ 0x1002e2d2a
};

// What the panel needs from the running game; one method per original call (global / slot in the comment).
class IGraphsViewHost {
public:
    virtual ~IGraphsViewHost() = default;
    virtual cIGZMessageServer2* MessageServer() = 0;                       // SL::spMS2
    // nsSC4TutorialSystem::FilterUICommandMessage @ 0x10006008e: false swallows the command message.
    virtual bool FilterUICommandMessage(cGZMessage& message) = 0;
    // SC4LoadExemplarString @ 0x100121132: the localized string a resource-key property points at; false when the
    // loader is not available (the panel then falls back to the plain name property 0x20).
    virtual bool LoadExemplarString(cIGZString& out, cISCPropertyHolder* holder, std::uint32_t property) = 0;
    // SL::spRM vslot 0x18 with key {0x6534284a, 0xca4ad545, option} and iid 0xa52160f5; one reference.
    virtual cISCResExemplar* LoadGraphExemplar(std::uint32_t option) = 0;
    // The history warehouse (spCity vslot 0x100 / SL::spHistoryWarehouse), vslot 0x40.
    virtual bool HistoryPoint(std::uint32_t series, const cIGZDate& date, float& value) = 0;
    virtual std::uint32_t SimulatorDayNumber() = 0;                        // simulator vslot 0x40
    virtual void SimulatorStartDate(cIGZDate& out) = 0;                    // simulator vslot 0x28
};
IGraphsViewHost* GraphsViewHost();
void SetGraphsViewHost(IGraphsViewHost* host);

class cSC4GraphsViewWinProc final : public cIGZMessageTarget2, public gz::cIGZWinProc {
public:
    explicit cSC4GraphsViewWinProc(cIGZWin* window);   // @ 0x1002dfbf6
    ~cSC4GraphsViewWinProc();                          // @ 0x1002dfca6

    // cIGZUnknown — one reference count behind both views.
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1002dff1e
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x1002dff82
    std::uint32_t Release() override;                              // @ 0x1002dff9e

    // cIGZWinProc
    bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) override;   // @ 0x1002e0358
    // cIGZMessageTarget2
    bool DoMessage(cIGZMessage2* message) override;                         // @ 0x1002e29b2

    bool Init();                                              // @ 0x1002dffda
    bool Shutdown();                                          // @ 0x1002dfdc0
    void SetTimeScale(std::uint32_t scale);                   // @ 0x1002e0266
    void SetExpanded(bool expanded);                          // @ 0x1002e0518
    void SelectOption(std::uint32_t option);                  // @ 0x1002e056a
    void ShowGraph(std::uint32_t option);                     // @ 0x1002e0688
    void RefreshGraph();                                      // @ 0x1002e1a80
    void ShowWindow();                                        // @ 0x1002e2a7a
    void HideWindow();                                        // @ 0x1002e2ad2
    void ShowGraphType(std::uint32_t option);                 // @ 0x1002e2b2e
    bool IsVisible();                                         // @ 0x1002e2b86

    // Test / AddGUI accessors (the original reads these members directly).
    cIGZWin* Window() const { return window_; }
    cIGZWin* CollapsedPanel() const { return collapsedPanel_; }   // +0x28
    cIGZWin* ExpandedPanel() const { return expandedPanel_; }     // +0x30
    gz::cGZGraph* Graph() const { return graph_; }                // +0x38
    std::uint32_t GraphStyle() const { return graphStyle_; }      // +0x18
    std::uint32_t CurrentOption() const { return currentOption_; }   // +0x1c
    std::uint32_t TimeScale() const { return timeScale_; }        // +0x80
    std::uint32_t PlotCount() const { return seriesCount_; }      // +0x78
    std::uint32_t OptionCount() const { return optionCount_; }    // +0x7c
    bool Expanded() const { return expanded_; }                   // +0x41
    bool Initialised() const { return initialised_; }             // +0x14
    float AxisMaxOverride() const { return axisMaxOverride_; }    // +0x84
    float AxisMinOverride() const { return axisMinOverride_; }    // +0x88
    float AutoTickSpacing() const { return autoTickSpacing_; }    // +0x8c
    const std::vector<LinePlotInfo>& Plots() const { return plots_; }          // +0x48
    const std::map<std::uint32_t, bool>& LegendState() const { return legendState_; }   // +0x60

private:
    void LoadPlots(cISCPropertyHolder* holder, bool bar);   // the per-style record builder of ShowGraph
    void RefreshLineGraph(gz::cIGZLineGraph* graph);        // the style 1 half of RefreshGraph
    void RefreshBarGraph(gz::cIGZBarGraph* graph);          // the style 2 half of RefreshGraph
    void SetVerticalAxis(float low, float high, float spacing, bool setHorizontalMin);
    void AddLegendEntry(std::uint32_t index, const cRZRect& area, int y, std::uint32_t outlineColor,
                        std::uint32_t textColor);

    std::uint32_t refCount_ = 0;              // +0x10
    bool initialised_ = false;                // +0x14
    std::uint32_t graphStyle_ = 0;            // +0x18
    std::uint32_t currentOption_ = 1;         // +0x1c
    cIGZWin* window_ = nullptr;               // +0x20 (referenced)
    cIGZWin* collapsedPanel_ = nullptr;       // +0x28 (referenced)
    cIGZWin* expandedPanel_ = nullptr;        // +0x30 (referenced)
    gz::cGZGraph* graph_ = nullptr;           // +0x38 (referenced)
    bool visible_ = false;                    // +0x40
    bool expanded_ = true;                    // +0x41
    std::vector<LinePlotInfo> plots_;         // +0x48
    std::map<std::uint32_t, bool> legendState_;   // +0x60
    std::uint32_t seriesCount_ = 0;           // +0x78
    std::uint32_t optionCount_ = 0;           // +0x7c
    std::uint32_t timeScale_ = 0;             // +0x80
    float axisMaxOverride_ = -3.4028235e38f;  // +0x84 (sentinel -FLT_MAX)
    float axisMinOverride_ = 3.4028235e38f;   // +0x88 (sentinel +FLT_MAX)
    float autoTickSpacing_ = 0.0f;            // +0x8c
};

// The static cIGZWin::EnumChildren callback Init installs on the option-button container (0x1002e0341): makes every
// button notify the graph frame window.
bool SetupGraphButtons(cIGZWin* parent, std::uint32_t id, void* child, void* context);

}  // namespace sc4
