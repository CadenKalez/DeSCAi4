// The native host of cSC4BuildingDevelopmentSimulator (NativeBuildingDevelopment.cpp).
#pragma once
#include <cstdint>

namespace sc4 { struct BuildingProfile; class cSC4Demand; }

namespace native {

// Replaces the generated neutral host; installed before the city's components Init.
void InstallBuildingDevelopmentHost();

// The building development simulator's catalogue for the growth developers' host (NativeGrowthDevelopers.cpp cannot include
// BuildingDevelopmentSimulator.h: the headers clash). Null / 0 / false without a city or a building development simulator.
const sc4::BuildingProfile* NativeBuildingProfile(std::uint32_t buildingType);            // vslot 0x138
float NativeMaxCapacity(int zoneType, std::uint32_t purpose, std::uint32_t wealth);      // vslot 0x168
sc4::cSC4Demand* NativeDemandChannel(std::uint32_t type, std::uint32_t keyOffset);       // SL::spDemandSim vslot 0x30
bool NativeBuildingFitsInZone(std::uint32_t buildingPurpose, std::uint32_t zoneType);    // @ 0x100150a86

}  // namespace native
