#include "sc4/QuadGrid.h"

#include <algorithm>
#include <bit>

#pragma STDC FP_CONTRACT OFF

void SC4QuadGridQuadrant::AddElement(SC4GridElement* element) {
    elements.push_back(element);
    ++element->refCount;
}

void SC4QuadGridQuadrant::RemoveElement(SC4GridElement* element) {
    auto it = std::find(elements.begin(), elements.end(), element);
    if (it == elements.end()) return;
    const std::int16_t remaining = --element->refCount;
    if (element && remaining == 0) delete element;
    if (it != elements.end() - 1) *it = elements.back();
    elements.pop_back();
}

void SC4QuadGridQuadrant::RemoveAllElements() {
    for (SC4GridElement* element : elements) {
        if (--element->refCount == 0 && element) delete element;
    }
    elements.clear();
}

SC4QuadGrid::SC4QuadGrid(float size, float cellSize) : size_(size) { SetSize(size, cellSize); }

SC4QuadGrid::~SC4QuadGrid() { RemoveAllElements(); }

bool SC4QuadGrid::SetSize(float size, float cellSize) {
    dimension_ = static_cast<int>(static_cast<double>(size / cellSize) + 0.1);
    size_ = size;
    cellSize_ = cellSize;
    inverseCellSize_ = 1.0f / cellSize;
    grid_.resize(static_cast<std::size_t>(dimension_));
    for (auto& column : grid_) column.resize(static_cast<std::size_t>(dimension_));
    return true;
}

void SC4QuadGrid::RemoveAllElements() {
    for (int x = 0; x < dimension_; ++x)
        for (int z = 0; z < dimension_; ++z) grid_[x][z].RemoveAllElements();
}

int SC4QuadGrid::Cell(float value) const {
    float t = value * inverseCellSize_;
    t = t + -0.5f;
    t = t + 12582912.0f;
    return std::bit_cast<std::int32_t>(t) - 0x4b400000;
}

bool SC4QuadGrid::ClampedRange(const SC4GridElement* e, int& xMin, int& zMin, int& xMax, int& zMax) const {
    xMin = e->x0 < 0 ? 0 : e->x0;
    zMin = e->z0 < 0 ? 0 : e->z0;
    xMax = e->x1 >= dimension_ ? dimension_ - 1 : e->x1;
    zMax = e->z1 >= dimension_ ? dimension_ - 1 : e->z1;
    return xMin <= xMax;
}

bool SC4QuadGrid::AddElement(SC4GridElement* element, std::uint32_t flags) {
    int xMin, zMin, xMax, zMax;
    if (!ClampedRange(element, xMin, zMin, xMax, zMax)) return false;
    bool added = false;
    for (int x = xMin; x <= xMax; ++x) {
        if (zMin <= zMax) {
            for (int z = zMin; z <= zMax; ++z) {
                SC4QuadGridQuadrant& q = grid_[x][z];
                q.AddElement(element);
                q.flags = (q.flags & 0xffff0000u) | ((q.flags | flags) & 0xffffu);
            }
            added = true;
        }
    }
    if (!added) return false;
    ++count_;
    return true;
}

bool SC4QuadGrid::RemoveElement(SC4GridElement* element) {
    int xMin, zMin, xMax, zMax;
    if (!ClampedRange(element, xMin, zMin, xMax, zMax)) return false;
    bool removed = false;
    for (int x = xMin; x <= xMax; ++x) {
        if (zMin <= zMax) {
            for (int z = zMin; z <= zMax; ++z) grid_[x][z].RemoveElement(element);
            removed = true;
        }
    }
    if (!removed) return false;
    --count_;
    return true;
}

bool SC4QuadGrid::MoveElement(SC4GridElement* element, const SC4RectFloat& rect, std::uint32_t flags) {
    const int nx0 = Cell(rect.x0), nz0 = Cell(rect.z0), nx1 = Cell(rect.x1), nz1 = Cell(rect.z1);
    if (nx0 == element->x0 && nz0 == element->z0 && nx1 == element->x1 && nz1 == element->z1) return true;
    ++element->refCount;
    bool result;
    if (!RemoveElement(element)) {
        result = false;
    } else {
        element->x0 = nx0;
        element->z0 = nz0;
        element->x1 = nx1;
        element->z1 = nz1;
        result = AddElement(element, flags);
    }
    if (--element->refCount == 0) delete element;
    return result;
}

void SC4QuadGrid::GetAllElements(SC4GridElement** out, int& capacity) {
    std::vector<SC4GridElement*> found;
    int count = 0;
    bool full = false;
    for (int x = 0; x < dimension_ && !full; ++x) {
        for (int z = 0; z < dimension_ && !full; ++z) {
            for (SC4GridElement* element : grid_[x][z].elements) {
                if ((element->flags & 0x4000) != 0) continue;
                element->flags |= 0x4000;
                found.push_back(element);
                ++count;
                if (capacity <= count) {
                    full = true;
                    break;
                }
            }
        }
    }
    capacity = static_cast<int>(found.size());
    for (int i = 0; i < capacity; ++i) {
        out[i] = found[static_cast<std::size_t>(i)];
        out[i]->flags &= static_cast<std::uint16_t>(~0x4000u);
    }
}

void SC4QuadGrid::GetQuadrantsOfScreenRect(const SC4RectFloat& rect, int& x0, int& z0, int& x1, int& z1) {
    x0 = Cell(rect.x0);
    z0 = Cell(rect.z0);
    x1 = Cell(rect.x1);
    z1 = Cell(rect.z1);
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (dimension_ <= x1) x1 = dimension_ - 1;
    if (dimension_ <= z1) z1 = dimension_ - 1;
}

void SC4QuadGrid::GetQuadrantsOfScreenRect(const SC4RectFloat& rect, SC4QuadGridQuadrant** out, int& capacity) {
    int x0, z0, x1, z1;
    GetQuadrantsOfScreenRect(rect, x0, z0, x1, z1);
    int count = 0;
    for (int x = x0; x <= x1; ++x) {
        for (int z = z0; z <= z1; ++z) {
            if (capacity <= count) {
                capacity = count;
                return;
            }
            out[count++] = &grid_[x][z];
        }
    }
    capacity = count;
}

void SC4QuadGrid::SetGridRectForElement(const SC4RectFloat& rect, SC4GridElement* element) {
    element->x0 = Cell(rect.x0);
    element->z0 = Cell(rect.z0);
    element->x1 = Cell(rect.x1);
    element->z1 = Cell(rect.z1);
}

void SC4QuadGrid::SetQuadrantFlagsForElement(const SC4GridElement* element, std::uint32_t flags) {
    int xMin, zMin, xMax, zMax;
    if (!ClampedRange(element, xMin, zMin, xMax, zMax)) return;
    for (int x = xMin; x <= xMax; ++x) {
        for (int z = zMin; z <= zMax; ++z) {
            SC4QuadGridQuadrant& q = grid_[x][z];
            q.flags = (q.flags & 0xffff0000u) | ((q.flags | flags) & 0xffffu);
        }
    }
}
