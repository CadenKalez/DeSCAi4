#include "sc4/BuildingSelectWinProc.h"

#include <algorithm>

#include "gz/WinBtn.h"
#include "gz/WinGen.h"
#include "gz/WinUtils.h"
#include "rz/cRZString.h"

namespace sc4 {

namespace {
ITractStyles* g_styles = nullptr;
ITutorialFilter* g_tutorial = nullptr;
constexpr std::uint32_t kStylePanelCollapsed = 0x8bca20c3, kStylePanelExpanded = 0x2bc619af;
constexpr std::uint32_t kButtonAllAtOnce = 0xebc61560, kButtonOneAtATime = 0xcbc61559;
constexpr std::uint32_t kButtonExpand = 0xbc61548, kButtonExpandAlt = 0x2bc619f3, kButtonCollapse = 0xcbc61567, kButtonExpandPanel = 0xebc619fd;
constexpr std::uint32_t kSpinner = 0xabc61550, kSpinnerCaption = 0xabc61589;
constexpr std::uint32_t kFirstStyleButton = 0x2000, kStyleButtonCount = 4;
constexpr std::uint32_t kSenderButton = 0x287259f6;
}  // namespace

void SetBuildingSelectEnvironment(ITractStyles* styles, ITutorialFilter* tutorial) {
    g_styles = styles;
    g_tutorial = tutorial;
}

cSC4BuildingSelectWinProc::cSC4BuildingSelectWinProc(cIGZWin* window) : window_(window) {
    if (window_) window_->AddRef();
}

cSC4BuildingSelectWinProc::~cSC4BuildingSelectWinProc() {
    Shutdown();
    if (window_) {
        cIGZWin* window = window_;
        window_ = nullptr;
        window->Release();
    }
}

bool cSC4BuildingSelectWinProc::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != gz::kGZIID_cIGZWinProc) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4BuildingSelectWinProc::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cSC4BuildingSelectWinProc::Shutdown() {
    if (initialised_) {
        initialised_ = false;
        gz::cIGZWinGen* gen = nullptr;
        if (window_->QueryInterface(gz::kGZIID_cIGZWinGen, reinterpret_cast<void**>(&gen))) {
            gen->SetWinProc(nullptr);
            gen->Release();
        }
        if (collapsedPanel_) { cIGZWin* w = collapsedPanel_; collapsedPanel_ = nullptr; w->Release(); }
        if (expandedPanel_) { cIGZWin* w = expandedPanel_; expandedPanel_ = nullptr; w->Release(); }
    }
    return true;
}

bool cSC4BuildingSelectWinProc::Init() {
    if (!initialised_) {
        auto adopt = [](cIGZWin*& slot, cIGZWin* found) {
            cIGZWin* old = slot;
            if (old != found) {
                if (found) found->AddRef();
                slot = found;
                if (old) old->Release();
            }
        };
        adopt(collapsedPanel_, window_->GetChildWindowFromID(kStylePanelCollapsed));
        adopt(expandedPanel_, window_->GetChildWindowFromID(kStylePanelExpanded));
        collapsedPanel_->HideWindow();
        expandedPanel_->ShowWindow();
        gz::cIGZWinGen* gen = nullptr;
        if (window_->QueryInterface(gz::kGZIID_cIGZWinGen, reinterpret_cast<void**>(&gen))) {
            gen->SetWinProc(this);
            gen->Release();
        }
        window_->SetFlag(0x800, false);
        collapsedPanel_->SetFlag(0x800, false);
        expandedPanel_->SetFlag(0x800, false);
        // the two mode buttons sit one after the other: the second is placed beside the first
        gz::cIGZWinBtn* mode = nullptr;
        if (window_->GetChildAsRecursive(kButtonOneAtATime, gz::kGZIID_cIGZWinBtn, reinterpret_cast<void**>(&mode))) {
            mode->FitSizeToContent();
            cIGZWin* first = window_->GetChildWindowFromIDRecursive(0xcbc3c2b9);
            cIGZWin* second = window_->GetChildWindowFromIDRecursive(0xcbc61581);
            if (first && second) {
                first->GZWinMoveTo(first->GetT(), mode->AsIGZWin()->GetR() + 2);
                second->GZWinMoveTo(second->GetT(), first->GetR() + 8);
            }
        }
        initialised_ = true;
        if (mode) mode->Release();
    }
    nsGZWinUtils::SetWindowNotificationTargetRecursive(window_, window_);
    return true;
}

