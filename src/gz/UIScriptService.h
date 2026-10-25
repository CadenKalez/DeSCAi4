// cGZUIScriptService — the UI script service (63 symbols: the 27 methods below plus the constructor/
// destructor variants and 33 non-virtual thunks; 0x10054c8b0..0x10054dd58, primary vtable 0x100acb330).
// It loads window descriptions ("window scripts") and their string / image resources for the UI: an
// Execute* call hands the text to a cGZScriptTerp interpreter (src/gz/ScriptTerp.h), which parses the
// tags and calls the service back as its cIGZScriptTarget / cIGZScriptInterpretingTarget /
// cIGZScriptCompilingTarget; the service creates one cGZLegacyControl per control tag through GZCOM
// (src/gz/LegacyControl.h), collects them into a cIGZScriptableControlSet and defines the script's
// "<define name=.. val=..>" tokens on the interpreter's vocabulary stack.
//
// Object layout (0xb0 bytes; offsets are the original's):
//   +0x00 cIGZUIScriptService vptr             (iid 0xfa3562fa, also answers iid 1)
//   +0x08 cIGZUILegacyScriptService vptr       (iid 0x7a4a48ed: SetLegacyParent / SetLegacyWinProc)
//   +0x10 cIGZScriptTarget vptr                (iid 0x1a2bdeb8: ScriptBegin / ScriptEnd / ScriptLiteral)
//   +0x18 cIGZScriptInterpretingTarget vptr    (iid 0x1a2bdeb9: Begin/EndExecuteTag, ApplyProperty)
//   +0x20 cIGZScriptCompilingTarget vptr       (iid 0x1a2bdeba: ScriptTag)
//   +0x28 cRZSystemService                      (cIGZSystemService vptr at +0x28, cRZUnknown vptr at +0x30,
//                                                service id 0x5a356e15 at +0x3c, priority -3000000 at +0x40,
//                                                tick priority -3000000 at +0x44, running at +0x48)
//   +0x49 bool initialised: Init() sets it, Shutdown() clears it; it lives in the system service's tail
//         padding (the original does the same - do not move it into a member block of its own)
//   +0x50 cIGZScriptTerp* interpreter (created by TestSetup, clsid 0xba2e7953 / iid 0x5a2c1570)
//   +0x58 cIGZScriptVocab* vocabulary (created by ScriptBegin, clsid 0xba2e7954 / iid 0x1a2bdebb)
//   +0x60 bool settingUp: set while TestSetup registers its tokens; ScriptBegin/ScriptEnd do nothing then
//   +0x68 std::list<cIGZScriptableControl*> parent stack (libc++ list: next +0, prev +8, size +0x10)
//   +0x80 cIGZScriptableControl* pending parent (a control whose children list is being read)
//   +0x88 bool tag active (BeginExecuteTag succeeded and pushed a vocabulary)
//   +0x8c u32 current tag id (set by BeginExecuteTag before the control is created)
//   +0x90 cIGZScriptableControlSet* control set (created by ScriptBegin; Execute* return it to the caller)
//   +0x98 cIGZScriptableControl* current control (created by BeginExecuteTag, consumed by ScriptTag)
//   +0xa0 cIGZWinProc* legacy window procedure installed on every new control
//   +0xa8 cIGZUIScriptServiceResourceFactory* resource factory (set by SetResourceFactory)
//
// The script protocol the service implements (the three callback interfaces are declared by ScriptTerp.h):
//   ScriptBegin(terp)       create the vocabulary (0xba2e7954) and push it; always create the control set
//                           (0x5a354595) into +0x90. It returns the control-set creation result, not the
//                           vocabulary one (the original's tail call to CreateInstance - 0x10054d8f9).
//   BeginExecuteTag(tag)    release the previous control, create a cGZLegacyControl for the tag's class id,
//                           install the legacy window proc, set the control's parent to the top of the
//                           parent stack, make it the pending parent and push its vocabulary.
//   ScriptTag(tag, params)  with a current control and a control set: SetParameters, add the control to the
//                           set, release it. Otherwise only the three pseudo tags do something:
//                           1 "children" push the pending parent, 2 "/children" pop it, 3 "define" set or
//                           remove a token (properties 4 "name" and 5 "val"); every other tag answers false.
//   EndExecuteTag(tag)      pop the vocabulary, but only when BeginExecuteTag succeeded (the +0x88 flag).
//   ScriptEnd(terp, ok)     ok=false: release the set and the current control, pop a still-active tag
//                           vocabulary, then release the vocabulary and pop the whole parent stack;
//                           ok=true: release the vocabulary and pop the parent stack only.
//   ScriptLiteral/ApplyProperty do nothing and answer true.
//
// Known quirks kept (addresses are the originals):
//  * CreateWindowFromScript turns the main window's size into a resource group with
//    sprintf("0x%.4u%.4u", w, h) - the two *decimal* numbers are then parsed as one hex number by
//    RZAtoUint32HexA (0x10054d30d), so 1024x768 becomes group 0x10240768. The computed group is used only
//    when the persistence manager already has a resource with it (TestForKey, 0x10054d323).
//  * ScriptBegin's early return is the control set creation result: when the vocabulary was already there
//    (or its creation failed) it still returns the set's result (0x10054d8f9).
//  * ScriptEnd dereferences the vocabulary pointer without a null check (0x10054d935 / 0x10054d9ba): a
//    failed ScriptBegin followed by a ScriptEnd crashes the original. This port adds the null check (the only
//    deliberate behavioural deviation) and keeps every other step.
//  * Shutdown releases the interpreter (+0x50), the control set (+0x90), the current control (+0x98) and the
//    resource factory (+0xa8) but neither the vocabulary (+0x58) nor the legacy window proc (+0xa0); the
//    destructor does not release them either, so those two leak when Shutdown runs without a script end
//    (0x10054cb52). Kept and asserted.
//  * Shutdown does nothing at all when Init was never called (+0x49 == 0), even though TestSetup and
//    Execute* work without Init; the destructor then leaks whatever they created (0x10054cb5b).
//  * Shutdown pops the parent stack with its PopParent loop (0x10054cbcf), but the destructor's
//    std::list clear (0x10054c9fa) only frees the list nodes: it never releases the controls. A service
//    destroyed without Init therefore leaks whatever the parent stack still holds. Kept and asserted.
//  * ScriptTag answers false for an unknown tag when no control is current (0x10054dcf4); it answers true
//    for every other case. Note that the pseudo tags are compared against the *tag id* the script wrote
//    ("children" = 1 etc., as registered by TestSetup), and that a current control with a null control set
//    falls into the same tag handling instead of applying the properties.
//  * ScriptTag's normal branch does not clear the pending parent (+0x80): the control it just added stays
//    pending so the next "<children>" pseudo tag can push it (0x10054dbd5 only clears +0x98).
//  * BeginExecuteTag records the tag id in +0x8c before it tries to create the control (0x10054da1f), but
//    no recovered function ever reads that field: it is write-only state.
//  * SetLegacyWinProc compares the raw pointers before doing anything (0x10054d68a), so installing the same
//    procedure twice neither adds nor drops a reference; on a real change it releases the old procedure
//    (0x10054d6ab) before AddRef'ing the new one.
//  * SetResourceFactory does take the new reference before dropping the old one (0x10054d5a6 / 0x10054d5b8),
//    and does nothing at all when handed the pointer it already holds.
//  * CreateWindowFromScript calls SetOwnControls(false) on the set it returns a window from - before
//    releasing the set - so the caller owns the window (0x10054d3be).
//  * TestSetup registers each special token twice, once with a leading underscore and once without
//    ("_null"/"none", "_children"/"children", "_/children"/"/children", "_define"/"define",
//    "_name"/"name", "_val"/"val"), plus "LEGACY" -> 0xfa450242 (the legacy control class) and
//    "comic9"/"comic10" -> 0x12/0x13 (0x10054cc48). The underscore batch is added before the script target
//    is set, the rest after.
//
// Seams: the interpreter (cGZScriptTerp) and the control set (cGZScriptableControlSet) are recovered /
// unrecovered neighbours reached only through their interfaces (ScriptTerp.h and the interface below); the
// service creates both through GZCOM(), so it never names their classes. The vocabulary is ScriptTerp.h's
// IScriptVocab view (iid 0x1a2bdebb, vtable 0x100acb640); cGZLegacyControl hands it out as
// LegacyControl.h's IVocabulary, which is the same object - the pointer is passed through unchanged.
#pragma once
#include <cstdint>
#include <list>

