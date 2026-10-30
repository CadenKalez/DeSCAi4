// cSC4BuildingDevelopmentSimulator::GrowthStageFitsInZone / BuildingFitsInZone (see BuildingZoneFit.h).
#include "sc4/BuildingZoneFit.h"

namespace sc4 {

bool GrowthStageFitsInZone(int stage, std::uint32_t zoneType) {   // @ 0x100150a4c
    if (zoneType > 9) return false;
    if ((0x3c8u >> zoneType) & 1) return true;
    if ((0x12u >> zoneType) & 1) return stage < 4;
    if ((0x24u >> zoneType) & 1) return stage < 7;
    return false;
}

bool BuildingFitsInZone(std::uint32_t buildingPurpose, std::uint32_t zoneType) {   // @ 0x100150a86
    if (buildingPurpose == zoneType) return true;
    switch (zoneType) {
    case 2: return buildingPurpose == 1;
    case 3: return buildingPurpose - 1 < 2;
    case 5: return buildingPurpose == 4;
    case 6: return (buildingPurpose | 1) == 5;
    case 9: return buildingPurpose == 8;
    case 0xb: case 0xc: case 0xf: return buildingPurpose == 0;
    default: return false;
    }
}

}  // namespace sc4
