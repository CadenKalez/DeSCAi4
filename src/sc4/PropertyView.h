// cSCPropertyView (42 functions at 0x10026799c..0x10026f628, plus 12 file-local helpers in the same translation unit) —
// the generic exemplar property editor window.
//
// It shows one property of a cISCProperty at a time: a background flat rect, a combo box with every property name from the
// "Properties" section of Sys/Ingred.ini, the value editor for the selected property (created from the property's editor
// class id through GZCOM, iid 0x06c5ff7b), a help label and Done / Cancel buttons. "Done" copies the current editor value
// back into the property (SetPropertyValue) and closes the window; "Cancel" just closes it.
//
// Static INI registry (one per process, original addresses in comments):
//   [Aliases]                      name = number                       -> mAliases                 (0x100b247b0)
//   [ExemplarFilters]              exemplar filter = filter list       -> mExemplarFilters         (0x100b247c8)
//   [Properties]                   id = type, count, default, filter, name, help, editor, display, aux
//                                                                      -> mPropertyInfoList        (0x100b24798)
//   [Uint8ValueMap_0x<id:016X>]    value = text                        -> mUint8ValueMaps          (0x100b24768)
//   [Uint32ValueMap_0x<id:016X>]   value = text                        -> mUint32ValueMaps         (0x100b24780)
//   [ResKeyTableEditorDefaults]    property id = 4 numbers             -> mResKeyTableEditorsDefaultMap (0x100b247e0)
// The filter tokens are separated by spaces and OR'ed; a token is a number or an alias. The default value is parsed with
// the comma separated array syntax of the original (count values, a count of 0 means "variable length array" and stores a
// single uint32 0). Section callbacks and their helpers live in PropertyView.cpp.
//
// Object layout (the 0x150-byte cGZWin, then):
//   +0x150 exemplar filter (the ctor maps the argument through mExemplarFilters: exact key, else 0xffffffff)
//   +0x158 std::vector<uint32_t> mPropertyIds        (combo row -> property id; +0x158/+0x160/+0x168 = begin/end/cap)
//   +0x170 std::map<uint32_t, cRZAutoRefCount<cISCValueEditView> > mViews
//   +0x188 cISCValueEditView* mCurrentView           (borrowed from mViews)
//   +0x190 embedded cSCProperty (constructed with id 0, unused by this class)
//   +0x1b8 cISCProperty* mProperty                  (borrowed; where Done writes the value)
//   +0x1c0 end of object
//
// Known quirks kept (asserted by scripts/tests/sc4_propertyview_test.cpp):
//   * processINI clears mPropertyInfoList, mAliases, mUint8ValueMaps and mUint32ValueMaps but NOT mExemplarFilters and
//     mResKeyTableEditorsDefaultMap, so stale entries survive a re-read of the file.
//   * getVariantType knows no name for UINT64 and CHAR: both parse as 0 (unknown).
//   * getDefaultValue supports the types 1..7, 9, 10 and STRING only; UINT64/CHAR defaults fail. The element count is
//     used as its absolute value.
//   * GetPropertyValueStr: SINT64 values and arrays are printed with "%d" (32-bit truncation); UINT64, RZUnicodeChar,
//     RZChar, VoidPtr and RZChar arrays print "???" (the char type does print "%c"); a bool array prints each element as
//     True/False; an unknown value prints its pointer as "Unk. ptr: %0x08X".
//   * getResource reads the key array as {group, type, instance} (the first two are swapped on the way into
//     cGZPersistResourceKey).
//   * getFilenameFromKey outputs the last (length - 9) characters when the file name is at least 10 characters long and
//     starts with a letter; otherwise the whole name.
//   * IsVariableLengthArray: true iff the editor class id is not 0xe8dd9d13 and the element count is < 1.
//   * KillSelf: window flag 0x1000 set -> EndDialog(0xf, this, result); otherwise a "delete child" (type 1) message is
//     posted to the parent with this window as its payload.
//   * Init only centers itself in the parent when its own width or height is 0; duplicate property names are inserted
//     before the first equal row (the combo keeps one row per property, not per name).
//   * GZOnCommand / doButtonPress look the combo up by id 0xfa12b77f and QueryInterface 0x412ce496 on it every time.
//   * Port deviation: Shutdown also nulls mCurrentView after clearing mViews (the original leaves the borrowed pointer
//     dangling, which no code reads afterwards). GZPaint skips a null draw buffer instead of dereferencing it.
//
// Seams: every unrecovered neighbour goes through IPropertyViewHost (GZCOM object creation, the window control manager,
// the persistence resource manager, the data directory). The two helper factories build controls the property view itself
// never uses (the value entry views call them), so they talk to the narrow IPropertyTextEdit / IPropertySpinner views.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "gz/Win.h"
#include "gz/WinBtn.h"
#include "gz/WinCombo.h"
#include "gz/WinFlatRect.h"
#include "gz/WinText.h"
#include "gz/WinFont.h"
#include "gz/cIGZSerializable.h"
#include "gz/cGZPersistResourceKey.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"
#include "sc4/ColorCustomizer.h"
#include "sc4/SCEntryView.h"
#include "sc4/SCProperty.h"
#include "sc4/SCPropertyHelp.h"

