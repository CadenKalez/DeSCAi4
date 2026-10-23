// cGZLegacyControl — the adapter between a scripted window description and a live GZ control window.
// 81 symbols, 0x100530b9c..0x1005522e8. It is the "legacy" (SC3/Win32-style) control object the UI script
// service builds for every element of a window script: SetParameters turns the element's property list
// (cGZArray<std::pair<u32, cIGZVariant*>>) into a control window created through GZCOM, applies the generic
// and the per-class properties to it, parents it into the window tree and later tears it down again.
//
// Object layout (0x40 bytes):
//   +0x00 cIGZScriptableControl vptr  (iid 0x1a356083; the interface the script service keeps)
//   +0x08 cIGZControl vptr            (iid 0x5a356159; render / area / event, the window-system face)
//   +0x10 cIGZLegacyControl vptr      (iid 0x9a4527cb; bind a window: GetControl/ForceControl/UseWinProc)
//   +0x18 u32 reference count, shared by the three interfaces (AddRef/Release of every vtable touch it)
//   +0x20 cIGZWin* the control window; owned when +0x38 is set, released otherwise
//   +0x28 cIGZWin* the parent window taken from the scriptable parent's GetControl(); released on rebind
//   +0x30 cIGZWinProc* window procedure installed on the control (owned, +0x38 does not affect it)
//   +0x38 bool ownControl: set by SetParameters (and SetOwnControl), makes the destructor pull the control
//         out of its parent and shut it down before releasing it
//
// The 21 ControlVocabulary* functions are static: the constructor runs them once, after it has obtained the
// shared script vocabulary (spLocalVocab, CLSID 0xba2e7954 / IID 0x1a2bdebb), and they fill three tables:
//   * the vocabulary itself: name -> property id (AddTranslation, vslot 0x20); names are the script keywords
//     ("clsid", "caption", "GZWinBtn", "IGZWinBtn", ...);
//   * sCLSIDtoIIDMap: control class id -> view interface id (used by SetParameters to find a class from an iid);
//   * sInterpretVocab: view interface id -> per-class property interpreter (void (*)(cIGZWin*, params, bool)).
// The tables are exactly the ones read out of the binary (see LegacyControl.cpp); the scripts' property ids are
// not contiguous, the vocabulary is what maps the keywords onto them.
//
// Property ids shared by every control (InterpretPropertiesGZWinCommon, 0x100..0x10f, 0xf01a..0xf026):
//   -1 clsid, -2 iid, 0x100 id, 0x101 area "(l,t,r,b)", 0x102 pos, 0x103 size, 0x104 fillcolor,
//   0x105 caption, 0x106 captionres "{group,instance}", 0x107 transparent, 0x108..0x10f comment strings,
//   0xf000 font, 0xf005 style, 0xf01a..0xf026 the winflag_* bits (see ControlVocabularyGZWinCommon).
//
// Known quirks kept (addresses are the originals):
//   * SetParameters ignores a second SetParameters call's clsid when it is 0 and leaks the previous control
//     (it clears +0x20 before asking GZCOM for the new one) — 0x10053bb33.
//   * GetID on a control-less adapter answers 0; SetID is a tail call into the bound control (0x10053b8be:
//     `mov 0x20(%rdi),%rdi; test; je; jmp *0x200(%rax)`), so it is a no-op when there is none.
//   * GetArea/SetArea return whether a control is bound, not whether the control accepted the rectangle.
//   * GetScriptFragment only writes *out when a control is bound; the caller owns one reference.
//   * AddRef/Release use the single +0x18 counter from all three vtables; Release deletes through the
//     deleting-destructor slot (0x58) when the count drops to 1.
//
// Seams (docs/SEAMS.md): the script vocabulary (cGZScriptVocab, 12 functions) and the UI script service
// (GZCOM factory, RZ::WM(), unscript_IGZWin, LoadStringA/LoadBuffer) are unrecovered neighbours and are reached
// through IVocabularyServices / IScriptControlHost. The control windows themselves are recovered
// (src/gz/Win*.h) and are used through their interfaces.
#pragma once
#include <cstdint>
#include <map>
#include <vector>

#include "gz/WinInterfaces.h"
#include "gz/WinProc.h"
#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZUnknown.h"
#include "gz/cIGZVariant.h"
#include "rz/cRZString.h"

