// cGZUIScriptService (src/gz/UIScriptService.{h,cpp}) — the UI script service.
//
// The rig is a fake GZ world: a fake cIGZCOM that creates the *real* cGZScriptTerp (src/gz/ScriptTerp.cpp)
// plus fake vocabularies, scriptable controls and control sets; a fake framework that hands out a fake
// persistence manager and a fake window manager whose main window is a real cGZWin. The real interpreter and
// the real window run the scripts, so most cases below go end to end through the actual parser.
//
// Covered here: the 0xb0-byte layout and the interface subobject offsets, QueryInterface for all six ids,
// Init/Shutdown and its two leaks, TestSetup's 15 token registrations and the script target it installs,
// ExecuteScriptString / ExecuteScriptFile and the ownership of the control set they return, the children /
// child-ren parent stack, define token insertion and removal, CreateWindowFromScript (including the
// decimal-digits-parsed-as-hex resolution group quirk, the failure paths and the 5-argument overload),
// SetLegacyParent / SetLegacyWinProc reference handling, Begin/EndExecuteTag, ApplyProperty, ScriptLiteral
// and ScriptTag's branches, the resource factory swap and the destructor.
//
// Build (from the repository root):
//   /usr/bin/clang++ -std=c++20 -Wall -Wextra -O1 -fsanitize=address,undefined -Isrc \
//       -Iscripts/tests/stubs -Iscripts/tests scripts/tests/gz_uiscriptservice_test.cpp \
//       src/gz/UIScriptService.cpp src/gz/ScriptTerp.cpp src/gz/GZCOMDirector.cpp src/gz/Win.cpp \
//       src/rz/cRZCOMDllDirector.cpp src/rz/cRZString.cpp src/rz/cRZVariant.cpp src/rz/RZConvert.cpp \
//       src/rz/RZStringUtils.cpp src/rz/cRZFile.cpp src/rz/cRZSysServPtr.cpp -o /tmp/uis_test && /tmp/uis_test
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "fake_resourcemanager.h"
#include "gz/GZCOMDirector.h"
#include "gz/UIScriptService.h"
#include "gz/Win.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZVariant.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
#define CHECK_EQ(a, b) do { const long long va = static_cast<long long>(a); const long long vb = static_cast<long long>(b); \
    if (va != vb) { std::printf("FAIL line %d: %s == %s (%lld vs %lld)\n", __LINE__, #a, #b, va, vb); ++failures; } } while (0)

namespace {

// ---------------------------------------------------------------------------------------------------------
// Fake vocabulary: records the translations in insertion order, keeps the variants alive and implements the
// counted-reference contract of IScriptVocab::GetTranslation the interpreter relies on.
// ---------------------------------------------------------------------------------------------------------
struct FakeVocab final : IScriptVocab {
    struct Entry {
        std::string name;
        cIGZVariant* value;
    };
    std::vector<Entry> entries;
    int refs = 0;
    int addCalls = 0, removeCalls = 0, getCalls = 0;

    ~FakeVocab() override {
        for (Entry& entry : entries) entry.value->Release();
    }

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }

    Entry* Find(const std::string& name) {
        for (Entry& entry : entries) {
            if (entry.name.size() != name.size()) continue;
            bool same = true;
            for (std::size_t i = 0; i < name.size() && same; ++i) {
                same = std::tolower(static_cast<unsigned char>(entry.name[i])) ==
                       std::tolower(static_cast<unsigned char>(name[i]));
            }
            if (same) return &entry;
        }
        return nullptr;
    }

    bool AddTranslation(const cIGZString& name, cIGZVariant* value) override {
        ++addCalls;
        if (!value) return false;
        // The original's vocabulary keeps the first value: a duplicate name refuses the new one (the caller
        // keeps the reference it passed).
        if (Find(name.Data())) return false;
        value->AddRef();   // the vocabulary takes its own reference
        entries.push_back(Entry{name.Data(), value});
        return true;
    }
    bool AddTranslation(const cIGZString& name, std::uint32_t value) override {
        ++addCalls;
        if (Find(name.Data())) return false;
        auto* stored = new cRZVariant(value);
        stored->AddRef();
        entries.push_back(Entry{name.Data(), stored});
        return true;
    }
    bool RemoveTranslation(const cIGZString& name) override {
        ++removeCalls;
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (it->name == name.Data()) {
                it->value->Release();
                entries.erase(it);
                return true;
            }
        }
        return false;
    }
    bool GetTranslation(const cIGZString& name, cIGZVariant** out) override {
        ++getCalls;
        Entry* entry = Find(name.Data());
        if (!entry) return false;
        entry->value->AddRef();
        *out = entry->value;
        return true;
    }
    void Clear() override {
        for (Entry& entry : entries) entry.value->Release();
        entries.clear();
    }
};

// The default vocabulary the interpreter's constructor asks its host for.
struct TestTerpHost final : IScriptTerpHost {
    std::vector<FakeVocab*> created;
    IScriptVocab* CreateVocabulary() override {
        auto* vocab = new FakeVocab();
        vocab->AddRef();
        created.push_back(vocab);
        return vocab;
    }
};

// ---------------------------------------------------------------------------------------------------------
// A minimal window manager (same shape as gz_win_test.cpp) whose main window is a real cGZWin.
// ---------------------------------------------------------------------------------------------------------
struct FakeMgr final : cIGZWinMgr {
    cIGZWin* mainWin = nullptr;
    cIGZWin* focus = nullptr;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    cIGZWin* GetMainWindow() override { return mainWin; }
    void AppendToPlotStrategy(cIGZWin*) override {}
    void Plot() override {}
    void PostMessageToTarget(cIGZWin*, const cGZMessage&) override {}
    void AddWindowToValidList(cIGZWin*) override {}
    void RemoveWindowFromValidList(cIGZWin*) override {}
    void CleanUpWindowReferences(cIGZWin*) override {}
    bool DestroyWindow(cIGZWin* w) override { return w != nullptr; }
    bool IsWindowValid(cIGZWin* w) override { return w == mainWin; }
    cIGZWin* GetWindowFromPoint(int, int) override { return nullptr; }
    void SetCurrentMouseWin(cIGZWin*) override {}
    cIGZWin* GZGetCapture() override { return nullptr; }
    void GZSetCapture(cIGZWin*) override {}
    void GZReleaseCapture(cIGZWin*) override {}
    cIGZCursorManager* GetCursorManager() override { return nullptr; }
    cIGZWin* GZGetFocus() override { return focus; }
    void GZSetFocus(cIGZWin* w) override { focus = w; }
    cIGZKeyboard* GetKeyboard() override { return nullptr; }
};

// ---------------------------------------------------------------------------------------------------------
// The fake controls' windows need a manager (cGZWin's destructor talks to it); one process-wide no-op
// manager is enough and keeps the fake controls independent of the rig's own manager.
struct FakeWin final : cGZWin {
    FakeWin();
};

cIGZWinMgr* FakeWindowManager();

inline cIGZWinMgr* FakeWindowManager() {
    static FakeMgr manager;
    return &manager;
}

inline FakeWin::FakeWin() : cGZWin(FakeWindowManager()) {}

// ---------------------------------------------------------------------------------------------------------
// Fake scriptable control: implements cIGZScriptableControl (iid 0x1a356083) and cIGZLegacyControl (0x9a4527cb).
// It snapshots the property list (the variants die with the interpreter's parameter array) and owns the window
// it hands back through GetControl.
// ---------------------------------------------------------------------------------------------------------
struct FakeControl final : cIGZScriptableControl, cIGZLegacyControl {
    struct Property {
        std::uint32_t id = 0;
        std::uint32_t uintValue = 0;
        std::string text;
    };
    int refs = 0;
    std::uint32_t id = 0;
    bool ownControl = false;
    int setParametersCalls = 0;
    std::vector<Property> properties;
    cIGZScriptableControl* parent = nullptr;
    cIGZWin* window = nullptr;        // owned
    cIGZWin* forcedWindow = nullptr;  // recorded by ForceControl, borrowed
    gz::cIGZWinProc* winProc = nullptr;
    FakeVocab* vocabulary = nullptr;
    int forceControlCalls = 0, useWinProcCalls = 0;

    FakeControl() {
        window = new FakeWin();
        window->AddRef();   // the control's own reference; released by the destructor
    }
    ~FakeControl() override { window->Release(); }

    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZScriptableControl) {
            *out = static_cast<cIGZScriptableControl*>(this);
        } else if (iid == kGZIID_cIGZLegacyControl) {
            *out = static_cast<cIGZLegacyControl*>(this);
        } else {
            return false;
        }
        AddRef();
        return true;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }

    bool SetParameters(tScriptPropertyArray& parameters) override {
        ++setParametersCalls;
        properties.clear();
        for (const tScriptProperty& property : parameters) {
            Property copy{property.id, 0, {}};
            if (property.value) {
                const std::uint16_t type = property.value->GetType();
                if (type == cIGZVariant::kTypeUint32) copy.uintValue = property.value->GetValUint32();
                if (type == (cIGZVariant::kTypeRZChar | cIGZVariant::kTypeArrayFlag)) {
                    cRZString text;
                    property.value->GetValString(text);
                    copy.text = text.Data();
                }
            }
            properties.push_back(copy);
            if (property.id == 0x100) id = copy.uintValue;   // what cGZLegacyControl does with "id"
        }
        return true;
    }
    IVocabulary* GetControlVocabulary() override { return reinterpret_cast<IVocabulary*>(vocabulary); }
    std::uint32_t GetID() override { return id; }
    void SetID(std::uint32_t value) override { id = value; }
    void SetOwnControl(bool own) override { ownControl = own; }
    bool ImplementInterface(cIGZScriptableControlSet*, std::uint32_t, std::uint32_t, cIGZVariant*) override { return false; }
    bool SetParent(cIGZScriptableControl* value) override { parent = value; return true; }
    bool Render(cIGZGDriver*) override { return true; }
    bool SetArea(cRZRect&) override { return true; }
    bool GetArea(cRZRect&) override { return false; }
    bool ControlEvent(cRZPoint&, std::uint32_t) override { return false; }
    cIGZWin* GetControl() override { return forcedWindow ? forcedWindow : window; }
    void ForceControl(cIGZWin* control) override { ++forceControlCalls; forcedWindow = control; }
    void UseWinProc(gz::cIGZWinProc* proc) override { ++useWinProcCalls; winProc = proc; }
    bool GetScriptFragment(cIGZString**) override { return false; }
};