class cIGZFont;
class cIGZPersistDBSegmentDirectoryFiles;

namespace sc4 {

// cISCValueEditView — what a value entry view is to the property view: the cISCStandardTypeEntryView interface
// (iid 0x06c5ff7b, +0x150 of the entry view) plus vslot 0x38 (GetValue). 0x38 is not part of the recovered
// cISCStandardTypeEntryView header, so it is declared here; the class owns it below the shown slots.
class cISCValueEditView : public cISCStandardTypeEntryView {
public:
    virtual void GetValue(cIGZVariant& out) = 0;          // vslot 0x38 (assignValuesToProperty @ 0x10026f528)
};

// cISCResKeyTableEntryView (iid 0x787ecc4) — the second interface of cSCResKeyTableEntryView (vtable 0x100acbae0, the
// +8 subobject). These are exactly the two calls GetValueEditor makes: SetKeyPropDefId(property id) then
// SetDefaultKey(mResKeyTableEditorsDefaultMap[property id] as a resource key).
class cISCResKeyTableEntryView : public cIGZUnknown {
public:
    virtual void SetDefaultKey(const cGZPersistResourceKey& key) = 0;   // vslot 0x18
    virtual void SetKeyPropDefId(std::uint32_t id) = 0;                 // vslot 0x20
};

// A narrow view of cGZWinTextEdit (class 0x231a1c58, iid 0x231a1c57) covering only the calls createLineInput makes.
// Vslot numbers are those of gz::cIGZWinTextEdit (WinTextEdit.h).
class IPropertyTextEdit : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                     // [5]  0x28
    virtual void SetFont(cIGZFont* font) = 0;                            // [9]  0x48
    virtual void SetFontColor(int index, std::uint32_t native) = 0;      // [13] 0x68
    virtual void SetCaretColor(std::uint32_t native) = 0;                // [18] 0x90
    virtual void SetHighlightFillColor(std::uint32_t native) = 0;        // [23] 0xb8
    virtual void SetText(const cIGZString& text, bool notify) = 0;       // [49] 0x188
    virtual void SetFlag(std::uint32_t flag) = 0;                        // [76] 0x260
};

// A narrow view of cGZWinSpinner (iid 0x612ce0c3) covering only the calls createSpinner makes.
class IPropertySpinner : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                     // [3] 0x18
    virtual void SetSpinnerFlag(std::uint32_t flag, bool on) = 0;        // [6] 0x30
};

// Every neighbour the property view cannot use directly. Each method mirrors one original call; the slot / address is in
// the comment. Implementations are supplied by the app (real engine) and by the test (fakes).
class IPropertyViewHost {
public:
    virtual ~IPropertyViewHost() = default;