class cIGZBuffer;
class cIGZGDriver;
class cIGZScriptableControl;
class cIGZScriptableControlSet;
namespace gz {
class cIGZWinGen;
class cIGZWinFlatRect;
}  // namespace gz

// ---------------------------------------------------------------------------------------------------------
// Script property list. Original: cGZArray<std::pair<unsigned int, cIGZVariant*>> passed by reference; the
// array is not recovered as a class and the adapter only walks it in order, which std::vector preserves.
// The variants are borrowed (the script system owns them) and are read through cIGZVariant.
struct tScriptProperty {
    std::uint32_t id = 0;
    cIGZVariant* value = nullptr;
};
using tScriptPropertyArray = std::vector<tScriptProperty>;

// The (unrecovered) script vocabulary object, cGZScriptVocab. Only the call the adapter makes is modelled:
// AddTranslation(cIGZString const&, unsigned int) at vslot 0x20.
class IVocabulary : public cIGZUnknown {
public:
    virtual bool AddTranslation(const cIGZString& name, cIGZVariant* value) = 0;                   // vslot 0x18
    virtual bool AddTranslation(const cIGZString& name, std::uint32_t value) = 0;                  // vslot 0x20
};

// ---------------------------------------------------------------------------------------------------------
// The three interfaces the original class implements.
//
// cIGZScriptableControl (iid 0x1a356083): the primary interface; every method the class has is declared here,
// in vtable order [3]..[19]. The two auxiliary interfaces repeat the methods they are allowed to see, so the
// single definition in cGZLegacyControl overrides all three (the secondary vtables get the non-virtual thunks
// the binary has at 0x10053b7ee, 0x10053b866, 0x10053cdd6/0x10053cde4, 0x10053ce00/0x10053ce0e,
// 0x10053ce38/0x10053ce58, 0x10053ccee, 0x10053cd14, 0x10053cd50, 0x10053cd76, 0x10054a32c, 0x10054a336,
// 0x10055e6ac).
class cIGZScriptableControl : public cIGZUnknown {
public:
    virtual bool SetParameters(tScriptPropertyArray& parameters) = 0;                                    // [3] 0x18
    virtual IVocabulary* GetControlVocabulary() = 0;                                                     // [4] 0x20
    virtual std::uint32_t GetID() = 0;                                                                   // [5] 0x28
    virtual void SetID(std::uint32_t id) = 0;                                                            // [6] 0x30
    virtual void SetOwnControl(bool own) = 0;                                                            // [7] 0x38
    virtual bool ImplementInterface(cIGZScriptableControlSet* set, std::uint32_t clsid, std::uint32_t iid,
                                    cIGZVariant* value) = 0;                                             // [8] 0x40
    virtual bool SetParent(cIGZScriptableControl* parent) = 0;                                           // [9] 0x48
    virtual ~cIGZScriptableControl() = default;                                                          // [10],[11] 0x50/0x58
    virtual bool Render(cIGZGDriver* driver) = 0;                                                        // [12] 0x60
    virtual bool SetArea(cRZRect& area) = 0;                                                             // [13] 0x68
    virtual bool GetArea(cRZRect& area) = 0;                                                             // [14] 0x70
    virtual bool ControlEvent(cRZPoint& point, std::uint32_t event) = 0;                                 // [15] 0x78
    virtual cIGZWin* GetControl() = 0;                                                                   // [16] 0x80
    virtual void ForceControl(cIGZWin* control) = 0;                                                     // [17] 0x88
    virtual void UseWinProc(gz::cIGZWinProc* proc) = 0;                                                  // [18] 0x90
    virtual bool GetScriptFragment(cIGZString** out) = 0;                                                // [19] 0x98
};

// cIGZControl (iid 0x5a356159): the window system's view of a control (vtable 0x100acb168, 7 entries).
class cIGZControl : public cIGZUnknown {
public:
    virtual bool Render(cIGZGDriver* driver) = 0;                                                        // [3] 0x18
    virtual bool SetArea(cRZRect& area) = 0;                                                             // [4] 0x20
    virtual bool GetArea(cRZRect& area) = 0;                                                             // [5] 0x28
    virtual bool ControlEvent(cRZPoint& point, std::uint32_t event) = 0;                                 // [6] 0x30
protected:
    ~cIGZControl() = default;   // not virtual in the original's secondary vtable (no destructor slots)
};

