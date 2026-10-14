// cHitMan: sem registry, data blocks / hit file parsing, duck map, control groups, DB segments,
// service life cycle and the layout facts the engine integration relies on.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "audio/HitClassIds.h"
#include "audio/HitMan.h"
#include "audio/HitTables.h"
#include "audio/interfaces/cIGZSndViewUpdateTarget.h"

namespace {

struct FakeSnd final : cIGZSnd {
    std::uint32_t refs = 1;
    bool killed = false;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cIGZSnd || iid == kGZIID_cIGZUnknown) { *out = this; AddRef(); return true; }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { if (--refs == 0) { delete this; return 0; } return refs; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool PlayWithVolumeAndLoop(std::int32_t, bool, SndDup) override { return true; }
    bool PlayWithScriptArguments(std::int32_t, std::int32_t, std::int32_t, std::int32_t) override { return true; }
    bool IsPlaying() const override { return false; }
    bool Stop() override { return true; }
    bool Kill() override { killed = true; return true; }
    bool StopAllInstances() override { return true; }
    std::uint32_t GetUseCount() const override { return 0; }
    std::uint32_t GetID() const override { return 0; }
    void SetID(std::uint32_t) override {}
    bool Load() override { return true; }
    bool Unload() override { return true; }
    bool IsLoaded() const override { return true; }
    bool Pause(bool) override { return true; }
    bool Unpause() override { return true; }
    bool IsPaused() const override { return false; }
    std::int32_t GetVolume() const override { return 0; }
    bool SetVolume(std::int32_t) override { return true; }
    bool FadeVolume(std::int32_t, std::int32_t, std::uint32_t, bool) override { return true; }
    bool IsFading() const override { return false; }
    std::int32_t GetPan() const override { return 0; }
    bool SetPan(std::int32_t) override { return true; }
    std::int32_t GetFrequency() const override { return 0; }
    bool SetFrequency(std::int32_t) override { return true; }
    std::int32_t GetOriginalFrequency() const override { return 0; }
    bool SetEffectsType(std::int32_t, std::uint32_t) override { return true; }
    bool SetEffectsLevel(std::int32_t, std::uint32_t) override { return true; }
    std::int32_t GetSoundPriority() const override { return 0; }
    bool SetSoundPriority(std::int32_t) override { return true; }
    bool SetName(const cIGZString&) override { return true; }
    bool GetName(cIGZString&) const override { return false; }
    bool SetSoundFileFromString(const cIGZString&, SndStreamingType) override { return true; }
    bool SetSoundFileFromChars(const char*, std::uint32_t, SndStreamingType) override { return true; }
};

struct FakeResource final : cIGZUnknown {
    std::uint32_t refs = 0;
    std::size_t defaults = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { if (--refs == 0) { delete this; return 0; } return refs; }
};

struct FakeUpdateTarget final : cIGZSndUpdateTarget, cIGZSndViewUpdateTarget {
    int views = 0;
    std::uint32_t refs = 1;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cIGZSndViewUpdateTarget) { *out = static_cast<cIGZSndViewUpdateTarget*>(this); AddRef(); return true; }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void Update(std::int32_t) override {}
    void UpdateView() override { ++views; }
};

struct FakeServices final : Hit::cIHitManServices {
    bool sound = true;
    FakeUpdateTarget target;
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::map<std::uint32_t, std::vector<std::uint8_t>> records;  // keyed by instance
    std::vector<std::string> debugInfoNames;
    std::vector<FakeResource*> resources;
    std::vector<std::size_t> resourceDefaultCounts;
    std::vector<FakeSnd*> channels;
    int hitListFetches = 0;

