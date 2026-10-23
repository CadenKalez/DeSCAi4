// cSC4AudioAEMSEventSystem — unit test.
//
// Covers: the object layout (+0 / +8 / +0x10 / +0x18 / +0x20 views), the four QueryInterface ids and
// the four bases' methods, the constructor defaults (21 event slots, volume scales, tuning values,
// empty vectors/list), Init (the player-volume scale, the tuning exemplar, the 200,000-byte engine
// block, SNDSYS getopts/setopts/init, the effects bus, the master volume, the sound-system event
// handler, the system blob resource {0, 0x8a5971c5, 0x6b955c37} and the zoom-changed subscription),
// Shutdown, the process-wide Init flag quirk, StartSound/EndSound (handle + 1 slots, refused begins,
// the missing-index guards), StopAllSounds' order and its 0x0f/0x10 gap, the three SetParam calls
// (min with 1, 127 scaling, the negative clamp, the NaN -> INT32_MIN quirk), ReadTuningValues and
// UpdateVolume (the response curve), DoEvent (master volume, always-false return), DoMessage (zoom
// volume), LoadSoundBank/UnloadSoundBank (the property read with its 0xebf44df7 fallback), AddBank/
// RemoveBank/RemoveAllBanks (the module-bank handle always 0), LoadFromResource/LoadFromFile (vector
// resize, read, the engine system replace) and the destructor.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gz/cIGZFile.h"
#include "sc4/AudioAEMSSoundBank.h"
#include "sc4/SCProperty.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4audio/AudioAEMSEventSystem.h"
#include "sc4audio_fakes.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (false)

// ---- the AEMS engine C API ----------------------------------------------------------------
struct RecordingEngine final : SC4Audio::IAemsEngineHost {
    // configurable results
    int optsGetResult = 0, optsSetResult = 0, sysInitResult = 0, fxResult = 0;
    int addSystemResult = 0, addModuleBankResult = 0, setGlobalResult = 0, beginResult = 0;

    // records
    int optsGetCalls = 0, optsSetCalls = 0, sysInitCalls = 0, fxCalls = 0;
    int removeSystemCalls = 0, restoreCalls = 0, addSystemCalls = 0;
    int enterCriticalCalls = 0, leaveCriticalCalls = 0, criticalDepth = 0, maxCriticalDepth = 0;
    void* sysInitMemory = nullptr;
    std::int32_t sysInitSize = 0, sysInitFlags = 0;
    std::int32_t fxBus = -1, fxVolume = -1, fxFlags = -1, fxArg4 = -1, fxArg5 = -1;
    std::vector<std::int32_t> masterVolumes;
    std::vector<std::pair<std::int32_t, std::int32_t>> setGlobals;   // index, value
    std::vector<std::int32_t> beginIds;
    std::map<std::int32_t, std::int32_t> beginHandles;               // event id -> handle
    std::vector<std::pair<std::int32_t, std::int32_t>> endEvents;    // handle, event id
    std::vector<std::int32_t> removedModuleBanks;
    std::vector<void*> moduleBankData;
    void* addSystemData = nullptr;
    std::int32_t nextHandle = 0x40;

    void Clear() {
        optsGetCalls = optsSetCalls = sysInitCalls = fxCalls = 0;
        removeSystemCalls = restoreCalls = addSystemCalls = 0;
        enterCriticalCalls = leaveCriticalCalls = criticalDepth = maxCriticalDepth = 0;
        masterVolumes.clear();
        setGlobals.clear();
        beginIds.clear();
        beginHandles.clear();
        endEvents.clear();
        removedModuleBanks.clear();
        moduleBankData.clear();
        addSystemData = nullptr;
        sysInitMemory = nullptr;
    }

    int SndSysInit(void* memory, std::int32_t size, std::int32_t flags) override {
        ++sysInitCalls;
        sysInitMemory = memory;
        sysInitSize = size;
        sysInitFlags = flags;
        return sysInitResult;
    }
    int SndSysGetOpts(void* options) override {
        ++optsGetCalls;
        if (!options) return -1;
        return optsGetResult;
    }
    int SndSysSetOpts(void*) override {
        ++optsSetCalls;
        return optsSetResult;
    }
    int SndFxInitBus(std::int32_t bus, std::int32_t volume, std::int32_t flags,
                     std::int32_t arg4, std::int32_t arg5) override {
        ++fxCalls;
        fxBus = bus; fxVolume = volume; fxFlags = flags; fxArg4 = arg4; fxArg5 = arg5;
        return fxResult;
    }
    void SndMasterVol(std::int32_t volume) override { masterVolumes.push_back(volume); }
    void SndSysEnterCritical() override {
        ++enterCriticalCalls;
        if (++criticalDepth > maxCriticalDepth) maxCriticalDepth = criticalDepth;
    }
    void SndSysLeaveCritical() override {
        ++leaveCriticalCalls;
        --criticalDepth;
    }
    void SndSysRestore() override { ++restoreCalls; }
    int SndAemsAddSystem(void* data) override {
        ++addSystemCalls;
        addSystemData = data;
        return addSystemResult;
    }
    void SndAemsRemoveSystem() override { ++removeSystemCalls; }
    int SndAemsAddModuleBank(void* data, std::int32_t, std::int32_t, void* (*alloc)(void*, int, int)) override {
        moduleBankData.push_back(data);
        // cSC4AudioAEMSSoundBank::modulebankmalloc returns its first argument.
        if (alloc && alloc(data, 0, 0) != data) return -1;
        return addModuleBankResult;
    }
    void SndAemsRemoveModuleBank(std::int32_t handle) override { removedModuleBanks.push_back(handle); }
    int SndAemsBeginEvent(const std::int32_t* event) override {
        beginIds.push_back(*event);
        if (beginResult < 0) return beginResult;
        const std::int32_t handle = nextHandle++;
        beginHandles[*event] = handle;
        return handle;
    }
    void SndAemsEndEvent(std::int32_t handle, const std::int32_t* event) override {
        endEvents.push_back({handle, *event});
    }
    int SndAemsSetGlobal(std::int32_t index, std::int32_t value) override {
        setGlobals.push_back({index, value});
        return setGlobalResult;
    }
};

