// sc4_propertyview_test — cSCPropertyView (PropertyView.h/.cpp).
//
// What it proves:
//  * the [Aliases] / [ExemplarFilters] / [Properties] / [Uint8ValueMap_*] / [Uint32ValueMap_*] /
//    [ResKeyTableEditorDefaults] sections build the static registries, including the parser quirks
//    (space separated OR'ed filters, quoted defaults with commas, count 0, the stale exemplar-filter map);
//  * the value formatter: custom printf format, uint8/uint32 maps, string resource, debug name, file name,
//    the property customizer and "???" / sint64 truncation quirks;
//  * the window: layout, controls, combo population with duplicate names, editor map with the filter mask,
//    combo -> editor selection, Done writing the value back into the property, Cancel, KillSelf's two paths,
//    key forwarding, and the two helper factories (createLineInput / createSpinner).
//
// The engine side is faked through IPropertyViewHost; the child controls are small cGZWin-derived test doubles so
// that the real window tree / ref counting / QueryInterface paths are exercised.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "fake_winmgr2.h"
#include "gz/cGZDBSegmentDirectoryFiles.h"
#include "sc4/PropertyView.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// fonts / graphics
struct FakeFont final : gz::IWinFont {
    std::uint32_t refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    std::uint32_t DefaultTextColor() override { return 0x111111; }
    std::uint32_t DefaultShadowColor() override { return 0x222222; }
    std::int16_t LineHeight(int, int) override { return 16; }
    int CountLines(const char*, std::uint32_t, int) override { return 1; }
    int WrappedWidth(const char*, std::uint32_t length, int) override { return int(length) * 8; }
    void MeasureText(const char*, std::uint32_t length, cRZRect& out) override { out = {0, 0, int(length) * 8, 16}; }
    int TextWidth(const char*, std::uint32_t length) override { return int(length) * 8; }
    void DrawText(IWinBuffer*, int, int, const char*, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, int) override {}
    void DrawTextBox(IWinBuffer*, const cRZRect&, const char*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, int) override {}
};

// ---------------------------------------------------------------------------------------------------------------------
// child controls (each is its own window, like the real gz controls)
struct FakeFlatRect final : cGZWin, gz::cIGZWinFlatRect {
    explicit FakeFlatRect(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == gz::kGZIID_cIGZWinFlatRect) { *out = static_cast<gz::cIGZWinFlatRect*>(this); AddRef(); return true; }
        return cGZWin::QueryInterface(iid, out);
    }
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    cIGZWin* AsIGZWin() override { return this; }
    void SetFillColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { cGZWin::SetFillColor(r, g, b); }
    void GetFillColor(std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) const override { cGZWin::GetFillColor(r, g, b); }
    void SetFillColor(std::uint32_t nativeColor) override { cGZWin::SetFillColor(nativeColor); }
    std::uint32_t GetFillColor() const override { return cGZWin::GetFillColor(); }
    void SetOutlineColor(int, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void GetOutlineColor(int, std::uint8_t&, std::uint8_t&, std::uint8_t&) const override {}
    void SetOutlineColor(std::uint8_t, std::uint8_t, std::uint8_t) override {}
    bool SetOutlineFlags(int flags) override { outlineFlags = flags; return true; }
    std::uint8_t GetOutlineFlags() override { return static_cast<std::uint8_t>(outlineFlags); }
    bool GetOutlineFlag(int flag) override { return (outlineFlags & flag) != 0; }
    void SetOutlineColor(std::uint32_t nativeColor) override { outlineColor = nativeColor; }
    void SetOutlineColors(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void GetOutlineColors(std::uint32_t&, std::uint32_t&, std::uint32_t&, std::uint32_t&) override {}
    bool AutoSize(const cRZRect&) override { return false; }
    bool SetSizeMode(int mode) override { sizeMode = mode; return true; }
    std::uint8_t GetSizeMode() override { return static_cast<std::uint8_t>(sizeMode); }
    int outlineFlags = 0, sizeMode = -1;
    std::uint32_t outlineColor = 0;
};

struct FakeText final : cGZWin, gz::cIGZWinText {
    explicit FakeText(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == gz::kGZIID_cIGZWinText) { *out = static_cast<gz::cIGZWinText*>(this); AddRef(); return true; }
        return cGZWin::QueryInterface(iid, out);
    }
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    cIGZWin* AsIGZWin() override { return this; }
    void SetCaption(const cIGZString& caption) override { cGZWin::SetCaption(caption); }
    bool FitWindowToText(bool, bool) override {
        const int width = int(caption_.Strlen()) * 8;
        SetSize(width > 0 ? width : 1, 16);
        fitted = true;
        return true;
    }
    bool GetWinTextFlag(int flag) override { return (textFlags & flag) != 0; }
    bool SetWinTextFlag(int flag, bool on) override {
        if (on) textFlags |= flag; else textFlags &= ~flag;
        return true;
    }
    void SetTextColor(std::uint32_t native) override { textColor = native; }
    void GetTextColor(std::uint32_t& native) override { native = textColor; }
    void ClearTextColor() override { textColor = 0; }
    bool SetTextColor(tFontColorIdx index, std::uint32_t native) override {
        if (index == kColorText) textColor = native;
        return true;
    }
    bool GetTextColor(tFontColorIdx, std::uint32_t& native) const override { native = textColor; return true; }
    bool SetTextColor(tFontColorIdx, gz::cRZColor) override { return true; }
    bool GetTextColor(tFontColorIdx, gz::cRZColor&) const override { return true; }
    bool ClearTextColor(tFontColorIdx) override { return true; }
    void SetBackgroundOpaque(bool) override {}
    bool IsBackgroundOpaque() override { return false; }
    bool SetFont(gz::IWinFont* value) override { font = value; return true; }
    bool SetFontStyle(std::uint32_t styleId) override { fontStyle = styleId; return true; }
    bool GetFontStyle(std::uint32_t& styleId) override { styleId = fontStyle; return true; }
    bool SetAlignment(std::uint32_t value) override { alignment = value; return true; }
    bool GetAlignment(std::uint32_t& value) const override { value = alignment; return true; }
    bool SetGutters(std::int8_t, std::int8_t) override { return true; }
    bool GetGutters(std::int8_t& x, std::int8_t& y) override { x = y = 2; return true; }
    bool SetTextOffsets(std::int8_t, std::int8_t) override { return true; }
    bool GetTextOffsets(std::int8_t& x, std::int8_t& y) override { x = y = 0; return true; }
    void SetOutlineUse(bool, std::uint32_t) override {}
    void GetOutlineUse(bool& on, std::uint32_t& color) const override { on = false; color = 0; }
    gz::IWinFont* font = nullptr;
    std::uint32_t textColor = 0, fontStyle = 0, alignment = 0;
    int textFlags = 0;
    bool fitted = false;
};

