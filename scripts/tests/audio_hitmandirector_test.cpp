#include <cstdio>
#include <vector>

#include "audio/HitManDirector.h"
#include "hit_iface_stubs.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4audio;

namespace {
struct Res : cIGZPersistResourceManagerStub {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> factories;
    bool RegisterObjectFactory(std::uint32_t classId, std::uint32_t type, cIGZPersistResourceFactory*) override { factories.push_back({classId, type}); return true; }
};
struct Mgr : cIGZSndMgrStub {
    int creators = 0;
    bool RegisterSndCreator(cIGZSoundCreator*) override { ++creators; return true; }
};
struct Hit : cIHitManStub { int shutdowns = 0; bool Shutdown() override { ++shutdowns; return true; } };
struct Debug : cIHitDebugInfoStub { int shutdowns = 0; bool Shutdown() override { ++shutdowns; return true; } };
struct Creator : cIGZSoundCreator {
    int refs = 1;
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    bool CreateSound(std::uint32_t, std::uint32_t, cIGZVariant*, std::uint32_t, void**) override { return false; }
};
struct Service : cIGZUnknown {
    int refs = 1;
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
};

struct Host : IHitDirectorHost {
    std::vector<std::uint32_t> added, registeredServices, created;
    Res res; Mgr mgr; Hit hit; Debug debug; Creator creator; Service service;
    int directors = 0;
    void AddClass(std::uint32_t id) override { added.push_back(id); }
    cIGZPersistResourceManager* ResourceManager() override { return &res; }
    cIGZSndMgr* SoundManager() override { return &mgr; }
    cIHitMan* HitManager() override { return &hit; }
    cIHitDebugInfo* DebugInfo() override { return &debug; }
    bool CreateInstance(std::uint32_t classId, std::uint32_t iid, void** out) override {
        created.push_back(classId);
        if (iid == kGZIID_cIGZSoundCreator) { creator.AddRef(); *out = &creator; return true; }
        service.AddRef();
        *out = &service;
        return true;
    }
    void RegisterSystemService(cIGZUnknown*) override { registeredServices.push_back(created.back()); }
    void AddSystemServiceDirector(cIGZUnknown*) override { ++directors; }
};
}  // namespace

int main() {
    Host host;
    cHitManCOMDirector director(&host);
    CHECK(director.GetDirectorID() == 0x9b2d3d2f);
    CHECK(host.added.size() == 10 && host.added[0] == 0x7b1bd993 && host.added[2] == 0x5b945038 && host.added[7] == 0x3d802d67 && host.added[9] == 0x7b1acfcd);
    CHECK(director.OnStart(nullptr) && host.directors == 1 && host.registeredServices.size() == 2);
    CHECK(director.PreAppInit());
    CHECK(host.res.factories.size() == 4 && host.res.factories[0] == std::make_pair(0x5b945038u, 0xdb9a4f83u) &&
          host.res.factories[3] == std::make_pair(0x5b945038u, 0xebfee345u));
    CHECK(host.mgr.creators == 1 && host.creator.refs == 1);
    CHECK(director.PreAppShutdown() && host.hit.shutdowns == 1 && host.debug.shutdowns == 1);
    std::printf(failures ? "FAILED\n" : "audio_hitmandirector: all passed\n");
    return failures ? 1 : 0;
}