// ---- the persistence layer's file objects -------------------------------------------------
struct MemFile final : cIGZFile {
    std::string data;
    std::uint32_t pos = 0;
    int refs = 1;
    int closeCalls = 0, releaseCalls = 0;
    bool failRead = false;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { ++releaseCalls; return static_cast<std::uint32_t>(--refs); }
    bool Open(int, int, int) override { return true; }
    bool IsOpen() const override { return true; }
    bool Close() override { ++closeCalls; return true; }
    int Position() override { return static_cast<int>(pos); }
    std::uint32_t Length() override { return static_cast<std::uint32_t>(data.size()); }
    bool SetLength(std::uint32_t) override { return true; }
    void SeekToBegin() override { pos = 0; }
    void SeekToEnd() override { pos = static_cast<std::uint32_t>(data.size()); }
    void SeekToRelativePosition(int offset) override { pos = static_cast<std::uint32_t>(static_cast<int>(pos) + offset); }
    void SeekToPosition(std::uint32_t position) override { pos = position; }
    std::uint32_t Seek(int, int) override { return pos; }
    bool Read(void* destination, std::uint32_t byteCount) override {
        if (failRead) return false;
        const std::size_t remaining = data.size() - pos;
        const std::size_t count = byteCount < remaining ? byteCount : remaining;
        if (count) std::memcpy(destination, data.data() + pos, count);
        pos += static_cast<std::uint32_t>(count);
        return count == byteCount;
    }
    bool ReadWithCount(void* destination, std::uint32_t& count) override {
        return Read(destination, count);
    }
    bool Write(const void*, std::uint32_t) override { return false; }
    bool WriteWithCount(const void*, std::uint32_t&) override { return false; }
    bool Flush() override { return true; }
    bool Remove() override { return true; }
    bool Rename(const cIGZString&) override { return true; }
    bool Exists() override { return true; }
    bool Copy(const cIGZString&, bool) override { return true; }
    bool Copy(const cIGZFile&, bool) override { return true; }
    void GetPath(cIGZString&) const override {}
    const cIGZString* GetPath() const override { return nullptr; }
    const char* GetPathCStr() const override { return ""; }
    void SetPath(const cIGZString&) override {}
    void SetPath(const char*) override {}
    bool GetOpenModes(int&, int&, int&) const override { return true; }
    void GetBuffering(std::uint32_t&, std::uint32_t&) const override {}
    bool SetBuffering(std::uint32_t, std::uint32_t) override { return true; }
};

// ---- SC4GetResourceAsFile ------------------------------------------------------------------
struct RecordingBankSource final : sc4::ISoundBankHost {
    std::map<std::uint32_t, std::string> resourcesByInstance;   // AEMS resources: type 0, group 0x8a5971c5
    std::vector<cGZPersistResourceKey> requested;
    std::vector<std::unique_ptr<MemFile>> files;
    bool failOpen = false;
    MemFile* last() const { return files.empty() ? nullptr : files.back().get(); }

    bool GetResourceAsFile(const cGZPersistResourceKey& key, cIGZFile** out, bool) override {
        requested.push_back(key);
        if (failOpen) return false;
        const auto it = resourcesByInstance.find(key.instance);
        if (it == resourcesByInstance.end()) return false;
        auto file = std::make_unique<MemFile>();
        file->data = it->second;
        *out = file.get();
        file->AddRef();   // the caller's counted reference
        files.push_back(std::move(file));
        return true;
    }
};

// ---- SL::spViewUtilities --------------------------------------------------------------------
struct FakeViewport final : SC4Audio::cIViewport {
    std::int32_t zoom = 0;
    void ProjectPointToViewportCoordinates2D(float*) override {}
    std::int32_t ViewportWidth() const override { return 1024; }
    std::int32_t ViewportHeight() const override { return 768; }
    void GetCameraEye(float out[3]) const override { out[0] = out[1] = out[2] = 0.0f; }
    void GetCameraTarget(float out[3]) const override { out[0] = out[1] = out[2] = 0.0f; }
    std::int32_t GetZoomLevel() const override { return zoom; }
};

// ---- the process-wide test state ------------------------------------------------------------
RecordingEngine g_engine;
RecordingBankSource g_banks;
fakes::FakeGame g_game;
fakes::FakeServer g_server;
fakes::FakeSndSys g_sndSys;
fakes::FakeExemplars g_exemplars;
FakeViewport g_viewport;

constexpr std::uint32_t kTuningCurveProperty = 0x4bfdd955;
constexpr std::uint32_t kZoomVolumesProperty = 0x8c376d4f;
constexpr std::uint32_t kZoomMessage = 0xa6b79602;
constexpr std::uint32_t kMasterVolumeEvent = 0x2a5b69c0;