// cIGZLegacyControl (iid 0x9a4527cb): what the UI script service binds a script element to (vtable
// 0x100acb1b8, 8 entries). cGZUIScriptService::SetLegacyParent queries this iid and calls ForceControl.
class cIGZLegacyControl : public cIGZUnknown {
public:
    virtual cIGZWin* GetControl() = 0;                                                                   // [3] 0x18
    virtual void SetOwnControl(bool own) = 0;                                                            // [4] 0x20
    virtual void ForceControl(cIGZWin* control) = 0;                                                     // [5] 0x28
    virtual void UseWinProc(gz::cIGZWinProc* proc) = 0;                                                  // [6] 0x30
    virtual bool GetScriptFragment(cIGZString** out) = 0;                                                // [7] 0x38
protected:
    ~cIGZLegacyControl() = default;
};

// Everything the script adapter needs from the (unrecovered) script services. One method per original call;
// the comment gives the original vtable slot / address.
class IVocabularyServices {
public:
    virtual ~IVocabularyServices() = default;
    // spLocalVocab, borrowed (null before the first control is built).
    virtual IVocabulary* Cached() = 0;
    // spLocalVocab->AddRef() (vslot 0x8): the constructor takes a reference for the object's lifetime.
    virtual void AddRefCached() = 0;
    // spLocalVocab->Release() (vslot 0x10); the service drops its pointer when the count reaches 0.
    virtual void ReleaseCached() = 0;
    // GZCOM()->CreateInstance(0xba2e7954, 0x1a2bdebb, &spLocalVocab) (cGZCOM vslot 0x18); true when created.
    virtual bool CreateCached() = 0;
};

class IScriptControlHost {
public:
    virtual ~IScriptControlHost() = default;
    // GZCOM()->CreateInstance(clsid, iid, &control) then QueryInterface(iid = 0x22ba0121) (cGZCOM vslot 0x18).
    virtual cIGZWin* CreateControl(std::uint32_t clsid, std::uint32_t iid) = 0;
    // RZ::WM()->GetMainWindow() (cIGZWinMgr vslot 0x18) — the default parent when no scriptable parent is set.
    virtual cIGZWin* GetMainWindow() = 0;
    // unscript_IGZWin(cIGZWin*, cRZString&) @ 0x10055235b: writes the window's script fragment into `out`.
    virtual bool UnscriptWindow(cIGZWin* control, cIGZString& out) = 0;
    // LoadStringA(group, instance, string) @ 0x10053d08f: localised string (UI script service slot 0x48/0x28,
    // falling back to the resource manager, type 0x89b7dc8).
    virtual bool LoadScriptString(std::uint32_t group, std::uint32_t instance, cIGZString& out) = 0;
    // LoadBuffer(group, instance) @ 0x10053e2a1: image resource (UI script service slot 0x48/0x18, falling
    // back to the resource manager, type 0x6f1568e). Returns a counted reference.
    virtual cIGZBuffer* LoadScriptBuffer(std::uint32_t group, std::uint32_t instance) = 0;
    // MacDisplay::GetNthNativeResolution(index, width, height) @ 0x10063fb77, the four native display modes
    // the resolution caption resources (group 0xa554ae8) are named after. That class is recovered
    // (src/render/MacDisplay.h) but needs CoreFoundation, which the class' test harness does not link; it stays
    // injectable like the other services.
    virtual void GetNativeResolution(int index, int& width, int& height) = 0;
    // RZ::WCM() vslot 0x130: the font style id registered under a name ("main9" for a button without a font); 0 when
    // unknown. Defaulted so test hosts that never reach it need not model it.
    virtual std::uint32_t FontStyleFromName(const cIGZString& name) { (void)name; return 0; }
};

IVocabularyServices* VocabularyServices();
void SetVocabularyServices(IVocabularyServices* services);
IScriptControlHost* ScriptControlHost();
void SetScriptControlHost(IScriptControlHost* host);

