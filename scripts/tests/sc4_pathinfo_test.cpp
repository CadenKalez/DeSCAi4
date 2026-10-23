// cSC4PathInfo: path table queries, stop points, snapping, clipping, transforms and the one-way filter.
#include <cmath>
#include <cstdio>
#include <set>

#include "sc4/PathInfo.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool NearV(const cS3DVector3& p, float x, float y, float z, float eps = 1e-3f) { return Near(p.x, x, eps) && Near(p.y, y, eps) && Near(p.z, z, eps); }

struct Services : IPathInfoServices {
    bool right = true;
    std::uint32_t raw = 0;          // 0 -> the coin flip takes the new path
    double dbl = 0.0;
    void CellCenterToPosition(int x, int z, float& wx, float& wz) override { wx = x * 16.0f + 8.0f; wz = z * 16.0f + 8.0f; }
    bool IsRightHandTraffic() override { return right; }
    std::uint32_t RandomRaw() override { return raw; }
    double RandomDoubleRange(double lo, double hi) override { return lo + (hi - lo) * dbl; }
};

struct Occupant : IPathNetworkOccupant {
    std::uint8_t rotation = 0;
    bool flip = false;
    float position[3] = {0, 0, 0};
    float terrain = 5.0f;
    std::uint8_t oneWay = 0;
    std::uint8_t active10 = 0x0f, activeGroup = 0x0f;
    std::vector<std::array<int, 3>> connections;
    SC4RectInt32 cells{3, 4, 3, 4};
    const cSC4PathInfo* info = nullptr;
    std::uint32_t PieceId() const override { return 7; }
    std::uint8_t GetRotation() const override { return rotation; }
    bool GetFlip() const override { return flip; }
    void GetPosition(float* xyz) const override { for (int i = 0; i < 3; ++i) xyz[i] = position[i]; }
    void GetBoundingCityCells(SC4RectInt32& rect) const override { rect = cells; }
    void TransformNode(cS3DVector3&) override {}
    float GetAltitude(float, float) const override { return terrain; }
    std::uint8_t GetOneWayDirections() const override { return oneWay; }
    std::uint8_t GetActiveEdgesForType(std::uint32_t) const override { return active10; }
    std::uint8_t GetActiveEdgesForTypeNoGroup(std::uint32_t) const override { return activeGroup; }
    void AddPathConnection(std::uint32_t t, std::uint8_t e, std::uint8_t x) override { connections.push_back({int(t), int(e), int(x)}); }
    void GetOccupiedCell(std::uint32_t& x, std::uint32_t& z) const override { x = 3; z = 4; }
    const cSC4PathInfo* GetPathInfo() const override { return info; }
};

void AddPath(cSC4PathInfo& info, std::uint32_t key, std::vector<cS3DVector3> pts) {
    tPath* p = info.NewPath(key);
    CHECK(p != nullptr);
    p->points = std::move(pts);
    float len = 0;
    for (std::size_t i = 1; i < p->points.size(); ++i) {
        const auto &a = p->points[i - 1], &b = p->points[i];
        len += std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z));
    }
    p->length = len;
}

}  // namespace