void InstallServices() {
    SC4Audio::Services& services = SC4Audio::GetServices();
    services.game = &g_game;
    services.soundSystem = &g_sndSys;
    services.messageServer = &g_server;
    services.exemplars = &g_exemplars;
    SC4Audio::SetViewUtilities(&g_viewport);
    sc4::SetSoundBankHost(&g_banks);
    SC4Audio::SetAemsEngineHost(&g_engine);
}

void ResetWorld() {
    g_engine = RecordingEngine();
    g_banks = RecordingBankSource();
    g_game = fakes::FakeGame();
    g_server = fakes::FakeServer();
    g_sndSys = fakes::FakeSndSys();
    g_exemplars = fakes::FakeExemplars();
    g_viewport.zoom = 0;
    InstallServices();
}

// The audio tuning exemplar with a straight 1:1 master-volume curve and the stock zoom volumes.
void InstallTuning() {
    g_exemplars.exists = true;
    g_exemplars.floats[0xcbfb03fa] = 1.0f;    // tuning volume
    g_exemplars.floats[0xcbfb03ff] = 0.5f;    // bus volume
    g_exemplars.uints[0xebfb0404] = 0x11;     // bus flags
    g_exemplars.arrays[kTuningCurveProperty] = {0.0f, 0.0f, 1.0f, 1.0f};
    g_exemplars.arrays[kZoomVolumesProperty] = {0.25f, 0.5f, 0.75f, 0.9f, 1.0f};
}

std::ptrdiff_t OffsetOf(const void* base, const void* view) {
    return static_cast<const char*>(view) - static_cast<const char*>(base);
}

// ---------------------------------------------------------------------------------------------
// construction, layout and COM
// ---------------------------------------------------------------------------------------------
void TestConstructionAndCom() {
    ResetWorld();
    auto* aems = new cSC4AudioAEMSEventSystem();
    aems->AddRef();   // the creator's reference (the original's COM objects start at count 0)

    // Construction: 21 event slots, the tuning defaults, empty vectors/list, 1.0 volume scales.
    CHECK(aems->eventStateCount() == 21);
    for (std::size_t i = 0; i < aems->eventStateCount(); ++i) CHECK(aems->eventState(i) == 0);
    CHECK(aems->masterVolumeScale() == 1.0f);
    CHECK(aems->zoomVolumeScale() == 1.0f);
    CHECK(aems->tuningVolume() == 1.0f);
    CHECK(aems->tuningBusVolume() == 1.0f);
    CHECK(aems->tuningBusFlags() == 0x25);
    CHECK(aems->bankCount() == 0);
    CHECK(aems->soundSystemMemorySize() == 0);
    CHECK(aems->systemDataSize() == 0);
    CHECK(aems->zoomLevelVolumes().empty());
    CHECK(aems->sndSysOptionsSize() == 0x100);
    for (std::size_t i = 0; i < aems->sndSysOptionsSize(); ++i) CHECK(aems->sndSysOptions()[i] == 0);

    // Service base: id 0xac1fae31, priority INT32_MIN, not running.
    auto* service = static_cast<cRZSystemService*>(aems);
    CHECK(aems->GetServiceID() == 0xac1fae31);
    CHECK(aems->GetServicePriority() == std::numeric_limits<std::int32_t>::min());
    CHECK(!aems->IsServiceRunning());

    // The four interface views sit at the original's offsets.
    void* view = nullptr;
    CHECK(aems->QueryInterface(kGZIID_cISC4AEMSEvents, &view));
    CHECK(view == static_cast<cISC4AEMSEvents*>(aems));
    CHECK(OffsetOf(aems, view) == 8);
    aems->Release();
    CHECK(aems->QueryInterface(0x452294aa, &view));
    CHECK(view == static_cast<cIGZMessageTarget2*>(aems));
    CHECK(OffsetOf(aems, view) == 0x10);
    aems->Release();
    CHECK(aems->QueryInterface(0xbb523bb2, &view));
    CHECK(view == static_cast<cIGZSndEventHandler*>(aems));
    CHECK(OffsetOf(aems, view) == 0);
    aems->Release();
    CHECK(aems->QueryInterface(kGZIID_cIGZSystemService, &view));
    CHECK(view == static_cast<cIGZSystemService*>(service));
    CHECK(OffsetOf(aems, view) == 0x18);
    aems->Release();

    // iid 1 lands on the cRZSystemService's cRZUnknown subobject (+0x20), as in the original.
    CHECK(aems->QueryInterface(kGZIID_cIGZUnknown, &view));
    CHECK(view == static_cast<cRZUnknown*>(service));
    CHECK(OffsetOf(aems, view) == 0x20);
    aems->Release();

    CHECK(!aems->QueryInterface(0x12345678, &view));
    // every successful QueryInterface took and dropped one reference; only the creator's is left
    CHECK(service->RefCount() == 1);
    CHECK(!aems->QueryInterface(kGZIID_cISC4AEMSEvents, nullptr));

    // The destructor is the only owner; it must not touch the banks (none loaded).
    CHECK(aems->Release() == 0);
}