// ---------------------------------------------------------------------------------------------------------
struct FakeControlSet final : cIGZScriptableControlSet {
    std::vector<cIGZScriptableControl*> controls;
    int refs = 0, inserts = 0, ownControlsCalls = 0;
    bool ownControls = true;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    void* AsGZArray() override { return &controls; }
    bool Insert(cIGZScriptableControl* control) override {
        if (!control) return false;
        control->AddRef();
        controls.push_back(control);
        ++inserts;
        return true;
    }
    cIGZScriptableControl* GetByID(std::uint32_t id) override {
        for (cIGZScriptableControl* control : controls) {
            if (control->GetID() == id) return control;
        }
        return nullptr;
    }
    cIGZScriptableControl* GetByIndex(std::uint32_t index) override {
        return index < controls.size() ? controls[index] : nullptr;
    }
    cIGZWin* GetByIDAsIGZWin(std::uint32_t) override { return nullptr; }
    cIGZWin* GetByIndexAsIGZWin(std::uint32_t) override { return nullptr; }
    void SetOwnControls(bool own) override {
        ++ownControlsCalls;
        ownControls = own;
        for (cIGZScriptableControl* control : controls) control->SetOwnControl(own);
    }
    std::uint32_t GetSize() override { return static_cast<std::uint32_t>(controls.size()); }
    bool ThroughClass(std::uint32_t, cIGZVariant*) override { return false; }
};

// ---------------------------------------------------------------------------------------------------------
struct FakeWinProc final : gz::cIGZWinProc {
    int refs = 0;
    int messages = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool DoWinProcMessage(cIGZWin*, cGZMessage&) override { ++messages; return false; }
};

struct FakeFactory final : cIGZUIScriptServiceResourceFactory {
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    cIGZBuffer* GZUIScriptLoadBuffer(const cGZPersistResourceKey&) override { return nullptr; }
    cIGZString* GZUIScriptLoadString(const cGZPersistResourceKey&) override { return nullptr; }
    void GZUIScriptLoadString(const cGZPersistResourceKey&, cIGZString& out) override { out.AssignFromCString(""); }
};

