// Part of gz_gzcomcreators_core: the creators of GZCOMCreators7.cpp (its headers cannot share a translation unit
// with the other creator file's).
#include <cstdio>

#include "fake_winmgr.h"
#include "gz/GZCOM.h"
#include "gz/SimHostRegistry.h"
#include "sc4/CommercialSimulator.h"
#include "sc4/FireProtectionSimulator.h"
#include "sc4/LandValueSimulator.h"
#include "sc4/LotDeveloper.h"
#include "sc4/PoliceSimulator.h"
#include "sc4/PollutionSimulator.h"
#include "sc4/PowerSimulator.h"
#include "sc4/ResidentialSimulator.h"
#include "sc4/TrafficSimulator.h"
#include "sc4/WealthRequester.h"
#include "sc4/WinAuraAlert.h"
#include "sc4/cSC4City.h"
#include "sc4/cSC4FlammabilitySimulator.h"
#include "sc4/cSC4ZoneManager.h"

namespace {
// The city's constructor and destructor talk to their host and the alert windows need a window manager, so the test
// installs minimal ones (the simulators accept the default null hosts until they are used).
struct NullCityHost final : sc4::ICityHost {
    std::uint32_t CreateCitySerialNumber() override { return 1; }
    void GetCurrentLanguageAndCountry(int& language, int& country) override { language = country = 0; }
    cIGZUnknown* CreateComponent(std::uint32_t, std::uint32_t) override { return nullptr; }
    void PostMessage(cIGZMessage2*) override {}
    void SendMessage(cIGZMessage2*) override {}
};

template <class T>
void Destroy(T* object) {
    if constexpr (requires { delete object; }) delete object;
    else object->Release();
}
}  // namespace

int CheckSimulatorCreators(int& created) {
    int failures = 0;
    static NullCityHost cityHost;
    if (!gzcom::GetSimHosts().city) gzcom::GetSimHosts().city = &cityHost;
    static FakeMgr windowManager;   // the alert windows are cGZWin objects and need a manager
    if (!gzcom::GetSimHosts().windowManager) gzcom::GetSimHosts().windowManager = &windowManager;
#define CREATE(Type, Name) do { \
        void* p = gzcom::GZCOM_Create##Name(); \
        const gzcom::ClassFactoryInfo* info = gzcom::FindClassFactory(gzcom::kCLSID_##Name); \
        if (!p || !info || info->create != &gzcom::GZCOM_Create##Name) { std::printf("FAIL %s\n", #Name); ++failures; } \
        if (p) { ++created; Destroy(static_cast<Type*>(p)); } \
    } while (0)
    CREATE(sc4::cSC4City, cSC4City);
    CREATE(sc4::cSC4TrafficSimulator, cSC4TrafficSimulator);
    CREATE(sc4::cSC4PollutionSimulator, cSC4PollutionSimulator);
    CREATE(sc4::cSC4PoliceSimulator, cSC4PoliceSimulator);
    CREATE(sc4::cSC4FlammabilitySimulator, cSC4FlammabilitySimulator);
    CREATE(sc4::cSC4LandValueSimulator, cSC4LandValueSimulator);
    CREATE(sc4::cSC4WealthRequester, cSC4WealthRequester);
    CREATE(sc4::cSC4LotDeveloper, cSC4LotDeveloper);
    CREATE(sc4::cSC4ZoneManager, cSC4ZoneManager);
    CREATE(sc4::cSC4CommercialSimulator, cSC4CommercialSimulator);
    CREATE(sc4::cSC4ResidentialSimulator, cSC4ResidentialSimulator);
    CREATE(sc4::cSC4PowerSimulator, cSC4PowerSimulator);
    CREATE(sc4::cSC4FireProtectionSimulator, cSC4FireProtectionSimulator);
    CREATE(sc4::cSC4WinAlertBorder, cSC4WinAlertBorder);
    CREATE(sc4::cSC4WinAuraBar, cSC4WinAuraBar);
#undef CREATE
    return failures;
}