#include "gz/ScriptTerp.h"
#include "gz/WinInterfaces.h"
#include "gz/WinProc.h"
#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZString.h"
#include "gz/cIGZVariant.h"
#include "rz/cRZString.h"
#include "rz/cRZSystemService.h"

class cIGZBuffer;
class cIGZScriptableControl;
class cIGZWinMgr;

// ---- ids ----------------------------------------------------------------------------------------------------
constexpr std::uint32_t kGZSID_cGZUIScriptService = 0x5a356e15;
constexpr std::int32_t kGZUIScriptServicePriority = -3000000;

constexpr std::uint32_t kGZIID_cIGZUIScriptService = 0xfa3562fa;
constexpr std::uint32_t kGZIID_cIGZUILegacyScriptService = 0x7a4a48ed;
constexpr std::uint32_t kGZIID_cIGZScriptableControl = 0x1a356083;
constexpr std::uint32_t kGZIID_cIGZScriptableControlSet = 0x1a35608a;
constexpr std::uint32_t kGZIID_cIGZLegacyControl = 0x9a4527cb;

// The classes the service asks GZCOM for. The control set's class id equals the service id's low half by
// coincidence (0x5a354595 vs 0x5a356e15); do not confuse kGZCLSID_cGZScriptableControlSet with the service.
constexpr std::uint32_t kGZCLSID_cGZScriptTerp = 0xba2e7953;
constexpr std::uint32_t kGZCLSID_cGZScriptVocab = 0xba2e7954;
constexpr std::uint32_t kGZCLSID_cGZScriptableControlSet = 0x5a354595;
constexpr std::uint32_t kGZCLSID_cGZLegacyControl = 0xfa450242;