    bool HasSoundSystem() const override { return sound; }
    void EnumerateUpdateTargets(EnumCallback cb, std::uint32_t a, void* b) override { cb(&target, a, b); }
    bool GetPersistResource(const cGZPersistResourceKey& key, std::uint32_t, void**) override {
        ++hitListFetches;
        return key.group == 0xcc && key.instance == 77;  // only group 0xcc has list 77
    }
    cIGZUnknown* CreatePropertySetResource(const cGZPersistResourceKey&, const Hit::DefaultProperty*,
                                           std::size_t count, bool) override {
        auto* r = new FakeResource();
        resources.push_back(r);
        resourceDefaultCounts.push_back(count);
        return r;
    }
    bool ReadPersistRecord(const cGZPersistResourceKey& key, std::vector<std::uint8_t>& bytes, std::string& name) override {
        const auto it = records.find(key.instance);
        if (it == records.end()) return false;
        bytes = it->second;
        name = "record.hit";
        return true;
    }
    bool GetSoundById(std::uint32_t, std::uint32_t, void**) override { return false; }
    void RegisterUpdateTarget(cIGZSndUpdateTarget*, std::uint32_t) override {}
    void UnregisterUpdateTarget(cIGZSndUpdateTarget*) override {}
    bool SetGlobalProperty(std::uint32_t, const cIGZAny&) override { return false; }
    bool GetGlobalProperty(std::uint32_t, cIGZAny*) override { return false; }
    bool SetInstanceProperty(std::uint32_t, std::uint32_t, const cIGZAny&) override { return false; }
    bool GetDefaultSndPlayerClassId(bool, std::uint32_t&) override { return false; }
    bool CreateComInstance(std::uint32_t, std::uint32_t, void**) override { return false; }
    bool ReadFileBytes(const std::string& path, std::vector<std::uint8_t>& bytes) override {
        const auto it = files.find(path);
        if (it == files.end()) return false;
        bytes = it->second;
        return true;
    }
    std::string ApplicationDirectory() const override { return "/app"; }
    std::string DataDirectory() const override { return "/data"; }
    void ParseDebugInfo(const cIGZString& name, const std::uint8_t*, std::uint32_t) override {
        debugInfoNames.emplace_back(name.ToChar());
    }
    bool CreateTrackPlayer(std::uint32_t, void**) override { return false; }
    bool CreateChannel(void** out) override {
        auto* s = new FakeSnd();
        channels.push_back(s);
        *out = static_cast<cIGZSnd*>(s);
        return true;
    }
};

// A hit file: 8 bytes of header, "ENTP", (key,offset) x2, "ENTE", trailing bytes.
std::vector<std::uint8_t> MakeHitFile() {
    std::vector<std::uint8_t> v = {1, 2, 3, 4, 5, 6, 7, 8};
    auto put = [&](std::uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>(x >> (8 * i))); };
    put(0x50544e45); put(0x11111111); put(16); put(0x22222222); put(40); put(0x544e4545); put(0xdeadbeef);
    return v;
}

}  // namespace

