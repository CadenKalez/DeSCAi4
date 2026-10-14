#include "render/Terrain.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "dbpf/DBPFReader.h"

namespace render {

bool LoadCityTerrain(const std::string& path, HeightGrid& out, std::string& error) {
    dbpf::Package pkg;
    if (!pkg.Open(path)) { error = "cannot open DBPF: " + path; return false; }
    const dbpf::Entry* e = pkg.Find(0xa9dd6ff4, 0xe98f9525, 1);
    if (!e) { error = "city has no terrain record"; return false; }
    std::vector<uint8_t> raw;
    if (!pkg.Read(*e, raw) || raw.size() < 2 + 4) { error = "terrain record unreadable"; return false; }
    size_t floats = (raw.size() - 2) / 4;
    uint32_t n = uint32_t(std::lround(std::sqrt(double(floats))));
    if (n < 2 || size_t(n) * n != floats) { error = "terrain size is not a square grid"; return false; }
    out.n = n;
    out.heights.resize(floats);
    std::memcpy(out.heights.data(), raw.data() + 2, floats * 4);  // little-endian host == file
    auto [lo, hi] = std::minmax_element(out.heights.begin(), out.heights.end());
    out.minHeight = *lo;
    out.maxHeight = *hi;
    return true;
}

}  // namespace render