// ---- interfaces ---------------------------------------------------------------------------------------------
// cIGZUIScriptServiceResourceFactory (iid 0x8aa22237): loads a script's string and image resources by key.
// Its concrete implementation is cSC4ScriptServiceResourceFactory (src/sc4/UIResourceObjects.h, vtable
// 0x100a95fd0, the same five methods); the service only holds and releases the pointer.
class cIGZUIScriptServiceResourceFactory : public cIGZUnknown {
public:
    virtual cIGZBuffer* GZUIScriptLoadBuffer(const cGZPersistResourceKey& key) = 0;                 // [3] 0x18
    virtual cIGZString* GZUIScriptLoadString(const cGZPersistResourceKey& key) = 0;                 // [4] 0x20
    virtual void GZUIScriptLoadString(const cGZPersistResourceKey& key, cIGZString& out) = 0;       // [5] 0x28
protected:
    ~cIGZUIScriptServiceResourceFactory() = default;
};

// cIGZScriptableControlSet (iid 0x1a35608a, vtable 0x100b0edf8, 12 slots - no destructor slots). The
// implementation (cGZScriptableControlSet, vtable 0x100acb210) is not recovered; the service only calls
// Insert, GetByID and SetOwnControls. Its second view (+8, cIGZRenderControlSet) carries Render and
// ControlEvent and is not used here.
class cIGZScriptableControlSet : public cIGZUnknown {
public:
    // [3] 0x18: the underlying cGZArray<cIGZScriptableControl*> (unrecovered template).
    virtual void* AsGZArray() = 0;
    virtual bool Insert(cIGZScriptableControl* control) = 0;                            // [4] 0x20
    virtual cIGZScriptableControl* GetByID(std::uint32_t id) = 0;                       // [5] 0x28
    virtual cIGZScriptableControl* GetByIndex(std::uint32_t index) = 0;                 // [6] 0x30
    virtual cIGZWin* GetByIDAsIGZWin(std::uint32_t id) = 0;                             // [7] 0x38
    virtual cIGZWin* GetByIndexAsIGZWin(std::uint32_t index) = 0;                       // [8] 0x40
    virtual void SetOwnControls(bool own) = 0;                                          // [9] 0x48
    virtual std::uint32_t GetSize() = 0;                                                // [10] 0x50
    virtual bool ThroughClass(std::uint32_t classId, cIGZVariant* value) = 0;           // [11] 0x58
protected:
    ~cIGZScriptableControlSet() = default;
};