int main() {
    // ---- layout facts the integration depends on ----
    static_assert(sizeof(cRZSystemService) == 0x28, "cRZSystemService is 0x28 bytes in the binary");
    {
        cHitMan* m = new cHitMan();
        char* base = reinterpret_cast<char*>(m);
        assert(reinterpret_cast<char*>(static_cast<cIHitMan*>(m)) - base == 0x28);   // cIHitMan at +0x28
        assert(reinterpret_cast<char*>(static_cast<cRZUnknown*>(m)) - base == 0x08);  // cRZUnknown at +8
        assert(m->GetServiceID() == kServiceId_HitMan && m->GetServicePriority() == 499999);
        assert(m->GetServiceTickPriority() == 0);  // through cIGZSystemService (slot 12 default)
        assert(static_cast<const cHitMan*>(m)->GetServiceTickPriority() == 0x7a116);  // const overload
        void* p = nullptr;
        assert(m->QueryInterface(kGZIID_cIHitMan, &p) && p == static_cast<cIHitMan*>(m));
        assert(m->QueryInterface(kGZIID_cIGZUnknown, &p) && p == static_cast<cIHitMan*>(m));  // quirk: id 1 -> cIHitMan
        assert(m->QueryInterface(kGZIID_cIGZSystemService, &p) && p == static_cast<cIGZSystemService*>(m));
        assert(!m->QueryInterface(0x1234, &p));
        assert(m->RefCount() == 3);                  // taken by the three successful QueryInterface calls
        m->Release(); m->Release();
        assert(m->RefCount() == 1);
        assert(m->Release() == 0);                   // Release at count 1 destroys the object
    }

    FakeServices services;
    Hit::SetServices(&services);
    cHitMan* hm = new cHitMan();
    cIHitMan* api = hm;

    // ---- sems: the two defaults are registered by Init ----
    assert(api->NumSems() == 0);
    assert(api->Init() && api->Init());                  // idempotent
    assert(api->NumSems() == 2 && api->GetSem(0x7dcded2d) != nullptr && api->GetSem(0x7dcded3d) != nullptr);
    assert(services.resources.size() == 3);
    assert(services.resourceDefaultCounts[0] == 0 && services.resourceDefaultCounts[1] == 54 &&
           services.resourceDefaultCounts[2] == 30);   // 1 explicit + 29 table entries
    assert(api->GetSemAt(0)->GetId() == 0x7dcded2d && api->GetSemAt(1)->GetId() == 0x7dcded3d);
    void* semIface = nullptr;
    assert(api->GetSem(0x7dcded2d, kGZIID_cIHitSem, &semIface) && !api->GetSem(5, kGZIID_cIHitSem, &semIface));
    assert(api->RemoveSem(0x7dcded2d) && !api->RemoveSem(0x7dcded2d) && api->NumSems() == 1);
    assert(api->RemoveAllSems() && api->NumSems() == 0);

    // ---- registers behave as a plain table (missing = 0) ----
    assert(api->GetRegisterVal(5) == 0 && api->SetRegisterVal(5, 42) && api->GetRegisterVal(5) == 42);
    assert(api->Random(10, 10) == 10);   // empty range -> lower bound

    // ---- hit file loading: ENTP/ENTE entry points, name handling ----
    services.files["/h/a.hit"] = MakeHitFile();
    cRZString name("/h/a.hit");
    assert(api->LoadHitFile(name));
    // The duplicate check compares the caller's path with the stored (upper-cased) file name, so
    // it only triggers for the canonical upper-case spelling; any other spelling loads again.
    cRZString canonical("/H/A.HIT");
    assert(!api->LoadHitFile(canonical));   // already loaded -> false
    assert(api->LoadHitFile(name));          // lower-case spelling: loaded a second time
    cIHitTrackDataBlock* block = nullptr;
    std::uint32_t off = 0;
    assert(api->GetTrackDataBlockAndOffset(0x22222222, &block, off) && off == 40);
    assert(api->GetTrackDataBlockAndOffset(0x11111111, &block, off) && off == 16);
    assert(!api->GetTrackDataBlockAndOffset(0x33333333, &block, off));
    assert(api->TrackDataEntryPoint(0x11111111) == block->DataStartAddress() + 16);
    assert(api->TrackDataEntryPoint(0x33333333) == nullptr);
    assert(block->DataSize() == MakeHitFile().size());
    assert(services.debugInfoNames.size() == 2);   // one per ParseHitFile (two loads)
    cRZString shown;
    block->GetFilename(shown);
    assert(shown.str() == "/H/A.HIT");                 // stored upper-cased
    cRZString unload("/h/a.hit");
    assert(api->UnloadHitFile(unload));                 // matches after upper-casing (first duplicate)
    assert(api->GetTrackDataBlockAndOffset(0x11111111, &block, off));
    assert(api->UnloadHitFile(unload));                 // second duplicate
    assert(!api->GetTrackDataBlockAndOffset(0x11111111, &block, off));
    assert(api->UnloadHitFile(unload));                 // unloading nothing is still true

    // embedded key: three hex numbers in the path -> persisted record
    services.records[0x0000beef] = MakeHitFile();
    cRZString keyed("/x/00000001-00000002-0000beef.hit");
    cGZPersistResourceKey key;
    assert(hm->FilepathHasEmbeddedKey(keyed, key));
    assert(key.type == 1 && key.group == 2 && key.instance == 0xbeef);
    assert(api->LoadHitFile(keyed));
    assert(api->GetTrackDataBlockAndOffset(0x22222222, &block, off));
    cRZString zero("/x/00000000-00000002-0000beef.hit"), shortPath("/x/abc.hit");
    assert(!hm->FilepathHasEmbeddedKey(zero, key) && !hm->FilepathHasEmbeddedKey(shortPath, key));
    cRZString contiguous("0000000100000002000000030");  // 25 hex digits: numbers overlap by one digit
    assert(hm->FilepathHasEmbeddedKey(contiguous, key));
    assert(key.type == 1 && key.group == 0x10000000 && key.instance == 0x02000000);

    // ---- data block registry: newest first, no duplicates, ownership untouched for foreign data ----
    static std::uint8_t foreign[16] = {};
    auto* custom = new cHitTrackDataBlock();
    custom->AddRef();                               // the creator's own reference (objects start at 0)
    custom->SetDataBlock(foreign, sizeof foreign);
    custom->AddEntryPoint(0x22222222, 3);          // shadows the loaded block's entry
    assert(api->RegisterTrackDataBlock(custom) && api->RegisterTrackDataBlock(custom));
    assert(api->GetTrackDataBlockAndOffset(0x22222222, &block, off) && block == custom && off == 3);
    assert(api->RemoveTrackDataBlock(custom));
    assert(api->GetTrackDataBlockAndOffset(0x22222222, &block, off) && block != custom && off == 40);
    assert(!api->RegisterTrackDataBlock(nullptr));
    custom->Release();                              // last reference: freed

    // ---- ducking: priorities drive register 0x1b and notify views ----
    FakeSnd a, b;
    const int viewsBefore = services.target.views;
    api->DuckMapSetSndobPri(&a, 5);
    assert(api->DuckMapSndobPri(&a) == 5 && api->DuckMapMaxPri() == 5 && api->GetRegisterVal(0x1b) == 5);
    assert(services.target.views == viewsBefore + 1);
    api->DuckMapSetSndobPri(&a, 5);                 // unchanged: no notification
    assert(services.target.views == viewsBefore + 1);
    api->DuckMapSetSndobPri(&b, 9);
    assert(api->DuckMapMaxPri() == 9 && api->GetRegisterVal(0x1b) == 9);
    api->DuckMapSetSndobPri(&b, 0);
    assert(api->GetRegisterVal(0x1b) == 5 && api->DuckMapSndobPri(&b) == 0);
    api->DuckMapSetSndobPri(&b, 0);                 // removing an absent entry: nothing
    assert(api->Pause() && api->GetRegisterVal(0x1b) == 0 && api->DuckMapMaxPri() == 0);
    assert(!api->IsPaused());                        // the flag is never set in the binary

    // ---- active track list: the binary's "remove" pops the front, so only the last add survives ----
    FakeSnd t1, t2;
    assert(hm->AddToActiveTrackList(&t1) && hm->AddToActiveTrackList(&t2));
    assert(hm->ActiveTrackList() != nullptr && hm->ActiveTrackList()->size() == 1 &&
           hm->ActiveTrackList()->front() == &t2);

    // ---- channels: FreeChannel releases only channels it allocated ----
    void* chan = nullptr;
    assert(api->AllocChannel(0, &chan) && chan != nullptr);
    FakeSnd* ch = services.channels.back();
    ch->AddRef();                                    // keep alive to observe the release
    const std::uint32_t before = ch->refs;
    api->FreeChannel(static_cast<cIGZSnd*>(chan));
    assert(ch->refs == before - 1);
    api->FreeChannel(static_cast<cIGZSnd*>(chan));   // second time: not in the list, no release
    assert(ch->refs == before - 1);
    ch->Release();                                   // the test's extra reference; frees the channel

    // ---- control groups, sequence sounds, DB segments, hit lists ----
    cIHitControlGroup* g = api->ControlGroup(4);
    assert(g == api->ControlGroup(4) && g->AsIGZSnd()->GetID() == 4);
    assert(api->AddToControlGroup(4, 99));
    assert(static_cast<cHitControlGroup*>(g)->tracks().count(99) == 1);

    FakeSnd* seq = new FakeSnd();
    api->SetSequenceGroupSnd(3, seq);
    cIGZSnd* got = nullptr;
    assert(api->GetSequenceGroupSnd(3, &got) && got == seq && !api->GetSequenceGroupSnd(8, &got) && got == nullptr);
    assert(seq->refs == 2);
    seq->Release();

    struct Seg final : cIGZPersistDBSegmentStub {
        std::uint32_t refs = 0;
        bool QueryInterface(std::uint32_t iid, void** out) override { if (iid == 0x77) { *out = this; return true; } return false; }
        std::uint32_t AddRef() override { return ++refs; }
        std::uint32_t Release() override { return --refs; }
    } seg;
    api->AddDBSegment(9, &seg);
    void* found = nullptr;
    assert(api->FindDBSegment(9, 0x77, &found) && found == &seg && !api->FindDBSegment(9, 0x78, &found) &&
           !api->FindDBSegment(10, 0x77, &found));
    api->RemoveDBSegment(9);
    assert(!api->FindDBSegment(9, 0x77, &found) && seg.refs == 0);

    assert(api->AddTypeToGroupAssociation(HitIds::kResTypeHitList, 0xaa));
    assert(api->AddTypeToGroupAssociation(HitIds::kResTypeHitList, 0xcc));
    assert(api->AddTypeToGroupAssociation(HitIds::kResTypeHitList, 0xcc));   // duplicate ignored
    void* list = nullptr;
    assert(api->GetHitList(77, 0, &list) && services.hitListFetches == 2);   // tried 0xaa then 0xcc
    assert(!api->GetHitList(78, 0, &list));

    // ---- source data handler ----
    struct Handler final : cIHitAppDataRequestHandler {
        std::uint32_t refs = 0;
        bool QueryInterface(std::uint32_t, void**) override { return false; }
        std::uint32_t AddRef() override { return ++refs; }
        std::uint32_t Release() override { return --refs; }
        bool GetSourceDataField(std::int32_t s, std::int32_t f, std::int32_t* out) override { *out = s * 100 + f; return true; }
    } handler;
    std::int32_t value = 0;
    assert(!api->GetSourceDataField(1, 2, &value));   // no handler: false (the binary would crash)
    assert(api->RegisterSourceDataRequestHandler(&handler) && handler.refs == 1);
    assert(api->GetSourceDataField(3, 4, &value) && value == 304 && api->GetSourceDataRequestHandler() == &handler);

    // ---- shutdown kills active tracks, drops everything, frees owned data ----
    assert(api->Shutdown());
    assert(!hm->ActiveTrackList() && api->NumSems() == 0 && handler.refs == 0);
    assert(!api->GetTrackDataBlockAndOffset(0x22222222, &block, off));
    assert(api->Shutdown());                          // idempotent
    assert(api->Init() && api->NumSems() == 2);       // can be brought up again

    api->SetTrackFlowErrorHandler(nullptr);
    api->HandleTrackFlowError(nullptr, "x");          // null handler: ignored

    delete hm;
    Hit::SetServices(nullptr);
    std::puts("hitman ok");
}
