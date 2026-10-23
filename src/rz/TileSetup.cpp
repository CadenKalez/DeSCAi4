#include "rz/TileSetup.h"

#include <algorithm>
#include <list>

namespace {

struct Rect {
    std::int32_t left, top, right, bottom;
};
using RectList = std::list<Rect>;

// 0x1004ecad3: adds `r` to the list, first trying to grow a rectangle that shares a full edge with it. New rectangles go in
// front, and the search runs from the front.
void MergeRectangle(RectList& list, const Rect& r) {
    for (Rect& n : list) {
        if (r.top == n.top && r.bottom == n.bottom) {
            if (r.left == n.right) { n.right = r.right; return; }
            if (r.right == n.left) { n.left = r.left; return; }
        }
        if (r.left == n.left && r.right == n.right) {
            if (r.top == n.bottom) { n.bottom = r.bottom; return; }
            if (r.bottom == n.top) { n.top = r.top; return; }
        }
    }
    list.push_front(r);
}

}  // namespace

// 0x1004ec1ba
void cRZTileSetup::Init(const cRZTileSize& size, std::uint32_t minTile, std::uint32_t maxTile, std::uint32_t align) {
    minTile_ = minTile;
    maxTile_ = maxTile;
    align_ = align;
    tiles_.clear();

    const std::uint32_t width = static_cast<std::uint32_t>(size.width);
    const std::uint32_t height = static_cast<std::uint32_t>(size.height);
    std::uint32_t paddedWidth = width - 1 + align;
    paddedWidth -= paddedWidth % align;
    std::uint32_t paddedHeight = align - 1 + height;
    paddedHeight -= paddedHeight % align;
    const std::uint32_t columns = paddedWidth / maxTile;
    const std::uint32_t rows = paddedHeight / maxTile;

    RectList pending, working, free;   // leftovers for the next pass / the pass being worked on / free corners of the newest tile

    // 1. full maxTile x maxTile tiles
    if (maxTile <= paddedHeight) {
        for (std::uint32_t row = 0; row < rows; ++row) {
            if (maxTile > paddedWidth) continue;
            for (std::uint32_t column = 0; column < columns; ++column) {
                const std::int32_t t = static_cast<std::int32_t>(maxTile);
                cRZTile tile;
                tile.size = {t, t};
                cRZSubTile sub;
                sub.x = t * static_cast<std::int32_t>(column);
                sub.y = t * static_cast<std::int32_t>(row);
                sub.width = t;
                // Kept: the width overhang is subtracted from the height too (the second test then overrides it).
                std::int32_t h = t;
                std::int32_t over = (sub.x + t) - size.width;
                if (over > 0) h = t - over;
                over = (sub.y + t) - size.height;
                if (over > 0) h = t - over;
                sub.height = h;
                tile.subTiles.push_back(sub);
                tiles_.push_back(std::move(tile));
            }
        }
    }

    // 2. leftovers: the strip right of the full columns and the strip below the full rows
    const std::uint32_t coveredWidth = maxTile * columns;
    const std::uint32_t coveredHeight = maxTile * rows;
    if (coveredWidth < width && coveredHeight != 0)
        pending.push_front({static_cast<std::int32_t>(coveredWidth), 0, static_cast<std::int32_t>(paddedWidth), static_cast<std::int32_t>(coveredHeight)});
    if (coveredHeight < height)
        pending.push_front({0, static_cast<std::int32_t>(coveredHeight), static_cast<std::int32_t>(paddedWidth), static_cast<std::int32_t>(paddedHeight)});
    maxFragments_ = 1;

    std::uint32_t remaining = paddedHeight * paddedWidth - maxTile * maxTile * columns * rows;
    while (!pending.empty()) {
        std::swap(pending, working);
        while (!working.empty()) {
            const std::uint32_t before = remaining;
            if (free.empty()) {
                // A new tile: the smallest power-of-two rectangle (at least minTile, at most maxTile^2 pixels) that holds
                // what is still missing.
                std::int32_t w = static_cast<std::int32_t>(minTile);
                std::int32_t h = w;
                std::uint32_t area = minTile * minTile;
                const std::uint32_t maxArea = maxTile * maxTile;
                if (area < remaining && area < maxArea) {
                    for (;;) {
                        w *= 2;
                        if (!(area * 2 < remaining)) break;
                        h *= 2;
                        area <<= 2;
                        if (remaining <= area || maxArea <= area) break;
                    }
                }
                cRZTile tile;
                tile.size = {w, h};
                tiles_.push_back(std::move(tile));
                free.push_back({0, 0, w, h});
            }
            cRZTile& current = tiles_.back();

            const Rect rect = working.front();
            const std::int32_t rectWidth = rect.right - rect.left;
            const std::int32_t rectHeight = rect.bottom - rect.top;
            // The free corner that takes the biggest piece of this rectangle.
            RectList::iterator best = free.end();
            std::uint32_t bestArea = 0;
            std::int32_t bestWidth = 0, bestHeight = 0;
            for (auto it = free.begin(); it != free.end(); ++it) {
                const std::int32_t fw = it->right - it->left;
                const std::int32_t fh = it->bottom - it->top;
                const std::int32_t cw = std::min<std::uint32_t>(static_cast<std::uint32_t>(rectWidth), static_cast<std::uint32_t>(fw));
                const std::int32_t ch = std::min<std::uint32_t>(static_cast<std::uint32_t>(rectHeight), static_cast<std::uint32_t>(fh));
                if (bestArea < static_cast<std::uint32_t>(ch) * static_cast<std::uint32_t>(cw)) {
                    bestArea = static_cast<std::uint32_t>(ch) * static_cast<std::uint32_t>(cw);
                    bestWidth = cw;
                    bestHeight = ch;
                    best = it;
                }
            }
            if (best == free.end()) best = free.begin();   // (the original dereferences a null node here)
            const Rect corner = *best;

            cRZSubTile sub;
            sub.x = rect.left;
            sub.y = rect.top;
            sub.tileX = corner.left;
            sub.tileY = corner.top;
            sub.width = bestWidth;
            sub.height = bestHeight;
            const std::int32_t overW = (bestWidth + rect.left) - size.width;
            if (overW > 0) sub.width = bestWidth - overW;
            const std::int32_t bottom = bestHeight + rect.top;
            if (bottom - size.height > 0) sub.height = bestHeight - (bottom - size.height);
            current.subTiles.push_back(sub);
            maxFragments_ = std::max<std::uint32_t>(maxFragments_, static_cast<std::uint32_t>(current.subTiles.size()));

            free.erase(best);
            if (corner.bottom - corner.top != bestHeight) MergeRectangle(free, {corner.left, corner.top + bestHeight, corner.right, corner.bottom});
            if (corner.right - corner.left != bestWidth) MergeRectangle(free, {corner.left + bestWidth, corner.top, corner.right, corner.top + bestHeight});

            working.pop_front();
            if (bestHeight != rectHeight) MergeRectangle(working, {rect.left, rect.top + bestHeight, rect.right, rect.bottom});
            if (bestWidth != rectWidth) MergeRectangle(pending, {rect.left + bestWidth, rect.top, rect.right, rect.top + bestHeight});
            remaining = before - bestArea;
        }
    }
}
