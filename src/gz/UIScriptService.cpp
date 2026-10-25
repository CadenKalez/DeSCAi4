#include "gz/UIScriptService.h"

#include <cstdio>

#include "gz/GZCOMDirector.h"          // GZCOM(), RZGetFramework(), cIGZCOM
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZPersistRecord.h"      // kGZSID_PersistResourceManager / kGZIID_PersistResourceManager
#include "gz/cIGZPersistResourceManager.h"
#include "rz/RZStringUtils.h"          // RZAtoUint32HexA
#include "rz/cRZAutoRefCount.h"

// The object size is part of the class's contract (the original's factory allocates exactly 0xb0 bytes:
// cGZUIScriptCOMDirector::CreateScriptService, 0x100523972). The members must keep the original offsets.
static_assert(sizeof(cGZUIScriptService) == 0xb0, "cGZUIScriptService must keep the original 0xb0-byte layout");

namespace {

// The window manager service the resolution group is derived from (WinMgrBase.h has the same ids; repeated
// here so this translation unit does not pull the whole window-manager header).
constexpr std::uint32_t kWinMgrServiceId = 0xa417445e;
constexpr std::uint32_t kWinMgrIid = 0x000005a4;

// The three pseudo tags cGZScriptTerp hands to ScriptTag (TestSetup registers their names: "children",
// "/children", "define") and the two properties the define tag reads ("name", "val").
constexpr std::uint32_t kTagChildren = 1;
constexpr std::uint32_t kTagChildOfChildren = 2;
constexpr std::uint32_t kTagDefine = 3;
constexpr std::uint32_t kPropertyName = 4;
constexpr std::uint32_t kPropertyValue = 5;

// The define tag only accepts a string-valued "name" property (variant type 0x800e, see 0x10054dc54).
constexpr std::uint16_t kStringVariantType =
    static_cast<std::uint16_t>(cIGZVariant::kTypeRZChar | cIGZVariant::kTypeArrayFlag);

// cGZLegacyControl hands its vocabulary out as LegacyControl.h's IVocabulary; it is the same object as
// ScriptTerp.h's IScriptVocab (iid 0x1a2bdebb, vtable 0x100acb640), which is how the interpreter sees it.
IScriptVocab* AsScriptVocab(IVocabulary* vocabulary) {
    return reinterpret_cast<IScriptVocab*>(vocabulary);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// Construction and lifetime
// ---------------------------------------------------------------------------------------------------------
cGZUIScriptService::cGZUIScriptService()
    : cRZSystemService(kGZSID_cGZUIScriptService, kGZUIScriptServicePriority) {}

// The body is Shutdown() and nothing else (0x10054c9f6): the vocabulary and the legacy window procedure are
// never released, and the std::list clear that follows the body only frees the list nodes - it does not
// release the controls the parent stack points at (quirk, kept). Shutdown's PopParent loop is what releases
// them, so a service destroyed without Init leaks the stack.
cGZUIScriptService::~cGZUIScriptService() { Shutdown(); }

bool cGZUIScriptService::Init() {
    if (!initialised_) initialised_ = true;
    return true;
}

bool cGZUIScriptService::Shutdown() {
    if (!initialised_) return true;
    if (interpreter_) {
        interpreter_->Release();
        interpreter_ = nullptr;
    }
    if (controlSet_) {
        controlSet_->Release();
        controlSet_ = nullptr;
    }
    if (currentControl_) {
        currentControl_->Release();
        currentControl_ = nullptr;
    }
    if (resourceFactory_) {
        resourceFactory_->Release();
        resourceFactory_ = nullptr;
    }
    while (PopParent()) {
    }
    initialised_ = false;
    return true;
}

bool cGZUIScriptService::QueryInterface(std::uint32_t iid, void** out) {
    void* view = nullptr;
    if (iid == kGZIID_cIGZUIScriptService || iid == kGZIID_cIGZUnknown) {
        view = static_cast<cIGZUIScriptService*>(this);
    } else if (iid == kGZIID_cIGZUILegacyScriptService) {
        view = static_cast<cIGZUILegacyScriptService*>(this);
    } else if (iid == kGZIID_cIGZScriptTarget) {
        view = static_cast<cIGZScriptTarget*>(this);
    } else if (iid == kGZIID_cIGZScriptInterpretingTarget) {
        view = static_cast<cIGZScriptInterpretingTarget*>(this);
    } else if (iid == kGZIID_cIGZScriptCompilingTarget) {
        view = static_cast<cIGZScriptCompilingTarget*>(this);
    } else {
        // Everything else - notably kGZIID_cIGZSystemService (0x287fb697) - is the system service's.
        return cRZSystemService::QueryInterface(iid, out);
    }
    *out = view;
    AddRef();
    return true;
}

// ---------------------------------------------------------------------------------------------------------
// The parameter stack (std::list<cIGZScriptableControl*> at +0x68: AddRef before inserting, Release before
// erasing) and the interpreter's one-time setup.
// ---------------------------------------------------------------------------------------------------------
bool cGZUIScriptService::PushParent() {
    if (pendingParent_) {
        pendingParent_->AddRef();
        parents_.push_back(pendingParent_);
        // The original clears the pending slot unconditionally after the push (0x10054d1a8).
        pendingParent_ = nullptr;
    }
    return true;
}

bool cGZUIScriptService::PopParent() {
    if (parents_.empty()) return false;
    cIGZScriptableControl* control = parents_.back();
    if (control) control->Release();
    parents_.pop_back();
    return true;
}

bool cGZUIScriptService::TestSetup() {
    if (!interpreter_) {
        cIGZCOM* com = GZCOM();
        if (com && com->CreateInstance(kGZCLSID_cGZScriptTerp, kGZIID_cIGZScriptTerp,
                                       reinterpret_cast<void**>(&interpreter_))) {
            // The underscore tokens are registered on the interpreter before it has a script target; the plain
            // ones after. The values are the pseudo tag / property ids the parser resolves the words to.
            static constexpr struct { std::uint32_t id; const char* name; } kLocalTokens[] = {
                {0, "_null"}, {1, "_children"}, {2, "_/children"}, {3, "_define"}, {4, "_name"}, {5, "_val"},
            };
            for (const auto& token : kLocalTokens) interpreter_->AddToken(cRZString(token.name), token.id);
            settingUp_ = true;
            interpreter_->SetScriptTarget(static_cast<cIGZScriptTarget*>(this));
            static constexpr struct { std::uint32_t id; const char* name; } kTargetTokens[] = {
                {0, "none"},           {1, "children"}, {2, "/children"}, {3, "define"},
                {4, "name"},           {5, "val"},      {kGZCLSID_cGZLegacyControl, "LEGACY"},
                {0x12, "comic9"},      {0x13, "comic10"},
            };
            for (const auto& token : kTargetTokens) interpreter_->AddToken(cRZString(token.name), token.id);
            settingUp_ = false;
        }
    }
    return interpreter_ != nullptr;
}

// ---------------------------------------------------------------------------------------------------------
// Executing scripts: hand the text to the interpreter, then give away the control set it filled.
// ---------------------------------------------------------------------------------------------------------
cIGZScriptableControlSet* cGZUIScriptService::ExecuteScriptString(const cIGZString& script) {
    if (TestSetup()) {
        interpreter_->SetScriptTarget(static_cast<cIGZScriptTarget*>(this));
        interpreter_->ExecuteScriptString(script);
    }
    cIGZScriptableControlSet* set = controlSet_;
    controlSet_ = nullptr;
    return set;
}

cIGZScriptableControlSet* cGZUIScriptService::ExecuteScriptFile(const cIGZString& path) {
    if (TestSetup()) {
        interpreter_->SetScriptTarget(static_cast<cIGZScriptTarget*>(this));
        interpreter_->ExecuteScriptFile(path);
    }
    cIGZScriptableControlSet* set = controlSet_;
    controlSet_ = nullptr;
    return set;
}

cIGZScriptableControlSet* cGZUIScriptService::ExecuteScriptResource(const cGZPersistResourceKey& key) {
    if (TestSetup()) {
        interpreter_->SetScriptTarget(static_cast<cIGZScriptTarget*>(this));
        interpreter_->ExecuteScriptResource(key);
    }
    cIGZScriptableControlSet* set = controlSet_;
    controlSet_ = nullptr;
    return set;
}

// ---------------------------------------------------------------------------------------------------------
// The window factory: run the resource, then hand one of the controls' windows to the caller.
// ---------------------------------------------------------------------------------------------------------
cIGZWin* cGZUIScriptService::CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent,
                                                    std::uint32_t id) {
    cGZPersistResourceKey scriptKey = key;   // the copy the resolution group is written into
    cIGZWin* window = nullptr;
    {
        // The original keeps two cRZSysServPtrs alive across the whole function: the persistence manager and
        // the window manager. Their references are dropped at the end (0x10054d332 / 0x10054d3cf).
        cRZAutoRefCount<cIGZPersistResourceManager> persist;
        if (cIGZFrameWork* framework = RZGetFramework()) {
            framework->GetSystemService(kGZSID_PersistResourceManager, kGZIID_PersistResourceManager,
                                        persist.AsOutParam());
        }
        if (persist) {
            cRZAutoRefCount<cIGZWinMgr> winMgr;
            if (cIGZFrameWork* framework = RZGetFramework()) {
                framework->GetSystemService(kWinMgrServiceId, kWinMgrIid, winMgr.AsOutParam());
            }
            if (winMgr) {
                // Quirk: the size is formatted as two decimal numbers, then parsed as one hex number - a
                // 1024x768 main window selects group 0x10240768, not 0x4000300.
                if (cIGZWin* main = winMgr->GetMainWindow()) {
                    char text[48] = {};   // the original's stack buffer (rbp-0x60 .. rbp-0x30 canary)
                    std::snprintf(text, sizeof(text), "0x%.4u%.4u", static_cast<unsigned>(main->GetW()),
                                  static_cast<unsigned>(main->GetH()));
                    scriptKey.group = RZAtoUint32HexA(text);
                    if (!persist->TestForKey(scriptKey)) scriptKey.group = key.group;
                }
            }
        }

        // Both calls are virtual in the original (vslot 0x78 and vslot 0x38).
        SetLegacyParent(parent);
        cIGZScriptableControlSet* set = ExecuteScriptResource(scriptKey);
        if (set) {
            if (cIGZScriptableControl* control = set->GetByID(id)) {
                cIGZLegacyControl* legacy = nullptr;
                if (control->QueryInterface(kGZIID_cIGZLegacyControl, reinterpret_cast<void**>(&legacy)) && legacy) {
                    window = legacy->GetControl();
                    if (window) window->AddRef();
                    legacy->Release();
                }
            }
            // The caller owns the extracted window from here on (0x10054d3be).
            set->SetOwnControls(false);
            set->Release();
        }
    }
    return window;
}

bool cGZUIScriptService::CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent,
                                                std::uint32_t id, std::uint32_t iid, void** out) {
    // The original calls the three-argument overload through vslot 0x18, then asks the window for `iid`.
    cIGZWin* window = CreateWindowFromScript(key, parent, id);
    if (!window) return false;
    const bool found = window->QueryInterface(iid, out);
    window->Release();
    return found;
}

