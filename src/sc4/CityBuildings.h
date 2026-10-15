// Reads every building of a city save: the class-object record of cSC4BuildingOccupant (0xa9bd882d) is a
// run of entries { u32 size, u32 crc, id (u64 from segment version 0x42, else u32), object body }.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gz/cGZPersistResourceKey.h"

namespace sc4 {

struct BuildingRecord {
    std::uint64_t objectId = 0;
    std::uint32_t buildingType = 0;       // exemplar instance of the building
    cGZPersistResourceKey exemplar;       // the occupant's default exemplar (type, group, instance)
    float min[3] = {}, max[3] = {};       // world bounding box in metres
    std::int32_t orientation = 0;         // 0..3 quarter turns
    bool lit = false;
    float completion = 1.0f;
};

bool LoadCityBuildings(const std::string& cityPath, std::vector<BuildingRecord>& out, std::string& error);

}  // namespace sc4