    // The window manager cGZWin's constructor takes (the original base constructor asks the global service).
    virtual cIGZWinMgr* WindowManager() = 0;

    // GZCOM() vslot 0x18 (0x18 = GetClassObject / CreateInstance): a new object of `classId` exposing `iid`, one
    // reference owned by the caller, null on failure.
    virtual cIGZUnknown* CreateObject(std::uint32_t classId, std::uint32_t iid) = 0;

    // cSLBuffer({type 0, group, instance}, colour type 1).Load(): the bitmap, one counted reference, or null.
    virtual cIGZBuffer* LoadBitmap(std::uint32_t group, std::uint32_t instance) = 0;

    // RZ::WCM() vslot 0x48: CreateButton(style 2, id, caption, image, null area, 0). Counted reference.
    virtual gz::cIGZWinBtn* CreateButton(std::uint32_t id, const cRZString& caption, cIGZBuffer* image) = 0;

    // RZ::WCM() vslot 0x28: the font registered under a style id (borrowed, null when unknown).
    virtual gz::IWinFont* LabelFont(std::uint32_t styleId) = 0;
    // RZ::WCM() vslot 0x28 as the text edit takes it (borrowed).
    virtual cIGZFont* TextEditFont(std::uint32_t styleId) = 0;

    // RZ::WCM() vslot 0x90: CreateSpinner(id, type, low, high, step, value). Counted reference.
    virtual IPropertySpinner* CreateSpinner(std::uint32_t id, int type, int low, int high, int step, int value) = 0;

    // GZCOM() class 0x231a1c58 / iid 0x231a1c57 (cGZWinTextEdit) seen through the narrow IPropertyTextEdit view,
    // one counted reference.
    virtual IPropertyTextEdit* CreateTextEdit() = 0;

    // RZ::DDir() @ 0x100127a12: the data directory (the original appends "Sys/Ingred.ini" to it).
    virtual void DataDir(cIGZString& out) = 0;

    // SL::spRM vslot 0x18 GetResource(key, iid, &out, 0, 0): one counted reference, null on failure.
    virtual cIGZUnknown* GetResource(const cGZPersistResourceKey& key, std::uint32_t iid) = 0;

    // cISCResExemplar vslot 0x30: the exemplar's property holder (borrowed).
    virtual cISCPropertyHolder* ExemplarProperties(cIGZUnknown* exemplar) = 0;

    // SL::spRM vslot 0x100 FindDBSegment(segmentId, &segment) followed by QueryInterface(0x856c8cf7) on the segment:
    // one counted reference to the directory interface, null when the segment is missing or not a directory segment.
    virtual cIGZPersistDBSegmentDirectoryFiles* FindDirectorySegment(std::uint32_t segmentId) = 0;
};
IPropertyViewHost* PropertyViewHost();
void SetPropertyViewHost(IPropertyViewHost* host);

// One [Properties] entry. Original offsets are from the mapped value (std::map node + 0x28):
//   +0x00 property id            +0x08 variant type          +0x0c element count        +0x10 filter
//   +0x18 name (cRZString)      +0x40 help text (cRZString)  +0x68 editor class id      +0x6c display class id
//   +0x70 default text (cRZString)                          +0x98 value-map owner id   +0xa0 aux number
//   +0xa8 default value (cRZVariant)
struct PropertyViewInfo {
    std::uint64_t propertyId = 0;              // +0x00
    std::uint32_t variantType = 0;             // +0x08
    std::uint32_t elementCount = 1;            // +0x0c
    std::uint32_t filter = 0;                  // +0x10
    cRZString name;                            // +0x18
    cRZString helpText;                        // +0x40
    std::uint32_t editorClassId = 0;           // +0x68
    std::uint32_t displayClassId = 0;          // +0x6c
    cRZString defaultText;                     // +0x70
    std::uint64_t valueMapOwnerId = 0;         // +0x98
    std::uint32_t auxValue = 0;                // +0xa0
    cRZVariant defaultValue;                   // +0xa8
};

