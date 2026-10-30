// cSC4NetworkWorldCache and cSC4NetworkCellInfo's constructor / RecomputeEdgesFromAllTypes — unit test.
#include "sc4/NetworkWorldCache.h"

#include <cstdio>

namespace {
int g_failures = 0;
#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                             \
        }                                                                             \
    } while (false)
using namespace sc4;
}  // namespace

int main() {
    cSC4NetworkCellInfo fresh;
    CHECK(fresh.solvedCellIndex == -1);
    CHECK(fresh.occupant == nullptr);
    fresh.tileNeighborhoods[0] = 0x11;
    fresh.tileNeighborhoods[12] = 0x100;
    fresh.RecomputeEdgesFromAllTypes();
    CHECK(fresh.compact54 == 0x111);

    cSC4NetworkWorldCache cache;
    CHECK(cache.GetCell(5) == nullptr);
    cSC4NetworkCellInfo* cell = cache.CreateCell(5 | 7 << 16);
    CHECK(cell && cell->solvedCellIndex == -1);
    cell->x = 5;
    cell->z = 7;
    cell->solvedCellIndex = 3;
    CHECK(cache.CreateCell(5 | 7 << 16) == cell);   // the same record
    CHECK(cache.GetCell(5 | 7 << 16) == cell);

    cache.Push();                                    // saved = {(5,7)}
    CHECK(cache.SavedCount() == 1);
    cache.CreateCell(9)->x = 9;
    cache.GetCell(5 | 7 << 16)->solvedCellIndex = 8;
    cache.ResetSolvedCellIDs();
    CHECK(cache.GetCell(9)->solvedCellIndex == -1 && cache.GetCell(5 | 7 << 16)->solvedCellIndex == -1);
    std::vector<cSC4NetworkCellInfo> all;
    cache.GetAllCells(all);
    CHECK(all.size() == 2);

    cache.Pop();                                     // back to the pushed state
    CHECK(cache.CellCount() == 1);
    CHECK(cache.GetCell(9) == nullptr);
    CHECK(cache.GetCell(5 | 7 << 16) && cache.GetCell(5 | 7 << 16)->solvedCellIndex == 3);

    cache.Clear();
    CHECK(cache.CellCount() == 0 && cache.SavedCount() == 0);

    if (g_failures == 0) std::puts("sc4_networkworldcache ok");
    return g_failures == 0 ? 0 : 1;
}
