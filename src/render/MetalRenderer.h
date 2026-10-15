// Native Metal terrain renderer (Apple silicon). No OpenGL anywhere.
// The terrain is drawn without a CPU mesh: heights live in one R32Float texture and the
// vertex stage expands (n-1)^2 quads from the vertex id, deriving normals by central
// differences. That keeps the resident cost at 4 bytes per height sample.
#pragma once
#include <cstdint>
#include <string>

#include <vector>

#include "render/BuildingMeshes.h"
#include "render/Terrain.h"

namespace render {

struct Camera {
    float yawDegrees = 45.0f;     // SC4's default isometric view sits on a diagonal
    float pitchDegrees = 35.0f;
    float distance = 1.0f;        // 0 = auto-fit the terrain
    float centerX = 0, centerZ = 0;
};

class MetalRenderer {
public:
    MetalRenderer();
    ~MetalRenderer();
    bool Init(std::string& error);                       // device, queue, pipelines
    bool SetTerrain(const HeightGrid& grid, std::string& error);
    // Upload the textured building batches (see BuildBuildingBatches).
    bool SetBuildings(const std::vector<BuildingBatch>& batches, std::string& error);
    // Render one frame into an offscreen target and write a PNG. Used by the screenshot survey.
    bool RenderToPng(const std::string& path, uint32_t width, uint32_t height, const Camera& cam,
                     std::string& error);
    // Attach to an existing CAMetalLayer (void* to avoid pulling ObjC types into C++ headers).
    bool AttachLayer(void* caMetalLayer, std::string& error);
    void DrawToLayer(const Camera& cam, uint32_t width, uint32_t height);

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace render
