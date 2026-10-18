#include <cstdio>

#include "sc4/QuadGrid.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
int g_deleted = 0;
struct Elem : SC4GridElement { ~Elem() override { ++g_deleted; } };
}  // namespace

int main() {
    SC4QuadGrid grid(64.0f, 16.0f);
    CHECK(grid.Dimension() == 4 && grid.ElementCount() == 0 && grid.CellSize() == 16.0f);
    auto* a = new Elem();
    grid.SetGridRectForElement({0, 0, 32, 16}, a);
    // 32 / 16 - 0.5 = 1.5 rounds to even 2; 16 / 16 - 0.5 = 0.5 rounds to 0; 0 / 16 - 0.5 = -0.5 rounds to 0
    CHECK(a->x0 == 0 && a->z0 == 0 && a->x1 == 2 && a->z1 == 0);
    CHECK(grid.AddElement(a, 0x5) && grid.ElementCount() == 1 && a->refCount == 3);
    CHECK(grid.Quadrant(0, 0).flags == 5 && grid.Quadrant(1, 0).flags == 5 && grid.Quadrant(3, 0).flags == 0);
    SC4GridElement* all[8]; int cap = 8;
    grid.GetAllElements(all, cap);
    CHECK(cap == 1 && all[0] == a && (a->flags & 0x4000) == 0);   // listed once although in three quadrants
    cap = 16;
    SC4QuadGridQuadrant* quads[16];
    grid.GetQuadrantsOfScreenRect({0, 0, 40, 40}, quads, cap);
    CHECK(cap == 9 && quads[0] == &grid.Quadrant(0, 0) && quads[3] == &grid.Quadrant(1, 0));   // cells 0..2 on both axes, x-major
    int x0, z0, x1, z1;
    grid.GetQuadrantsOfScreenRect({-100, -100, 1000, 1000}, x0, z0, x1, z1);
    CHECK(x0 == 0 && z0 == 0 && x1 == 3 && z1 == 3);
    grid.SetQuadrantFlagsForElement(a, 0x10);
    CHECK(grid.Quadrant(2, 0).flags == 0x15 && grid.Quadrant(3, 0).flags == 0);
    // move: new rect covers one quadrant -> two quadrants drop the element
    CHECK(grid.MoveElement(a, {16, 32, 16, 32}, 0x2) && a->x1 == 0 && a->z0 == 2 && a->refCount == 1);
    CHECK(grid.MoveElement(a, {16, 32, 16, 32}, 0x2));   // unchanged rectangle: nothing happens
    CHECK(grid.Quadrant(0, 2).elements.size() == 1 && grid.Quadrant(1, 0).elements.empty());
    CHECK(grid.RemoveElement(a) && grid.ElementCount() == 0 && g_deleted == 1);   // the last quadrant lets go -> deleted
    auto* b = new Elem();
    b->x0 = 9; b->x1 = 9; b->z0 = 0; b->z1 = 0;   // entirely outside the grid
    CHECK(!grid.AddElement(b, 1));
    delete b;
    auto* c = new Elem();
    c->x0 = -3; c->z0 = -3; c->x1 = 100; c->z1 = 100;   // clamped to the whole grid
    CHECK(grid.AddElement(c, 0x40) && c->refCount == 16 && grid.Quadrant(3, 3).flags == 0x40);
    grid.RemoveAllElements();
    CHECK(g_deleted == 3 && grid.Quadrant(0, 0).elements.empty());
    std::printf(failures ? "FAILED\n" : "sc4_quadgrid ok\n");
    return failures != 0;
}
