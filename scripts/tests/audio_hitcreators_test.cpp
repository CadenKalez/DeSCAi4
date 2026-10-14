#include <cassert>
#include <cstdio>

#include "audio/AudioServices.h"
#include "audio/HitSegmentCreators.h"
#include "audio/HitManServices.h"
#include "audio/GZSnd.h"
#include "rz/cRZString.h"
#include "sc4audio_fakes.h"

namespace {

struct Services final : Hit::cIHitManServices {
    cGZSnd* lastChannel = nullptr;
    bool HasSoundSystem() const override { return false; }
    void EnumerateUpdateTargets(EnumCallback, std::uint32_t, void*) override {}
    bool GetPersistResource(const cGZPersistResourceKey&, std::uint32_t, void**) override { return false; }
    cIGZUnknown* CreatePropertySetResource(const cGZPersistResourceKey&, const Hit::DefaultProperty*, std::size_t, bool) override { return nullptr; }
    bool ReadPersistRecord(const cGZPersistResourceKey&, std::vector<std::uint8_t>&, std::string&) override { return false; }
    bool GetDefaultSndPlayerClassId(bool, std::uint32_t&) override { return false; }
    bool CreateComInstance(std::uint32_t, std::uint32_t, void**) override { return false; }
    bool GetSoundById(std::uint32_t, std::uint32_t, void**) override { return false; }
    void RegisterUpdateTarget(cIGZSndUpdateTarget*, std::uint32_t) override {}
    void UnregisterUpdateTarget(cIGZSndUpdateTarget*) override {}
    bool SetGlobalProperty(std::uint32_t, const cIGZAny&) override { return false; }
    bool GetGlobalProperty(std::uint32_t, cIGZAny*) override { return false; }
    bool SetInstanceProperty(std::uint32_t, std::uint32_t, const cIGZAny&) override { return false; }
    bool ReadFileBytes(const std::string&, std::vector<std::uint8_t>&) override { return false; }
    std::string ApplicationDirectory() const override { return ""; }
    std::string DataDirectory() const override { return ""; }
    void ParseDebugInfo(const cIGZString&, const std::uint8_t*, std::uint32_t) override {}
    bool CreateTrackPlayer(std::uint32_t, void**) override { return false; }
    bool CreateChannel(void** outSnd) override {
        lastChannel = new cGZSnd();
        lastChannel->AddRef();
        *outSnd = static_cast<cIGZSnd*>(lastChannel);
        return true;
    }
};

struct Source final : cIGZSndDataSource {
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool SetPath(const char*) override { return false; }
    bool GetPath(cIGZString&) override { return false; }
    bool SetKey(const cGZPersistResourceKey&) override { return true; }
    bool GetKey(cGZPersistResourceKey&) override { return true; }
    bool Open() override { return true; }
    bool Close() override { return true; }
    bool IsOpen() const override { return false; }
    std::uint32_t GetDataSize() const override { return 0; }
    bool Lock(std::uint8_t**, std::uint32_t*) override { return false; }
    bool Unlock() override { return true; }
    bool IsLocked() const override { return false; }
    std::int32_t ReadPart(std::uint8_t*, std::int32_t) override { return 0; }
    void ReadReset() override {}
    const GZSndWaveFormat* GetWaveFormat() const override { return nullptr; }
    bool DataIsValid() override { return true; }
    bool SetDataIsValid(bool) override { return true; }
    std::uint32_t GetPosition() const override { return 0; }
    bool SetPosition(std::uint32_t) override { return true; }
};

struct Mgr final : fakes::FakeSndMgr {
    Source* source = nullptr;
    int asked = 0;
    bool GetSndDataSource(std::uint32_t id, std::uint32_t, cIGZVariant*, std::uint32_t iid, void** out) override {
        ++asked;
        if (id != 9 || iid != 0x3afd825f) return false;
        ++source->refs;
        *out = static_cast<cIGZSndDataSource*>(source);
        return true;
    }
};

}  // namespace

int main() {
    Services services;
    Hit::SetServices(&services);
    Source source;
    Mgr mgr;
    mgr.source = &source;
    Audio::SetSoundManagerService(&mgr);

    auto* c = new cHitSegmentDataSourceChannelCreator();
    c->AddRef();
    void* p = nullptr;
    assert(c->QueryInterface(0x3af9866f, &p) && p == static_cast<cIGZSoundCreator*>(c));
    assert(c->QueryInterface(1, &p) && p == static_cast<cIGZSndDefRegistrar*>(c));      // id 1 -> the +8 interface
    assert(c->QueryInterface(0x5b8d131c, &p) && p == static_cast<cIGZSndTrackDefParserRegistrar*>(c));
    assert(!c->QueryInterface(0x1234, &p));
    c->Release(); c->Release(); c->Release();

    void* out = nullptr;
    assert(!c->CreateSound(5, 0, nullptr, 0x206077d2, &out) && services.lastChannel == nullptr);
    assert(c->CreateSound(9, 0, nullptr, 0x206077d2, &out));
    auto* snd = static_cast<cIGZSnd*>(out);
    assert(snd == static_cast<cIGZSnd*>(services.lastChannel));
    assert(services.lastChannel->GetDataSource() == &source);
    cRZString name("n");
    assert(c->RegisterSoundDef(7, name, 0, nullptr));
    snd->Release();                 // the caller's reference: shuts the channel down and frees it
    assert(source.refs == 1);
    assert(c->Release() == 0);

    // track creator: no resource -> false; no Hit manager -> false
    fakes::FakeSndSys unused;
    auto* t = new cHitSegmentTrackCreator();
    t->AddRef();
    t->RegisterSoundDef(0x1234, name, 0, nullptr);
    assert(t->groups().size() == 1 && t->groups().front() == 0x1234);
    assert(!t->CreateSound(1, 0, nullptr, 0xbb1be367, &out));
    assert(t->Release() == 0);
    Audio::SetSoundManagerService(nullptr);
    Hit::SetServices(nullptr);
    std::puts("hitcreators ok");
}
