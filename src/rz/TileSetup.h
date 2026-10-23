// cRZTileSetup — splits an image of any size into power-of-two texture tiles (4 functions 0x1004ec1ba..0x1004ecbac plus the
// anonymous-namespace helper MergeRectangle at 0x1004ecad3). Used by the GL buffer cache to upload large buffers as several
// textures.
//
// Init(size, minTile, maxTile, align): the image (padded up to `align`) is first cut into full maxTile x maxTile tiles; the
// right-hand and bottom leftovers are then packed greedily: a new tile is picked just big enough (at least minTile) for the
// pixels still missing, the biggest leftover rectangle that fits into one of its free corners is copied in, and the
// remainders of both rectangles go back on their lists (neighbouring remainders with an equal edge are merged).
//
// Object layout of the original (0x40 bytes): +0 deque<cRZTile>, +0x30 minTile, +0x34 maxTile, +0x38 align,
// +0x3c most subtiles any tile holds.
#pragma once
#include <cstdint>
#include <deque>
#include <vector>

// (the same layout as cRZSize in gz/WinScrollbar2.h, kept separate so both headers can be included together)
struct cRZTileSize {
    std::int32_t width = 0, height = 0;
};

// One piece of the image placed into a tile: `x, y` its position in the image, `tileX, tileY` where it sits inside the
// tile texture, `width, height` its size (clipped to the image).
struct cRZSubTile {
    std::int32_t x = 0, y = 0;
    std::int32_t tileX = 0, tileY = 0;
    std::int32_t width = 0, height = 0;
};
static_assert(sizeof(cRZSubTile) == 0x18);

struct cRZTile {
    std::vector<cRZSubTile> subTiles;   // +0
    cRZTileSize size;                       // +0x18 texture size
};

class cRZTileSetup {
public:
    void Init(const cRZTileSize& size, std::uint32_t minTile, std::uint32_t maxTile, std::uint32_t align);   // 0x1004ec1ba
    const cRZTile& GetTile(int index) const { return tiles_[static_cast<std::size_t>(index)]; }         // 0x1004ecb7e
    std::int32_t GetTileCount() const { return static_cast<std::int32_t>(tiles_.size()); }              // 0x1004ecba2
    std::uint32_t GetMaxFragmentsPerTile() const { return maxFragments_; }                              // 0x1004ecbac

private:
    std::deque<cRZTile> tiles_;
    std::uint32_t minTile_ = 0, maxTile_ = 0, align_ = 0, maxFragments_ = 0;
};