struct FakeButton final : cGZWin, gz::cIGZWinBtn {
    explicit FakeButton(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == gz::kGZIID_cIGZWinBtn) { *out = static_cast<gz::cIGZWinBtn*>(this); AddRef(); return true; }
        return cGZWin::QueryInterface(iid, out);
    }
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    cIGZWin* AsIGZWin() override { return this; }
    bool GetBtnFlag(int flag) const override { return (flags & flag) != 0; }
    bool SetBtnFlag(int flag, bool on) override {
        if (on) flags |= flag; else flags &= ~flag;
        return true;
    }
    bool ToggleOn() override { flags |= gz::kBtnOn; return true; }
    bool ToggleOff() override { flags &= ~gz::kBtnOn; return true; }
    bool IsOn() const override { return (flags & gz::kBtnOn) != 0; }
    bool SetChecked(bool checked) override { return SetBtnFlag(gz::kBtnOn, checked); }
    bool IsChecked() const override { return IsOn(); }
    int GetStyle() override { return style; }
    bool SetStyle(int value, cIGZBuffer*, const cRZRect*) override { style = value; return true; }
    bool SetButtonStyle(int value) override { style = value; return true; }
    int GetImageW() const override { return 1; }
    int GetImageH() const override { return 1; }
    bool GetImageArea(cRZRect& out) const override { out = {0, 0, 1, 1}; return true; }
    bool SetBuffer(cIGZBuffer* image) override { buffer = image; if (image) image->AddRef(); return true; }
    bool GetBuffer(cIGZBuffer*& out) const override { out = buffer; return true; }
    bool SetFont(gz::IWinButtonFont*) override { return true; }
    bool SetFontStyle(std::uint32_t) override { return true; }
    bool GetFontStyle(std::uint32_t& styleId) override { styleId = 0; return true; }
    bool SetFontColor(int, std::uint32_t) override { return true; }
    bool GetFontColor(int, std::uint32_t& native) const override { native = 0; return true; }
    bool SetFontColor(int, gz::cRZColor) override { return true; }
    bool GetFontColor(int, gz::cRZColor&) const override { return true; }
    bool ClearFontColor(int) override { return true; }
    bool SetCaptionStyle(int) override { return true; }
    bool GetCaptionStyle(int& style) const override { style = 0; return true; }
    bool SetGutters(std::uint8_t, std::uint8_t) override { return true; }
    bool GetGutters(int& x, int& y) const override { x = y = 0; return true; }
    bool SetGutters(std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) override { return true; }
    bool GetGutters(int& l, int& t, int& r, int& b) const override { l = t = r = b = 0; return true; }
    bool FitSizeToContent() override { fitted = true; if (GetW() == 0 || GetH() == 0) SetSize(40, 20); return true; }
    bool GetMinMaxInfo(cRZRect& out) override { out = {40, 20, 40, 20}; return true; }
    bool SetTipText(const cIGZString& text) override { tipText = text.ToChar(); return true; }
    bool GetTipText(cIGZString& out) const override { out.AssignFromCString(tipText.c_str()); return true; }
    bool ClearTipText() override { tipText.clear(); return true; }
    bool SetTipPlacementType(int) override { return true; }
    bool GetTipPlacementType(int& type) const override { type = 0; return true; }
    bool SetTipPlacementOffsets(int, int) override { return true; }
    bool GetTipPlacementOffsets(int& x, int& y) const override { x = y = 0; return true; }
    bool SetTipColor(int, std::uint8_t, std::uint8_t, std::uint8_t) override { return true; }
    bool GetTipColor(int, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) const override { r = g = b = 0; return true; }
    void Flash(std::uint32_t, std::uint32_t) override {}
    bool SetSoundID(int, std::uint32_t) override { return true; }
    std::uint32_t GetSoundID(int) const override { return 0; }
    int flags = 0, style = 0;
    bool fitted = false;
    std::string tipText;
    cIGZBuffer* buffer = nullptr;
};

struct FakeCombo final : cGZWin, gz::cIGZWinCombo {
    explicit FakeCombo(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == gz::kGZIID_cIGZWinCombo) { *out = static_cast<gz::cIGZWinCombo*>(this); AddRef(); return true; }
        return cGZWin::QueryInterface(iid, out);
    }
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    cIGZWin* AsIGZWin() override { return this; }
    gz::cIGZWinTextEdit* GetLineInput() override { return nullptr; }
    gz::cIGZWinTextEdit* GetTextEdit() override { return nullptr; }
    void SetFlag(std::uint32_t flag) override { flags |= flag; }
    std::uint32_t SetFlag(std::uint32_t flag, bool on) override { return cGZWin::SetFlag(flag, on); }
    void SetFlags(std::uint32_t value) override { flags = value; }
    void ClearFlag(std::uint32_t flag) override { flags &= ~flag; }
    bool IsFlagSet(std::uint32_t flag) override { return (flags & flag) != 0; }
    std::uint32_t GetFlags() override { return flags; }
    void SetFont(cIGZFont*) override {}
    bool SetFontColor(tFontColorIdx index, std::uint32_t native) override { fontColors[index] = native; return true; }
    std::uint32_t GetFontColor(tFontColorIdx index) const override { auto it = fontColors.find(index); return it == fontColors.end() ? 0 : it->second; }
    bool GetFontColor(tFontColorIdx index, std::uint32_t& native) const override { native = GetFontColor(index); return true; }
    bool ClearFontColor(tFontColorIdx) override { return true; }
    bool SetHighlightColor(std::uint32_t native) override { highlight = native; return true; }
    bool GetHighlightColor(std::uint32_t& native) const override { native = highlight; return true; }
    bool SetHighlightFillAlpha(float) override { return true; }
    bool GetHighlightFillAlpha(float& alpha) const override { alpha = 1; return true; }
    bool GetEditable() const override { return editable; }
    bool SetEditable(bool value) override { editable = value; return true; }
    bool SetDropDownOutlineColor(std::uint32_t) override { return true; }
    bool GetDropDownOutlineColor(std::uint32_t& native) const override { native = 0; return true; }
    bool RemoveString(const cIGZString&) override { return false; }
    bool RemoveString(int) override { return false; }
    void RemoveAllStrings() override { strings.clear(); }
    bool InsertString(const cIGZString& text, int index) override {
        const std::string copy(text.ToChar());
        if (index == 0) strings.insert(strings.begin(), copy);
        else if (index != -2 && index < int(strings.size())) {
            if (index < 0) return false;
            strings.insert(strings.begin() + index, copy);
        } else strings.push_back(copy);
        return true;
    }
    bool SetSelection(int index, bool) override {
        if (index < 0 || index >= int(strings.size())) return false;
        selection = index;
        return true;
    }
    int GetSelection() const override { return selection; }
    bool GetSelection(int& index, cIGZString& text) const override {
        if (selection < 0 || selection >= int(strings.size())) return false;
        index = selection;
        text.AssignFromCString(strings[selection].c_str());
        return true;
    }
    bool GetStringFromIndex(int index, cIGZString& out) const override {
        if (index < 0 || index >= int(strings.size())) return false;
        out.AssignFromCString(strings[index].c_str());
        return true;
    }
    bool GetIndexFromString(int& index, const cIGZString& text) const override {
        for (std::size_t i = 0; i < strings.size(); ++i) {
            if (strings[i] == text.ToChar()) { index = int(i); return true; }
        }
        return false;
    }
    bool SetComboDownArrow(cIGZBuffer*, const cRZRect*) override { return false; }
    bool GetComboDownArrow(cIGZBuffer*& image, cRZRect&) const override { image = nullptr; return false; }
    bool SetComboDownColor(std::uint32_t) override { return true; }
    bool GetComboDownColor(std::uint32_t& native) const override { native = 0; return true; }
    bool SetBtnGutter(std::int8_t) override { return true; }
    bool GetBtnGutter(std::int8_t& gutter) const override { gutter = 0; return true; }
    bool SetTextGutters(std::int8_t, std::int8_t) override { return true; }
    bool GetTextGutters(std::int8_t& x, std::int8_t& y) const override { x = y = 0; return true; }
    bool GetMinMaxInfo(cRZRect& info) override { info = {0, 0, 0, 0}; return true; }
    std::vector<std::string> strings;
    std::map<int, std::uint32_t> fontColors;
    std::uint32_t flags = 0, highlight = 0;
    int selection = -1;
    bool editable = true;
};