// cIGZUIScriptService (iid 0xfa3562fa, vtable 0x100b0ef38, 11 slots - no destructor slots). Every method is
// declared again by the class, which is why the concrete primary vtable continues with the class's own
// virtuals. Execute* transfer the control set the script built to the caller (one reference): the return
// type is the set, not a success flag.
class cIGZUIScriptService : public cIGZUnknown {
public:
    virtual cIGZWin* CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent,
                                            std::uint32_t id) = 0;                              // [3] 0x18
    virtual bool CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent,
                                        std::uint32_t id, std::uint32_t iid, void** out) = 0;    // [4] 0x20
    virtual cIGZScriptableControlSet* ExecuteScriptString(const cIGZString& script) = 0;         // [5] 0x28
    virtual cIGZScriptableControlSet* ExecuteScriptFile(const cIGZString& path) = 0;             // [6] 0x30
    virtual cIGZScriptableControlSet* ExecuteScriptResource(const cGZPersistResourceKey& key) = 0;   // [7] 0x38
    virtual cIGZScriptTerp* GetScriptInterpreter() = 0;                                          // [8] 0x40
    virtual cIGZUIScriptServiceResourceFactory* GetResourceFactory() = 0;                        // [9] 0x48
    virtual void SetResourceFactory(cIGZUIScriptServiceResourceFactory* factory) = 0;            // [10] 0x50
protected:
    ~cIGZUIScriptService() = default;
};

// cIGZUILegacyScriptService (iid 0x7a4a48ed, vtable 0x100b0efa0, 5 slots).
class cIGZUILegacyScriptService : public cIGZUnknown {
public:
    // Creates a cGZLegacyControl (0xfa450242) that wraps `window` and pushes it as the current parent.
    virtual bool SetLegacyParent(cIGZWin* window) = 0;                                  // [3] 0x18
    virtual bool SetLegacyWinProc(gz::cIGZWinProc* proc) = 0;                           // [4] 0x20
protected:
    ~cIGZUILegacyScriptService() = default;
};