// The free helpers of the same translation unit (addresses in the original). They are not members of the class
// but only the script adapter uses them; recovered here so the property interpreters can be checked.
bool GetPropertyUint32(const tScriptPropertyArray& parameters, std::uint32_t id, std::uint32_t& out);   // 0x10053bbf8
// Clears `out` first, then looks for a string-typed property (variant type kTypeRZChar|kTypeArrayFlag).
bool GetPropertyString(const tScriptPropertyArray& parameters, std::uint32_t id, cRZString& out);      // 0x10053ce78
bool GetPropertyCoords(const tScriptPropertyArray& parameters, std::uint32_t id, int& x, int& y);       // 0x10053cf2a
bool GetPropertyColor(const tScriptPropertyArray& parameters, std::uint32_t id, std::uint8_t& r,
                      std::uint8_t& g, std::uint8_t& b);                                                // 0x10053cfcd
bool GetPropertySint32(const tScriptPropertyArray& parameters, std::uint32_t id, int& out);             // 0x100540829
bool GetPropertyRect(const tScriptPropertyArray& parameters, std::uint32_t id, cRZRect& out);           // 0x10053e3fb
void SetGZWinFont(cIGZWin* window, std::uint32_t styleId, std::uint32_t index);                         // 0x10053d8ad
void SetGZWinFont(cIGZWin* window, const cIGZString& name, std::uint32_t index);                        // 0x10053d90d
void SetResourceKeyParam(cIGZWin* window, std::uint32_t key, const cGZPersistResourceKey& value);       // 0x10053d1f3
void SetGZWinImage(cIGZWin* window, std::uint32_t group, std::uint32_t instance, std::uint32_t index);  // 0x10053e4a0
bool LoadStringA(std::uint32_t group, std::uint32_t instance, cRZString& out);                          // 0x10053d08f
cIGZBuffer* LoadBuffer(std::uint32_t group, std::uint32_t instance);                                    // 0x10053e2a1

class cGZLegacyControl : public cIGZScriptableControl, public cIGZControl, public cIGZLegacyControl {
public:
    cGZLegacyControl();
    ~cGZLegacyControl() override;

    // cIGZUnknown (shared by the three vtables)
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override;
    std::uint32_t Release() override;

    // cIGZScriptableControl
    bool SetParameters(tScriptPropertyArray& parameters) override;
    IVocabulary* GetControlVocabulary() override;
    std::uint32_t GetID() override;
    void SetID(std::uint32_t id) override;
    void SetOwnControl(bool own) override;
    bool ImplementInterface(cIGZScriptableControlSet* set, std::uint32_t clsid, std::uint32_t iid,
                            cIGZVariant* value) override;
    bool SetParent(cIGZScriptableControl* parent) override;
    bool Render(cIGZGDriver* driver) override;
    bool SetArea(cRZRect& area) override;
    bool GetArea(cRZRect& area) override;
    bool ControlEvent(cRZPoint& point, std::uint32_t event) override;
    cIGZWin* GetControl() override { return control_; }
    void ForceControl(cIGZWin* control) override;
    void UseWinProc(gz::cIGZWinProc* proc) override;
    bool GetScriptFragment(cIGZString** out) override;

    // --- static vocabulary bootstrap (called once by the constructor) ---
    static void ControlVocabularyGZWinCommon();                                              // 0x100530c8e
    static void ControlVocabularyGZWinFlatRect();                                            // 0x10053227c
    static void ControlVocabularyGZWinText();                                                // 0x10053277e
    static void ControlVocabularyGZWinTextEdit();                                            // 0x100532ed0
    static void ControlVocabularyGZWinListBox();                                             // 0x100533a42
    static void ControlVocabularyGZWinCombo();                                               // 0x100533ee4
    static void ControlVocabularyGZWinBtn();                                                 // 0x100534272
    static void ControlVocabularyGZWinBMP();                                                 // 0x100534e42
    static void ControlVocabularyGZWinSlider();                                              // 0x100535156
    static void ControlVocabularyGZWinScrollbar();                                           // 0x1005354d0
    static void ControlVocabularyGZWinLineInput();                                           // 0x1005357f4
    static void ControlVocabularyGZWinOptGrp();                                              // 0x100535e5c
    static void ControlVocabularyGZWinSpinner();                                             // 0x10053675c
    static void ControlVocabularyGZWinOutline();                                             // 0x100536d5e
    static void ControlVocabularyGZWinTextTicker();                                          // 0x100537080
    static void ControlVocabularyGZWinFileBrowser();                                         // 0x10053739e
    static void ControlVocabularyGZWinTreeView();                                            // 0x10053907e
    static void ControlVocabularyGZWinFolders();                                             // 0x100539972
    static void ControlVocabularyGZWinGrid();                                                // 0x100539b88
    static void ControlVocabularyGZWinCustom();                                              // 0x10053ab60
    static void ControlVocabularySC3WinGen();                                                // 0x10053ad6a