// ---------------------------------------------------------------------------------------------------------
// The persistence manager: everything inherited from the shared fake answers "nothing"; TestForKey records
// the key and returns the scripted answer (the window factory only asks about the resolution group).
// ---------------------------------------------------------------------------------------------------------
struct FakePersist final : FakeResourceManager {
    cGZPersistResourceKey lastKey{};
    int testForKeyCalls = 0;
    bool found = false;
    bool TestForKey(cGZPersistResourceKey& key) override {
        ++testForKeyCalls;
        lastKey = key;
        return found;
    }
};

struct MainWin final : cGZWin {
    explicit MainWin(cIGZWinMgr* mgr) : cGZWin(mgr) {}
};

// ---------------------------------------------------------------------------------------------------------
struct FakeFramework final : cIGZFrameWork {
    cIGZPersistResourceManager* persist = nullptr;
    cIGZWinMgr* mgr = nullptr;
    cIGZCOM* com = nullptr;
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool AddSystemService(cIGZSystemService*) override { return false; }
    bool RemoveSystemService(cIGZSystemService*) override { return false; }
    bool GetSystemService(std::uint32_t serviceId, std::uint32_t iid, void** out) override {
        if (serviceId == kGZSID_PersistResourceManager && iid == kGZIID_PersistResourceManager && persist) {
            persist->AddRef();
            *out = static_cast<cIGZPersistResourceManager*>(persist);
            return true;
        }
        if (serviceId == 0xa417445e && iid == 0x5a4 && mgr) {
            mgr->AddRef();
            *out = mgr;
            return true;
        }
        return false;
    }
    bool EnumSystemServices(cIGZUnknownEnumerator&, cIGZUnknown*, std::uint32_t) override { return false; }
    bool AddHook(cIGZFrameWorkHooks*) override { return false; }
    bool RemoveHook(cIGZFrameWorkHooks*) override { return false; }
    bool AddToTick(cIGZSystemService*) override { return false; }
    bool RemoveFromTick(cIGZSystemService*) override { return false; }
    bool AddToOnIdle(cIGZSystemService*) override { return false; }
    bool RemoveFromOnIdle(cIGZSystemService*) override { return false; }
    int GetOnIdleInterval() const override { return 1; }
    void SetOnIdleInterval(int) override {}
    void OnTick(std::uint32_t) override {}
    void OnIdle() override {}
    bool IsTickEnabled() const override { return true; }
    void ToggleTick(bool) override {}
    void Quit(int) override {}
    void AbortiveQuit(int) override {}
    cIGZCmdLine* CommandLine() override { return nullptr; }
    bool IsInstall() const override { return false; }
    cIGZCOM* GetCOMObject() override { return com; }
    int GetState() const override { return 0; }
    cIGZDebugStream* GetDebugStream() override { return nullptr; }
    cIGZDebugStream* DefaultDebugStream() override { return nullptr; }
    cIGZDebugStream* DebugStream() override { return nullptr; }
    void SetDebugStream(cIGZDebugStream*) override {}
    void SetDebugLevel(int) override {}
    int GetDebugLevel() const override { return 0; }
    cIGZOStream* StdOut() override { return nullptr; }
    cIGZOStream* StdErr() override { return nullptr; }
    cIGZIStream* StdIn() override { return nullptr; }
    bool GetStream(int, std::uint32_t, void**) override { return false; }
    bool SetStream(int, cIGZUnknown*) override { return false; }
    void SetApplication(cIGZApp*) override {}
    cIGZApp* Application() const override { return nullptr; }
    void ReportException(const char*) override {}
    cIGZUnknown* ExceptionNotificationObj() const override { return nullptr; }
};

// ---------------------------------------------------------------------------------------------------------
// The registry: creates the real interpreter (clsid 0xba2e7953) and the fakes for the other three classes.
// ---------------------------------------------------------------------------------------------------------
struct FakeCOM final : cIGZCOM {
    int refs = 0;
    cGZScriptTerp* terp = nullptr;
    int terps = 0;
    std::vector<FakeVocab*> vocabs;
    std::vector<FakeControlSet*> sets;
    std::vector<FakeControl*> controls;
    bool failTerp = false, failVocab = false, failSets = false, failControls = false;
    int unknownClasses = 0;
    FakeVocab* controlVocabulary = nullptr;   // installed on every control this registry creates

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    cIGZFrameWork* GetFrameWork() override { return nullptr; }

    FakeVocab* LastVocab() const { return vocabs.empty() ? nullptr : vocabs.back(); }
    FakeControlSet* LastSet() const { return sets.empty() ? nullptr : sets.back(); }
    FakeControl* LastControl() const { return controls.empty() ? nullptr : controls.back(); }

    bool CreateInstance(std::uint32_t clsid, std::uint32_t iid, void** out) override {
        if (clsid == kGZCLSID_cGZScriptTerp && iid == kGZIID_cIGZScriptTerp) {
            if (failTerp) return false;
            auto* created = new cGZScriptTerp();
            created->AddRef();
            terp = created;
            ++terps;
            *out = static_cast<cIGZScriptTerp*>(created);
            return true;
        }
        if (clsid == kGZCLSID_cGZScriptVocab && iid == kGZIID_cIGZScriptVocab) {
            if (failVocab) return false;
            auto* created = new FakeVocab();
            created->AddRef();
            vocabs.push_back(created);
            *out = static_cast<IScriptVocab*>(created);
            return true;
        }
        if (clsid == kGZCLSID_cGZScriptableControlSet && iid == kGZIID_cIGZScriptableControlSet) {
            if (failSets) return false;
            auto* created = new FakeControlSet();
            created->AddRef();
            sets.push_back(created);
            *out = static_cast<cIGZScriptableControlSet*>(created);
            return true;
        }
        if (clsid == kGZCLSID_cGZLegacyControl && iid == kGZIID_cIGZScriptableControl) {
            if (failControls) return false;
            auto* created = new FakeControl();
            created->AddRef();
            created->vocabulary = controlVocabulary;
            controls.push_back(created);
            *out = static_cast<cIGZScriptableControl*>(created);
            return true;
        }
        ++unknownClasses;
        return false;
    }
};