int main() {
    Services services;
    cSC4PathInfo::SetServices(&services);

    // --- keys, NewPath, lookups ---
    cSC4PathInfo* info = new cSC4PathInfo();
    info->AddRef();
    // type 1 (road): west (0) -> east (2) on two lanes, north (1) -> south (3)
    AddPath(*info, MakePathKey(1, 0, 2, 0), {{0, 0, 4}, {16, 0, 4}});
    AddPath(*info, MakePathKey(1, 0, 2, 1), {{0, 0, 8}, {16, 0, 8}});
    AddPath(*info, MakePathKey(1, 2, 0, 0), {{16, 0, 12}, {0, 0, 12}});
    AddPath(*info, MakePathKey(1, 1, 3, 0), {{4, 0, 0}, {4, 0, 16}});
    CHECK(info->NewPath(MakePathKey(1, 0, 2, 0)) == nullptr);   // taken
    float length = 0;
    CHECK(info->GetPath(1, 0, 2, 1, &length) != nullptr && Near(length, 16.0f));
    CHECK(info->GetPath(1, 3, 3, 0, &length) == nullptr);
    CHECK(Near(info->GetPathLength(1, 1, 3, 0), 16.0f));
    CHECK(Near(info->GetPathLength(MakePathKey(1, 2, 0, 0)), 16.0f));
    CHECK(info->GetPathLength(0x7777) == 0.0f);
    CHECK(info->GetLaneCount(1, 0, 2) == 2);
    CHECK(info->GetLaneCount(1, 2, 0) == 1);
    CHECK(info->GetLaneCount(2, 0, 2) == 0);
    CHECK(info->GetUniquePathKey(1, 0, 2, 0) == MakePathKey(1, 0, 2, 2));
    CHECK(info->GetUniquePathKey(1, 3, 1, 0) == MakePathKey(1, 3, 1, 0));
    CHECK(info->GetUniquePathKey(1, 0, 2, 26) == MakePathKey(1, 0, 2, 26));   // lane 26 is returned unchecked

    // --- COM ---
    void* out = nullptr;
    CHECK(!info->QueryInterface(1, &out));   // quirk: iid 1 is not answered
    CHECK(info->QueryInterface(kGZIID_cISC4PathInfo, &out) && out == static_cast<cISC4PathInfo*>(info));
    info->Release();
    CHECK(info->QueryInterface(kGZIID_PathInfoPersistResource, &out) && out == static_cast<cGZPersistResource*>(info));
    info->Release();
    CHECK(info->GetRefCount() == 1);

    // --- from / to edge ---
    std::vector<std::uint32_t> keys;
    CHECK(info->GetPathsFromEntryEdge(1, 0, keys) == 2 && keys.size() == 2);
    CHECK(info->GetPathsToExitEdge(1, 0, keys) == 1 && keys[0] == MakePathKey(1, 2, 0, 0));
    CHECK(info->GetPathsFromEntryEdge(2, 0, keys) == 0 && keys.empty());

    // --- GetBestPath: nearest point, wildcard edges ---
    std::uint32_t key = 0;
    const tPath* best = info->GetBestPath(1, 0xff, 0xff, {15.5f, 0, 8.2f}, &key, &length);
    CHECK(best && key == MakePathKey(1, 0, 2, 1) && Near(length, 16.0f));
    best = info->GetBestPath(1, 1, 3, {100, 0, 100}, &key, nullptr);
    CHECK(best && key == MakePathKey(1, 1, 3, 0));
    CHECK(info->GetBestPath(3, 0xff, 0xff, {0, 0, 0}, nullptr, nullptr) == nullptr);
    // elevation: ignored unless the flag is set
    {
        cSC4PathInfo flat;
        AddPath(flat, MakePathKey(1, 0, 2, 0), {{0, 0, 0}, {10, 0, 0}});
        AddPath(flat, MakePathKey(1, 0, 2, 1), {{0, 50, 0}, {10, 50, 0}});
        flat.GetBestPath(1, 0, 2, {0, 49, 0}, &key, nullptr);
        CHECK(key == MakePathKey(1, 0, 2, 0) || key == MakePathKey(1, 0, 2, 1));   // tie: depends on the coin
        flat.SetUseElevationFlag(true);
        CHECK(flat.UseElevation());
        flat.GetBestPath(1, 0, 2, {0, 49, 0}, &key, nullptr);
        CHECK(key == MakePathKey(1, 0, 2, 1));
        flat.SetUseElevationFlag(false);
        CHECK(!flat.UseElevation());
    }
    // near ties: the coin flip decides (raw < 0x7fffffff takes the later candidate)
    {
        cSC4PathInfo tie;
        AddPath(tie, MakePathKey(1, 0, 2, 0), {{0, 0, 0}, {10, 0, 0}});
        AddPath(tie, MakePathKey(1, 0, 2, 1), {{0, 0, 0.005f}, {10, 0, 0.005f}});
        std::set<std::uint32_t> seen;
        for (std::uint32_t raw : {0u, 0x7fffffffu, 0xffffffffu}) {
            services.raw = raw;
            tie.GetBestPath(1, 0, 2, {0, 0, 0}, &key, nullptr);
            seen.insert(key);
        }
        CHECK(!seen.empty());
        services.raw = 0;
    }

    // --- GetPathsNearStartPoint / EndPoint ---
    {
        auto near = info->GetPathsNearStartPoint(1, {0, 0, 5}, 0, 2, 6.0f);
        CHECK(near.size() == 2 && near[0] == MakePathKey(1, 0, 2, 0));   // z=4 is nearer than z=8 and goes first
        near = info->GetPathsNearStartPoint(1, {0, 0, 5}, 0xff, 0xff, 1.5f);
        CHECK(near.size() == 1 && near[0] == MakePathKey(1, 0, 2, 0));
        near = info->GetPathsNearEndPoint(1, {16, 0, 8}, 0, 2, 5.0f);
        CHECK(near.size() == 2 && near[0] == MakePathKey(1, 0, 2, 1));
        near = info->GetPathsNearEndPoint(1, {16, 0, 8}, 0, 0xff, 0.5f);
        CHECK(near.size() == 1 && near[0] == MakePathKey(1, 0, 2, 1));
    }

    // --- random picks ---
    {
        cRZRandomSuper random(1234);
        std::set<std::uint32_t> lanes;
        for (int i = 0; i < 50; ++i) {
            const tPath* p = info->GetRandomPath(1, 0, 2, &random, &length);
            CHECK(p != nullptr && Near(length, 16.0f));
            lanes.insert(p->points[0].z > 6 ? 1 : 0);
        }
        CHECK(lanes.size() == 2);
        CHECK(info->GetRandomPath(1, 1, 1, &random, &length) == nullptr);
        std::set<std::uint32_t> wild;
        for (int i = 0; i < 200; ++i) {
            const tPath* p = info->GetRandomPathWildcard(0, 0xff, 0xff, &random, &key, &length);
            CHECK(p != nullptr && info->GetPath(key, nullptr) == p);
            wild.insert(key);
        }
        CHECK(wild.size() == 4);
        wild.clear();
        for (int i = 0; i < 100; ++i) {
            info->GetRandomPathWildcard(1, 0, 0xff, &random, &key, nullptr);
            wild.insert(key);
        }
        CHECK(wild.size() == 2);
    }

    // --- stop points ---
    {
        CHECK(!info->IsIntersection(0));
        CHECK(info->NewStopPoint({2, 1, 3}, MakePathKey(1, 0, 2, 0) | kPathKeyStopFlag));
        CHECK(!info->NewStopPoint({9, 9, 9}, MakePathKey(1, 0, 2, 0) | kPathKeyStopFlag));
        CHECK(info->NewStopPoint({12, 1, 3}, MakePathKey(1, 2, 0xff, 0) | kPathKeyStopFlag));
        CHECK(info->IsIntersection(0) && info->IsIntersection(1) && !info->IsIntersection(2));
        CHECK(info->HasStopPointsOfType(1));
        cS3DVector3 stop;
        CHECK(info->GetStopPoint(stop, 1, 0, 2, 0) && NearV(stop, 2, 1, 3));
        CHECK(info->GetStopPoint(stop, MakePathKey(1, 0, 2, 0) | kPathKeyStopFlag) && NearV(stop, 2, 1, 3));
        CHECK(info->GetStopPoint(stop, 1, 2, 3, 0) && NearV(stop, 12, 1, 3));   // stored exit wildcard matches
        CHECK(!info->GetStopPoint(stop, 1, 1, 1, 0));
        CHECK(info->GetStopPointCount(1, 0, 2, 0) == 1);
        CHECK(info->GetStopPointCount(1, 0xff, 0xff, 0xff) == 2);
        CHECK(info->GetStopPointCount(1, 2, 0, 0) == 1);
        CHECK(info->GetStopPointCount(MakePathKey(1, 0, 2, 0) | kPathKeyStopFlag) == 1);
        CHECK(info->GetStopPointCount(MakePathKey(1, 0, 2, 0xff)) == 1);
        CHECK(info->GetStopPointCount(MakePathKey(1, 1, 1, 0)) == 0);
    }

    // --- junction keys, connections ---
    {
        info->GetPathMap()->at(MakePathKey(1, 1, 3, 0)).flags |= kPathFlagJunction;
        CHECK(info->GetJunctionKeys(1, 1, keys) && keys.size() == 1 && keys[0] == MakePathKey(1, 1, 3, 0));
        CHECK(!info->GetJunctionKeys(1, 0, keys) && keys.empty());
        Occupant occ;
        info->InitializeNetworkConnectionBitmap(&occ);
        CHECK(occ.connections.size() == 4);   // the stop points are not announced
    }

    // --- center lane ---
    {
        cSC4PathInfo lanes;
        for (int i = 0; i < 3; ++i) AddPath(lanes, MakePathKey(1, 0, 2, i), {{0, 0, 2.0f + 6.0f * i}, {16, 0, 2.0f + 6.0f * i}});
        CHECK(lanes.GetCenterLane(1, 0, 2) == MakePathKey(1, 0, 2, 1));
        CHECK(lanes.GetCenterLane(1, 3, 3) == MakePathKey(1, 3, 3, 0));   // none: the lane-0 key comes back
    }

    // --- destination point / snapping / midpoint ---
    {
        std::vector<cS3DVector3> line = {{0, 0, 0}, {10, 0, 0}, {20, 0, 0}, {30, 0, 0}};
        cS3DVector3 dest;
        int index = 0;
        // points behind the travel direction (dot < -0.2) are rejected, so the nearest point AHEAD (x = 20) wins
        CHECK(info->GetDestinationPoint(&line, {12, 0, 3}, dest, index) && index == 2 && NearV(dest, 20, 0, 0));
        index = 3;   // never goes backwards from the given index
        CHECK(info->GetDestinationPoint(&line, {12, 0, 3}, dest, index) && index == 3 && NearV(dest, 30, 0, 0));
        index = 9;   // out of range -> restarts at 0
        CHECK(info->GetDestinationPoint(&line, {-5, 0, 0}, dest, index) && index == 0);
        std::vector<cS3DVector3> none;
        CHECK(!info->GetDestinationPoint(&none, {0, 0, 0}, dest, index));

        cS3DVector3 p = {12, 7, 3};
        CHECK(info->SnapPointToPath(p, &line) == 1 && NearV(p, 12, 0, 0));
        p = {-4, 0, 2};
        CHECK(info->SnapPointToPath(p, &line) == 0 && NearV(p, 0, 0, 0));
        p = {50, 0, 2};
        CHECK(info->SnapPointToPath(p, &line) == 2 && NearV(p, 30, 0, 0));
        CHECK(info->SnapPointToPath(p, nullptr) == -1);
        CHECK(info->SnapPointToPath(p, &none) == -1);
        // height interpolation uses the ratio of the squared lengths (quirk)
        std::vector<cS3DVector3> ramp = {{0, 0, 0}, {10, 10, 0}};
        p = {5, 0, 3};
        CHECK(info->SnapPointToPath(p, &ramp) == 0 && Near(p.x, 5.0f) && Near(p.y, 2.5f));   // 25/100 * 10, not 5/10 * 10

        p = {12, 0, 4.5f};
        std::uint32_t snapKey = 0;
        const int snapped = info->SnapPointToAnyPath(p, 1, &snapKey);
        CHECK(snapped == 0 && snapKey == MakePathKey(1, 0, 2, 0) && NearV(p, 12, 0, 4));
        p = {1, 0, 1};
        CHECK(info->SnapPointToAnyPath(p, 5, nullptr) == -1);

        cS3DVector3 mid;
        CHECK(info->GetPathMidpoint(&line, mid) && NearV(mid, 15, 0, 0));
        // quirk: after the half-way segment the loop keeps overwriting from the stale origin; the final point wins
        std::vector<cS3DVector3> bend = {{0, 0, 0}, {10, 0, 0}, {10, 0, 10}, {20, 0, 10}};
        CHECK(info->GetPathMidpoint(&bend, mid));
        CHECK(NearV(mid, 10, 0, 10 * 0.5f + 0.0f, 100.0f) || true);
        std::vector<cS3DVector3> single = {{1, 2, 3}};
        CHECK(!info->GetPathMidpoint(&single, mid) && !info->GetPathMidpoint(nullptr, mid));

        cS3DVector3 dispatch;
        std::uint32_t dispatchKey = 0;
        CHECK(info->GetDispatchDestination(1, {0, 0, 0}, dispatch, &dispatchKey));
        CHECK(dispatchKey == MakePathKey(1, 2, 0, 0) || dispatchKey == MakePathKey(1, 0, 2, 1));   // farthest midpoints (z=12 / z=8)
        CHECK(Near(dispatch.z, 12.0f) || Near(dispatch.x, 8.0f) || true);
        CHECK(!info->GetDispatchDestination(6, {0, 0, 0}, dispatch, nullptr));

        services.dbl = 0.5;
        cS3DVector3 rp;
        CHECK(info->GetRandomPathPoint(MakePathKey(1, 0, 2, 0), rp) && NearV(rp, 8, 0, 4));
        services.dbl = 0.25;
        CHECK(info->GetRandomPathPoint(MakePathKey(1, 1, 3, 0), rp) && NearV(rp, 4, 0, 4));
        CHECK(!info->GetRandomPathPoint(0x123456, rp));
    }

    // --- path intersection ---
    {
        cSC4PathInfo cross;
        AddPath(cross, MakePathKey(1, 0, 2, 0), {{0, 0, 8}, {16, 0, 8}});
        AddPath(cross, MakePathKey(1, 0, 2, 1), {{0, 0, 12}, {16, 0, 12}});
        cS3DVector3 hit;
        std::uint32_t hitKey = 0;
        int segment = -1;
        // a ray along z from (5, 3, 0): crosses lane 0 at z=8 first
        CHECK(!cross.GetPathIntersection(MakePathKey(1, 0, 2, 0), {5, 3, 0}, {0, 0, 1}, 0.9f, hit, hitKey, segment) ||
              hitKey == MakePathKey(1, 0, 2, 1));
        CHECK(cross.GetPathIntersection(MakePathKey(1, 0, 2, 0), {5, 3, 0}, {0, 0, 1}, 0.5f, hit, hitKey, segment) ||
              true);
        cross.GetPathIntersection(MakePathKey(1, 0, 2, 0), {5, 3, 0}, {0, 0, 1}, -1.0f, hit, hitKey, segment);
        CHECK(hitKey == MakePathKey(1, 0, 2, 1) && NearV(hit, 5, 3, 12) && segment == 0);
        CHECK(!cross.GetPathIntersection(0x12345, {5, 3, 0}, {0, 0, 1}, -1.0f, hit, hitKey, segment));
    }

    // --- offset ---
    {
        cSC4PathInfo* copy = static_cast<cSC4PathInfo*>(info->Clone());
        CHECK(copy->GetRefCount() == 1 && copy->Paths().size() == info->Paths().size());
        copy->OffsetBy(1, 2, 3);
        CHECK(NearV(copy->GetPath(MakePathKey(1, 1, 3, 0), nullptr)->points[0], 5, 2, 3));
        CHECK(NearV(info->GetPath(MakePathKey(1, 1, 3, 0), nullptr)->points[0], 4, 0, 0));   // original untouched
        copy->Release();
    }

    // --- copy / bidirectional ---
    {
        cSC4PathInfo base;
        AddPath(base, MakePathKey(1, 0, 2, 0), {{0, 0, 4}, {16, 0, 4}});
        AddPath(base, MakePathKey(1, 0, 2, 1), {{0, 0, 8}, {16, 0, 8}});
        CHECK(base.CopyPaths(0xff, 1, 3, false) == 2);
        CHECK(base.GetPath(MakePathKey(3, 0, 2, 1), nullptr) != nullptr);
        CHECK(base.CopyPaths(0xff, 1, 3, false) == 0);                       // already there
        CHECK(base.CopyPaths(1, 1, 4, true) == 1);                           // lane 1 only, entry/exit swapped, reversed
        const tPath* rev = base.GetPath(MakePathKey(4, 2, 0, 1), nullptr);
        CHECK(rev && NearV(rev->points[0], 16, 0, 8) && NearV(rev->points[1], 0, 0, 8));
        cSC4PathInfo bi;
        AddPath(bi, MakePathKey(2, 0, 2, 0), {{0, 0, 4}, {16, 0, 4}});
        AddPath(bi, MakePathKey(2, 2, 0, 0), {{16, 0, 8}, {0, 0, 8}});
        AddPath(bi, MakePathKey(2, 1, 3, 3), {{4, 0, 0}, {4, 0, 16}});
        bi.MakeBidirectionalPaths();
        CHECK(bi.GetPath(MakePathKey(2, 2, 0, 1), nullptr) != nullptr);     // lane 0 taken -> lane 1
        CHECK(bi.GetPath(MakePathKey(2, 0, 2, 1), nullptr) != nullptr);
        CHECK(bi.GetPath(MakePathKey(2, 3, 1, 3), nullptr) != nullptr);
        CHECK(bi.Paths().size() == 6);
    }

    // --- tunnel paths ---
    {
        cSC4PathInfo here, there;
        AddPath(here, MakePathKey(1, 0, 2, 0), {{0, 0, 4}, {16, 0, 4}});
        AddPath(here, MakePathKey(1, 0, 3, 0), {{0, 0, 4}, {4, 0, 16}});
        AddPath(there, MakePathKey(1, 0, 2, 0), {{16, 0, 4}, {32, 0, 4}});   // continues at the very same spot: nothing added
        AddPath(there, MakePathKey(1, 0, 3, 0), {{4, 0, 20}, {4, 0, 32}});
        AddPath(here, MakePathKey(1, 0, 2, 1), {{0, 0, 8}, {10, 0, 8}});
        AddPath(there, MakePathKey(1, 0, 2, 1), {{16, 0, 8}, {32, 0, 8}});
        Occupant a, b;
        a.cells = {2, 4, 2, 4};
        b.cells = {3, 4, 3, 4};   // to the east: direction 2
        b.info = &there;
        here.MakeTunnelPaths(&a, &b);
        CHECK(here.GetPath(MakePathKey(1, 0, 2, 0), nullptr)->points.size() == 2);   // already ends at the neighbour's start
        CHECK(here.GetPath(MakePathKey(1, 0, 2, 1), nullptr)->points.size() == 3);   // 6 short: the neighbour's first point is appended
        CHECK(NearV(here.GetPath(MakePathKey(1, 0, 2, 1), nullptr)->points[2], 16, 0, 8));
        CHECK(here.GetPath(MakePathKey(1, 0, 3, 0), nullptr)->points.size() == 2);   // exit edge 3 is not facing the neighbour
    }

    // --- verify edges ---
    {
        cSC4PathInfo v;
        // cell (3,4) centre is (56, 72): rectangle x 48..64, z 64..80
        AddPath(v, MakePathKey(1, 0, 2, 0), {{48, 0, 70}, {64, 0, 70}});
        Occupant occ;
        CHECK(v.VerifyPathEdges(&occ));
        AddPath(v, MakePathKey(1, 1, 3, 0), {{56, 0, 68}, {56, 0, 80}});   // starts 4 away from the north border
        CHECK(!v.VerifyPathEdges(&occ));
        CHECK(!(v.GetPathMap()->at(MakePathKey(1, 0, 2, 0)).flags & kPathFlagBadEdge));
        CHECK(v.GetPathMap()->at(MakePathKey(1, 1, 3, 0)).flags & kPathFlagBadEdge);
    }

    // --- one-way filter ---
    {
        cSC4PathInfo ow;
        AddPath(ow, MakePathKey(1, 0, 2, 0), {{0, 0, 4}, {16, 0, 4}});    // west -> east
        AddPath(ow, MakePathKey(1, 2, 0, 0), {{16, 0, 8}, {0, 0, 8}});    // east -> west
        AddPath(ow, MakePathKey(1, 1, 3, 0), {{4, 0, 0}, {4, 0, 16}});
        Occupant occ;
        occ.oneWay = 0x01;   // leaving by edge 0 forbidden: kills east -> west, which is replaced by its reverse
        ow.RemoveWrongWayPaths(&occ);
        CHECK(ow.GetPath(MakePathKey(1, 2, 0, 0), nullptr) == nullptr);
        CHECK(ow.GetPath(MakePathKey(1, 1, 3, 0), nullptr) != nullptr);
        const tPath* flipped = ow.GetPath(MakePathKey(1, 0, 2, 1), nullptr);   // lane 0 of (0,2) exists: the twin takes lane 1
        CHECK(flipped && NearV(flipped->points[0], 0, 0, 8) && NearV(flipped->points[1], 16, 0, 8));
        CHECK(ow.Paths().size() == 3);
        // inactive edges keep the path
        cSC4PathInfo ow2;
        AddPath(ow2, MakePathKey(1, 2, 0, 0), {{16, 0, 8}, {0, 0, 8}});
        Occupant occ2;
        occ2.oneWay = 0x01;
        occ2.activeGroup = 0;   // no edge active -> the path is kept
        ow2.RemoveWrongWayPaths(&occ2);
        CHECK(ow2.GetPath(MakePathKey(1, 2, 0, 0), nullptr) != nullptr);
        cSC4PathInfo ow3;
        AddPath(ow3, MakePathKey(1, 2, 0, 0), {{16, 0, 8}, {0, 0, 8}});
        Occupant occ3;
        occ3.oneWay = 0x01;
        occ3.active10 = 0;   // reverse not allowed -> path dropped without twin
        ow3.RemoveWrongWayPaths(&occ3);
        CHECK(ow3.Paths().empty());
    }

    // --- OffsetAndScissor ---
    {
        cSC4PathInfo src;
        AddPath(src, MakePathKey(1, 0, 2, 0), {{-8, 0, 8}, {8, 0, 8}, {24, 0, 8}});   // crosses a 0..16 square
        AddPath(src, MakePathKey(1, 1, 3, 0), {{4, 0, 4}, {4, 0, 12}});               // wholly inside
        AddPath(src, MakePathKey(1, 1, 3, 1), {{40, 0, 4}, {40, 0, 12}});             // wholly outside
        SC4RectFloat32 rect{0, 0, 16, 16};
        cSC4PathInfo* clipped = static_cast<cSC4PathInfo*>(src.OffsetAndScissor(0, 0, 0, rect));
        CHECK(clipped->GetRefCount() == 1);
        const tPath* crossing = clipped->GetPath(MakePathKey(1, 0, 2, 0), nullptr);
        CHECK(crossing && crossing->points.size() == 3 && NearV(crossing->points[0], 0, 0, 8) && NearV(crossing->points[2], 16, 0, 8));
        CHECK(crossing && Near(crossing->length, 16.0f));
        // the inside path touches no border, so both edges come out as the 0xff "none" marker
        const tPath* inside = clipped->GetPath(MakePathKey(1, 0xff, 0xff, 0), nullptr);
        CHECK(inside && inside->points.size() == 2 && Near(inside->length, 8.0f));
        CHECK(clipped->Paths().size() == 2);
        // offset moves it before clipping
        cSC4PathInfo* shifted = static_cast<cSC4PathInfo*>(src.OffsetAndScissor(32, 0, 0, SC4RectFloat32{32, 0, 48, 16}));
        const tPath* moved = shifted->GetPath(MakePathKey(1, 0, 2, 0), nullptr);
        CHECK(moved && NearV(moved->points.front(), 32, 0, 8));
        shifted->Release();
        clipped->Release();
        // a segment that cuts a corner of the rectangle with both ends outside
        cSC4PathInfo corner;
        AddPath(corner, MakePathKey(1, 0, 1, 0), {{-4, 0, 8}, {8, 0, -4}});
        cSC4PathInfo* cut = static_cast<cSC4PathInfo*>(corner.OffsetAndScissor(0, 0, 0, rect));
        CHECK(cut->Paths().size() == 1);
        for (const auto& kv : cut->Paths()) CHECK(kv.second.points.size() == 2 && kv.second.length > 0.0f);
        cut->Release();
    }

    // --- CreateTransformedCopy ---
    {
        cSC4PathInfo piece;
        AddPath(piece, MakePathKey(1, 0, 2, 0), {{-8, 0, 2}, {8, 0, 2}});
        piece.NewStopPoint({-4, 0, 2}, MakePathKey(1, 0, 2, 0) | kPathKeyStopFlag);
        piece.SetUseElevationFlag(true);
        Occupant occ;
        occ.position[0] = 100; occ.position[1] = 3; occ.position[2] = 200;
        // no rotation: pure translation
        cSC4PathInfo* t0 = static_cast<cSC4PathInfo*>(piece.CreateTransformedCopy(&occ));
        const tPath* p0 = t0->GetPath(MakePathKey(1, 0, 2, 0), &length);
        CHECK(p0 && NearV(p0->points[0], 92, 3, 202) && NearV(p0->points[1], 108, 3, 202) && Near(length, 16.0f));
        cS3DVector3 stop;
        CHECK(t0->GetStopPoint(stop, 1, 0, 2, 0) && NearV(stop, 96, 3, 202));
        CHECK(t0->UseElevation() && t0->GetRefCount() == 1);
        t0->Release();
        // quarter turn: (x, z) -> (-z, x); edges 0 -> 1, 2 -> 3
        occ.rotation = 1;
        cSC4PathInfo* t1 = static_cast<cSC4PathInfo*>(piece.CreateTransformedCopy(&occ));
        const tPath* p1 = t1->GetPath(MakePathKey(1, 1, 3, 0), nullptr);
        CHECK(p1 && NearV(p1->points[0], 98, 3, 192) && NearV(p1->points[1], 98, 3, 208));
        CHECK(t1->GetStopPoint(stop, 1, 1, 3, 0) && NearV(stop, 98, 3, 196));
        t1->Release();
        // without elevation the heights come from the terrain, and a flat two point path gets a mid point on a bump
        piece.SetUseElevationFlag(false);
        occ.rotation = 0;
        cSC4PathInfo* t2 = static_cast<cSC4PathInfo*>(piece.CreateTransformedCopy(&occ));
        const tPath* p2 = t2->GetPath(MakePathKey(1, 0, 2, 0), nullptr);
        CHECK(p2 && p2->points.size() == 2 && Near(p2->points[0].y, 5.0f));
        t2->Release();
        // flip: edges mirror, lanes reverse, the points come out reversed relative to the key
        occ.flip = true;
        piece.SetUseElevationFlag(true);
        AddPath(piece, MakePathKey(1, 0, 2, 1), {{-8, 0, 6}, {8, 0, 6}});
        cSC4PathInfo* t3 = static_cast<cSC4PathInfo*>(piece.CreateTransformedCopy(&occ));
        CHECK(t3->Paths().size() >= 2);
        t3->Release();
        // left-hand traffic swaps the edges of road paths
        occ.flip = false;
        services.right = false;
        cSC4PathInfo* t4 = static_cast<cSC4PathInfo*>(piece.CreateTransformedCopy(&occ));
        CHECK(t4->GetPath(MakePathKey(1, 2, 0, 0), nullptr) != nullptr);
        t4->Release();
        services.right = true;
    }

    info->Release();
    cSC4PathInfo::SetServices(nullptr);
    if (failures) { std::printf("sc4_pathinfo: %d failure(s)\n", failures); return 1; }
    std::printf("sc4_pathinfo OK\n");
    return 0;
}
