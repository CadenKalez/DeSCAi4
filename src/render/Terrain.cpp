#include "render/Terrain.h"

#include <algorithm>

#include "gz/cGZDBSegmentPackedFile.h"
#include "rz/cRZString.h"
#include "sc4/cSTETerrain.h"

namespace render {

// Loads the city through the recovered persistence layer and terrain model.
bool LoadCityTerrain(const std::string& path, HeightGrid& out, std::string& error) {
    auto* segment = new cGZDBSegmentPackedFile();
    segment->AddRef();
    segment->Init();
    cRZString p(path.c_str());
    segment->SetPath(p);
    if (!segment->Open(true, false)) {
        error = "cannot open package: " + path;
        segment->Release();
        return false;
    }
    sc4::cSTETerrain terrain;
    bool ok = terrain.Load(segment) && terrain.LastLoadSucceeded();
    segment->Close();
    segment->Shutdown();
    segment->Release();
    if (!ok) {
        error = "city has no terrain subfiles";
        return false;
    }
    out.n = terrain.VertexCountX();
    out.heights.resize(terrain.Vertices().size());
    for (std::size_t i = 0; i < out.heights.size(); ++i) out.heights[i] = terrain.Vertices()[i].altitude;
    auto [lo, hi] = std::minmax_element(out.heights.begin(), out.heights.end());
    out.minHeight = *lo;
    out.maxHeight = *hi;
    return true;
}

}  // namespace render