// ---------------------------------------------------------------------------------------------------------
// A service whose ExecuteScriptResource is scripted: the CreateWindowFromScript cases need to see the key
// that reaches the resource loader and to hand back a prepared control set.
// ---------------------------------------------------------------------------------------------------------
struct TestService final : cGZUIScriptService {
    cIGZScriptableControlSet* scriptSet = nullptr;   // transferred to the caller
    cGZPersistResourceKey lastResourceKey{};
    int resourceCalls = 0;

    cIGZScriptableControlSet* ExecuteScriptResource(const cGZPersistResourceKey& key) override {
        ++resourceCalls;
        lastResourceKey = key;
        cIGZScriptableControlSet* set = scriptSet;
        scriptSet = nullptr;
        return set;
    }
};

// ---------------------------------------------------------------------------------------------------------
// Rig: one fake world per test.
// ---------------------------------------------------------------------------------------------------------
struct Rig {
    FakeCOM com;
    TestTerpHost host;
    FakePersist persist;
    FakeMgr mgr;
    MainWin* mainWin = nullptr;
    FakeFramework framework;
    FakeVocab* controlVocab = nullptr;   // the vocabulary the fake controls hand out

    explicit Rig(int width = 1024, int height = 768) {
        mainWin = new MainWin(&mgr);
        mainWin->SetArea(0, 0, width, height);
        mgr.mainWin = mainWin;
        framework.persist = &persist;
        framework.mgr = &mgr;
        framework.com = &com;
        controlVocab = new FakeVocab();
        controlVocab->AddRef();
        com.controlVocabulary = controlVocab;
        SetScriptTerpHost(&host);
        SetRZFramework(&framework);
        RZGetCOMDllDirector()->InitializeCOM(&com, cRZString(""));
    }
    ~Rig() {
        SetScriptTerpHost(nullptr);
        SetRZFramework(nullptr);
        delete mainWin;
        for (FakeControl* control : com.controls) delete control;
        for (FakeControlSet* set : com.sets) delete set;
        for (FakeVocab* vocab : com.vocabs) delete vocab;
        for (FakeVocab* vocab : host.created) delete vocab;
        delete controlVocab;
    }
};

cGZUIScriptService* MakeService(bool initialize = true) {
    auto* service = new cGZUIScriptService();
    service->AddRef();
    if (initialize) service->Init();
    return service;
}

// Pairs Shutdown with the final Release so the interpreter (which holds three references on the target) is
// released while the service is still alive; Release then deletes the object.
void DropService(cGZUIScriptService* service, bool shutdown = true) {
    if (shutdown) service->Shutdown();
    service->Release();
}

// ---------------------------------------------------------------------------------------------------------
// Property-list helper (keeps the variants alive for the duration of the list).
// ---------------------------------------------------------------------------------------------------------
struct Params {
    std::vector<std::unique_ptr<cRZVariant>> owned;
    tScriptPropertyArray list;
    Params& Uint(std::uint32_t id, std::uint32_t value) {
        owned.push_back(std::make_unique<cRZVariant>(value));
        list.push_back(tScriptProperty{id, owned.back().get()});
        return *this;
    }
    Params& String(std::uint32_t id, const char* text) {
        owned.push_back(std::make_unique<cRZVariant>(cRZString(text)));
        list.push_back(tScriptProperty{id, owned.back().get()});
        return *this;
    }
};

// ---------------------------------------------------------------------------------------------------------
// 1. The object: size, service identity, the six interface views and the reference count.
// ---------------------------------------------------------------------------------------------------------
void TestLayoutAndIdentity() {
    Rig rig;
    auto* service = MakeService();

    CHECK_EQ(sizeof(cGZUIScriptService), 0xb0);
    CHECK_EQ(service->GetServiceID(), kGZSID_cGZUIScriptService);
    CHECK_EQ(service->GetServicePriority(), kGZUIScriptServicePriority);
    // cIGZSystemService's non-const slot has a do-nothing default; cRZSystemService's const overload has the
    // real value (see cIGZSystemService.h).
    CHECK_EQ(service->GetServiceTickPriority(), 0);
    CHECK_EQ(static_cast<const cGZUIScriptService&>(*service).GetServiceTickPriority(), kGZUIScriptServicePriority);
    CHECK(!service->IsServiceRunning());

    void* out = nullptr;
    CHECK(service->QueryInterface(kGZIID_cIGZUIScriptService, &out));
    CHECK(out == static_cast<cIGZUIScriptService*>(service));
    CHECK_EQ(reinterpret_cast<char*>(out) - reinterpret_cast<char*>(service), 0);

    out = nullptr;
    CHECK(service->QueryInterface(kGZIID_cIGZUILegacyScriptService, &out));
    CHECK(out == static_cast<cIGZUILegacyScriptService*>(service));
    CHECK_EQ(reinterpret_cast<char*>(out) - reinterpret_cast<char*>(service), 8);

    out = nullptr;
    CHECK(service->QueryInterface(kGZIID_cIGZScriptTarget, &out));
    CHECK(out == static_cast<cIGZScriptTarget*>(service));
    CHECK_EQ(reinterpret_cast<char*>(out) - reinterpret_cast<char*>(service), 0x10);

    out = nullptr;
    CHECK(service->QueryInterface(kGZIID_cIGZScriptInterpretingTarget, &out));
    CHECK(out == static_cast<cIGZScriptInterpretingTarget*>(service));
    CHECK_EQ(reinterpret_cast<char*>(out) - reinterpret_cast<char*>(service), 0x18);

    out = nullptr;
    CHECK(service->QueryInterface(kGZIID_cIGZScriptCompilingTarget, &out));
    CHECK(out == static_cast<cIGZScriptCompilingTarget*>(service));
    CHECK_EQ(reinterpret_cast<char*>(out) - reinterpret_cast<char*>(service), 0x20);

    out = nullptr;
    CHECK(service->QueryInterface(kGZIID_cIGZUnknown, &out));
    CHECK(out == static_cast<cIGZUIScriptService*>(service));

    // 0x287fb697 is the system service view: the base class answers it at +0x28.
    out = nullptr;
    CHECK(service->QueryInterface(kGZIID_cIGZSystemService, &out));
    CHECK(out == static_cast<cIGZSystemService*>(service));
    CHECK_EQ(reinterpret_cast<char*>(out) - reinterpret_cast<char*>(service), 0x28);

    // An unknown id falls through to cRZSystemService and leaves the out pointer alone.
    void* untouched = reinterpret_cast<void*>(0x1234);
    CHECK(!service->QueryInterface(0xdeadbeef, &untouched));
    CHECK(untouched == reinterpret_cast<void*>(0x1234));

    // The seven successful QueryInterface calls above took seven references; drop them again.
    for (int i = 0; i < 7; ++i) service->Release();
    DropService(service);
}