bool cSC4BuildingSelectWinProc::DoWinProcMessage(cIGZWin*, cGZMessage& message) {
    if (message.type == kBuildingSelectMessageSpinner) {
        if (message.param1 == kSpinner) {
            g_styles->SetYearsBetweenStyleChanges(static_cast<int>(message.param2));
            const cRZString text(std::to_string(g_styles->GetYearsBetweenStyleChanges()).c_str());
            nsGZWinUtils::SetWindowCaption(window_, kSpinnerCaption, text);
        }
    } else if (message.type == kGZMsgCommand) {
        if (g_tutorial && !g_tutorial->Allows(message)) return true;
        if (message.param1 == kSenderButton) {
            const std::uint64_t id = message.param2;
            if (id - kFirstStyleButton < kStyleButtonCount) {
                if (message.param3 == 0) RemoveActiveStyle(static_cast<std::uint32_t>(id));
                else AddActiveStyle(static_cast<std::uint32_t>(id));
                EnableStyleButtons();
            } else if (id == kButtonExpand || id == kButtonExpandAlt) {
                SetExpanded(!expanded_);
            } else if (id == kButtonOneAtATime) {
                g_styles->UseAllStylesAtOnce(false);
                nsGZWinUtils::SetButtonToggleState(window_, kButtonOneAtATime, true);
                nsGZWinUtils::SetButtonToggleState(window_, kButtonAllAtOnce, false);
            } else if (id == kButtonAllAtOnce) {
                g_styles->UseAllStylesAtOnce(true);
                nsGZWinUtils::SetButtonToggleState(window_, kButtonAllAtOnce, true);
                nsGZWinUtils::SetButtonToggleState(window_, kButtonOneAtATime, false);
            } else if (id == kButtonCollapse) {
                expandedPanel_->HideWindow();
                collapsedPanel_->ShowWindow();
                expanded_ = false;
            } else if (id == kButtonExpandPanel) {
                collapsedPanel_->HideWindow();
                expandedPanel_->ShowWindow();
                expanded_ = true;
            }
        }
    }
    return false;
}

void cSC4BuildingSelectWinProc::SetExpanded(bool expanded) {
    if (!expanded) {
        expandedPanel_->HideWindow();
        collapsedPanel_->ShowWindow();
    } else {
        collapsedPanel_->HideWindow();
        expandedPanel_->ShowWindow();
    }
    expanded_ = expanded;
}

void cSC4BuildingSelectWinProc::AddActiveStyle(std::uint32_t style) {
    std::vector<std::uint32_t> styles = g_styles->GetActiveStyles();
    if (std::find(styles.begin(), styles.end(), style) == styles.end()) {
        styles.push_back(style);
        g_styles->SetActiveStyles(styles);
    }
}

void cSC4BuildingSelectWinProc::RemoveActiveStyle(std::uint32_t style) {
    std::vector<std::uint32_t> styles = g_styles->GetActiveStyles();
    auto it = std::find(styles.begin(), styles.end(), style);
    if (it != styles.end()) {
        styles.erase(it);
        g_styles->SetActiveStyles(styles);
    }
}

bool cSC4BuildingSelectWinProc::IsStyleActive(std::uint32_t style) {
    const std::vector<std::uint32_t> styles = g_styles->GetActiveStyles();
    return std::find(styles.begin(), styles.end(), style) != styles.end();
}

// With a single style pressed that button is disabled so the last style cannot be switched off.
void cSC4BuildingSelectWinProc::EnableStyleButtons() {
    std::uint32_t pressed = 0;
    for (std::uint32_t id = kFirstStyleButton; id != kFirstStyleButton + kStyleButtonCount; ++id)
        pressed += nsGZWinUtils::GetButtonToggleState(window_, id);
    for (std::uint32_t id = kFirstStyleButton; id != kFirstStyleButton + kStyleButtonCount; ++id) {
        if (pressed < 2) {
            if (nsGZWinUtils::GetButtonToggleState(window_, id)) nsGZWinUtils::SetWindowEnabled(window_, id, false);
        } else {
            nsGZWinUtils::SetWindowEnabled(window_, id, true);
        }
    }
}

void cSC4BuildingSelectWinProc::ShowWindow() {
    const bool all = g_styles->IsUsingAllStylesAtOnce();
    nsGZWinUtils::SetButtonToggleState(window_, kButtonAllAtOnce, all);
    nsGZWinUtils::SetButtonToggleState(window_, kButtonOneAtATime, !all);
    for (std::uint32_t id = kFirstStyleButton; id != kFirstStyleButton + kStyleButtonCount; ++id)
        nsGZWinUtils::SetButtonToggleState(window_, id, IsStyleActive(id));
    EnableStyleButtons();
    const cRZString text(std::to_string(g_styles->GetYearsBetweenStyleChanges()).c_str());
    nsGZWinUtils::SetWindowCaption(window_, kSpinnerCaption, text);
    nsGZWinUtils::SetSpinnerValue(window_, kSpinner, g_styles->GetYearsBetweenStyleChanges());
    window_->ShowWindow();
}

}  // namespace sc4
