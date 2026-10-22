// cSCStandardTypeEntryView (0x100523b10..0x10052e754) — base class of the property editor's value entry views (text boxes,
// spinners, combo boxes, curve editors ...). A cGZWin plus the cISCStandardTypeEntryView interface (iid 0x06c5ff7b) at +0x150.
// Layout: the 0x150-byte window, +0x150 the interface view, +0x158 the shown value (cRZVariant), +0x170 text colour (black),
// +0x174 selection colour (dark blue), +0x178 background (white), +0x17c the property id being edited.
// The subclasses (cSCUint32TextEntryView ...) override SetValue / SetAdditionalViewParameters and build their controls.
#pragma once
#include <cstdint>

#include "gz/Win.h"
#include "gz/WinFlatRect.h"
#include "rz/cRZVariant.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISCStandardTypeEntryView = 0x06c5ff7b;

class cISCStandardTypeEntryView : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                           // [3]
    virtual void SetPropertyID(std::uint32_t id) = 0;          // [4]
    virtual void GetPropertyID(std::uint32_t& id) = 0;         // [5]
    virtual void SetValue(const cIGZVariant& value) = 0;       // [6]
};

class cSCStandardTypeEntryView : public cGZWin, public cISCStandardTypeEntryView {
public:
    explicit cSCStandardTypeEntryView(cIGZWinMgr* mgr);        // 0x100523b10

    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x06c5ff7b -> the interface view, else cGZWin
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    cIGZWin* AsIGZWin() override { return this; }
    void SetPropertyID(std::uint32_t id) override { propertyId_ = id; }
    void GetPropertyID(std::uint32_t& id) override { id = propertyId_; }
    void SetValue(const cIGZVariant& value) override { value_ = value; }
    virtual void SetAdditionalViewParameters(const cIGZString&) {}   // the base ignores them

    const cRZVariant& Value() const { return value_; }
    std::uint32_t TextColor() const { return textColor_; }
    std::uint32_t SelectionColor() const { return selectionColor_; }
    std::uint32_t BackgroundColor() const { return backgroundColor_; }

    // Adds a light-grey flat rectangle with a black outline over the whole view as the first child (shared by the subclasses).
    void AddBackgroundFillControl();   // 0x100523c9c

private:
    cRZVariant value_;                       // +0x158
    std::uint32_t textColor_;                // +0x170
    std::uint32_t selectionColor_;           // +0x174
    std::uint32_t backgroundColor_;          // +0x178
    std::uint32_t propertyId_ = 0;           // +0x17c
};

}  // namespace sc4
