#include <cmath>
#include <cstdio>
#include "sc4/PowerLineGeometry.h"
#include "sc4/SCProperty.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static bool Near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e * (1.0f + std::fabs(a) + std::fabs(b)); }
using namespace sc4;
int main() {
    PowerLineTunables t;
    t.Read(nullptr);
    CHECK(t.maxHorizontalControlPointDistance == 16.0f && t.horizontalControlPointPercent == 10.0f && t.maxVerticalControlPointDistance == 10.0f);
    CHECK(Near(t.verticalControlPointPercent, 0.15f) && t.curveDistanceBetweenPoints == 8.0f && t.minBezierPoints == 5);
    cS3DVector3 c0, c1;
    int n = 0;
    GetPowerLineControlPoints(t, {0, 0, 0}, {30, 0, 0}, c0, c1, n);
    CHECK(Near(c0.x, 12.0f) && Near(c1.x, 18.0f) && Near(c0.y, -1.35f, 1e-4f) && Near(c1.y, -1.35f, 1e-4f) && c0.z == 0 && n == 5);
    // long span: the control offset and drop saturate at their maxima, more segments
    GetPowerLineControlPoints(t, {0, 5, 0}, {0, 5, 400}, c0, c1, n);
    CHECK(Near(c0.z, 200.0f - 16.0f) && Near(c1.z, 200.0f + 16.0f) && Near(c0.y, 5.0f - 10.0f) && n == 50);
    // the ends need not be level: control points follow the chord, then drop
    GetPowerLineControlPoints(t, {0, 0, 0}, {10, 10, 0}, c0, c1, n);
    CHECK(c0.x < c1.x && n == 5);
    // tunables from an exemplar
    cSCPropertyHolder ex;
    cSCPropertyHelp::SetPropertyValue(&ex, 0xe9a18d3f, 4.0f);
    cSCPropertyHelp::SetPropertyValue(&ex, 0x89a1e8fb, std::uint32_t(9));
    t.Read(&ex);
    CHECK(t.maxHorizontalControlPointDistance == 4.0f && t.minBezierPoints == 9 && t.curveDistanceBetweenPoints == 8.0f);
    GetPowerLineControlPoints(t, {0, 0, 0}, {30, 0, 0}, c0, c1, n);
    CHECK(Near(c0.x, 12.0f) && n == 9);
    t.Read(&ex);
    cSCPropertyHolder none;
    t.Read(&none);
    CHECK(t.maxHorizontalControlPointDistance == 16.0f);

    std::vector<WireCell> cells;
    GetPowerWireCells(0, 0, 4, 2, cells);
    const WireCell e1[] = {{0, 0}, {1, 0}, {2, 1}, {3, 1}, {4, 2}};
    CHECK(cells.size() == 5);
    for (size_t i = 0; i < cells.size() && i < 5; ++i) CHECK(cells[i].x == e1[i].x && cells[i].z == e1[i].z);
    GetPowerWireCells(4, 2, 0, 0, cells);
    const WireCell e2[] = {{4, 2}, {3, 1}, {2, 1}, {1, 0}, {0, 0}};
    CHECK(cells.size() == 5);
    for (size_t i = 0; i < cells.size() && i < 5; ++i) CHECK(cells[i].x == e2[i].x && cells[i].z == e2[i].z);
    GetPowerWireCells(3, 1, 3, 4, cells);                         // vertical
    CHECK(cells.size() == 4 && cells[0].x == 3 && cells[3].z == 4 && cells[2].x == 3);
    GetPowerWireCells(1, 7, 5, 7, cells);                         // horizontal
    CHECK(cells.size() == 5 && cells[4].x == 5 && cells[4].z == 7 && cells[1].z == 7);
    GetPowerWireCells(2, 2, 2, 2, cells);
    CHECK(cells.size() == 1 && cells[0].x == 2 && cells[0].z == 2);
    GetPowerWireCells(10, 10, 7, 7, cells);                       // diagonal, down-left
    CHECK(cells.size() == 4 && cells[3].x == 7 && cells[3].z == 7 && cells[1].x == 9 && cells[1].z == 9);
    GetPowerWireCells(2.5f, 2.5f, 2, 2, cells);                   // fractional start
    CHECK(cells.size() == 1 && cells[0].x == 2 && cells[0].z == 2);
    if (failures) return 1;
    std::printf("sc4_powerlinegeometry: all passed\n");
}
