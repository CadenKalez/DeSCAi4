// cSC4BuildingDevelopmentSimulator's two static zone predicates, in their own unit so that the classes whose headers clash with
// BuildingDevelopmentSimulator.h (the tract developer) can call them; the simulator's statics forward here.
#pragma once
#include <cstdint>

namespace sc4 {

bool GrowthStageFitsInZone(int stage, std::uint32_t zoneType);                    // @ 0x100150a4c
bool BuildingFitsInZone(std::uint32_t buildingPurpose, std::uint32_t zoneType);   // @ 0x100150a86

}  // namespace sc4
