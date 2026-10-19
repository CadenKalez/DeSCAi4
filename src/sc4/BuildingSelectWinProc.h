// cSC4BuildingSelectWinProc — the building style panel of the tract developer window (18 functions,
// 0x10075913c..0x100759be2). Two panels (collapsed +0x18, expanded +0x20) switch with the expand/collapse buttons; the
// four style buttons 0x2000..0x2003 toggle the tract developer's active building styles (at least one stays on: when
// only one is pressed it is disabled), "all styles at once" (0xebc61560) and "one at a time" (0xcbc61559) set its mode,
// and the spinner 0xabc61550 sets the years between style changes (caption 0xabc61589).
#pragma once
#include <cstdint>
#include <vector>

#include "gz/WinProc.h"

namespace sc4 {

constexpr std::uint32_t kBuildingSelectMessageSpinner = 0x682876c9;

class ITractStyles {            // SL::spTractDeveloper
public:
    virtual ~ITractStyles() = default;
    virtual std::vector<std::uint32_t> GetActiveStyles() = 0;          // vslot 0x48
    virtual void SetActiveStyles(const std::vector<std::uint32_t>& styles) = 0;   // 0x50
    virtual bool IsUsingAllStylesAtOnce() = 0;                          // 0x58
    virtual void UseAllStylesAtOnce(bool all) = 0;                      // 0x60
    virtual int GetYearsBetweenStyleChanges() = 0;                      // 0x68
    virtual void SetYearsBetweenStyleChanges(int years) = 0;            // 0x70
};
class ITutorialFilter {         // nsSC4TutorialSystem::FilterUICommandMessage
public:
    virtual ~ITutorialFilter() = default;
    virtual bool Allows(const cGZMessage& message) = 0;
};
void SetBuildingSelectEnvironment(ITractStyles* styles, ITutorialFilter* tutorial);

class cSC4BuildingSelectWinProc final : public gz::cIGZWinProc {
public:
    explicit cSC4BuildingSelectWinProc(cIGZWin* window);
    ~cSC4BuildingSelectWinProc();
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) override;

    bool Init();
    bool Shutdown();
    void ShowWindow();
    void HideWindow() { window_->HideWindow(); }
    bool IsVisible() { return window_->IsVisible(); }
    void SetExpanded(bool expanded);
    void AddActiveStyle(std::uint32_t style);
    void RemoveActiveStyle(std::uint32_t style);
    bool IsStyleActive(std::uint32_t style);
    void EnableStyleButtons();

private:
    std::uint32_t refCount_ = 0;      // +8
    bool initialised_ = false;        // +0xc
    cIGZWin* window_;                 // +0x10
    cIGZWin* collapsedPanel_ = nullptr;   // +0x18
    cIGZWin* expandedPanel_ = nullptr;    // +0x20
    bool expanded_ = true;            // +0x28
};

}  // namespace sc4