// ---------------------------------------------------------------------------------------------------------
// 2. Init / Shutdown, including the "no Init, no Shutdown" quirk.
// ---------------------------------------------------------------------------------------------------------
void TestInitShutdown() {
    Rig rig;

    // Without Init, Shutdown does nothing at all - not even release a factory installed beforehand.
    auto* noInit = new cGZUIScriptService();
    noInit->AddRef();
    FakeFactory leaked;
    noInit->SetResourceFactory(&leaked);
    CHECK_EQ(leaked.refs, 1);
    CHECK(noInit->Shutdown());          // the +0x49 gate: a no-op
    CHECK_EQ(leaked.refs, 1);
    noInit->Release();                  // quirk: the factory leaks (the destructor does not release it either)
    CHECK_EQ(leaked.refs, 1);

    auto* service = MakeService();
    CHECK(service->Shutdown());         // a Shutdown without Init is a no-op and still succeeds
    CHECK(service->Init());
    CHECK(service->Init());
    CHECK(!service->IsServiceRunning());   // Init does not touch the service's running flag
    FakeFactory factory;
    service->SetResourceFactory(&factory);
    CHECK_EQ(factory.refs, 1);
    CHECK(service->Shutdown());
    CHECK_EQ(factory.refs, 0);
    CHECK(service->GetResourceFactory() == nullptr);
    DropService(service, false);
}

// ---------------------------------------------------------------------------------------------------------
// 3. TestSetup: the interpreter, its 15 tokens and the script target.
// ---------------------------------------------------------------------------------------------------------
void TestSetup() {
    Rig rig;
    auto* service = MakeService();
    CHECK(service->GetScriptInterpreter() != nullptr);
    CHECK_EQ(rig.com.terps, 1);
    CHECK_EQ(rig.com.vocabs.size(), 0);   // no ScriptBegin yet
    cIGZScriptTerp* terp = service->GetScriptInterpreter();
    CHECK(terp == service->GetScriptInterpreter());   // created once
    CHECK(terp == rig.com.terp);
    CHECK_EQ(rig.com.terps, 1);

    // The tokens land in the interpreter's own (host-created) vocabulary, in two batches.
    FakeVocab* vocabulary = rig.host.created.front();
    CHECK_EQ(vocabulary->entries.size(), 15);
    static const struct { const char* name; std::uint32_t id; } kExpected[] = {
        {"_null", 0},        {"_children", 1}, {"_/children", 2}, {"_define", 3}, {"_name", 4}, {"_val", 5},
        {"none", 0},         {"children", 1},  {"/children", 2},  {"define", 3},  {"name", 4},  {"val", 5},
        {"LEGACY", 0xfa450242u}, {"comic9", 0x12}, {"comic10", 0x13},
    };
    for (std::size_t i = 0; i < 15 && i < vocabulary->entries.size(); ++i) {
        CHECK(vocabulary->entries[i].name == std::string(kExpected[i].name));
        CHECK_EQ(vocabulary->entries[i].value->GetType(), cIGZVariant::kTypeUint32);
        CHECK_EQ(vocabulary->entries[i].value->GetValUint32(), kExpected[i].id);
    }
    DropService(service);
}

// ---------------------------------------------------------------------------------------------------------
// 4. Script execution end to end (real interpreter): controls, parents, define tokens and set ownership.
// ---------------------------------------------------------------------------------------------------------
void TestExecuteScriptString() {
    Rig rig;
    auto* service = MakeService();

    cIGZScriptableControlSet* set = service->ExecuteScriptString(cRZString("<LEGACY 0x100=7 >"));
    CHECK(set != nullptr);
    CHECK(set == rig.com.LastSet());
    CHECK_EQ(rig.com.sets.size(), 1);
    CHECK_EQ(rig.com.controls.size(), 1);
    CHECK_EQ(set->GetSize(), 1u);
    CHECK_EQ(rig.com.LastControl()->setParametersCalls, 1);
    CHECK_EQ(rig.com.LastControl()->id, 7u);
    CHECK_EQ(rig.com.LastControl()->properties.size(), 1);
    CHECK_EQ(rig.com.LastControl()->properties[0].id, 0x100u);
    CHECK_EQ(rig.com.LastControl()->properties[0].uintValue, 7u);
    CHECK(rig.com.LastControl()->parent == nullptr);
    set->Release();

    // The service gave the set away: a second call builds a new one.
    cIGZScriptableControlSet* second = service->ExecuteScriptString(cRZString("<LEGACY 0x100=8 >"));
    CHECK(second != nullptr && second != set);
    CHECK_EQ(second->GetSize(), 1u);
    CHECK(second->GetByID(8) != nullptr);
    second->Release();

    // Children / child-ren: the control becomes the parent of the following controls.
    cIGZScriptableControlSet* tree = service->ExecuteScriptString(
        cRZString("<LEGACY 0x100=1 ><children ><LEGACY 0x100=2 ></children ><LEGACY 0x100=3 >"));
    CHECK(tree != nullptr);
    CHECK_EQ(tree->GetSize(), 3u);
    cIGZScriptableControl* first = tree->GetByID(1);
    cIGZScriptableControl* nested = tree->GetByID(2);
    cIGZScriptableControl* last = tree->GetByID(3);
    CHECK(first && nested && last);
    CHECK(static_cast<FakeControl*>(nested)->parent == first);
    CHECK(static_cast<FakeControl*>(first)->parent == nullptr);
    CHECK(static_cast<FakeControl*>(last)->parent == nullptr);
    tree->Release();

    // define: a name + val pair defines a token in the vocabulary the service pushed for this script; a
    // define without a value removes it again (both in one script, so both see the same vocabulary).
    cIGZScriptableControlSet* defined = service->ExecuteScriptString(
        cRZString("<define name=\"myToken\" val=42 ><define name=\"myToken\" >"));
    CHECK(defined != nullptr);
    CHECK_EQ(defined->GetSize(), 0u);
    defined->Release();
    FakeVocab* serviceVocab = rig.com.LastVocab();
    CHECK(serviceVocab != nullptr);
    CHECK_EQ(serviceVocab->removeCalls, 2);            // the first define's remove misses, the second hits
    CHECK(serviceVocab->Find("myToken") == nullptr);   // so the token is gone again

    // A script with only a definition: the token stays in that script's vocabulary (which is then released).
    cIGZScriptableControlSet* added = service->ExecuteScriptString(cRZString("<define name=\"kept\" val=43 >"));
    CHECK(added != nullptr);
    CHECK_EQ(added->GetSize(), 0u);
    added->Release();
    FakeVocab* addedVocab = rig.com.LastVocab();
    CHECK(addedVocab != serviceVocab);
    auto* token = addedVocab->Find("kept");
    CHECK(token != nullptr);
    if (token) {
        CHECK_EQ(token->value->GetType(), cIGZVariant::kTypeUint32);
        CHECK_EQ(token->value->GetValUint32(), 43u);
    }

    // A script the interpreter rejects: ScriptEnd(false) releases the set, so nothing is returned.
    CHECK(service->ExecuteScriptString(cRZString("<LEGACY =1>")) == nullptr);
    DropService(service);
}