// One [ResKeyTableEditorDefaults] value: the four uint32 of the original, stored in the section callback's order.
struct PropertyViewResKeyTableInfo {
    std::uint32_t values[4] = {0, 0, 0, 0};
};

// The original maps are std::map<cRZString, ...>; cRZString has no operator<, so the recovered class needs the same
// byte-wise ordering explicitly (the binary's std::less<cRZString> compares the character data).
struct PropertyViewStringLess {
    bool operator()(const cRZString& a, const cRZString& b) const { return std::strcmp(a.ToChar(), b.ToChar()) < 0; }
};

using PropertyViewInfoMap = std::map<std::uint64_t, PropertyViewInfo>;
using PropertyViewAliasMap = std::map<cRZString, std::uint32_t, PropertyViewStringLess>;
using PropertyViewExemplarFilterMap = std::map<std::uint32_t, std::uint32_t>;
using PropertyViewUint8MapTable = std::map<std::uint64_t, std::map<std::uint8_t, cRZString>>;
using PropertyViewUint32MapTable = std::map<std::uint64_t, std::map<std::uint32_t, cRZString>>;
using PropertyViewResKeyTableInfoMap = std::map<std::uint32_t, PropertyViewResKeyTableInfo>;

// Display class ids the value formatter knows (the original compares the +0x6c word to them).
constexpr std::uint32_t kPropertyViewDisplayFormat = 0x073b6fd3;      // +0x70 text is a printf format
constexpr std::uint32_t kPropertyViewDisplayUint8Map = 0x67c3501e;     // mUint8ValueMaps
constexpr std::uint32_t kPropertyViewDisplayUint32Map = 0xa73a2f21;    // mUint32ValueMaps (0xa73a2f21 = -0x58c5d0df)
constexpr std::uint32_t kPropertyViewDisplayStringRes = 0xc73b723f;    // getStringResource (0xc73b723f = -0x38c48dc1)
constexpr std::uint32_t kPropertyViewDisplayDebugName = 0xc77c3528;    // getDebugNameResource (0xc77c3528 = -0x3883cad8)
constexpr std::uint32_t kPropertyViewDisplayFilename = 0x68727736;     // getFilenameFromKey
constexpr std::uint32_t kPropertyViewNewValueClass = 0xe8dd9d13;       // value-map-editor marker checked by IsVariableLengthArray

// Window ids and message ids (original values).
constexpr std::uint32_t kPropertyViewDoneId = 0x24c93018;
constexpr std::uint32_t kPropertyViewCancelId = 0x24c93019;
constexpr std::uint32_t kPropertyViewComboId = 0xfa12b77f;
constexpr std::uint32_t kPropertyViewHelpLabelId = 0x87c30d17;
constexpr std::uint32_t kPropertyViewDoneBitmapGroup = 0x22dec92d;
constexpr std::uint32_t kPropertyViewDoneBitmapInstance = 0x2001;
constexpr std::uint32_t kPropertyViewCancelBitmapInstance = 0x2007;
constexpr std::uint32_t kPropertyViewMsgCommandSender = 0x287259f6;    // button activation (button id in param2)
constexpr std::uint32_t kPropertyViewMsgComboChanged = 0xe8711255;     // combo notification (combo id in param2)
constexpr std::uint32_t kPropertyViewEndDialogCode = 0xf;
constexpr std::uint32_t kPropertyViewDeleteChildMessage = 1;           // GZ message type 1
constexpr std::uint32_t kPropertyViewFlagNoEndDialog = 0x1000;         // KillSelf tests this window flag
constexpr std::uint32_t kPropertyViewDefaultWindowW = 400, kPropertyViewDefaultWindowH = 0x1c2;
constexpr std::uint32_t kPropertyViewDefaultLabelFont = 0xc8963c5c;    // bold button font
constexpr std::uint32_t kPropertyViewValueEditorIID = 0x06c5ff7b;
constexpr std::uint32_t kPropertyViewResKeyTableEntryViewIID = 0x0787ecc4;
constexpr std::uint32_t kPropertyViewCustomizerIID = 0x273b784c;
constexpr std::uint32_t kPropertyViewStringResIID = 0x089b7dc8;
constexpr std::uint32_t kPropertyViewExemplarIID = 0xa52160f5;
constexpr std::uint32_t kPropertyViewDebugNameProperty = 0xfae2411e;
constexpr std::uint32_t kPropertyViewSerializableIID = 0xe4fda3d4;   // cIGZSerializable (sc4/DBSegment.h)
constexpr std::uint32_t kPropertyViewFlatRectClass = 0xc2afa76e;
constexpr std::uint32_t kPropertyViewTextClass = 0x592;
constexpr std::uint32_t kPropertyViewComboClass = 0x59b;
constexpr std::uint32_t kPropertyViewTextEditClass = 0x231a1c58;
constexpr std::uint32_t kPropertyViewTextEditIID = 0x231a1c57;
constexpr std::uint32_t kPropertyViewResKeyTableEditorClasses[2] = {0xa7847d6a, 0x281f7625};

