// cSC4GrowthDeveloper for the building development host, as an opaque handle (NativeGrowthDevelopers.cpp says why).
#pragma once

#include <cstdint>
#include <string>

namespace sc4 { class cISCPropertyHolder; template <class T> class cSC4SimGrid; }

namespace native {

void* NewGrowthDeveloper();                                          // new cSC4GrowthDeveloper, one reference
bool GrowthDeveloperInit(void* developer, sc4::cISCPropertyHolder* exemplar);   // vslot 0x18
void GrowthDeveloperStartMapUpdate(void* developer);                 // vslot 0xd8
bool GrowthDeveloperMapUpdateFinished(void* developer);              // vslot 0xe0

// The land value simulator's maps (NativeGrowthMaps.cpp)
sc4::cSC4SimGrid<std::uint8_t>* NativeTotalLandValueMap();
sc4::cSC4SimGrid<std::uint8_t>* NativeIntrinsicLandValueMap();
sc4::cSC4SimGrid<float>* NativeSlopeMap();

void InstallGrowthDeveloperHost();
// " subtype:growth/decline" per developer ('!' when the lists are not valid yet), for SC4_SIMDEBUG
std::string GrowthDevelopersSummary();   // the growth developers' IGrowthDeveloperHost (growth layer 2)

}  // namespace native