// The value entry view: cISCValueEditView plus the (seamed) cISCResKeyTableEntryView interface.
struct FakeEditor final : cGZWin, cISCValueEditView, cISCResKeyTableEntryView {
    explicit FakeEditor(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kPropertyViewValueEditorIID) { *out = static_cast<cISCValueEditView*>(this); AddRef(); return true; }
        if (iid == kPropertyViewResKeyTableEntryViewIID) { *out = static_cast<cISCResKeyTableEntryView*>(this); AddRef(); return true; }
        return cGZWin::QueryInterface(iid, out);
    }
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    cIGZWin* AsIGZWin() override { return this; }
    void SetPropertyID(std::uint32_t id) override { propertyId = id; }
    void GetPropertyID(std::uint32_t& id) override { id = propertyId; }
    void SetValue(const cIGZVariant& value) override { value_ = value; }
    void GetValue(cIGZVariant& out) override { out.CopyFrom(value_); }
    void SetDefaultKey(const cGZPersistResourceKey& value) override { defaultKey = value; hasKey = true; }
    void SetKeyPropDefId(std::uint32_t id) override { keyPropertyId = id; }
    std::uint32_t propertyId = 0;
    cRZVariant value_;
    cGZPersistResourceKey defaultKey;
    std::uint32_t keyPropertyId = 0;
    bool hasKey = false;
};

struct FakeCustomizer final : cISCPropertyCustomizer {
    std::uint32_t refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool GetPropertyValueString(std::uint32_t propertyId, const cIGZVariant&, cIGZString& out) override {
        out.AssignFromCString(("custom:" + std::to_string(propertyId)).c_str());
        return true;
    }
    bool ValueIsValid(std::uint32_t, const cIGZVariant&) override { return true; }
};

struct DummyUnknown final : cIGZUnknown {
    std::uint32_t refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};

struct FakeExemplar final : cIGZSerializable {
    std::uint32_t refs = 1;
    std::uint32_t classId = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Write(cIGZOStream&) const override { return false; }
    bool Read(cIGZIStream&) override { return false; }
    std::uint32_t GetGZCLSID() const override { return classId; }
};

struct FakeDirectorySegment final : cIGZPersistDBSegmentDirectoryFiles {
    std::uint32_t refs = 1;
    std::string fileName;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    cIGZPersistDBSegment* AsIDBSegment() override { return nullptr; }
    bool Init() override { return false; }
    bool Shutdown() override { return false; }
    bool Init(std::uint32_t, const cIGZString*, bool) override { return false; }
    const cRZString& Path() const override { static cRZString empty; return empty; }
    void GetPath(cIGZString&) const override {}
    bool SetPath(const cIGZString&) override { return false; }
    void SetDefaultGroupId(std::uint32_t) override {}
    std::uint32_t GroupId() const override { return 0; }
    cGZRESIDtoExtensionMap* ExtensionMap() override { return nullptr; }
    bool GetRESIDtoExtensionMap(cIGZPersistRESIDtoExtensionMap** out) override { *out = nullptr; return false; }
    void AddExtensionAssociation(std::uint32_t, const cIGZString&) override {}
    void SetAllowedIDPositions(bool, bool) override {}
    void SetAllowTextAsKey(bool) override {}
    bool GetFileNameFromKey(const cGZPersistResourceKey& key, cIGZString& out) override {
        if (fileName.empty()) return false;
        out.AssignFromCString(fileName.c_str());
        lastKey = key;
        return true;
    }
    bool GetFilePathFromKey(const cGZPersistResourceKey&, cIGZString&) override { return false; }
    bool GetKeyFromFileName(const cIGZString&, const cIGZString&, cGZPersistResourceKey&) override { return false; }
    bool IsFileNameValid(const cIGZString&) override { return false; }
    bool GenerateNewKey(std::uint32_t, cGZPersistResourceKey&) override { return false; }
    std::uint32_t EnumerateRecordFiles(cIGZString**, std::uint32_t) override { return 0; }
    bool GetFileChangeDetection() override { return false; }
    bool SetFileChangeDetection(bool, std::uint32_t) override { return false; }
    bool DoFileChangeCheck() override { return false; }
    cGZPersistResourceKey lastKey;
};

// The narrow helper-control views.
struct FakeTextEdit final : cGZWin, IPropertyTextEdit {
    explicit FakeTextEdit(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    bool QueryInterface(std::uint32_t iid, void** out) override { return cGZWin::QueryInterface(iid, out); }
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    cIGZWin* AsIGZWin() override { return this; }
    void SetFont(cIGZFont*) override { ++fonts; }
    void SetFontColor(int index, std::uint32_t native) override { fontColors[index] = native; }
    void SetCaretColor(std::uint32_t native) override { caret = native; }
    void SetHighlightFillColor(std::uint32_t native) override { highlight = native; }
    void SetText(const cIGZString& value, bool notify) override { text = value.ToChar(); notified = notify; }
    void SetFlag(std::uint32_t flag) override { flags |= flag; }
    using cGZWin::SetFlag;   // the narrow view's SetFlag(uint32) hides the window's SetFlag(uint32, bool)
    int fonts = 0;
    std::uint32_t caret = 0, highlight = 0, flags = 0;
    std::map<int, std::uint32_t> fontColors;
    std::string text;
    bool notified = true;
};

struct FakeSpinner final : cGZWin, IPropertySpinner {
    explicit FakeSpinner(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    bool QueryInterface(std::uint32_t iid, void** out) override { return cGZWin::QueryInterface(iid, out); }
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    cIGZWin* AsIGZWin() override { return this; }
    void SetSpinnerFlag(std::uint32_t flag, bool on) override { if (on) flags |= flag; else flags &= ~flag; }
    std::uint32_t flags = 0;
};

struct Target final : cGZWin {
    explicit Target(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& message) override { got.push_back(message); return true; }
};

// The engine seam.
struct Host final : IPropertyViewHost {
    Host(cIGZWinMgr* m, const std::string& tempDir) : mgr(m), dir(tempDir) {}

    cIGZWinMgr* WindowManager() override { return mgr; }

    cIGZUnknown* CreateObject(std::uint32_t classId, std::uint32_t iid) override {
        if (classId == kPropertyViewFlatRectClass && iid == gz::kGZIID_cIGZWinFlatRect) {
            auto* object = new FakeFlatRect(mgr);
            object->AddRef();
            flatRects.push_back(object);
            return static_cast<gz::cIGZWinFlatRect*>(object);
        }
        if (classId == kPropertyViewTextClass && iid == gz::kGZIID_cIGZWinText) {
            auto* object = new FakeText(mgr);
            object->AddRef();
            texts.push_back(object);
            return static_cast<gz::cIGZWinText*>(object);
        }
        if (classId == kPropertyViewComboClass && iid == gz::kGZIID_cIGZWinCombo) {
            auto* object = new FakeCombo(mgr);
            object->AddRef();
            combos.push_back(object);
            return static_cast<gz::cIGZWinCombo*>(object);
        }
        if (iid == kPropertyViewValueEditorIID) {
            auto* object = new FakeEditor(mgr);
            object->AddRef();
            editors.push_back(object);
            editorClasses.push_back(classId);
            return static_cast<cISCValueEditView*>(object);
        }
        if (iid == kPropertyViewCustomizerIID) {
            auto* object = new FakeCustomizer();
            customizers.push_back(object);
            return object;
        }
        if (iid == 1) {
            auto* object = new FakeExemplar();
            object->classId = classId;
            exemplars.push_back(object);
            return object;
        }
        return nullptr;
    }

