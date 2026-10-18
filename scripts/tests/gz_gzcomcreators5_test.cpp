#include <cstdio>

#include "gz/GZCOM.h"
#include "sc4/DisasterManagers.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    using namespace gzcom;
    struct Entry { void* (*create)(); std::uint32_t clsid; std::uint32_t managerId; } entries[] = {
        {&GZCOM_CreatecSC4DisasterManagerRiot, kCLSID_cSC4DisasterManagerRiot, sc4::kDisasterManagerIdRiot},
        {&GZCOM_CreatecSC4DisasterManagerEarthquake, kCLSID_cSC4DisasterManagerEarthquake, sc4::kDisasterManagerIdEarthquake},
        {&GZCOM_CreatecSC4DisasterManagerPipeBurst, kCLSID_cSC4DisasterManagerPipeBurst, sc4::kDisasterManagerIdPipeBurst},
        {&GZCOM_CreatecSC4DisasterManagerVolcano, kCLSID_cSC4DisasterManagerVolcano, sc4::kDisasterManagerIdVolcano}};
    for (const Entry& e : entries) {
        void* object = e.create();
        CHECK(object != nullptr);
        const ClassFactoryInfo* info = FindClassFactory(e.clsid);
        CHECK(info != nullptr && info->create == e.create);
        auto* manager = static_cast<sc4::cISC4DisasterManager*>(object);
        manager->AddRef();
        CHECK(manager->GetID() == e.managerId);
        manager->Release();
    }
    void* fire = GZCOM_CreatecSC4DisasterManagerFire();
    CHECK(fire != nullptr && FindClassFactory(kCLSID_cSC4DisasterManagerFire)->create == &GZCOM_CreatecSC4DisasterManagerFire);
    auto* fireView = static_cast<sc4::cISC4DisasterManagerFire*>(fire);
    fireView->AddRef();
    void* manager = nullptr;
    CHECK(fireView->QueryInterface(sc4::kGZIID_cISC4DisasterManager, &manager));
    CHECK(static_cast<sc4::cISC4DisasterManager*>(manager)->GetID() == sc4::kDisasterManagerIdFire);
    static_cast<sc4::cISC4DisasterManager*>(manager)->Release();
    fireView->Release();
    void* instance = GZCOM_CreatecSC4DisasterInstancePipeBurst();
    CHECK(instance != nullptr && FindClassFactory(kCLSID_cSC4DisasterInstancePipeBurst)->create == &GZCOM_CreatecSC4DisasterInstancePipeBurst);
    auto* disaster = static_cast<sc4::cISC4DisasterInstance*>(instance);
    disaster->AddRef();
    CHECK(disaster->GetID() == sc4::kDisasterManagerIdPipeBurst);
    disaster->Release();
    std::printf(failures ? "FAILED\n" : "gz_gzcomcreators5 ok\n");
    return failures != 0;
}