// ---------------------------------------------------------------------------------------------------------
// 5. ExecuteScriptFile: the same path through a real file.
// ---------------------------------------------------------------------------------------------------------
void TestExecuteScriptFile() {
    Rig rig;
    auto* service = MakeService();
    const char* path = "/tmp/gz_uiscriptservice_test_script.txt";
    if (FILE* file = std::fopen(path, "wb")) {
        const char* script = "<LEGACY 0x100=5 >";
        std::fwrite(script, 1, std::strlen(script), file);
        std::fclose(file);
    }
    cIGZScriptableControlSet* set = service->ExecuteScriptFile(cRZString(path));
    CHECK(set != nullptr);
    CHECK_EQ(set->GetSize(), 1u);
    CHECK(set->GetByID(5) != nullptr);
    if (set) set->Release();
    CHECK(service->ExecuteScriptFile(cRZString("/tmp/gz_uiscriptservice_test_missing.txt")) == nullptr);
    std::remove(path);
    DropService(service);
}

// ---------------------------------------------------------------------------------------------------------
// 6. CreateWindowFromScript: the resource key, the window extraction and the 5-argument overload.
// ---------------------------------------------------------------------------------------------------------
void TestCreateWindowFromScript() {
    Rig rig;
    auto* service = new TestService();
    service->AddRef();
    service->Init();

    const cGZPersistResourceKey key{0x1234, 0x5678, 0x9abc};
    auto* set = new FakeControlSet();
    set->AddRef();   // the test's handle
    rig.com.sets.push_back(set);

    auto* control = new FakeControl();
    control->AddRef();   // the test's handle (the set takes another one below)
    control->id = 0x77;
    rig.com.controls.push_back(control);
    set->Insert(control);
    control->Release();

    // The service releases the set it receives; the test's handle keeps the object inspectable.
    auto give = [&]() { service->scriptSet = set; set->AddRef(); };

    give();
    cIGZWin* window = service->CreateWindowFromScript(key, rig.mainWin, 0x77);
    CHECK(window == control->GetControl());
    CHECK_EQ(service->resourceCalls, 1);
    CHECK(!set->ownControls);
    CHECK_EQ(set->ownControlsCalls, 1);
    // SetLegacyParent ran: a legacy control was created and ForceControl got the parent window.
    CHECK_EQ(rig.com.controls.size(), 2);   // the prepared control and the legacy parent wrapper
    CHECK_EQ(rig.com.LastControl()->forceControlCalls, 1);
    CHECK(rig.com.LastControl()->GetControl() == rig.mainWin);
    window->Release();

    // The resolution group quirk: 1024x768 is formatted as "0x10240768" and parsed as hex.
    CHECK_EQ(rig.persist.testForKeyCalls, 1);
    CHECK_EQ(rig.persist.lastKey.type, key.type);
    CHECK_EQ(rig.persist.lastKey.instance, key.instance);
    CHECK_EQ(rig.persist.lastKey.group, 0x10240768);
    // ... and the service keeps the computed group only when the persistence manager has such a resource.
    CHECK_EQ(service->lastResourceKey.group, key.group);
    rig.persist.found = true;
    give();
    cIGZWin* again = service->CreateWindowFromScript(key, nullptr, 0x77);
    CHECK(again != nullptr);
    CHECK_EQ(rig.persist.testForKeyCalls, 2);
    CHECK_EQ(service->lastResourceKey.group, 0x10240768);
    again->Release();

    // Without a framework (and therefore without a persistence service) the key reaches the loader unchanged.
    SetRZFramework(nullptr);
    give();
    cIGZWin* plain = service->CreateWindowFromScript(key, nullptr, 0x77);
    CHECK(plain != nullptr);
    CHECK_EQ(service->lastResourceKey.group, key.group);
    CHECK_EQ(rig.persist.testForKeyCalls, 2);
    plain->Release();
    SetRZFramework(&rig.framework);

    // Failure paths: no resource at all, no control with that id.
    CHECK(service->CreateWindowFromScript(key, nullptr, 0x77) == nullptr);   // the previous call consumed the set
    give();
    CHECK(service->CreateWindowFromScript(key, nullptr, 0x99) == nullptr);   // unknown id
    CHECK_EQ(set->ownControlsCalls, 4);                                     // SetOwnControls(false) still ran
    void* out = nullptr;
    give();
    CHECK(!service->CreateWindowFromScript(key, nullptr, 0x99, 0x22ba0121, &out));
    CHECK(out == nullptr);

    // The 5-argument overload asks the extracted window for the requested interface: cIGZWin answers iid 1.
    give();
    out = nullptr;
    CHECK(service->CreateWindowFromScript(key, nullptr, 0x77, kGZIID_cIGZUnknown, &out));
    CHECK(out == static_cast<cIGZWin*>(control->GetControl()));
    static_cast<cIGZWin*>(out)->Release();   // the QueryInterface reference

    set->Release();   // the test's handle
    DropService(service);
}

