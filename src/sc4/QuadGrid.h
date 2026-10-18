// SC4QuadGrid — the uniform bucket grid the 3D view uses to find elements by screen rectangle (17 functions at
// 0x100366d80..0x10036764c). A grid of n x n quadrants (n = size / cell size + 0.1, truncated) holds raw pointers to
// SC4GridElements; every quadrant an element's cell rectangle covers keeps a pointer and one reference count, and
// the element is deleted (virtual destructor) when the last quadrant lets go.
//
// Cell coordinates come from screen floats as round-to-nearest of (value / cellSize - 0.5), computed with the original's
// add-1.5*2^23 trick on single-precision floats so ties and rounding match bit for bit.
// Layout: +0 element count, +4 size, +8 cell size, +0xc inverse cell size, +0x10 n, +0x18 vector (x) of vector (z) of quadrants.
#pragma once
#include <cstdint>
#include <vector>

struct SC4RectFloat {
    float x0 = 0, z0 = 0, x1 = 0, z1 = 0;
};

class SC4GridElement {
public:
    virtual ~SC4GridElement() = default;
    std::int16_t refCount = 0;       // +8 (one per quadrant holding it, plus temporary holds)
    std::uint16_t flags = 0;         // +0xa (0x4000: already collected by GetAllElements)
    std::int32_t x0 = 0, z0 = 0, x1 = 0, z1 = 0;   // +0xc..+0x18 inclusive cell rectangle
};

class SC4QuadGridQuadrant {
public:
    void AddElement(SC4GridElement* element);       // @ 0x100366d80
    void RemoveElement(SC4GridElement* element);    // @ 0x100366dbc (swap-with-last removal)
    void RemoveAllElements();                       // @ 0x100366e28

    std::vector<SC4GridElement*> elements;          // +0
    std::uint32_t flags = 0;                        // +0x18 (only the low 16 bits are ever written)
};

class SC4QuadGrid {
public:
    SC4QuadGrid(float size, float cellSize);        // @ 0x100366e7a
    ~SC4QuadGrid();                                 // @ 0x100366f96
    bool SetSize(float size, float cellSize);       // @ 0x100366ed8

    void RemoveAllElements();                       // @ 0x100366fd4
    bool AddElement(SC4GridElement* element, std::uint32_t flags);    // @ 0x100367038
    bool RemoveElement(SC4GridElement* element);                      // @ 0x100367146
    bool MoveElement(SC4GridElement* element, const SC4RectFloat& rect, std::uint32_t flags);   // @ 0x10036722a
    void GetAllElements(SC4GridElement** out, int& capacity);          // @ 0x1003672ea
    void GetQuadrantsOfScreenRect(const SC4RectFloat& rect, SC4QuadGridQuadrant** out, int& capacity);   // @ 0x10036744a
    void GetQuadrantsOfScreenRect(const SC4RectFloat& rect, int& x0, int& z0, int& x1, int& z1);         // @ 0x1003674f6
    void SetGridRectForElement(const SC4RectFloat& rect, SC4GridElement* element);    // @ 0x1003675be
    void SetQuadrantFlagsForElement(const SC4GridElement* element, std::uint32_t flags);   // @ 0x10036764c

    int ElementCount() const { return count_; }
    int Dimension() const { return dimension_; }
    float CellSize() const { return cellSize_; }
    SC4QuadGridQuadrant& Quadrant(int x, int z) { return grid_[x][z]; }

private:
    int Cell(float value) const;
    bool ClampedRange(const SC4GridElement* element, int& xMin, int& zMin, int& xMax, int& zMax) const;

    int count_ = 0;                  // +0
    float size_ = 0;                 // +4
    float cellSize_ = 1.0f;          // +8
    float inverseCellSize_ = 1.0f;   // +0xc
    int dimension_ = 1;              // +0x10
    std::vector<std::vector<SC4QuadGridQuadrant>> grid_;   // +0x18
};