// ---------------------------------------------------------------------------------------------------------
// Interpreter target callbacks
// ---------------------------------------------------------------------------------------------------------
bool cGZUIScriptService::ScriptBegin(cGZScriptTerp* terp) {
    if (!settingUp_) {
        cIGZCOM* com = GZCOM();
        void* created = nullptr;
        if (com && com->CreateInstance(kGZCLSID_cGZScriptVocab, kGZIID_cIGZScriptVocab, &created) && created) {
            // Quirk: a second ScriptBegin without a ScriptEnd overwrites the pointer without releasing it.
            vocabulary_ = static_cast<IScriptVocab*>(static_cast<cIGZUnknown*>(created));
            terp->PushVocabulary(vocabulary_);
        }
    }
    // The original ends with a tail call into CreateInstance, so the *control set* result is what it returns
    // (0x10054d8f9) - even when the vocabulary already existed or could not be created.
    cIGZCOM* com = GZCOM();
    if (!com) return false;
    return com->CreateInstance(kGZCLSID_cGZScriptableControlSet, kGZIID_cIGZScriptableControlSet,
                               reinterpret_cast<void**>(&controlSet_));
}

bool cGZUIScriptService::ScriptEnd(cGZScriptTerp* terp, bool success) {
    if (success) {
        if (settingUp_) return true;
        terp->PopVocabulary();
        // The original dereferences +0x58 without a null check (0x10054d935): a failed ScriptBegin followed by
        // a ScriptEnd crashes it. Guarded here.
        if (vocabulary_) {
            vocabulary_->Release();
            vocabulary_ = nullptr;
        }
        while (PopParent()) {
        }
    } else {
        if (controlSet_) {
            controlSet_->Release();
            controlSet_ = nullptr;
        }
        if (currentControl_) {
            currentControl_->Release();
            currentControl_ = nullptr;
        }
        if (tagActive_) {
            terp->PopVocabulary();
            tagActive_ = false;
        }
        if (settingUp_) return true;
        terp->PopVocabulary();
        if (vocabulary_) {
            vocabulary_->Release();
            vocabulary_ = nullptr;
        }
        while (PopParent()) {
        }
    }
    pendingParent_ = nullptr;
    return true;
}