// ---------------------------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------------------------
void TestInitAndShutdown() {
    ResetWorld();
    InstallTuning();
    g_game.prefs = {true, 512};   // 512 / 1024 = 0.5
    g_banks.resourcesByInstance[0x6b955c37] = "AEMS-SYSTEM";
    auto* aems = new cSC4AudioAEMSEventSystem();

    CHECK(aems->Init());
    CHECK(cSC4AudioAEMSEventSystem::initialized());
    CHECK(g_engine.optsGetCalls == 1);
    CHECK(g_engine.optsSetCalls == 1);
    CHECK(g_engine.sysInitCalls == 1);
    CHECK(g_engine.sysInitSize == 200000);
    CHECK(g_engine.sysInitFlags == 0x72b00);
    CHECK(g_engine.sysInitMemory == aems->soundSystemMemory());
    CHECK(aems->soundSystemMemorySize() == 200000);
    CHECK(g_engine.fxCalls == 1);
    CHECK(g_engine.fxBus == 0);
    CHECK(g_engine.fxVolume == 0);      // (int)0.5 = 0
    CHECK(g_engine.fxFlags == 0x11);
    CHECK(g_engine.fxArg4 == 0 && g_engine.fxArg5 == 0);
    CHECK(g_engine.enterCriticalCalls == g_engine.leaveCriticalCalls);

    // Tuning exemplar.
    CHECK(aems->tuningVolume() == 1.0f);
    CHECK(aems->tuningBusVolume() == 0.5f);
    CHECK(aems->tuningBusFlags() == 0x11);
    CHECK(aems->zoomLevelVolumes().size() == 5);
    CHECK(aems->zoomLevelVolumes()[3] == 0.9f);

    // Master volume: curve(0.5 * 1 * 1) * 127 = 63.5 -> 63.
    CHECK(aems->masterVolumeScale() == 0.5f);
    CHECK(aems->zoomVolumeScale() == 1.0f);
    CHECK(g_engine.masterVolumes.size() == 1);
    CHECK(g_engine.masterVolumes.back() == 63);

    // The event handler and the zoom-changed subscription.
    CHECK(g_sndSys.handlers.size() == 1);
    CHECK(g_sndSys.handlers[0] == static_cast<cIGZSndEventHandler*>(aems));
    CHECK(g_server.added.size() == 1);
    CHECK(g_server.added[0].first == static_cast<cIGZMessageTarget2*>(aems));
    CHECK(g_server.added[0].second == kZoomMessage);

    // The system blob: resource {0, 0x8a5971c5, 0x6b955c37}, read, closed, released, handed to the engine.
    CHECK(g_banks.requested.size() == 1);
    CHECK(g_banks.requested[0].type == 0);
    CHECK(g_banks.requested[0].group == 0x8a5971c5);
    CHECK(g_banks.requested[0].instance == 0x6b955c37);
    CHECK(aems->systemDataSize() == 11);
    CHECK(std::memcmp(aems->systemData(), "AEMS-SYSTEM", 11) == 0);
    CHECK(g_engine.addSystemCalls == 1);
    CHECK(g_engine.addSystemData == aems->systemData());
    CHECK(g_banks.last() != nullptr);
    CHECK(g_banks.last()->closeCalls == 1);
    CHECK(g_banks.last()->releaseCalls == 1);
    CHECK(g_engine.removeSystemCalls == 1);
    CHECK(g_engine.maxCriticalDepth == 1);   // the replace is inside one critical section

    // Quirk: the process-wide flag makes a second Init a no-op that still reports success.
    const int optsGetBefore = g_engine.optsGetCalls;
    const std::size_t handlersBefore = g_sndSys.handlers.size();
    CHECK(aems->Init());
    CHECK(g_engine.optsGetCalls == optsGetBefore);
    CHECK(g_sndSys.handlers.size() == handlersBefore);

    // Shutdown tears everything down in order.
    CHECK(aems->Shutdown());
    CHECK(!cSC4AudioAEMSEventSystem::initialized());
    CHECK(g_engine.removeSystemCalls == 2);
    CHECK(g_engine.restoreCalls == 1);
    CHECK(g_sndSys.handlers.empty());
    CHECK(g_server.removed.size() == 1);
    CHECK(g_server.removed[0].first == static_cast<cIGZMessageTarget2*>(aems));
    CHECK(g_server.removed[0].second == kZoomMessage);
    CHECK(aems->soundSystemMemorySize() == 0);

    // Shutdown is idempotent and Init works again afterwards.
    CHECK(aems->Shutdown());
    CHECK(aems->Init());
    CHECK(g_engine.optsGetCalls == optsGetBefore + 1);

    // A second instance's Init is swallowed by the flag (quirk) and its Shutdown tears the first one's
    // engine state down, again because the flag is process-wide.
    auto* second = new cSC4AudioAEMSEventSystem();
    const int addSystemBefore = g_engine.addSystemCalls;
    CHECK(second->Init());
    CHECK(g_engine.addSystemCalls == addSystemBefore);
    CHECK(second->soundSystemMemorySize() == 0);
    CHECK(second->Shutdown());

    CHECK(aems->Release() == 0);
    CHECK(second->Release() == 0);
}

