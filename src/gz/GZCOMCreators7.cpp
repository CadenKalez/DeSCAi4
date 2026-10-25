// GZCOM creators of the simulators and windows whose constructors need services: the hosts come from the SimHosts registry.
#include "gz/GZCOM.h"
#include "gz/SimHostRegistry.h"
#include "sc4/CommercialSimulator.h"
#include "sc4/FireProtectionSimulator.h"
#include "sc4/LandValueSimulator.h"
#include "sc4/LotDeveloper.h"
#include "sc4/PoliceSimulator.h"
#include "sc4/PlumbingSimulator.h"
#include "sc4/PollutionSimulator.h"
#include "sc4/PowerSimulator.h"
#include "sc4/ResidentialSimulator.h"
#include "sc4/TrafficSimulator.h"
#include "sc4/WealthRequester.h"
#include "sc4/WinAuraAlert.h"
#include "sc4/cSC4City.h"
#include "sc4/cSC4FlammabilitySimulator.h"
#include "sc4/cSC4ZoneManager.h"
#include "sc4/cSTETerrainView3D.h"

namespace gzcom {

SimHosts& GetSimHosts() {
    static SimHosts hosts;
    return hosts;
}

cRZRandom& SharedRandom() {
    static cRZRandom fallback(0xffffffffu);
    SimHosts& hosts = GetSimHosts();
    return hosts.random ? *hosts.random : fallback;
}

void* GZCOM_CreatecSC4City() {
    // Prefer the host installed with gzcom::SetCityHost(); the core creator test only fills the SimHosts registry.
    sc4::ICityHost* host = CityHost();
    if (!host) host = GetSimHosts().city;
    return host ? static_cast<void*>(new sc4::cSC4City(host)) : nullptr;
}
void* GZCOM_CreatecSC4TrafficSimulator() {
    return static_cast<void*>(new sc4::cSC4TrafficSimulator(GetSimHosts().traffic, &SharedRandom()));
}
void* GZCOM_CreatecSC4PollutionSimulator() { return static_cast<void*>(new sc4::cSC4PollutionSimulator(GetSimHosts().pollution)); }
void* GZCOM_CreatecSC4PoliceSimulator() { return static_cast<void*>(new sc4::cSC4PoliceSimulator(GetSimHosts().police)); }
void* GZCOM_CreatecSC4FlammabilitySimulator() {
    return static_cast<void*>(new sc4::cSC4FlammabilitySimulator(GetSimHosts().flammability));
}
void* GZCOM_CreatecSC4LandValueSimulator() { return static_cast<void*>(new sc4::cSC4LandValueSimulator(GetSimHosts().landValue)); }
void* GZCOM_CreatecSC4WealthRequester() {
    return static_cast<void*>(new sc4::cSC4WealthRequester(GetSimHosts().wealth, &SharedRandom()));
}
void* GZCOM_CreatecSC4LotDeveloper() { return static_cast<void*>(new sc4::cSC4LotDeveloper(GetSimHosts().lotDeveloper)); }
void* GZCOM_CreatecSC4ZoneManager() { return static_cast<void*>(new sc4::cSC4ZoneManager(GetSimHosts().zoneManager)); }
void* GZCOM_CreatecSC4CommercialSimulator() {
    return static_cast<void*>(new sc4::cSC4CommercialSimulator(GetSimHosts().commercial));
}
void* GZCOM_CreatecSC4ResidentialSimulator() {
    return static_cast<void*>(new sc4::cSC4ResidentialSimulator(GetSimHosts().residential, &SharedRandom()));
}
void* GZCOM_CreatecSC4PowerSimulator() {
    return static_cast<void*>(new sc4::cSC4PowerSimulator(GetSimHosts().power, &SharedRandom()));
}
void* GZCOM_CreatecSC4PlumbingSimulator() {
    return static_cast<void*>(new sc4::cSC4PlumbingSimulator(GetSimHosts().plumbing, &SharedRandom()));
}
void* GZCOM_CreatecSTETerrainView3D() {
    return static_cast<void*>(new sc4::cSTETerrainView3D(GetSimHosts().terrainView));
}
void* GZCOM_CreatecSC4FireProtectionSimulator() {
    return static_cast<void*>(new sc4::cSC4FireProtectionSimulator(GetSimHosts().fire));
}
void* GZCOM_CreatecSC4WinAlertBorder() { return static_cast<void*>(new sc4::cSC4WinAlertBorder(GetSimHosts().windowManager)); }
void* GZCOM_CreatecSC4WinAuraBar() { return static_cast<void*>(new sc4::cSC4WinAuraBar(GetSimHosts().windowManager)); }

}  // namespace gzcom