    cIGZBuffer* LoadBitmap(std::uint32_t group, std::uint32_t instance) override {
        bitmapGroup = group;
        bitmapInstance = instance;
        return nullptr;   // no image: the button is created without one
    }

    gz::cIGZWinBtn* CreateButton(std::uint32_t id, const cRZString& caption, cIGZBuffer* image) override {
        auto* button = new FakeButton(mgr);
        button->AddRef();
        button->SetID(id);
        button->SetCaption(caption);
        if (image) button->SetBuffer(image);
        button->SetSize(40, 20);
        buttons.push_back(button);
        return button;
    }

    gz::IWinFont* LabelFont(std::uint32_t styleId) override { labelFontStyle = styleId; return &font; }
    cIGZFont* TextEditFont(std::uint32_t styleId) override { textFontStyle = styleId; return nullptr; }
    IPropertySpinner* CreateSpinner(std::uint32_t id, int type, int low, int high, int step, int value) override {
        auto* spinner = new FakeSpinner(mgr);
        spinner->AddRef();
        spinner->SetID(id);
        spinner->SetSize(0x30, 0x14);
        spinnerArgs = {type, low, high, step, value};
        spinners.push_back(spinner);
        return spinner;
    }
    void DataDir(cIGZString& out) override {
        std::string path = dir;
        if (path.empty() || path.back() != '/') path += '/';
        out.AssignFromCString(path.c_str());
    }
    cIGZUnknown* GetResource(const cGZPersistResourceKey& key, std::uint32_t iid) override {
        resourceKey = key;
        if (iid == kPropertyViewStringResIID) {
            if (!stringResource) return nullptr;
            stringResource->AddRef();
            return stringResource;
        }
        if (iid == kPropertyViewExemplarIID) {
            if (!exemplarResource) return nullptr;
            exemplarResource->AddRef();
            return exemplarResource;
        }
        return nullptr;
    }
    cISCPropertyHolder* ExemplarProperties(cIGZUnknown*) override { return exemplarHolder; }
    cIGZPersistDBSegmentDirectoryFiles* FindDirectorySegment(std::uint32_t segmentId) override {
        directorySegment = segmentId;
        if (!directory) return nullptr;
        directory->AddRef();
        return directory;
    }