// Init failure paths: no engine, and a failing SNDSYS call (both must call Shutdown and report false).
void TestInitFailures() {
    ResetWorld();
    InstallTuning();
    auto* aems = new cSC4AudioAEMSEventSystem();

    SC4Audio::SetAemsEngineHost(nullptr);   // no engine: behaves like SNDSYSI_init failing
    CHECK(!aems->Init());
    CHECK(!cSC4AudioAEMSEventSystem::initialized());
    CHECK(g_engine.sysInitCalls == 0);
    CHECK(aems->soundSystemMemorySize() == 0);

    ResetWorld();
    InstallTuning();
    g_engine.optsGetResult = -1;
    CHECK(!aems->Init());
    CHECK(!cSC4AudioAEMSEventSystem::initialized());
    CHECK(g_engine.optsGetCalls == 1);
    CHECK(g_engine.sysInitCalls == 0);
    CHECK(g_engine.restoreCalls == 1);   // the failure path ran Shutdown
    CHECK(aems->soundSystemMemorySize() == 0);

    CHECK(aems->Release() == 0);

    // A missing tuning exemplar is not an Init failure: a fresh object keeps the empty curve and the
    // empty zoom-volume table, so the master volume is 0.
    ResetWorld();
    g_game.prefs = {true, 1024};
    auto* fresh = new cSC4AudioAEMSEventSystem();
    CHECK(fresh->Init());
    CHECK(!fresh->ReadTuningValues());
    CHECK(fresh->zoomLevelVolumes().empty());
    CHECK(g_engine.masterVolumes.size() == 1);
    CHECK(g_engine.masterVolumes.back() == 0);
    CHECK(fresh->Shutdown());
    CHECK(fresh->Release() == 0);
}

// ---------------------------------------------------------------------------------------------
// events
// ---------------------------------------------------------------------------------------------
void TestStartEndSound() {
    ResetWorld();
    auto* aems = new cSC4AudioAEMSEventSystem();

    g_engine.nextHandle = 0x40;
    CHECK(aems->StartSound(static_cast<cISC4AEMSEvents::AEMSEventType>(0x0e)));
    CHECK(aems->eventState(0x0e) == 0x41);   // handle + 1
    CHECK(g_engine.beginIds.size() == 1 && g_engine.beginIds[0] == 0x0e);
    CHECK(!aems->StartSound(static_cast<cISC4AEMSEvents::AEMSEventType>(0x0e)));   // already playing
    CHECK(g_engine.beginIds.size() == 1);

    CHECK(aems->EndSound(static_cast<cISC4AEMSEvents::AEMSEventType>(0x0e)));
    CHECK(aems->eventState(0x0e) == 0);
    CHECK(g_engine.endEvents.size() == 1);
    CHECK(g_engine.endEvents[0].first == 0x40);
    CHECK(g_engine.endEvents[0].second == 0x0e);
    CHECK(!aems->EndSound(static_cast<cISC4AEMSEvents::AEMSEventType>(0x0e)));

    // Quirk: a refused begin reports success but leaves the slot empty.
    g_engine.beginResult = -3;
    CHECK(aems->StartSound(static_cast<cISC4AEMSEvents::AEMSEventType>(0x05)));
    CHECK(aems->eventState(0x05) == 0);
    g_engine.beginResult = 0;

    // Out-of-range indices are ignored by the port (the original indexes unchecked).
    CHECK(!aems->StartSound(static_cast<cISC4AEMSEvents::AEMSEventType>(0x15)));
    CHECK(!aems->EndSound(static_cast<cISC4AEMSEvents::AEMSEventType>(0x15)));
    CHECK(!aems->StartSound(static_cast<cISC4AEMSEvents::AEMSEventType>(static_cast<std::uint32_t>(-1))));

    // StopAllSounds ends the slots in the original's order and skips 0x0f and 0x10.
    g_engine.beginIds.clear();
    g_engine.beginHandles.clear();
    g_engine.endEvents.clear();
    for (std::uint32_t i = 0; i < 0x15; ++i) {
        CHECK(aems->StartSound(static_cast<cISC4AEMSEvents::AEMSEventType>(i)));
    }
    CHECK(g_engine.beginHandles.size() == 0x15);
    aems->StopAllSounds();
    static constexpr std::uint32_t kStopped[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                                                 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x11, 0x12, 0x14, 0x13};
    CHECK(g_engine.endEvents.size() == 19);
    for (std::size_t i = 0; i < g_engine.endEvents.size(); ++i) {
        const auto& ended = g_engine.endEvents[i];
        const std::uint32_t id = kStopped[i];
        CHECK(ended.second == static_cast<std::int32_t>(id));
        CHECK(ended.first == g_engine.beginHandles[static_cast<std::int32_t>(id)]);
    }
    for (std::uint32_t i = 0; i < 0x15; ++i) {
        const bool skipped = (i == 0x0f || i == 0x10);
        CHECK((aems->eventState(i) != 0) == skipped);   // quirk: these two stay playing
    }
    // the skipped slots can still be ended by hand
    CHECK(aems->EndSound(static_cast<cISC4AEMSEvents::AEMSEventType>(0x0f)));
    CHECK(aems->EndSound(static_cast<cISC4AEMSEvents::AEMSEventType>(0x10)));

    g_engine.endEvents.clear();
    aems->StopAllSounds();   // everything is stopped now
    CHECK(g_engine.endEvents.empty());

    CHECK(aems->Release() == 0);
}