bool cGZUIScriptService::ScriptLiteral(cGZScriptTerp* terp, cIGZString* text) {
    (void)terp;
    (void)text;
    return true;
}

bool cGZUIScriptService::BeginExecuteTag(cGZScriptTerp* terp, std::uint32_t tag) {
    currentTag_ = tag;   // set before the creation attempt, so a failed tag still records its id
    if (currentControl_) {
        currentControl_->Release();
        currentControl_ = nullptr;
    }
    cIGZCOM* com = GZCOM();
    if (!com || !com->CreateInstance(tag, kGZIID_cIGZScriptableControl,
                                     reinterpret_cast<void**>(&currentControl_)) ||
        !currentControl_) {
        return false;   // the tag-active flag stays false, so EndExecuteTag will not pop a vocabulary
    }
    cIGZLegacyControl* legacy = nullptr;
    if (currentControl_->QueryInterface(kGZIID_cIGZLegacyControl, reinterpret_cast<void**>(&legacy)) && legacy) {
        legacy->UseWinProc(legacyWinProc_);
        legacy->Release();
    }
    cIGZScriptableControl* parent = parents_.empty() ? nullptr : parents_.back();
    currentControl_->SetParent(parent);
    pendingParent_ = currentControl_;
    terp->PushVocabulary(AsScriptVocab(currentControl_->GetControlVocabulary()));
    tagActive_ = true;
    return true;
}