    // The lookup SetParameters uses to pick the per-class interpreter. The keys are view interface ids
    // (0xc2afa76f for IGZWinFlatRect, 0x8810 for IGZWinBtn, ...).
    using InterpretFn = void (*)(cIGZWin*, tScriptPropertyArray&, bool);
    struct Registries {
        std::map<std::uint32_t, std::uint32_t> clsidToIid;
        std::map<std::uint32_t, InterpretFn> interpret;
    };
    static Registries& Tables();

    // Per-class property interpreters (static in the original: the map stores plain function pointers).
    static void InterpretPropertiesGZWinCommon(cIGZWin* window, tScriptPropertyArray& parameters, bool init);   // 0x10053bc8e
    static void InterpretPropertiesGZWinFlatRect(cIGZWin* window, tScriptPropertyArray& parameters, bool init); // 0x10053d25c
    static void InterpretPropertiesGZWinText(cIGZWin* window, tScriptPropertyArray& parameters, bool init);     // 0x10053d4f0
    static void InterpretPropertiesGZWinTextEdit(cIGZWin* window, tScriptPropertyArray& parameters, bool init); // 0x10053d96e
    static void InterpretPropertiesGZWinListBox(cIGZWin* window, tScriptPropertyArray& parameters, bool init);  // 0x10053e53e
    static void InterpretPropertiesGZWinCombo(cIGZWin* window, tScriptPropertyArray& parameters, bool init);    // 0x10053ec98
    static void InterpretPropertiesGZWinBtn(cIGZWin* window, tScriptPropertyArray& parameters, bool init);      // 0x10053f238
    static void InterpretPropertiesGZWinBMP(cIGZWin* window, tScriptPropertyArray& parameters, bool init);      // 0x1005402d0
    static void InterpretPropertiesGZWinSlider(cIGZWin* window, tScriptPropertyArray& parameters, bool init);   // 0x1005405c2
    static void InterpretPropertiesGZWinScrollbar(cIGZWin* window, tScriptPropertyArray& parameters, bool init); // 0x1005408be
    static void InterpretPropertiesGZWinLineInput(cIGZWin* window, tScriptPropertyArray& parameters, bool init); // 0x100540bb4
    static void InterpretPropertiesGZWinOptGrp(cIGZWin* window, tScriptPropertyArray& parameters, bool init);   // 0x10054130e
    static void InterpretPropertiesGZWinSpinner(cIGZWin* window, tScriptPropertyArray& parameters, bool init);  // 0x1005422b8
    static void InterpretPropertiesGZWinOutline(cIGZWin* window, tScriptPropertyArray& parameters, bool init);  // 0x100542a9a
    static void InterpretPropertiesGZWinTextTicker(cIGZWin* window, tScriptPropertyArray& parameters, bool init); // 0x100542e1c
    static void InterpretPropertiesSC3WinGen(cIGZWin* window, tScriptPropertyArray& parameters, bool init);     // 0x1005432b8
    static void InterpretPropertiesGZWinFileBrowser(cIGZWin* window, tScriptPropertyArray& parameters, bool init); // 0x100543942
    static void InterpretPropertiesGZWinTreeView(cIGZWin* window, tScriptPropertyArray& parameters, bool init); // 0x100544dd8
    static void InterpretPropertiesGZWinFolders(cIGZWin* window, tScriptPropertyArray& parameters, bool init);  // 0x100547684
    static void InterpretPropertiesGZWinGrid(cIGZWin* window, tScriptPropertyArray& parameters, bool init);     // 0x1005479ca
    static void InterpretPropertiesGZWinCustom(cIGZWin* window, tScriptPropertyArray& parameters, bool init);   // 0x10054a2fc

private:
    void DoExceptions(cIGZWin* window, bool install);                                        // 0x10053cc5c

    std::uint32_t refCount_ = 0;        // +0x18
    cIGZWin* control_ = nullptr;        // +0x20
    cIGZWin* parentControl_ = nullptr;  // +0x28
    gz::cIGZWinProc* winProc_ = nullptr;  // +0x30
    bool ownControl_ = false;           // +0x38
};