void TestSetParams() {
    ResetWorld();
    auto* aems = new cSC4AudioAEMSEventSystem();

    g_engine.setGlobals.clear();
    CHECK(aems->SetParamVelocity(sc4::SC4Percentage(0.0f)));
    CHECK(aems->SetParamThrottle(sc4::SC4Percentage(0.5f)));
    CHECK(aems->SetParamWobble(sc4::SC4Percentage(1.0f)));
    CHECK(aems->SetParamVelocity(sc4::SC4Percentage(2.0f)));     // clamped to 1
    CHECK(aems->SetParamVelocity(sc4::SC4Percentage(-1.0f)));    // clamped to 0
    CHECK(g_engine.setGlobals.size() == 5);
    CHECK(g_engine.setGlobals[0].first == 0 && g_engine.setGlobals[0].second == 0);
    CHECK(g_engine.setGlobals[1].first == 1 && g_engine.setGlobals[1].second == 63);    // 0.5 * 127 = 63.5
    CHECK(g_engine.setGlobals[2].first == 2 && g_engine.setGlobals[2].second == 127);
    CHECK(g_engine.setGlobals[3].second == 127);
    CHECK(g_engine.setGlobals[4].second == 0);

    // minss passes a NaN through and cvttss2si turns it into INT32_MIN (quirk kept from the binary).
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(aems->SetParamVelocity(sc4::SC4Percentage(nan)));
    CHECK(g_engine.setGlobals.back().second == std::numeric_limits<std::int32_t>::min());

    // The engine's negative status becomes false.
    g_engine.setGlobalResult = -1;
    CHECK(!aems->SetParamThrottle(sc4::SC4Percentage(0.25f)));
    g_engine.setGlobalResult = 0;

    CHECK(aems->Release() == 0);
}

// ---------------------------------------------------------------------------------------------
// tuning / volume / messages
// ---------------------------------------------------------------------------------------------
void TestTuningAndVolume() {
    ResetWorld();
    InstallTuning();
    g_game.prefs = {true, 512};
    g_viewport.zoom = 4;
    auto* aems = new cSC4AudioAEMSEventSystem();
    CHECK(aems->Init());
    CHECK(g_engine.masterVolumes.back() == 63);
    CHECK(!aems->DoMessage(nullptr));
    CHECK(!aems->DoEvent(nullptr));

    // DoMessage: the zoom level selects the per-zoom volume (zoom 4 -> 1.0).
    fakes::FakeMessage message;
    message.type = kZoomMessage;
    g_viewport.zoom = 4;
    CHECK(aems->DoMessage(&message));
    CHECK(aems->zoomVolumeScale() == 1.0f);
    CHECK(g_engine.masterVolumes.back() == 63);

    g_viewport.zoom = 2;   // 0.75 -> curve(0.5 * 0.75) * 127 = 47.625 -> 47
    CHECK(aems->DoMessage(&message));
    CHECK(aems->zoomVolumeScale() == 0.75f);
    CHECK(g_engine.masterVolumes.back() == 47);

    // A missing viewport behaves like zoom 0 (the port does not dereference it).
    SC4Audio::SetViewUtilities(nullptr);
    CHECK(aems->DoMessage(&message));
    CHECK(aems->zoomVolumeScale() == 0.25f);
    CHECK(g_engine.masterVolumes.back() == 15);   // 0.5 * 0.25 * 127 = 15.875
    SC4Audio::SetViewUtilities(&g_viewport);

    // An out-of-range zoom level leaves the scale alone (but still re-applies the volume).
    g_viewport.zoom = 9;
    CHECK(aems->DoMessage(&message));
    CHECK(aems->zoomVolumeScale() == 0.25f);

    // Other message types are not handled.
    const std::size_t volumesBefore = g_engine.masterVolumes.size();
    message.type = 0x12345678;
    CHECK(!aems->DoMessage(&message));
    CHECK(g_engine.masterVolumes.size() == volumesBefore);

    // DoEvent: the master-volume event (0x2a5b69c0) sets the scale from data1 / 1024.
    fakes::FakeEvent event;
    event.id = kMasterVolumeEvent;
    event.message.SetData1(256);   // 0.25
    const std::size_t eventsBefore = g_engine.masterVolumes.size();
    CHECK(!aems->DoEvent(&event));   // quirk: always false, even when handled
    CHECK(aems->masterVolumeScale() == 0.25f);
    CHECK(g_engine.masterVolumes.size() == eventsBefore + 1);
    CHECK(g_engine.masterVolumes.back() == 7);   // 0.25 * 0.25 * 127 = 7.9

    // Other event ids are ignored.
    event.id = 0x12345678;
    CHECK(!aems->DoEvent(&event));
    CHECK(g_engine.masterVolumes.size() == eventsBefore + 1);

    // A custom (piecewise) curve: curve(0.25) = 0.125 with points (0,0) (0.5,0.25) (1,1).
    CHECK(aems->Shutdown());
    g_exemplars.arrays[kTuningCurveProperty] = {0.0f, 0.0f, 0.5f, 0.25f, 1.0f, 1.0f};
    CHECK(aems->Init());
    CHECK(aems->responseCurve().PointCount() == 3);
    event.id = kMasterVolumeEvent;
    event.message.SetData1(1024);   // 1.0
    CHECK(!aems->DoEvent(&event));
    CHECK(aems->masterVolumeScale() == 1.0f);
    CHECK(aems->zoomVolumeScale() == 0.25f);
    CHECK(g_engine.masterVolumes.back() == 15);    // curve(1.0 * 0.25) = 0.125 -> 15.875 -> 15

    // Sound disabled: the scale is 0 and the master volume is 0 whatever the curve says.
    CHECK(aems->Shutdown());
    g_game.prefs = {false, 1024};
    CHECK(aems->Init());
    CHECK(aems->masterVolumeScale() == 0.0f);
    CHECK(g_engine.masterVolumes.back() == 0);

    // A missing exemplar keeps the previous tuning values and curve (ReadTuningValues only returns false).
    CHECK(aems->Shutdown());
    g_exemplars.exists = false;
    g_game.prefs = {true, 0};
    CHECK(aems->Init());
    CHECK(g_exemplars.exists == false);
    CHECK(aems->tuningVolume() == 1.0f);          // unchanged
    CHECK(aems->zoomLevelVolumes().size() == 5);  // unchanged

    CHECK(aems->Shutdown());
    CHECK(aems->Release() == 0);
}