    cIGZWinMgr* mgr;
    std::string dir;
    FakeFont font;
    std::uint32_t labelFontStyle = 0, textFontStyle = 0;
    std::uint32_t bitmapGroup = 0, bitmapInstance = 0;
    std::vector<FakeFlatRect*> flatRects;
    std::vector<FakeText*> texts;
    std::vector<FakeButton*> buttons;
    std::vector<FakeCombo*> combos;
    std::vector<FakeEditor*> editors;
    std::vector<std::uint32_t> editorClasses;
    std::vector<FakeCustomizer*> customizers;
    std::vector<FakeExemplar*> exemplars;
    std::vector<FakeSpinner*> spinners;
    std::vector<int> spinnerArgs;
    FakeTextEdit* textEdit = nullptr;
    FakeSpinner* standaloneSpinner = nullptr;
    IPropertyTextEdit* CreateTextEdit() override {
        textEdit = new FakeTextEdit(mgr);
        textEdit->AddRef();
        return textEdit;
    }
    cRZString* stringResource = nullptr;
    DummyUnknown* exemplarResource = nullptr;
    cISCPropertyHolder* exemplarHolder = nullptr;
    FakeDirectorySegment* directory = nullptr;
    cGZPersistResourceKey resourceKey;
    std::uint32_t directorySegment = 0;
};

}  // namespace

namespace {

std::string TempRoot() {
    return "/tmp/sc4_pv_test_" + std::to_string(getpid());
}

void WriteTextFile(const std::string& path, const std::string& content) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

const char* kMainINI =
    "[Aliases]\n"
    "Editor = 0x11111111\n"
    "Filt = 0x2\n"
    "FormatDisp = 0x73b6fd3\n"
    "Map8 = 0x67c3501e\n"
    "Map32 = 0xa73a2f21\n"
    "StringDisp = 0xc73b723f\n"
    "DebugDisp = 0xc77c3528\n"
    "FileDisp = 0x68727736\n"
    "CustomDisp = 0x22222222\n"
    "ResKeyEditor = 0xa7847d6a\n"
    "ResKeyEditor2 = 0x281f7625\n"
    "VlaEditor = 0xe8dd9d13\n"
    "[ExemplarFilters]\n"
    "0x1 = 0x1 Filt 0x4\n"
    "[Properties]\n"
    "0x100 = UINT32, 1, 0x2A, 0x1, \"Alpha\", \"Alpha help\", Editor, \"value=%d\", 0\n"
    "0x101 = UINT8, 1, 7, 0x1, \"Beta\", \"Beta help\", Editor, Map8, 0\n"
    "0x102 = STRING, 1, \"hello, world\", 0x1, \"Gamma\", \"Gamma help\", Editor, 0, 0\n"
    "0x103 = SINT64, 1, -5, 0x1, \"Delta\", \"Delta help\", Editor, 0, 0\n"
    "0x104 = UINT32, 0, 0, 0x1, \"Epsilon\", \"Epsilon help\", VlaEditor, 0, 0\n"
    "0x105 = UINT32, 0, 0, 0x1, \"Zeta\", \"Zeta help\", Editor, 0, 0\n"
    "0x106 = UINT32, 1, 3, 0x8, \"Excluded\", \"Excluded help\", Editor, 0, 0\n"
    "0x107 = UINT32, 1, 42, 0x1, \"Eta\", \"Eta help\", Editor, Map32, 0\n"
    "0x108 = UINT32, 1, 0, 0x1, \"Theta\", \"Theta help\", ResKeyEditor, 0, 0\n"
    "0x109 = UINT32, 1, 0, 0x1, \"Iota\", \"Iota help\", ResKeyEditor2, 0, 0\n"
    "0x10a = UINT32, 1, 0x2A, 0x1, \"Alpha\", \"Alpha help 2\", Editor, \"value=%d\", 0\n"
    "0x10b = STRING, 1, \"x\", 0x1, \"Kappa\", \"Kappa help\", Editor, \"<%s>\", 0\n"
    "0x10c = UINT32, 1, 1, 0x1, \"Lambda\", \"Lambda help\", Editor, CustomDisp, 0\n"
    "0x10d = UINT32, 3, 1, 0x1, \"Mu\", \"Mu help\", Editor, StringDisp, 0\n"
    "0x10e = UINT32, 3, 1, 0x1, \"Nu\", \"Nu help\", Editor, DebugDisp, 0\n"
    "0x10f = UINT32, 3, 1, 0x1, \"Xi\", \"Xi help\", Editor, FileDisp, 0\n"
    "0x110 = BOOL, 1, T, 0x1, \"Omicron\", \"Omicron help\", Editor, 0, 0\n"
    "0x111 = UINT8, 1, 0, 0x1, \"Pi\", \"Pi help\", Editor, 0, 0\n"
    "0x112 = FLOAT32, 1, 0, 0x1, \"Rho\", \"Rho help\", Editor, 0, 0\n"
    "0x113 = UINT64, 1, 0, 0x1, \"Sigma\", \"Sigma help\", Editor, 0, 0\n"
    "[Uint8ValueMap_0x0000000000000101]\n"
    "7 = \"Seven\"\n"
    "9 = \"Nine\"\n"
    "[Uint32ValueMap_0x0000000000000107]\n"
    "42 = \"FortyTwo\"\n"
    "[ResKeyTableEditorDefaults]\n"
    "0x108 = 1, 2, 3, 4\n"
    "0x109 = 5, 6, 7, 8\n";

}  // namespace

int main() {
    const std::string root = TempRoot();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string iniPath = root + "/Ingred.ini";
    WriteTextFile(iniPath, kMainINI);

    FakeMgr2 mgr;
    FakeWinGraphics2 graphics;
    SetWinGraphics(&graphics);

    Host host(&mgr, root);
    SetPropertyViewHost(&host);
    cSCPropertyView::ResetStaticsForTest();
    cSCPropertyView::SetSettingsIniFilePath(cRZString(iniPath.c_str()));

    cSCProperty property;
    property.AddRef();
    {
        // the registry has to exist before the window is built: the constructor maps its exemplar filter through
        // mExemplarFilters, so read the file with a throw-away view first
        auto* iniReader = new cSCPropertyView(property, 1);
        iniReader->AddRef();
        iniReader->processINI();
        iniReader->Release();
    }
    auto* view = new cSCPropertyView(property, 1);
    view->AddRef();
    CHECK(view->exemplarFilter() == 7);

    // ---- INI parsing -----------------------------------------------------------------------------------------------
    CHECK(cSCPropertyView::INIProcessed());
    CHECK(cSCPropertyView::PropertyInfos().size() == 19);
    CHECK(cSCPropertyView::PropertyInfos().count(0x113) == 0);   // UINT64 has no type name (quirk)
    CHECK(cSCPropertyView::Aliases().size() == 12);
    {
        const auto alias = cSCPropertyView::Aliases().find(cRZString("Editor"));
        CHECK(alias != cSCPropertyView::Aliases().end() && alias->second == 0x11111111);
    }
    CHECK(cSCPropertyView::ExemplarFilters().size() == 1);
    CHECK(cSCPropertyView::ExemplarFilters().at(1) == 7);   // 0x1 | 0x2 | 0x4 (space separated, OR'ed)

    const auto& infos = cSCPropertyView::PropertyInfos();
    {
        const auto& info = infos.at(0x100);
        CHECK(info.variantType == cIGZVariant::kTypeUint32);
        CHECK(info.elementCount == 1);
        CHECK(info.filter == 1);
        CHECK(std::string(info.name.ToChar()) == "Alpha");
        CHECK(std::string(info.helpText.ToChar()) == "Alpha help");
        CHECK(info.editorClassId == 0x11111111);
        CHECK(info.displayClassId == kPropertyViewDisplayFormat);
        CHECK(std::string(info.defaultText.ToChar()) == "value=%d");
        CHECK(info.defaultValue.GetType() == cIGZVariant::kTypeUint32 && info.defaultValue.GetValUint32() == 42);
    }
    {
        const auto& info = infos.at(0x102);   // a quoted default may contain the field separator
        CHECK(info.defaultValue.GetType() == (cIGZVariant::kTypeArrayFlag | cIGZVariant::kTypeRZChar));
        cRZString value;
        CHECK(info.defaultValue.GetValString(value));
        CHECK(std::string(value.ToChar()) == "hello, world");
    }
    {
        const auto& info = infos.at(0x104);   // count 0 stores a single uint32 0
        CHECK(info.elementCount == 0);
        CHECK(info.defaultValue.GetType() == (cIGZVariant::kTypeArrayFlag | cIGZVariant::kTypeUint32));
        CHECK(info.defaultValue.GetCount() == 1 && info.defaultValue.RefUint32()[0] == 0);
        CHECK(!view->IsVariableLengthArray(0x104));   // its editor class is the 0xe8dd9d13 marker
    }
    CHECK(view->IsVariableLengthArray(0x105));
    CHECK(!view->IsVariableLengthArray(0x999));
    {
        const auto& info = infos.at(0x101);
        CHECK(info.displayClassId == kPropertyViewDisplayUint8Map);
        CHECK(info.valueMapOwnerId == 0x101);
    }
    CHECK(cSCPropertyView::Uint8ValueMaps().size() == 1);
    CHECK(cSCPropertyView::Uint8ValueMaps().at(0x101).at(7).str() == "Seven");
    CHECK(cSCPropertyView::Uint8ValueMaps().at(0x101).at(9).str() == "Nine");
    CHECK(cSCPropertyView::Uint32ValueMaps().at(0x107).at(42).str() == "FortyTwo");
    CHECK(cSCPropertyView::ResKeyTableDefaults().at(0x108).values[0] == 1);
    CHECK(cSCPropertyView::ResKeyTableDefaults().at(0x108).values[1] == 3);   // {0,2,1,3} swap kept
    CHECK(cSCPropertyView::ResKeyTableDefaults().at(0x108).values[2] == 2);
    CHECK(cSCPropertyView::ResKeyTableDefaults().at(0x108).values[3] == 4);
    CHECK(cSCPropertyView::ResKeyTableDefaults().at(0x109).values[0] == 5);

    // ---- name / help / value map lookups ---------------------------------------------------------------------------
    {
        cRZString name;
        view->GetPropertyNameStr(0x100, name);
        CHECK(std::string(name.ToChar()) == "Alpha");
        view->GetPropertyNameStr(0x1234, name);
        CHECK(std::string(name.ToChar()) == "0x00001234");
        cRZString help;
        CHECK(view->GetHelpText(0x100, help) && std::string(help.ToChar()) == "Alpha help");
        CHECK(!view->GetHelpText(0x1234, help) && std::string(help.ToChar()).empty());
        std::map<std::uint8_t, cRZString> bytes;
        CHECK(view->GetPropertyValueMap(0x101, bytes) && bytes.size() == 2 && bytes.at(7).str() == "Seven");
        std::map<std::uint32_t, cRZString> words;
        CHECK(view->GetPropertyValueMap(0x107, words) && words.at(42).str() == "FortyTwo");
        CHECK(!view->GetPropertyValueMap(0x100, bytes));
        CHECK(!view->GetPropertyValueMap(0x100, words));
    }

    // ---- value formatting ------------------------------------------------------------------------------------------
    const auto format = [view](std::uint32_t id, const cIGZVariant& value) {
        cRZString out;
        view->GetPropertyValueStr(id, value, out);
        return std::string(out.ToChar());
    };
    {
        cRZVariant value(static_cast<std::uint32_t>(42));
        CHECK(format(0x100, value) == "value=42");                    // custom printf format
        cRZVariant number(static_cast<std::uint8_t>(0x2a));
        CHECK(format(0x111, number) == "0x2A");                       // built-in 0x%02X
        cRZVariant text(cRZString("hello"));
        CHECK(format(0x102, text) == "hello");
        CHECK(format(0x10b, text) == "<hello>");                      // custom format for a string
        cRZVariant big;
        big.SetValSint64(0x100000005LL);
        CHECK(format(0x103, big) == "5");                             // quirk: %d truncates sint64
        cRZVariant negative;
        negative.SetValSint64(-5);
        CHECK(format(0x103, negative) == "-5");
        cRZVariant truth;
        truth.SetValBool(true);
        CHECK(format(0x110, truth) == "True");
        cRZVariant lie;
        lie.SetValBool(false);
        CHECK(format(0x110, lie) == "False");
        cRZVariant real(1.5f);
        CHECK(format(0x112, real) == "1.500000");
        cRZVariant uint64Value;
        uint64Value.SetValUint64(7);
        CHECK(format(0x100, uint64Value) == "???");                   // UINT64 has no display case
        const std::uint8_t bytes[3] = {7, 9, 5};
        cRZVariant byteArray;
        byteArray.RefUint8(bytes, 3);
        CHECK(format(0x101, byteArray) == "Seven, Nine, ???");        // uint8 map array with a missing value
        cRZVariant seven(static_cast<std::uint8_t>(7));
        CHECK(format(0x101, seven) == "Seven");
        cRZVariant nine(static_cast<std::uint8_t>(9));
        CHECK(format(0x101, nine) == "Nine");
        cRZVariant missingByte(static_cast<std::uint8_t>(5));
        CHECK(format(0x101, missingByte) == "???");
        const std::uint32_t words[2] = {42, 1};
        cRZVariant wordArray;
        wordArray.RefUint32(words, 2);
        CHECK(format(0x107, wordArray) == "FortyTwo, ???");
        const bool flags[3] = {false, true, true};
        cRZVariant boolArray;
        boolArray.RefBool(flags, 3);
        CHECK(format(0x110, boolArray) == "False, True, True");
        DummyUnknown object;
        object.AddRef();
        cRZVariant unknownValue;
        unknownValue.SetValUnknown(&object);
        CHECK(format(0x100, unknownValue).rfind("Unk. ptr: ", 0) == 0);
        CHECK(object.refs >= 1);
    }

    // ---- engine-backed displays ------------------------------------------------------------------------------------
    {
        auto* resource = new cRZString("res-string");
        resource->AddRef();
        host.stringResource = resource;
        const std::uint32_t key[3] = {0x1111, 0x2222, 0x3333};
        cRZVariant value;
        value.RefUint32(key, 3);
        CHECK(format(0x10d, value) == "res-string");
        CHECK(host.resourceKey.group == 0x1111 && host.resourceKey.type == 0x2222 && host.resourceKey.instance == 0x3333);
        resource->Release();
        host.stringResource = nullptr;
        CHECK(format(0x10d, value) == "Invalid String Res key: G:00001111 T:00002222 I:00003333");
    }
    {
        auto* holder = new cSCPropertyHolder();
        holder->AddRef();
        holder->AddProperty(kPropertyViewDebugNameProperty, cRZString("DebugName"), false);
        host.exemplarHolder = holder;
        auto* exemplar = new DummyUnknown();
        exemplar->AddRef();
        host.exemplarResource = exemplar;
        const std::uint32_t key[3] = {1, 2, 3};
        cRZVariant value;
        value.RefUint32(key, 3);
        CHECK(format(0x10e, value) == "DebugName");
        holder->RemoveProperty(kPropertyViewDebugNameProperty, false);
        CHECK(format(0x10e, value) == "Missing Debug Name Exemplar Res Key: G:00000001 T:00000002 I:00000003");
        delete exemplar;
        host.exemplarResource = nullptr;
        holder->Release();
        host.exemplarHolder = nullptr;
    }
    {
        auto* directory = new FakeDirectorySegment();
        directory->fileName = "abcdefgh.txt";   // 12 characters, starts with a letter -> last 3
        host.directory = directory;
        const std::uint32_t key[3] = {4, 5, 6};
        cRZVariant value;
        value.RefUint32(key, 3);
        CHECK(format(0x10f, value) == "txt");
        CHECK(host.directorySegment == 4);
        CHECK(directory->lastKey.type == 5 && directory->lastKey.group == 4 && directory->lastKey.instance == 6);
        directory->fileName = "xy";
        CHECK(format(0x10f, value) == "xy");
        directory->fileName.clear();
        CHECK(format(0x10f, value) == "Missing Res Key: G:00000004 T:00000005 I:00000006");
        host.directory = nullptr;
        delete directory;
        CHECK(format(0x10f, value).empty());   // no segment: the original leaves the string untouched
    }
    {
        const std::uint32_t value = 3;
        cRZVariant variant(value);
        CHECK(format(0x10c, variant) == "custom:268");   // 0x10c = 268, through the property customizer
        CHECK(host.customizers.size() == 1);
        delete host.customizers.back();
        host.customizers.clear();
    }

    // ---- value editors ---------------------------------------------------------------------------------------------
    {
        cISCValueEditView* editor = nullptr;
        CHECK(view->GetValueEditor(0x100, nullptr, kPropertyViewValueEditorIID, reinterpret_cast<void**>(&editor)));
        CHECK(editor != nullptr && editor->AsIGZWin() != nullptr);
        CHECK(static_cast<FakeEditor*>(editor)->propertyId == 0x100);
        CHECK(static_cast<FakeEditor*>(editor)->value_.GetType() == cIGZVariant::kTypeUint32);
        CHECK(static_cast<FakeEditor*>(editor)->value_.GetValUint32() == 42);
        editor->Release();
        CHECK(host.editorClasses.back() == 0x11111111);

        cRZVariant supplied(static_cast<std::uint32_t>(5));
        editor = nullptr;
        CHECK(view->GetValueEditor(0x100, &supplied, kPropertyViewValueEditorIID, reinterpret_cast<void**>(&editor)));
        CHECK(static_cast<FakeEditor*>(editor)->value_.GetValUint32() == 5);
        editor->Release();

        editor = nullptr;   // res-key-table editor: it gets the default map's first value
        CHECK(view->GetValueEditor(0x108, nullptr, kPropertyViewValueEditorIID, reinterpret_cast<void**>(&editor)));
        auto* keyEditor = static_cast<FakeEditor*>(editor);
        CHECK(keyEditor->hasKey && keyEditor->propertyId == 0x108);
        CHECK(keyEditor->keyPropertyId == 0x108);
        CHECK(keyEditor->defaultKey.type == 1 && keyEditor->defaultKey.group == 3 && keyEditor->defaultKey.instance == 2);
        editor->Release();
        CHECK(host.editorClasses.back() == 0xa7847d6a);

        editor = nullptr;
        CHECK(view->GetValueEditor(0x103, nullptr, kPropertyViewValueEditorIID, reinterpret_cast<void**>(&editor)));
        CHECK(static_cast<FakeEditor*>(editor)->value_.GetType() == cIGZVariant::kTypeSint64);
        CHECK(static_cast<FakeEditor*>(editor)->value_.GetValSint64() == -5);
        editor->Release();

        editor = nullptr;
        CHECK(!view->GetValueEditor(0x999, nullptr, kPropertyViewValueEditorIID, reinterpret_cast<void**>(&editor)));
        CHECK(editor == nullptr);
    }

    // ---- window ----------------------------------------------------------------------------------------------------
    auto* parent = new cGZWin(&mgr);
    parent->AddRef();
    parent->SetArea(0, 0, 800, 600);
    view->SetParentWin(parent);
    CHECK(view->Init());
    CHECK(view->GetW() == 400 && view->GetH() == 450);
    CHECK(view->GetL() == 200 && view->GetT() == 75);   // centred in the 800x600 parent
    CHECK(view->GetFlag(kWinFlag800) && view->GetFlag(kWinPrivateBuffer));
    CHECK(!view->propertyIds().empty());

    // background
    CHECK(host.flatRects.size() == 1);
    if (!host.flatRects.empty()) {
        FakeFlatRect* background = host.flatRects[0];
        CHECK(background->GetOutlineFlags() == 0x0f);
        CHECK(background->GetSizeMode() == 0);
        CHECK(background->outlineColor == 0xff000000);
        CHECK(background->GetFillColor() == 0xffe0e0e0);
        CHECK(background->GetW() == 400 && background->GetH() == 450);
    }

    // Done / Cancel
    CHECK(host.buttons.size() == 2);
    if (host.buttons.size() == 2) {
        FakeButton* done = host.buttons[0];
        FakeButton* cancel = host.buttons[1];
        CHECK(done->GetID() == kPropertyViewDoneId && cancel->GetID() == kPropertyViewCancelId);
        CHECK(done->fitted && cancel->fitted);
        CHECK(done->tipText == "Done" && cancel->tipText == "Cancel");
        CHECK(done->GetBtnFlag(gz::kBtnTipEnabled) && !done->GetBtnFlag(0x200) && !done->GetBtnFlag(0x400));
        CHECK(done->GetL() == 355 && done->GetT() == 425);
        CHECK(cancel->GetL() == 310 && cancel->GetT() == 425);
        CHECK(std::string(done->GetCaption().ToChar()) == "Done");
    }
    CHECK(host.bitmapGroup == kPropertyViewDoneBitmapGroup);
    CHECK(host.bitmapInstance == kPropertyViewCancelBitmapInstance);   // the last bitmap asked for is Cancel's

    // help label and "Property ID" label
    cIGZWin* helpWindow = view->GetChildWindowFromID(kPropertyViewHelpLabelId);
    CHECK(helpWindow != nullptr);
    if (helpWindow) {
        CHECK(std::string(helpWindow->GetCaption().ToChar()) == "<help>");
        CHECK(helpWindow->GetL() == 5 && helpWindow->GetT() == 425 && helpWindow->GetR() == 310 && helpWindow->GetB() == 445);
    }
    CHECK(host.texts.size() == 2);
    if (host.texts.size() == 2) {
        FakeText* propertyLabel = host.texts[1];
        CHECK(std::string(propertyLabel->GetCaption().ToChar()) == "Property ID");
        CHECK(propertyLabel->GetL() == (400 * 2) / 5 - 11 * 8 && propertyLabel->GetT() == 10);
        CHECK(propertyLabel->textColor == 0xff000000);
        CHECK(propertyLabel->font != nullptr && host.labelFontStyle == kPropertyViewDefaultLabelFont);
        CHECK(propertyLabel->fitted);
    }

    // combo: one row per property, duplicate names inserted before the first equal row
    CHECK(host.combos.size() == 1);
    if (!host.combos.empty()) {
        FakeCombo* combo = host.combos[0];
        CHECK(combo->strings.size() == 19);
        CHECK(combo->strings.size() >= 2 && combo->strings[0] == "Alpha" && combo->strings[1] == "Alpha");
        CHECK(!combo->editable);
        CHECK(combo->GetID() == kPropertyViewComboId);
        CHECK(combo->GetL() == 5 && combo->GetR() == 395 && combo->GetT() == 10 && combo->GetB() == 0x1c);
        CHECK(combo->GetSelection() == 0);
        CHECK(combo->GetFontColor(gz::cIGZWinCombo::kColorText) == 0xff000000);
        CHECK(combo->GetFontColor(gz::cIGZWinCombo::kColorSelected) == 0xffffffff);
        CHECK(combo->highlight == 0xff000080);
        CHECK(view->propertyIds().size() == 19);
        CHECK(view->propertyIds()[0] == 0x10a && view->propertyIds()[1] == 0x100);
    }

    // editors: every included property, the filter mask drops 0x106
    CHECK(view->views().size() == 18);
    CHECK(view->views().count(0x100) == 1);
    CHECK(view->views().count(0x106) == 0);
    CHECK(view->currentView() == nullptr);

    // combo -> editor
    CHECK(view->GZOnCommand(kPropertyViewMsgComboChanged, kPropertyViewComboId));
    CHECK(view->currentView() != nullptr);
    FakeEditor* firstEditor = dynamic_cast<FakeEditor*>(view->currentView());
    CHECK(firstEditor != nullptr && firstEditor->propertyId == 0x10a);
    CHECK(firstEditor && firstEditor->GetFlag(kWinVisible));
    if (helpWindow) CHECK(std::string(helpWindow->GetCaption().ToChar()) == "Alpha help 2");

    // a property whose filter excludes it has no editor: the view pointer is cleared but the help still updates
    if (!host.combos.empty()) host.combos[0]->SetSelection(7, false);   // "Excluded" (0x106, filter 0x8)
    CHECK(view->GZOnCommand(kPropertyViewMsgComboChanged, kPropertyViewComboId));
    CHECK(view->currentView() == nullptr);
    if (helpWindow) CHECK(std::string(helpWindow->GetCaption().ToChar()) == "Excluded help");

    // select the second "Alpha" row (property 0x100)
    if (!host.combos.empty()) host.combos[0]->SetSelection(1, false);
    CHECK(view->GZOnCommand(kPropertyViewMsgComboChanged, kPropertyViewComboId));
    FakeEditor* secondEditor = dynamic_cast<FakeEditor*>(view->currentView());
    CHECK(secondEditor != nullptr && secondEditor->propertyId == 0x100);
    CHECK(secondEditor && secondEditor->GetFlag(kWinVisible));
    CHECK(firstEditor && !firstEditor->GetFlag(kWinVisible));
    if (helpWindow) CHECK(std::string(helpWindow->GetCaption().ToChar()) == "Alpha help");

    // Done: the editor value goes into the property, then the window posts its own deletion to the parent
    if (secondEditor) secondEditor->SetValue(cRZVariant(static_cast<std::uint32_t>(99)));
    mgr.posted.clear();
    CHECK(!view->GZOnCommand(kPropertyViewMsgCommandSender, kPropertyViewDoneId));
    CHECK(property.GetPropertyID() == 0x100);
    CHECK(property.GetPropertyValue()->GetType() == cIGZVariant::kTypeUint32);
    CHECK(property.GetPropertyValue()->GetValUint32() == 99);
    CHECK(mgr.posted.size() == 1);
    if (!mgr.posted.empty()) {
        CHECK(mgr.posted[0].type == kPropertyViewDeleteChildMessage);
        CHECK(mgr.posted[0].param1 == static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(view)));
    }
    // Cancel with the "no EndDialog" flag takes the second KillSelf path
    mgr.dialogs.clear();
    view->SetFlag(kPropertyViewFlagNoEndDialog, true);
    view->doButtonPress(kPropertyViewCancelId);
    CHECK(mgr.dialogs.size() == 1);
    if (!mgr.dialogs.empty()) {
        CHECK(mgr.dialogs[0].code == kPropertyViewEndDialogCode);
        CHECK(mgr.dialogs[0].window == view);
        CHECK(mgr.dialogs[0].result == 1);
    }
    view->SetFlag(kPropertyViewFlagNoEndDialog, false);
    mgr.dialogs.clear();
    view->doButtonPress(0xdeadbeef);   // unknown button: nothing happens
    CHECK(mgr.dialogs.empty() && mgr.posted.size() == 1);