class cSCPropertyView : public cGZWin {
public:
    cSCPropertyView(cISCProperty& property, std::uint32_t exemplarFilter);      // @ 0x1002679c6 (+ thunk 0x100267b0e)
    ~cSCPropertyView() override;                                                 // @ 0x100267b18 (+ thunk 0x100267bcc, deleting 0x100267bd6)

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x100267c04, forwards to cGZWin
    std::uint32_t AddRef() override;                                // @ 0x100267c0e
    std::uint32_t Release() override;                               // @ 0x100267c18
    bool Init() override;                                           // @ 0x100267c22
    bool Shutdown() override;                                       // @ 0x1002693e4
    bool DoMessage(cGZMessage& message) override;                   // @ 0x10026945a, forwards to cGZWin
    bool GZPaint() override;                                        // @ 0x10026f20e
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;   // @ 0x100269464
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;     // @ 0x10026948a
    bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) override;     // @ 0x1002694b0
    bool GZOnCommand(std::uint32_t type, std::uint64_t param) override;      // @ 0x10026f266

    void processINI();                                             // @ 0x1002686a2
    void mapPropertiesToViews(int x, int y, int w, int h);         // @ 0x100268ea0
    void doButtonPress(std::uint32_t buttonId);                    // @ 0x10026f412
    void assignValuesToProperty(std::uint32_t propertyId);         // @ 0x10026f528
    void KillSelf(std::uint32_t result);                           // @ 0x10026f5c6

    // Value formatting / lookup helpers (public like the original private members; the entry views use them through
    // the class when the tool chain is complete).
    bool GetValueEditor(std::uint32_t propertyId, const cIGZVariant* value, std::uint32_t iid, void** out);  // 0x10026cb0c
    void GetPropertyNameStr(std::uint32_t propertyId, cRZString& out);                        // @ 0x10026cea8
    void GetPropertyValueStr(std::uint32_t propertyId, const cIGZVariant& value, cRZString& out);   // @ 0x10026cf26
    bool GetPropertyValueMap(std::uint32_t propertyId, std::map<std::uint8_t, cRZString>& out);      // @ 0x10026e97a
    bool GetPropertyValueMap(std::uint32_t propertyId, std::map<std::uint32_t, cRZString>& out);     // @ 0x10026eb56
    bool GetHelpText(std::uint32_t propertyId, cRZString& out);                               // @ 0x10026ed32
    bool IsVariableLengthArray(std::uint32_t propertyId);                                      // @ 0x10026eda4

    // Original private helpers, kept as members because they use the per-object state / statics.
    void createButton(gz::cIGZWinBtn*& out, std::uint32_t bitmapInstance, std::uint32_t bitmapGroup,
                      std::uint32_t buttonId, const cRZString& caption);                        // @ 0x100268bba
    void createLabel(gz::cIGZWinText*& out, const cRZString& caption, std::uint32_t textColor,
                     gz::IWinFont* font);                                                       // @ 0x100268d64
    void createLineInput(int x, int y, cIGZWin* parent, std::uint32_t id, const cRZString& text,
                         std::uint32_t color);                                                  // @ 0x10026ee9a
    void createSpinner(int x, int y, cIGZWin* parent, std::uint32_t id, int low, int high, int value);   // @ 0x10026f0fe
    bool getResource(const cIGZVariant& value, std::uint32_t iid, cIGZUnknown** out);          // @ 0x10026ee04
    void getStringResource(cRZString& out, const cIGZVariant& value);                          // @ 0x10026e5a4
    void getDebugNameResource(cRZString& out, const cIGZVariant& value);                       // @ 0x10026e64e
    void getFilenameFromKey(cRZString& out, const cIGZVariant& value);                         // @ 0x10026e754

    // -- [Properties] / [Aliases] / ... section callbacks (cRZIniFile::EnumCallback signature) --
    static void aliasesSectionCallback(const cRZString& key, const cRZString& value, void* context);         // 0x1002694b8
    static void propertiesSectionCallback(const cRZString& key, const cRZString& value, void* context);      // 0x10026bb10
    static void exemplarFiltersSectionCallback(const cRZString& key, const cRZString& value, void* context); // 0x10026c286
    static void Uint8ValueMapSectionCallback(const cRZString& key, const cRZString& value, void* context);   // 0x10026c346
    static void Uint32ValueMapSectionCallback(const cRZString& key, const cRZString& value, void* context);  // 0x10026c690
    static void ResKeyTableEditorDefaultSectionCallback(const cRZString& key, const cRZString& value, void* context);   // 0x10026c9ea
    static std::uint32_t convertStringToFilter(cRZString& text);                               // @ 0x10026b6d6
    static std::uint32_t getFilter(cRZString& text);                                           // @ 0x10026b746

    // -- static registry accessors (tests and the app's INI loader) --
    static const PropertyViewInfoMap& PropertyInfos();
    static const PropertyViewAliasMap& Aliases();
    static const PropertyViewExemplarFilterMap& ExemplarFilters();
    static const PropertyViewUint8MapTable& Uint8ValueMaps();
    static const PropertyViewUint32MapTable& Uint32ValueMaps();
    static const PropertyViewResKeyTableInfoMap& ResKeyTableDefaults();
    static bool INIProcessed();
    static void SetINIProcessed(bool processed);
    // Original global cRZString at 0x100b24800 (written by the app before the first window opens).
    static void SetSettingsIniFilePath(const cRZString& path);
    static const cRZString& SettingsIniFilePath();
    static void ResetStaticsForTest();

    // The original object's fields, with the original names where they are known.
    std::uint32_t exemplarFilter() const { return filter_; }             // +0x150
    const std::vector<std::uint32_t>& propertyIds() const { return propertyIds_; }   // +0x158
    const std::map<std::uint32_t, cRZAutoRefCount<cISCValueEditView>>& views() const { return views_; }   // +0x170
    cISCValueEditView* currentView() const { return currentView_; }      // +0x188

private:
    std::uint32_t filter_ = 0xffffffff;                              // +0x150
    std::vector<std::uint32_t> propertyIds_;                        // +0x158
    std::map<std::uint32_t, cRZAutoRefCount<cISCValueEditView>> views_;   // +0x170
    cISCValueEditView* currentView_ = nullptr;                      // +0x188
    cSCProperty valueHolder_;                                       // +0x190
    cISCProperty* property_;                                        // +0x1b8 (borrowed)
};

}  // namespace sc4