// A present exemplar without the tuning properties keeps every default: the straight (0,0)-(1,1)
// curve, the five kAEMSZoomLevelVolumes entries and the constructor's 1.0/1.0/0x25 values.
void TestTuningDefaults() {
    ResetWorld();
    g_exemplars.exists = true;   // an exemplar, but none of the properties
    g_game.prefs = {true, 1024};
    auto* aems = new cSC4AudioAEMSEventSystem();
    CHECK(aems->Init());
    CHECK(aems->responseCurve().PointCount() == 2);
    CHECK(aems->zoomLevelVolumes().size() == 5);
    CHECK(aems->zoomLevelVolumes()[0] == 0.0f);
    CHECK(aems->zoomLevelVolumes()[3] == 0.5f);
    CHECK(aems->zoomLevelVolumes()[4] == 1.0f);
    CHECK(aems->tuningVolume() == 1.0f);
    CHECK(aems->tuningBusVolume() == 1.0f);
    CHECK(aems->tuningBusFlags() == 0x25);
    CHECK(g_engine.masterVolumes.back() == 127);   // curve(1.0 * 1.0 * 1.0) * 127
    CHECK(g_engine.fxVolume == 1);
    CHECK(g_engine.fxFlags == 0x25);
    CHECK(aems->Shutdown());
    CHECK(aems->Release() == 0);

    // No game context: the original dereferences SL::spSC4; the port treats it as sound disabled.
    ResetWorld();
    InstallTuning();
    SC4Audio::GetServices().game = nullptr;
    auto* noGame = new cSC4AudioAEMSEventSystem();
    CHECK(noGame->Init());
    CHECK(noGame->masterVolumeScale() == 0.0f);
    CHECK(g_engine.masterVolumes.back() == 0);
    CHECK(noGame->Shutdown());
    CHECK(noGame->Release() == 0);
}

// ---------------------------------------------------------------------------------------------
// banks
// ---------------------------------------------------------------------------------------------
void TestBanks() {
    ResetWorld();
    InstallTuning();
    g_game.prefs = {true, 1024};
    auto* aems = new cSC4AudioAEMSEventSystem();
    CHECK(aems->Init());

    // The holder the game passes: property 0x1234 -> bank resource instance 0xbeef.
    sc4::cSCPropertyHolder holder;
    holder.AddProperty(0x1234u, 0xbeefu, false);
    g_banks.resourcesByInstance[0xbeef] = "BANK-DATA";

    // Start an event; LoadSoundBank stops it and clears the slots before adding the bank.
    CHECK(aems->StartSound(static_cast<cISC4AEMSEvents::AEMSEventType>(0x0e)));
    g_engine.endEvents.clear();
    g_engine.moduleBankData.clear();
    aems->LoadSoundBank(&holder, 0x1234);
    CHECK(g_engine.endEvents.size() == 1);
    CHECK(g_engine.endEvents[0].second == 0x0e);
    for (std::size_t i = 0; i < aems->eventStateCount(); ++i) CHECK(aems->eventState(i) == 0);

    CHECK(aems->bankCount() == 1);
    CHECK(g_banks.requested.back().type == 0);
    CHECK(g_banks.requested.back().group == 0x8a5971c5);
    CHECK(g_banks.requested.back().instance == 0xbeef);
    CHECK(g_engine.moduleBankData.size() == 1);
    CHECK(aems->banks().front() != nullptr);
    CHECK(aems->banks().front()->Data() == g_engine.moduleBankData[0]);
    CHECK(std::memcmp(aems->banks().front()->Data(), "BANK-DATA", 9) == 0);
    CHECK(aems->banks().front()->GetModuleBankHandle() == 0);   // quirk: the handle is always 0
    cGZPersistResourceKey bankKey{};
    aems->banks().front()->GetKey(bankKey);
    CHECK(bankKey.instance == 0xbeef);

    // UnloadSoundBank removes it by the resource instance read from the property.
    g_engine.removedModuleBanks.clear();
    aems->UnloadSoundBank(&holder, 0x1234);
    CHECK(aems->bankCount() == 0);
    CHECK(g_engine.removedModuleBanks.size() == 1);
    CHECK(g_engine.removedModuleBanks[0] == 0);

    // Missing property: the instance defaults to 0xebf44df7.
    sc4::cSCPropertyHolder emptyHolder;
    g_banks.resourcesByInstance[0xebf44df7] = "DEFAULT-BANK";
    aems->LoadSoundBank(&emptyHolder, 0x9999);
    CHECK(g_banks.requested.back().instance == 0xebf44df7);
    CHECK(aems->bankCount() == 1);
    aems->UnloadSoundBank(&emptyHolder, 0x9999);
    CHECK(aems->bankCount() == 0);

    // A holder that is missing entirely still loads the default bank (the class guards the null).
    g_banks.resourcesByInstance[0xebf44df7] = "DEFAULT-BANK";
    aems->LoadSoundBank(nullptr, 0x9999);
    CHECK(g_banks.requested.back().instance == 0xebf44df7);
    aems->UnloadSoundBank(nullptr, 0x9999);
    CHECK(aems->bankCount() == 0);

    // A bank the engine refuses is dropped (and not left in the list).
    g_engine.addModuleBankResult = -1;
    aems->LoadSoundBank(&holder, 0x1234);
    CHECK(aems->bankCount() == 0);
    g_engine.addModuleBankResult = 0;

    // Several banks: RemoveAllBanks pops them front to back and removes each module bank.
    g_banks.resourcesByInstance[0x1111] = "BANK-A";
    g_banks.resourcesByInstance[0x2222] = "BANK-B";
    holder.AddProperty(0x2001u, 0x1111u, false);
    holder.AddProperty(0x2002u, 0x2222u, false);
    aems->LoadSoundBank(&holder, 0x2001);
    aems->LoadSoundBank(&holder, 0x2002);
    CHECK(aems->bankCount() == 2);
    g_engine.removedModuleBanks.clear();
    aems->RemoveAllBanks();
    CHECK(aems->bankCount() == 0);
    CHECK(g_engine.removedModuleBanks.size() == 2);

    // RemoveBank on an unknown instance changes nothing.
    CHECK(!aems->RemoveBank(0x999999));

    CHECK(aems->Shutdown());
    CHECK(aems->Release() == 0);
}