    // key messages are forwarded to the notification target
    {
        auto* target = new Target(&mgr);
        target->AddRef();
        view->SetNotificationTarget(target);
        view->GZOnKeyDown(65, 2);
        view->GZOnKeyUp(66, 3);
        CHECK(target->got.size() == 2);
        if (target->got.size() == 2) {
            CHECK(target->got[0].type == kGZMsgKeyDown && target->got[0].param1 == 65 && target->got[0].param2 == 2);
            CHECK(target->got[1].type == kGZMsgKeyUp && target->got[1].param1 == 66 && target->got[1].param2 == 3);
        }
        CHECK(view->GZOnMouseDownL(1, 2, 0));
        CHECK(view->GZPaint());
        view->SetNotificationTarget(parent);
        target->Release();
    }

    // ---- helper factories (used by the value entry views) ----------------------------------------------------------
    view->createLineInput(3, 4, parent, 0x1234, cRZString("hello"), 0xff00ff);
    CHECK(host.textEdit != nullptr);
    if (host.textEdit) {
        FakeTextEdit* edit = host.textEdit;
        CHECK(edit->GetID() == 0x1234);
        CHECK(edit->GetL() == 3 && edit->GetT() == 4 && edit->GetR() == 790 && edit->GetB() == 22);
        CHECK(edit->text == "hello" && !edit->notified);
        CHECK((edit->flags & 1) && (edit->flags & 0x200));
        CHECK(edit->fontColors[0] == 0xff00ff);
        CHECK(edit->fontColors[4] == 0xffffffff);
        CHECK(edit->fontColors[5] == 0xff000080);
        CHECK(edit->caret == 0xff00ff && edit->highlight == 0xff000080);
        CHECK(edit->fonts == 1 && host.textFontStyle == kPropertyViewDefaultLabelFont);
        CHECK(parent->GetChildWindowFromID(0x1234) == edit);
    }
    view->createSpinner(10, 20, parent, 0x99, 0, 10, 5);
    CHECK(host.spinners.size() == 1);
    if (!host.spinners.empty()) {
        FakeSpinner* spinner = host.spinners[0];
        CHECK(host.spinnerArgs.size() == 5 && host.spinnerArgs[0] == 2 && host.spinnerArgs[1] == 0 &&
              host.spinnerArgs[2] == 10 && host.spinnerArgs[3] == 1 && host.spinnerArgs[4] == 5);
        CHECK(spinner->flags == 3);
        CHECK(spinner->GetL() == 10 && spinner->GetT() == 20 && spinner->GetR() == 58 && spinner->GetB() == 40);
        CHECK(spinner->GetFillColor() == 0xffe0e0e0);
        CHECK(parent->GetChildWindowFromID(0x99) == spinner);
    }