// ---------------------------------------------------------------------------------------------------------
// 7. SetLegacyParent / SetLegacyWinProc.
// ---------------------------------------------------------------------------------------------------------
void TestLegacyWiring() {
    Rig rig;
    auto* service = MakeService();
    cIGZScriptTerp* terp = service->GetScriptInterpreter();
    CHECK(terp != nullptr);

    // A control set is needed for ScriptTag to consume a control.
    CHECK(service->ScriptBegin(static_cast<cGZScriptTerp*>(terp)));
    FakeControlSet* set = rig.com.LastSet();
    CHECK(set != nullptr);

    // SetLegacyParent wraps the window in a fresh legacy control and pushes it as the current parent.
    CHECK(service->SetLegacyParent(rig.mainWin));
    CHECK_EQ(rig.com.controls.size(), 1);
    FakeControl* wrapper = rig.com.LastControl();
    CHECK_EQ(wrapper->forceControlCalls, 1);
    CHECK(wrapper->GetControl() == rig.mainWin);
    CHECK_EQ(wrapper->refs, 1);   // only the parent stack holds it

    // The next control's parent is that wrapper; the control set takes the control on ScriptTag.
    CHECK(service->BeginExecuteTag(static_cast<cGZScriptTerp*>(terp), kGZCLSID_cGZLegacyControl));
    FakeControl* child = rig.com.LastControl();
    CHECK(child != wrapper);
    CHECK(child->parent == static_cast<cIGZScriptableControl*>(wrapper));
    CHECK(service->EndExecuteTag(static_cast<cGZScriptTerp*>(terp), kGZCLSID_cGZLegacyControl));
    Params none;
    CHECK(service->ScriptTag(static_cast<cGZScriptTerp*>(terp), kGZCLSID_cGZLegacyControl, none.list));
    CHECK_EQ(set->GetSize(), 1u);
    CHECK_EQ(wrapper->refs, 1);   // still only the parent stack

    // <children> pushes the (now pending) child; </children> pops it again, leaving the wrapper on top.
    CHECK(service->ScriptTag(static_cast<cGZScriptTerp*>(terp), 1, none.list));
    CHECK_EQ(child->refs, 2);     // set + parent stack
    CHECK(service->ScriptTag(static_cast<cGZScriptTerp*>(terp), 2, none.list));
    CHECK_EQ(child->refs, 1);
    // The next control is a child of the wrapper again.
    CHECK(service->BeginExecuteTag(static_cast<cGZScriptTerp*>(terp), kGZCLSID_cGZLegacyControl));
    CHECK(rig.com.LastControl()->parent == static_cast<cIGZScriptableControl*>(wrapper));
    CHECK(service->ScriptEnd(static_cast<cGZScriptTerp*>(terp), false));   // releases the set and the pending control
    CHECK_EQ(wrapper->refs, 0);

    // The window procedure: the same pointer is a no-op, a change releases the old one before adding the new one.
    FakeWinProc first, second;
    CHECK(service->SetLegacyWinProc(&first));
    CHECK_EQ(first.refs, 1);
    CHECK(service->SetLegacyWinProc(&first));
    CHECK_EQ(first.refs, 1);
    CHECK(service->SetLegacyWinProc(&second));
    CHECK_EQ(first.refs, 0);
    CHECK_EQ(second.refs, 1);
    // A new control picks the current procedure up through cIGZLegacyControl::UseWinProc.
    CHECK(service->BeginExecuteTag(static_cast<cGZScriptTerp*>(terp), kGZCLSID_cGZLegacyControl));
    CHECK(rig.com.LastControl()->winProc == &second);
    CHECK_EQ(rig.com.LastControl()->useWinProcCalls, 1);
    // Quirk: Shutdown does not release the window procedure (nor the vocabulary) it still holds.
    service->Shutdown();
    CHECK_EQ(second.refs, 1);
    service->Release();
}