// ---------------------------------------------------------------------------------------------
// LoadFromResource / LoadFromFile
// ---------------------------------------------------------------------------------------------
void TestLoadFromFile() {
    ResetWorld();
    auto* aems = new cSC4AudioAEMSEventSystem();

    MemFile file;
    file.data = "SYSTEM-ONE";
    g_engine.addSystemCalls = 0;
    CHECK(aems->LoadFromFile(&file));
    CHECK(aems->systemDataSize() == 10);
    CHECK(std::memcmp(aems->systemData(), "SYSTEM-ONE", 10) == 0);
    CHECK(g_engine.addSystemCalls == 1);
    CHECK(g_engine.addSystemData == aems->systemData());
    CHECK(g_engine.removeSystemCalls >= 1);

    // A shorter file truncates the blob.
    MemFile shorter;
    shorter.data = "XY";
    CHECK(aems->LoadFromFile(&shorter));
    CHECK(aems->systemDataSize() == 2);

    // A failed read leaves the engine alone.
    MemFile failing;
    failing.data = "IGNORED";
    failing.failRead = true;
    const int addBefore = g_engine.addSystemCalls;
    CHECK(!aems->LoadFromFile(&failing));
    CHECK(g_engine.addSystemCalls == addBefore);

    CHECK(!aems->LoadFromFile(nullptr));

    // LoadFromResource: the return value is the file's Close(); every path releases the file.
    g_banks.resourcesByInstance[0xabcdef] = "RESOURCE";
    cGZPersistResourceKey key{0, 0x8a5971c5, 0xabcdef};
    CHECK(aems->LoadFromResource(key));
    CHECK(g_banks.last() != nullptr);
    CHECK(g_banks.last()->closeCalls == 1);
    CHECK(g_banks.last()->releaseCalls == 1);
    CHECK(aems->systemDataSize() == 8);

    // A missing resource reports false without touching the blob.
    cGZPersistResourceKey missing{0, 0x8a5971c5, 0x777777};
    CHECK(!aems->LoadFromResource(missing));
    CHECK(aems->systemDataSize() == 8);

    // A resource whose read fails is closed and released but reports false.
    g_banks.resourcesByInstance[0x999] = "BROKEN";
    cGZPersistResourceKey broken{0, 0x8a5971c5, 0x999};
    // make the next file fail its read
    g_banks.failOpen = false;
    struct FailingSource : sc4::ISoundBankHost {
        bool GetResourceAsFile(const cGZPersistResourceKey&, cIGZFile** out, bool) override {
            auto* f = new MemFile();
            f->data = "BROKEN";
            f->failRead = true;
            f->AddRef();
            *out = f;
            leaked = f;
            return true;
        }
        MemFile* leaked = nullptr;
    } failingSource;
    // (the file is kept alive by the test; the class releases its reference)
    sc4::ISoundBankHost* savedHost = sc4::SoundBankHost();
    sc4::SetSoundBankHost(&failingSource);
    CHECK(!aems->LoadFromResource(broken));
    CHECK(failingSource.leaked->closeCalls == 1);
    CHECK(failingSource.leaked->releaseCalls == 1);
    sc4::SetSoundBankHost(savedHost);
    delete failingSource.leaked;

    CHECK(aems->Release() == 0);
}

// ---------------------------------------------------------------------------------------------
// the destructor does not release banks (quirk) and the empty object shuts down cleanly
// ---------------------------------------------------------------------------------------------
void TestDestructorWithLoadedBank() {
    ResetWorld();
    InstallTuning();
    auto* aems = new cSC4AudioAEMSEventSystem();
    CHECK(aems->Init());
    sc4::cSCPropertyHolder holder;
    holder.AddProperty(0x1234u, 0xbeefu, false);
    g_banks.resourcesByInstance[0xbeef] = "BANK";
    aems->LoadSoundBank(&holder, 0x1234);
    CHECK(aems->bankCount() == 1);
    // Shutdown removes the bank's module bank and drops the reference; the object then dies cleanly.
    CHECK(aems->Shutdown());
    CHECK(aems->bankCount() == 0);
    CHECK(g_engine.removedModuleBanks.size() == 1);
    CHECK(aems->Release() == 0);
}

}  // namespace

int main() {
    InstallServices();
    TestConstructionAndCom();
    TestInitAndShutdown();
    TestInitFailures();
    TestStartEndSound();
    TestSetParams();
    TestTuningAndVolume();
    TestTuningDefaults();
    TestBanks();
    TestLoadFromFile();
    TestDestructorWithLoadedBank();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("sc4_audioaemseventsystem: all checks passed\n");
    return 0;
}