    // ---- shutdown --------------------------------------------------------------------------------------------------
    view->Shutdown();
    CHECK(view->views().empty());
    CHECK(view->propertyIds().empty());
    CHECK(view->currentView() == nullptr);
    view->Release();
    view = nullptr;
    parent->Release();
    parent = nullptr;

    // ---- processINI keeps stale maps and uses the data directory when no path is set ---------------------------------
    {
        WriteTextFile(iniPath,
                      "[Aliases]\nNewAlias = 0x5\n[ExemplarFilters]\n0x2 = 0x9\n[Properties]\n"
                      "0x120 = SINT32, 3, 4, 0x1, \"Tau\", \"Tau help\", NewAlias, 0, 0\n"
                      "0x121 = FLOAT32, 2, 1.5, 0x1, \"Upsilon\", \"Upsilon help\", NewAlias, 0, 0\n"
                      "0x122 = UINT16, 2, 0x10, 0x1, \"Phi\", \"Phi help\", NewAlias, 0, 0\n"
                      "0x123 = BOOL, 2, T, 0x1, \"Chi\", \"Chi help\", NewAlias, 0, 0\n");
        auto* second = new cSCPropertyView(property, 2);
        second->AddRef();
        second->processINI();
        CHECK(cSCPropertyView::Aliases().size() == 1);
        CHECK(cSCPropertyView::Aliases().count(cRZString("NewAlias")) == 1);
        CHECK(cSCPropertyView::Aliases().count(cRZString("Editor")) == 0);
        CHECK(cSCPropertyView::PropertyInfos().size() == 4);
        {
            // array defaults: only as many values as the field can hold - the split helper leaves the first
            // (count - 1) elements 0 when the text has no comma, and the last one gets the whole text (quirk)
            const auto& tau = cSCPropertyView::PropertyInfos().at(0x120);
            CHECK(tau.editorClassId == 0x5);   // resolved through the alias map
            CHECK(tau.defaultValue.GetType() == (cIGZVariant::kTypeArrayFlag | cIGZVariant::kTypeSint32));
            CHECK(tau.defaultValue.GetCount() == 3);
            CHECK(tau.defaultValue.RefSint32()[0] == 0 && tau.defaultValue.RefSint32()[1] == 0 &&
                  tau.defaultValue.RefSint32()[2] == 4);
            const auto& upsilon = cSCPropertyView::PropertyInfos().at(0x121);
            CHECK(upsilon.defaultValue.GetCount() == 2);
            CHECK(upsilon.defaultValue.RefFloat32()[0] == 0.0f && upsilon.defaultValue.RefFloat32()[1] == 1.5f);
            const auto& phi = cSCPropertyView::PropertyInfos().at(0x122);
            CHECK(phi.defaultValue.RefUint16()[0] == 0 && phi.defaultValue.RefUint16()[1] == 0x10);
            const auto& chi = cSCPropertyView::PropertyInfos().at(0x123);
            CHECK(!chi.defaultValue.RefBool()[0] && chi.defaultValue.RefBool()[1]);
        }
        CHECK(cSCPropertyView::ExemplarFilters().count(1) == 1);   // stale (quirk)
        CHECK(cSCPropertyView::ExemplarFilters().count(2) == 1);
        CHECK(cSCPropertyView::ResKeyTableDefaults().count(0x108) == 1);   // stale too
        auto* filtered = new cSCPropertyView(property, 2);
        filtered->AddRef();
        CHECK(filtered->exemplarFilter() == 9);
        filtered->Release();

        cSCPropertyView::SetSettingsIniFilePath(cRZString());
        WriteTextFile(root + "/Sys/Ingred.ini", "[Aliases]\nFallbackAlias = 0x7\n");
        second->processINI();
        CHECK(cSCPropertyView::Aliases().size() == 1);
        CHECK(cSCPropertyView::Aliases().count(cRZString("FallbackAlias")) == 1);
        second->Release();
    }

    // filters: an absent exemplar filter maps to 0xffffffff
    {
        auto* third = new cSCPropertyView(property, 0x77);
        third->AddRef();
        CHECK(third->exemplarFilter() == 0xffffffff);
        third->Release();
    }

    SetPropertyViewHost(nullptr);
    cSCPropertyView::ResetStaticsForTest();
    std::filesystem::remove_all(root);

    if (failures == 0) {
        std::printf("sc4_propertyview: all checks passed\n");
        return 0;
    }
    std::printf("sc4_propertyview: %d failure(s)\n", failures);
    return 1;
}