bool cGZUIScriptService::EndExecuteTag(cGZScriptTerp* terp, std::uint32_t tag) {
    (void)tag;
    if (tagActive_) {
        terp->PopVocabulary();
        tagActive_ = false;
    }
    return true;
}

bool cGZUIScriptService::ApplyProperty(cGZScriptTerp* terp, std::uint32_t id, cIGZVariant* value) {
    (void)terp;
    (void)id;
    (void)value;
    return true;
}

bool cGZUIScriptService::ScriptTag(cGZScriptTerp* terp, std::uint32_t tag, tScriptPropertyArray& parameters) {
    if (currentControl_ && controlSet_) {
        currentControl_->SetParameters(parameters);
        controlSet_->Insert(currentControl_);
        currentControl_->Release();
        currentControl_ = nullptr;
        return true;
    }
    switch (tag) {
        case kTagChildren:
            // The "children" pseudo tag: the pending control becomes the parent of the following controls.
            if (pendingParent_) {
                PushParent();
                pendingParent_ = nullptr;
            }
            return true;
        case kTagChildOfChildren:
            // The "/children" pseudo tag: leave the children list again.
            PopParent();
            pendingParent_ = parents_.empty() ? nullptr : parents_.back();
            return true;
        case kTagDefine: {
            // "<define name=... val=...>" closed: set the token, or remove it when there is no value.
            cIGZVariant* name = nullptr;
            cIGZVariant* value = nullptr;
            for (const tScriptProperty& property : parameters) {
                if (property.id == kPropertyValue) value = property.value;
                else if (property.id == kPropertyName) name = property.value;
            }
            if (!name || name->GetType() != kStringVariantType) return true;
            cRZString text;
            name->GetValString(text);
            terp->RemoveToken(text);
            if (value) terp->AddToken(text, value);
            return true;
        }
        default:
            // Quirk: an unknown tag with no current control is reported as a failure (0x10054dcf4).
            return false;
    }
}

// ---------------------------------------------------------------------------------------------------------
// Legacy parent / window procedure and the resource factory
// ---------------------------------------------------------------------------------------------------------
bool cGZUIScriptService::SetLegacyParent(cIGZWin* window) {
    cIGZScriptableControl* control = nullptr;
    cIGZCOM* com = GZCOM();
    if (com && com->CreateInstance(kGZCLSID_cGZLegacyControl, kGZIID_cIGZScriptableControl,
                                   reinterpret_cast<void**>(&control)) &&
        control) {
        cIGZLegacyControl* legacy = nullptr;
        if (control->QueryInterface(kGZIID_cIGZLegacyControl, reinterpret_cast<void**>(&legacy)) && legacy) {
            legacy->ForceControl(window);
            legacy->Release();
        }
        pendingParent_ = control;
        PushParent();
        control->Release();   // the parent stack owns the reference PushParent took
    }
    return true;
}

bool cGZUIScriptService::SetLegacyWinProc(gz::cIGZWinProc* proc) {
    if (legacyWinProc_ != proc) {
        if (legacyWinProc_) legacyWinProc_->Release();   // the old one first in the source ...
        legacyWinProc_ = proc;
        if (proc) proc->AddRef();                        // ... the new one's AddRef follows the store
    }
    return true;
}

cIGZScriptTerp* cGZUIScriptService::GetScriptInterpreter() {
    TestSetup();
    return interpreter_;
}

cIGZUIScriptServiceResourceFactory* cGZUIScriptService::GetResourceFactory() { return resourceFactory_; }

void cGZUIScriptService::SetResourceFactory(cIGZUIScriptServiceResourceFactory* factory) {
    if (resourceFactory_ == factory) return;
    // AddRef the new one before releasing the old one, so passing the service's own reference back survives
    // (0x10054d582).
    if (factory) factory->AddRef();
    if (resourceFactory_) resourceFactory_->Release();
    resourceFactory_ = factory;
}
