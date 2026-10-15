// CPU-side scene assembly: every building of a city becomes textured triangles, batched by texture.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "game/GameData.h"
#include "sc4/CityBuildings.h"

namespace render {

struct BuildingBatch {
    std::uint32_t textureInstance = 0;
    int texW = 0, texH = 0;
    std::vector<std::uint8_t> rgba;          // decoded FSH frame 0
    std::vector<float> vertices;             // x y z u v
    std::vector<std::uint32_t> indices;
};

struct BuildingSceneStats {
    std::size_t buildings = 0, placed = 0, missingExemplar = 0, missingModel = 0, missingTexture = 0, triangles = 0;
};

// zoom: 0..4 (4 = the most detailed model); the model rotation variant follows the building orientation
bool BuildBuildingBatches(game::GameData& data, const std::vector<sc4::BuildingRecord>& buildings, int zoom,
                          std::vector<BuildingBatch>& out, BuildingSceneStats& stats);

}  // namespace render