// ---- the class ----------------------------------------------------------------------------------------------
class cGZUIScriptService : public cIGZUIScriptService, public cIGZUILegacyScriptService,
                           public cIGZScriptTarget, public cIGZScriptInterpretingTarget,
                           public cIGZScriptCompilingTarget, public cRZSystemService {
public:
    cGZUIScriptService();                                  // 0x10054c8b0 (C1) / 0x10054c9ec (C2)
    ~cGZUIScriptService() override;                        // 0x10054c9f6

    // Declaration order matters: it is the order of the class's own entries in the primary vtable (the
    // binary has ~dtor at [11]/[12], Init [13], Shutdown [14], SetLegacyParent [15], SetLegacyWinProc [16],
    // ScriptBegin [17], ScriptEnd [18], ScriptLiteral [19], BeginExecuteTag [20], EndExecuteTag [21],
    // ApplyProperty [22], ScriptTag [23]). Keep it when editing.
    bool Init() override;                                  // 0x10054cb2e ([13], and the service view)
    bool Shutdown() override;                              // 0x10054cb52 ([14], and the service view)
    bool SetLegacyParent(cIGZWin* window) override;        // 0x10054d5c8
    bool SetLegacyWinProc(gz::cIGZWinProc* proc) override; // 0x10054d68a
    bool ScriptBegin(cGZScriptTerp* terp) override;        // 0x10054d886
    bool ScriptEnd(cGZScriptTerp* terp, bool success) override;                  // 0x10054d90a
    bool ScriptLiteral(cGZScriptTerp* terp, cIGZString* text) override;          // 0x10054d9f8
    bool BeginExecuteTag(cGZScriptTerp* terp, std::uint32_t tag) override;       // 0x10054da08
    bool EndExecuteTag(cGZScriptTerp* terp, std::uint32_t tag) override;         // 0x10054db1e
    bool ApplyProperty(cGZScriptTerp* terp, std::uint32_t id, cIGZVariant* value) override;   // 0x10054db70
    bool ScriptTag(cGZScriptTerp* terp, std::uint32_t tag, tScriptPropertyArray& parameters) override;  // 0x10054db80

    // cIGZUnknown — one override serves all five interface views and the system service (the binary has a
    // thunk per subobject: 0x10054d776..0x10054d7bc for QueryInterface).
    bool QueryInterface(std::uint32_t iid, void** out) override;      // 0x10054d70a
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }      // 0x10054d7ca
    std::uint32_t Release() override { return cRZSystemService::Release(); }    // 0x10054d828

    // cIGZUIScriptService
    cIGZWin* CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent,
                                    std::uint32_t id) override;       // 0x10054d210
    bool CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent, std::uint32_t id,
                                std::uint32_t iid, void** out) override;   // 0x10054d438
    cIGZScriptableControlSet* ExecuteScriptString(const cIGZString& script) override;   // 0x10054d482
    cIGZScriptableControlSet* ExecuteScriptFile(const cIGZString& path) override;       // 0x10054d4ca
    cIGZScriptableControlSet* ExecuteScriptResource(const cGZPersistResourceKey& key) override;   // 0x10054d512
    cIGZScriptTerp* GetScriptInterpreter() override;                                    // 0x10054d55a
    cIGZUIScriptServiceResourceFactory* GetResourceFactory() override;                  // 0x10054d574
    void SetResourceFactory(cIGZUIScriptServiceResourceFactory* factory) override;      // 0x10054d582

    // The class's own non-virtual functions (not in any vtable).
    bool TestSetup();      // 0x10054cc48: create the interpreter and register its tokens, once
    bool PushParent();     // 0x10054d1a8: push the pending parent (AddRef) onto the parent stack
    bool PopParent();      // 0x10054cbe8: release and remove the top parent; false when the stack is empty

private:
    bool initialised_ = false;                                     // +0x49 (see the layout note)
    cIGZScriptTerp* interpreter_ = nullptr;                        // +0x50
    IScriptVocab* vocabulary_ = nullptr;                           // +0x58
    bool settingUp_ = false;                                       // +0x60
    std::list<cIGZScriptableControl*> parents_;                    // +0x68
    cIGZScriptableControl* pendingParent_ = nullptr;               // +0x80
    bool tagActive_ = false;                                       // +0x88
    std::uint32_t currentTag_ = 0;                                 // +0x8c
    cIGZScriptableControlSet* controlSet_ = nullptr;               // +0x90
    cIGZScriptableControl* currentControl_ = nullptr;              // +0x98
    gz::cIGZWinProc* legacyWinProc_ = nullptr;                     // +0xa0
    cIGZUIScriptServiceResourceFactory* resourceFactory_ = nullptr;   // +0xa8
};
