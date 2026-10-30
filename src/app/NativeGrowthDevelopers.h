// cSC4GrowthDeveloper for the building development host, as an opaque handle (NativeGrowthDevelopers.cpp says why).
#pragma once

namespace sc4 { class cISCPropertyHolder; }

namespace native {

void* NewGrowthDeveloper();                                          // new cSC4GrowthDeveloper, one reference
bool GrowthDeveloperInit(void* developer, sc4::cISCPropertyHolder* exemplar);   // vslot 0x18
void GrowthDeveloperStartMapUpdate(void* developer);                 // vslot 0xd8
bool GrowthDeveloperMapUpdateFinished(void* developer);              // vslot 0xe0

}  // namespace native
