// Terrain heightmap loaded from a SC4 city save (subfile 0xa9dd6ff4 / group 0xe98f9525 / inst 1).
// Layout: u16 version, then N*N little-endian float32 heights (meters), N = tiles+1.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace render {

struct HeightGrid {
    uint32_t n = 0;                 // points per side
    std::vector<float> heights;     // n*n, row-major
    float minHeight = 0, maxHeight = 0;
};

// Loads the terrain from a .sc4 city file. Returns false on failure; fills `error`.
bool LoadCityTerrain(const std::string& path, HeightGrid& out, std::string& error);

// Metres per terrain cell (a city tile is 16 m wide) and SC4's default sea level.
constexpr float kCellMeters = 16.0f;
constexpr float kSeaLevelMeters = 250.0f;

}  // namespace render
