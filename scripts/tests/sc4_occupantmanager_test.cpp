#include <cstdio>

#include "rz/PortMemoryStreams.h"
#include "sc4/OccupantManager.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {
class Box : public cSC4Occupant {
public:
    Box(float x0, float z0, float x1, float z1, std::uint32_t type) : x0_(x0), z0_(z0), x1_(x1), z1_(z1) { type_ = type; }
    void GetBoundingBox(float* lo, float* hi) override {
        lo[0] = x0_; lo[1] = 0; lo[2] = z0_;
        hi[0] = x1_; hi[1] = 10; hi[2] = z1_;
    }
    void Move(float dx) { x0_ += dx; x1_ += dx; }
    float x0_, z0_, x1_, z1_;
};
bool CountCb(cISC4Occupant*, void* ctx) { ++*static_cast<int*>(ctx); return true; }
}  // namespace

int main() {
    auto* m = new cSC4OccupantManager();
    m->AddRef();
    CHECK(m->SetCitySize(1024, 1024));
    CHECK(m->Init());
    CHECK(!m->SetCitySize(2048, 2048));
    int cx, cz;
    m->PositionToWorldCell(0, 0, cx, cz);
    CHECK(cx == 64 && cz == 64);
    m->PositionToStandardCityCell(33.0f, 16.0f, cx, cz);
    CHECK(cx == 2 && cz == 1);
    m->PositionToStandardCityCell(-1.0f, 15.9f, cx, cz);
    CHECK(cx == -1 && cz == 0);

    auto* a = new Box(10, 10, 14, 14, 7);      // one cell
    auto* b = new Box(10, 10, 140, 20, 8);     // spans several cells
    a->AddRef(); b->AddRef();
    CHECK(m->InsertOccupant(a, 0) && m->InsertOccupant(b, 0));
    CHECK(!m->InsertOccupant(a, 0));
    CHECK(a->IsFlagSet(1) && a->IsInitialized());
    CHECK(!m->IsCellEmpty(64, 64) && m->IsCellEmpty(0, 0));

    float lo[3] = {0, -1e9f, 0}, hi[3] = {1000, 1e9f, 1000};
    OccupantList all;
    CHECK(m->GetOccupantsByBBox(all, lo, hi, std::uint32_t{0}, 100) && all.size() == 2);   // b counted once
    m->ReleaseOccupantList(all);
    OccupantList typed;
    CHECK(m->GetOccupantsByBBox(typed, lo, hi, std::uint32_t{8}, 100) && typed.size() == 1 && typed.front() == b);
    m->ReleaseOccupantList(typed);

    cISC4Occupant* hit = nullptr;
    CHECK(m->GetFirstOccupantByPosition(hit, 12.0f, 12.0f, static_cast<cISC4OccupantFilter*>(nullptr)) && (hit == a || hit == b));
    hit->Release();
    CHECK(!m->GetFirstOccupantByPosition(hit, 500.0f, 500.0f, static_cast<cISC4OccupantFilter*>(nullptr)));

    int n = 0;
    CHECK(m->IterateOccupantsByBBox(&CountCb, &n, lo, hi, static_cast<cISC4OccupantFilter*>(nullptr)) && n == 2);

    int found[2];
    CHECK(m->FindOccupant(b, false, &found[0], &found[1]));

    // move b away from the origin cells
    b->Move(600);
    CHECK(m->MoveOccupant(b, false));
    OccupantList near;
    float lo2[3] = {0, -1e9f, 0}, hi2[3] = {64, 1e9f, 64};
    CHECK(m->GetOccupantsByBBox(near, lo2, hi2, static_cast<cISC4OccupantFilter*>(nullptr), 100) && near.size() == 1 && near.front() == a);
    m->ReleaseOccupantList(near);

    CHECK(m->RemoveOccupant(a, false, 0) && !a->IsFlagSet(1));
    CHECK(m->IsCellEmpty(64, 64));
    CHECK(m->RemoveOccupants(8, true, 0));
    CHECK(!b->IsFlagSet(1) && !b->IsInitialized());
    int cells[16][2], count = 0;
    CHECK(m->GetBoundingCells(100, 100, cells, count) == 8 && count == 8);
    CHECK(m->GetBoundingCells(0, 0, cells, count) == 3);

    a->Release(); b->Release();
    m->Release();
    std::puts(failures ? "sc4_occupantmanager: FAILED" : "sc4_occupantmanager: ok");
    return failures != 0;
}