// ---------------------------------------------------------------------------------------------------------
// 8. The interpreter callbacks called directly: Begin/EndExecuteTag, ApplyProperty, ScriptLiteral, ScriptTag.
// ---------------------------------------------------------------------------------------------------------
void TestCallbacks() {
    Rig rig;
    auto* service = MakeService();
    cIGZScriptTerp* terp = service->GetScriptInterpreter();
    CHECK(terp != nullptr);

    // ScriptBegin: one vocabulary + one control set; the vocabulary is pushed on the interpreter.
    CHECK(service->ScriptBegin(static_cast<cGZScriptTerp*>(terp)));
    CHECK_EQ(rig.com.vocabs.size(), 1);
    FakeVocab* vocab = rig.com.LastVocab();
    CHECK_EQ(vocab->refs, 2);   // creation + the interpreter's push
    FakeControlSet* set = rig.com.LastSet();
    CHECK_EQ(set->refs, 1);

    // BeginExecuteTag: the new control hands out controlVocab, so the service pushes it (refs 1 -> 2).
    CHECK(service->BeginExecuteTag(static_cast<cGZScriptTerp*>(terp), kGZCLSID_cGZLegacyControl));
    FakeControl* control = rig.com.LastControl();
    CHECK(control != nullptr);
    CHECK_EQ(rig.controlVocab->refs, 2);
    // EndExecuteTag pops it again.
    CHECK(service->EndExecuteTag(static_cast<cGZScriptTerp*>(terp), kGZCLSID_cGZLegacyControl));
    CHECK_EQ(rig.controlVocab->refs, 1);

    // ScriptTag with the current control and a set: SetParameters + Insert + Release.
    Params none;
    CHECK(service->ScriptTag(static_cast<cGZScriptTerp*>(terp), kGZCLSID_cGZLegacyControl, none.list));
    CHECK_EQ(control->setParametersCalls, 1);
    CHECK_EQ(set->GetSize(), 1u);
    // The pending parent is now that control.
    CHECK(service->ScriptTag(static_cast<cGZScriptTerp*>(terp), 1, none.list));   // <children>
    CHECK_EQ(control->refs, 2);                                                   // set + parent stack
    CHECK(service->ScriptTag(static_cast<cGZScriptTerp*>(terp), 2, none.list));   // </children>
    CHECK_EQ(control->refs, 1);

    // A failed tag creation: false, the tag-active flag stays down, so EndExecuteTag does not pop.
    rig.com.failControls = true;
    CHECK(!service->BeginExecuteTag(static_cast<cGZScriptTerp*>(terp), 0x12345678));
    CHECK(service->EndExecuteTag(static_cast<cGZScriptTerp*>(terp), 0x12345678));
    CHECK_EQ(vocab->refs, 2);
    rig.com.failControls = false;

    // ApplyProperty and ScriptLiteral do nothing and answer true.
    auto value = std::make_unique<cRZVariant>(7u);
    CHECK(service->ApplyProperty(static_cast<cGZScriptTerp*>(terp), 0x100, value.get()));
    cRZString literal("text");
    CHECK(service->ScriptLiteral(static_cast<cGZScriptTerp*>(terp), &literal));

    // ScriptTag with no current control: unknown tags answer false, the pseudo tags answer true.
    CHECK(!service->ScriptTag(static_cast<cGZScriptTerp*>(terp), 0x12345678, none.list));
    CHECK(!service->ScriptTag(static_cast<cGZScriptTerp*>(terp), 0, none.list));
    CHECK(service->ScriptTag(static_cast<cGZScriptTerp*>(terp), 1, none.list));   // no pending parent: no-op
    CHECK(service->ScriptTag(static_cast<cGZScriptTerp*>(terp), 2, none.list));   // empty stack: no-op

    // ScriptTag(tag 3) needs a string-valued "name"; a uint32 name variant is ignored.
    Params numericName;
    numericName.Uint(4, 12);
    CHECK(service->ScriptTag(static_cast<cGZScriptTerp*>(terp), 3, numericName.list));
    CHECK_EQ(vocab->removeCalls, 0);
    Params stringName;
    stringName.String(4, "token");
    CHECK(service->ScriptTag(static_cast<cGZScriptTerp*>(terp), 3, stringName.list));   // no value: RemoveToken
    CHECK_EQ(vocab->removeCalls, 1);
    CHECK(vocab->Find("token") == nullptr);
    Params define;
    define.String(4, "token");
    // The vocabulary takes its own reference to the value, so this one is allocated for it (the fake's
    // AddTranslation AddRefs, like the original's vocabulary) and never owned by the Params vector.
    define.list.push_back(tScriptProperty{5, new cRZVariant(99u)});
    CHECK(service->ScriptTag(static_cast<cGZScriptTerp*>(terp), 3, define.list));
    CHECK_EQ(vocab->removeCalls, 2);
    auto* entry = vocab->Find("token");
    CHECK(entry != nullptr);
    if (entry) CHECK_EQ(entry->value->GetValUint32(), 99u);

    // ScriptEnd(false): releases the set, the current control, the tag vocabulary and the parent stack.
    CHECK(service->BeginExecuteTag(static_cast<cGZScriptTerp*>(terp), kGZCLSID_cGZLegacyControl));
    FakeControl* pending = rig.com.LastControl();
    CHECK_EQ(pending->refs, 1);   // the COM creation reference held by the service's current-control slot
    CHECK(service->ScriptEnd(static_cast<cGZScriptTerp*>(terp), false));
    CHECK_EQ(vocab->refs, 0);     // popped by the tag and released by the service
    CHECK_EQ(set->refs, 0);       // released by the failure path
    CHECK_EQ(pending->refs, 0);
    DropService(service);
}

// ---------------------------------------------------------------------------------------------------------
// 9. ScriptEnd(success) after a ScriptBegin that could not create the vocabulary: the original dereferences
//    the null pointer; the port answers without crashing (documented deviation).
// ---------------------------------------------------------------------------------------------------------
void TestMissingVocabulary() {
    Rig rig;
    auto* service = MakeService();
    cIGZScriptTerp* terp = service->GetScriptInterpreter();
    rig.com.failVocab = true;
    CHECK(service->ScriptBegin(static_cast<cGZScriptTerp*>(terp)));   // the set creation still succeeds
    CHECK_EQ(rig.com.vocabs.size(), 0);
    CHECK_EQ(rig.com.sets.size(), 1);
    CHECK(service->ScriptEnd(static_cast<cGZScriptTerp*>(terp), true));
    CHECK_EQ(rig.com.LastSet()->refs, 1);   // success keeps the set for the caller (Execute* would return it)
    rig.com.LastSet()->Release();
    DropService(service);
}

// ---------------------------------------------------------------------------------------------------------
// 10. The resource factory: AddRef before Release, and the same-pointer no-op.
// ---------------------------------------------------------------------------------------------------------
void TestResourceFactory() {
    Rig rig;
    auto* service = MakeService();
    FakeFactory factory;
    CHECK(service->GetResourceFactory() == nullptr);
    service->SetResourceFactory(&factory);
    CHECK_EQ(factory.refs, 1);
    CHECK(service->GetResourceFactory() == &factory);
    service->SetResourceFactory(&factory);   // same pointer: nothing happens
    CHECK_EQ(factory.refs, 1);
    service->SetResourceFactory(nullptr);
    CHECK_EQ(factory.refs, 0);
    CHECK(service->GetResourceFactory() == nullptr);
    service->SetResourceFactory(&factory);
    CHECK_EQ(factory.refs, 1);
    CHECK(service->Shutdown());
    CHECK_EQ(factory.refs, 0);
    DropService(service, false);
}

// ---------------------------------------------------------------------------------------------------------
// 11. Quirk: the destructor's std::list clear only frees the nodes, it never releases the controls (the
//     binary's clear at 0x10054c9f6 is the plain libc++ list clean-up). Only Shutdown's PopParent loop does;
//     a service destroyed without Init therefore leaks whatever its parent stack holds.
// ---------------------------------------------------------------------------------------------------------
void TestDestructorQuirks() {
    Rig rig;
    auto* service = new cGZUIScriptService();
    service->AddRef();
    CHECK(service->SetLegacyParent(nullptr));
    CHECK_EQ(rig.com.controls.size(), 1);
    FakeControl* parent = rig.com.LastControl();
    CHECK_EQ(parent->refs, 1);   // the parent stack owns the only reference
    service->Release();          // Shutdown is a no-op (no Init), the list clear does not release
    CHECK_EQ(parent->refs, 1);
}

}  // namespace

int main() {
    TestLayoutAndIdentity();
    TestInitShutdown();
    TestSetup();
    TestExecuteScriptString();
    TestExecuteScriptFile();
    TestCreateWindowFromScript();
    TestLegacyWiring();
    TestCallbacks();
    TestMissingVocabulary();
    TestResourceFactory();
    TestDestructorQuirks();
    if (failures == 0) std::printf("all cGZUIScriptService tests passed\n");
    return failures == 0 ? 0 : 1;
}
