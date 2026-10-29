// cSC4Vehicle — construction / QueryInterface / the headlight and ghost-model wiring, the headlight timer, the path
// position / previous orientation / path history bookkeeping, the path step, the vehicle geometry helpers
// (LineIntersectsSphere, FindPathIntersection, Check2DIntersection), the player-drive speed and steering translation,
// the pull-over / plummet / collision state entry, the damage thresholds, the persistence round trip and the off-road
// lot crossing. The host seams of Vehicle.h are faked; the traffic network map, the path info and the occupants are the
// recovered classes / traffic-test style fakes.
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <set>
#include <vector>

#include "rz/PortMemoryStreams.h"
#include "sc4/Occupant.h"
#include "sc4/PathInfo.h"
#include "sc4/TrafficNetworkMap.h"
#include "sc4/EmergencyVehicle.h"
#include "sc4/Vehicle.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
#define CHECK_NEAR(a, b) do { if (std::fabs((a) - (b)) > 1e-3f) { \
    std::printf("FAIL line %d: %s == %f, expected %f\n", __LINE__, #a, static_cast<double>(a), static_cast<double>(b)); \
    ++failures; } } while (0)

using namespace sc4;

namespace {

constexpr std::uint32_t kOccupantIid = 0x26bb767e;
constexpr std::uint32_t kNetworkOccupantIid = 0xa821ef94;
constexpr std::uint32_t kSerializableIid = 0xe4fda3d4;
constexpr std::uint32_t kMessageTargetIid = 0x452294aa;

cS3DVector3 Vec(float x, float y, float z) {
    cS3DVector3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

float TestDistance(const cS3DVector3& a, const cS3DVector3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---- cISC4Occupant stand-in -----------------------------------------------------------------------------------------
struct FakeBaseOccupant final : cISC4Occupant {
    SC4CellRect cells{};
    float position[3] = {0, 0, 0};
    std::uint32_t type = kVehicleOccupantType;
    std::uint32_t flags = 4;
    int refs = 1;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 1 || iid == kOccupantIid) { *out = this; ++refs; return true; }
        return false;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool IsInitialized() const override { return true; }
    sc4::cISCPropertyHolder* AsPropertyHolder() override { return nullptr; }
    std::uint32_t GetType() const override { return type; }
    void GetPosition(float* xyz) override { for (int i = 0; i < 3; ++i) xyz[i] = position[i]; }
    void SetPosition(const float*) override {}
    void GetBoundingBox(float*, float*) override {}
    void GetBoundingCityCells(SC4CellRect& rect) override { rect = cells; }
    void SetRemovalFlags(int) override {}
    void UnsetRemovalFlags(int) override {}
    bool CanRemove(int) override { return true; }
    void PostOccupantMessage(std::uint32_t, std::uint32_t) override {}
    std::uint32_t GetHighlight() const override { return 0; }
    void SetHighlight(int, bool) override {}
    void SetVisibility(bool, bool) override {}
    cISC43DPlaceableObject* GetPlaceableObject() const override { return nullptr; }
    cISC43DPlaceableObject* GetOrCreatePlaceableObject() override { return nullptr; }
    void SetPlaceableObject(cISC43DPlaceableObject*) override {}
    bool IsOccupantGroup(std::uint32_t) const override { return false; }
    void AddOccupantGroup(std::uint32_t) override {}
    bool GetOccupantGroups(std::set<std::uint32_t>&) const override { return false; }
    bool GetOccupantManagerBBox(std::uint8_t*) const override { return false; }
    void SetOccupantManagerBBox(const std::uint8_t*) override {}
    bool GetLotTag(std::uint32_t&) const override { return false; }
    void SetLotTag(std::uint32_t) override {}
    void SetFlag(std::uint32_t) override {}
    void SetAllFlags(std::uint32_t) override {}
    void ClearFlag(std::uint32_t) override {}
    bool IsFlagSet(std::uint32_t) override { return false; }
    std::uint32_t GetFlags() override { return flags; }
};

struct FakeOccupant final : ITrafficAutomatonOccupant {
    FakeBaseOccupant base;
    cISC4PathInfo* pathInfo = nullptr;   // borrowed; the test owns the reference
    int refs = 1;
    std::uint32_t networkFlags = 0x10000001u;
    std::uint32_t types = 1u << 2;   // IsOfType(2) = road
    std::uint32_t cellX = 0, cellZ = 0;
    float altitude = 3.0f;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 1 || iid == kNetworkOccupantIid) { *out = this; AddRef(); return true; }
        if (iid == 0x8a4bd52b && (networkFlags & kVehicleNetworkFlagTunnelInterface)) {
            *out = this;
            AddRef();
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool HasNetworkFlag(std::uint32_t f) const override { return (networkFlags & f) == f; }
    bool HasAnyNetworkFlag(std::uint32_t f) const override { return (networkFlags & f) != 0; }
    void SetNetworkFlag(std::uint32_t f) override { networkFlags |= f; }
    void ClearNetworkFlag(std::uint32_t f) override { networkFlags &= ~f; }
    bool ConnectsToNeighbor(int, std::uint32_t) const override { return true; }
    cISC4Occupant* AsOccupant() override { return &base; }
    ITrafficNetworkMapPathInfo* GetPathInfo() override { return nullptr; }
    void ReleasePathInfo() override {}
    bool PathConnectionExists(std::uint32_t, std::uint8_t, std::uint8_t) const override { return true; }
    bool IsIntersection() const override { return false; }
    float GetAltitude(float, float) const override { return altitude; }
    void GetOccupiedCell(std::uint32_t& x, std::uint32_t& z) const override { x = cellX; z = cellZ; }
    bool IsOfType(std::uint32_t type) const override { return type < 32 && ((types >> type) & 1u) != 0; }
    bool IsUsable() const override { return true; }
    cISC4PathInfo* GetPathInfoObject() override { return pathInfo; }
};

// ---- model / effect stand-ins ---------------------------------------------------------------------------------------
struct FakeModel final : IAutomatonModel {
    int refs = 1;
    int opacityCalls = 0;
    float lastOpacity = 0.0f;
    bool transparent = false;
    bool QI(std::uint32_t, void** out) { *out = this; ++refs; return true; }
    void AddRef() override { ++refs; }
    void Release() override { --refs; }
    void GetBounds(float* minXyz, float* maxXyz) override {
        minXyz[0] = -2.0f; minXyz[1] = 0.0f; minXyz[2] = -1.0f;
        maxXyz[0] = 2.0f; maxXyz[1] = 1.5f; maxXyz[2] = 1.0f;
    }
    void GetTransform(cS3DTransform& out) override { out = cS3DTransform{}; }
    void SetTransform(const cS3DTransform&) override {}
    void SetOpacity(float opacity, bool translucent) override {
        ++opacityCalls;
        lastOpacity = opacity;
        transparent = translucent;
    }
    void UpdateAnimation(float) override {}
    int GetFrame() const override { return 0; }
    int GetFrameCount() const override { return 1; }
    void SetZoomAndRotation(int, int) override {}
    void SetPlayMode(s3d::PlayMode) override {}
};

struct FakeEffect final : IVehicleEffect {
    int refs = 1;
    int transforms = 0;
    int flags = 0;
    int detaches = 0;
    float matrix[16] = {0};
    bool QueryInterface(std::uint32_t, void** out) override { *out = this; ++refs; return true; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    void SetTransform(const float* value) override {
        ++transforms;
        std::memcpy(matrix, value, sizeof(matrix));
    }
    void SetFlag(bool) override { ++flags; }
    void Detach(bool) override { ++detaches; }
};

struct FakeUnknown final : cIGZUnknown {
    int refs = 1;
    bool QueryInterface(std::uint32_t, void** out) override { *out = this; ++refs; return true; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
};

// ---- IAutomatonHost -------------------------------------------------------------------------------------------------
struct FakeAutomatonHost final : IAutomatonHost {
    float simSpeedModifier = 1.0f;
    float maxSpeedUnits = 3600.0f;
    float terrainHeight = 0.0f;
    int modelsCreated = 0;
    int createdRefs = 0;
    IAutomatonModel* CreateModel(const cGZPersistResourceKey&) override {
        ++modelsCreated;
        auto* model = new FakeModel();
        createdRefs = model->refs;
        return model;
    }
    void DestroyModel(IAutomatonModel* model) override { if (model) model->Release(); }
    IAutomatonEffects* CreateEffects() override { return nullptr; }
    void DestroyEffects(IAutomatonEffects*) override {}
    bool AddModelToRenderer(IAutomatonModel*) override { return true; }
    bool RemoveModelFromRenderer(IAutomatonModel*) override { return true; }
    void FadeModelIn(IAutomatonModel*) override {}
    void FadeModelOut(IAutomatonModel*, int) override {}
    bool InsertOccupant(cISC4Occupant*) override { return true; }
    void RemoveOccupant(cISC4Occupant*) override {}
    bool RefreshOccupant(cISC4Occupant*) override { return true; }
    bool HasCity() override { return true; }
    float CitySizeX() override { return 128.0f; }
    float CitySizeZ() override { return 64.0f; }
    float TerrainHeight(float, float) override { return terrainHeight; }
    bool NetworkAt(int, int) override { return true; }
    bool LotAt(int, int) override { return false; }
    void PlaceOnNetwork(const cS3DVector3&, const cS3DVector3&, std::uint32_t) override {}
    cSC4AutomataController* FindAttraction(cISC4Occupant*, cS3DVector3*, float*, float*) override { return nullptr; }
    IAutomataGroupTemplate* FindAutomataGroup(std::uint32_t) override { return nullptr; }
    bool AnimationUpdateAllowed() override { return true; }
    float SimSpeedModifier() override { return simSpeedModifier; }
    float MaxSpeedUnits() override { return maxSpeedUnits; }
    float FadeDurationSeconds() override { return 0.2f; }
    void PostMessage(cIGZMessage2*) override {}
    std::uint32_t GenerateSoundInstanceId() override { return 1; }
    void StopSoundLoop(std::uint32_t) override {}
    void PostAudioEvent(std::uint32_t) override {}
    std::uint32_t RandomBelow(std::uint32_t range) override { return range ? range - 1 : 0; }
    std::int32_t RandomInt(std::int32_t lo, std::int32_t) override { return lo; }
    double RandomDouble(double lo, double) override { return lo; }
    double RandomUnit() override { return 0.5; }
};

// ---- traffic automaton host -----------------------------------------------------------------------------------------
struct FakeMapHost final : ITrafficNetworkMapHost {
    int CityCellsX() override { return 8; }
    int CityCellsZ() override { return 4; }
    void PositionToCell(float x, float z, int& cellX, int& cellZ) override {
        cellX = static_cast<int>(std::floor(x / 16.0f));
        cellZ = static_cast<int>(std::floor(z / 16.0f));
    }
    bool SimulationActive() override { return true; }
    ITrafficNetworkMapLot* LotAtCell(int, int) override { return nullptr; }
    ITrafficNetworkMapLot* LotOf(cISC4Occupant*) override { return nullptr; }
    std::vector<cISC4Occupant*> NetworkOccupantsInBox(const float*, const float*) override { return {}; }
    void ReleaseOccupants(const std::vector<cISC4Occupant*>&) override {}
    std::vector<ITrafficNetworkMapProp*> PropsAtCell(int, int) override { return {}; }
    bool HasTerrain() override { return true; }
    float TerrainHeightAt(float, float) override { return 0.0f; }
    ITrafficNetworkMapBuilding* LotBuilding(ITrafficNetworkMapLot*) override { return nullptr; }
    int ActiveEdgesForBuilding(std::uint32_t, std::uint8_t) override { return 0; }
    void GetViewPosition(float& x, float& y, float& z) override { x = y = z = 0.0f; }
    void GetViewTarget(float& x, float& y, float& z) override { x = y = z = 0.0f; }
    void SubscribeMessage(std::uint32_t) override {}
    void UnsubscribeMessage(std::uint32_t) override {}
    void MakeTunnelPaths(ITrafficNetworkMapPathInfo*, ITrafficNetworkMapOccupant*, ITrafficNetworkMapOccupant*) override {}
};

struct FakeManager final : ITrafficAutomataManager {
    bool IsTrafficAutomaton(cSC4Automaton*) const override { return true; }
    void SetCurrentLayer(std::uint32_t) override {}
    void ComputeEdgeWeights(ITrafficAutomatonOccupant*, TrafficTileInfo*, std::uint32_t, float* weights) override {
        for (int i = 0; i < 16; ++i) weights[i] = 1.0f;
    }
    std::uint32_t GetTravelType(cSC4Automaton*) override { return 1; }
    double GetLayerDensity(std::uint32_t) override { return 1.0; }
    cSC4TrafficEffectManager* EffectManager() override { return nullptr; }
};

struct FakeTrafficHost final : ITrafficAutomatonHost {
    cSC4TrafficNetworkMap* map = nullptr;
    FakeManager* manager = nullptr;
    TrafficAutomatonTuning tuning;
    int audioStarts = 0;
    int audioStops = 0;
    std::uint32_t lastEvent = 0;
    cSC4TrafficNetworkMap* TrafficNetworkMap() override { return map; }
    float CitySizeX() override { return 128.0f; }
    float CitySizeZ() override { return 64.0f; }
    void PositionToCell(float x, float z, int& cellX, int& cellZ) override {
        cellX = static_cast<int>(std::floor(x / 16.0f));
        cellZ = static_cast<int>(std::floor(z / 16.0f));
    }
    int CityCellsX() override { return 8; }
    int CityCellsZ() override { return 4; }
    ITrafficAutomataManager* TrafficAutomataManager() override { return manager; }
    cISC4PathFinder* PathFinder() override { return nullptr; }
    void NeighborConnectionCells(std::vector<std::int32_t>&) override {}
    void StartAudioEvent(std::uint32_t event) override { ++audioStarts; lastEvent = event; }
    void StopAudioEvent(std::uint32_t event) override { ++audioStops; lastEvent = event; }
    ITrafficParticleSystem* CreateParticleSystem() override { return nullptr; }
    ITrafficAutomatonOccupant* ReadNetworkOccupant(cIGZIStream&) override { return nullptr; }
    const TrafficAutomatonTuning* Tuning() override { return &tuning; }
};

// ---- the vehicle host -----------------------------------------------------------------------------------------------
struct FakeVehicleHost final : IVehicleHost {
    VehicleTuning tuning;
    bool defaultCollide = true;
    bool defaultHeadlights = true;
    bool lightsOn = false;
    bool insideCity = true;
    float terrain = 0.0f;
    float surface = 0.0f;
    bool hasGroup = true;
    bool inOccupantGroup = false;
    bool hasLot = false;
    std::int32_t lotRect[4] = {0, 0, 0, 0};
    std::vector<ITrafficAutomatonOccupant*> cellOccupants;
    std::vector<cISC4Occupant*> boxOccupants;
    std::function<bool(int, int)> canDrive = [](int, int) { return true; };
    tAutomataPlayerDriveParams params;
    int effectCreations = 0;
    int accidents = 0;
    int collisionMessages = 0;
    int leaderCalls = 0;
    int density = 0;
    float disasterDamage = 0.0f;
    std::vector<FakeEffect*> effects;
    std::vector<FakeUnknown*> madeAccidents;
    // Tuning / manager
    const VehicleTuning* Tuning() override { return &tuning; }
    bool DefaultCollide() override { return defaultCollide; }
    bool DefaultHeadlights() override { return defaultHeadlights; }
    bool SceneLightsOn() override { return lightsOn; }
    void ManagerRecalculateLeaders(cSC4Automaton*) override { ++leaderCalls; }
    bool IsOccupantGroup(cISC4Occupant*, std::uint32_t group) override {
        return inOccupantGroup && group == kVehicleGroupPlayerDrive;
    }
    bool FindPlayerDriveParams(cSC4Automaton*, const tAutomataPlayerDriveParams** out) override {
        *out = hasGroup ? &params : nullptr;
        return true;
    }
    int CellAutomatonCount(int, int, int) override { return density; }
    bool IsInsideCity(float, float) override { return insideCity; }
    float SurfaceHeight(float, float) override { return surface; }
    void OccupantsInCell(int, int, std::vector<ITrafficAutomatonOccupant*>& out) override { out = cellOccupants; }
    bool CanDriveOnCell(int cellX, int cellZ, float, float) override { return canDrive(cellX, cellZ); }
    void CellToWorld(int cellX, int cellZ, float& x, float& z) override {
        x = static_cast<float>(cellX) * 16.0f;
        z = static_cast<float>(cellZ) * 16.0f;
    }
    bool LotBoundingRectAtCell(int, int, std::int32_t rect[4]) override {
        if (!hasLot) return false;
        for (int i = 0; i < 4; ++i) rect[i] = lotRect[i];
        return true;
    }
    IVehicleEffect* CreateNamedEffect(const char*) override {
        ++effectCreations;
        auto* effect = new FakeEffect();
        effects.push_back(effect);
        return effect;
    }
    cIGZUnknown* CreateAccident(cISC4Occupant*, std::int32_t, std::uint32_t) override {
        ++accidents;
        auto* unknown = new FakeUnknown();
        madeAccidents.push_back(unknown);
        return unknown;
    }
    void OccupantsInBox(const float*, const float*, std::uint32_t, int, std::vector<cISC4Occupant*>& out) override {
        out = boxOccupants;
    }
    float PlayerDriveDisasterDamage(float, float) override { return disasterDamage; }
    void PostCollisionMessage() override { ++collisionMessages; }
    cSC4Automaton* CollidingAutomaton(cSC4Automaton*, float) override { return nullptr; }
    void ResetEffects() {
        for (FakeEffect* effect : effects) delete effect;
        effects.clear();
        for (FakeUnknown* unknown : madeAccidents) delete unknown;
        madeAccidents.clear();
    }
};

// ---- the world ------------------------------------------------------------------------------------------------------
struct World {
    FakeMapHost mapHost;
    cSC4TrafficNetworkMap* map = nullptr;
    FakeManager manager;
    FakeTrafficHost host;
    FakeAutomatonHost automatonHost;
    FakeVehicleHost vehicleHost;
    World() {
        map = new cSC4TrafficNetworkMap(&mapHost);
        map->Init();
        host.map = map;
        host.manager = &manager;
        SetVehicleHost(&vehicleHost);
    }
    ~World() {
        SetVehicleHost(nullptr);
        vehicleHost.ResetEffects();
        delete map;
    }
};

cSC4PathInfo* MakePathInfo() {
    auto* info = new cSC4PathInfo();
    info->AddRef();   // the test's own reference
    return info;
}

std::uint32_t AddPath(cSC4PathInfo* info, std::uint32_t type, std::uint8_t entry, std::uint8_t exit,
                      const std::vector<cS3DVector3>& points) {
    const std::uint32_t key = MakePathKey(type, entry, exit, 0);
    tPath* path = info->NewPath(key);
    if (!path) return key;
    path->points = points;
    path->length = 0.0f;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const float dx = points[i].x - points[i - 1].x;
        const float dy = points[i].y - points[i - 1].y;
        const float dz = points[i].z - points[i - 1].z;
        path->length += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    return key;
}

FakeOccupant* MakeOccupant(cISC4PathInfo* info) {
    auto* occupant = new FakeOccupant();
    occupant->pathInfo = info;
    occupant->AddRef();   // the test's own reference
    return occupant;
}

cSC4Vehicle* MakeVehicle(World& world) {
    auto* vehicle = new cSC4Vehicle(&world.host, &world.automatonHost);
    vehicle->AddRef();
    return vehicle;
}

void TestConstructionAndQueries() {
    World world;
    world.vehicleHost.tuning.defaultSpeedMin = 3600;
    world.vehicleHost.tuning.defaultSpeedMax = 3600;
    world.vehicleHost.tuning.pathOffsetMin = 2.0f;
    world.vehicleHost.tuning.pathOffsetMax = 2.0f;
    cSC4Vehicle* vehicle = MakeVehicle(world);
    CHECK(vehicle->GetType() == kVehicleOccupantType);
    CHECK_NEAR(vehicle->GetDefaultSpeed(), 1.0f);   // 3600 units/hour * 1/3600
    CHECK(!vehicle->IsInitialized());
    CHECK(vehicle->AutomataFlags() == 0);
    CHECK_NEAR(vehicle->HeadlightTimer(), 0.0f);
    CHECK_NEAR(vehicle->PathRemaining(), 0.0f);
    CHECK(vehicle->HeadlightModel() == nullptr);
    CHECK(vehicle->MotionBlurModel(0) == nullptr);
    CHECK(vehicle->GetPathHistory().empty());
    CHECK(vehicle->GetGZCLSID() == kVehicleIID);

    void* out = reinterpret_cast<void*>(1);
    CHECK(vehicle->QueryInterface(kVehicleIID, &out) && out == vehicle);
    CHECK(vehicle->QueryInterface(kGZIID_cISC4TrafficAutomaton, &out) && out == vehicle);
    CHECK(vehicle->QueryInterface(1, &out) && out != nullptr);
    CHECK(vehicle->QueryInterface(0xa9b40f05, &out) && out != nullptr);
    CHECK(vehicle->QueryInterface(kOccupantIid, &out) && out != nullptr);
    CHECK(vehicle->QueryInterface(kSerializableIid, &out) && out != nullptr);
    CHECK(vehicle->QueryInterface(kMessageTargetIid, &out) && out != nullptr);
    CHECK(!vehicle->QueryInterface(0x12345678, &out) && out == nullptr);

    vehicle->SetPathPos(Vec(4.0f, 5.0f, 6.0f));
    CHECK_NEAR(vehicle->GetPathPos().x, 4.0f);
    CHECK_NEAR(vehicle->GetPathPos().z, 6.0f);
    vehicle->SetPrevOrient(Vec(0.0f, 0.0f, 1.0f));
    CHECK_NEAR(vehicle->GetPrevOrient().z, 1.0f);
    vehicle->SetHeadlightTimer(2.5f);
    CHECK_NEAR(vehicle->HeadlightTimer(), 2.5f);

    // Init: the collision flag, the random path offset and the headlights (the port has no auxiliary model).
    CHECK(vehicle->Init());
    CHECK(vehicle->IsInitialized());
    CHECK(vehicle->CheckAutomataFlag(kAutomataFlagCollideCandidate));
    CHECK_NEAR(vehicle->GetPathOffset(), 2.0f);
    CHECK(vehicle->Shutdown());
    CHECK(!vehicle->IsInitialized());
    vehicle->Release();
}

void TestGeometry() {
    World world;
    cSC4Vehicle* vehicle = MakeVehicle(world);

    cS3DVector3 first;
    cS3DVector3 second;
    // Segment through the sphere: the larger parameter lands in `first`, the smaller in `second`.
    CHECK(vehicle->LineIntersectsSphere(Vec(0, 0, 0), Vec(10, 0, 0), Vec(5, 0, 0), 1.0f, first, second) == 2);
    CHECK_NEAR(first.x, 6.0f);
    CHECK_NEAR(second.x, 4.0f);
    // A miss.
    CHECK(vehicle->LineIntersectsSphere(Vec(0, 0, 0), Vec(10, 0, 0), Vec(5, 5, 0), 1.0f, first, second) == 0);
    // One intersection: the start point is inside the sphere.
    CHECK(vehicle->LineIntersectsSphere(Vec(5, 0, 0), Vec(10, 0, 0), Vec(5.5f, 0, 0), 1.0f, first, second) == 1);
    CHECK_NEAR(first.x, 6.5f);

    // FindPathIntersection prefers the second point when the centre faces the direction of travel.
    cS3DVector3 hit;
    CHECK(vehicle->FindPathIntersection(Vec(0, 0, 0), Vec(10, 0, 0), Vec(4.5f, 0.5f, 0), 1.0f, hit, 0.1f));
    CHECK_NEAR(hit.x, 3.633975f);   // the point facing the direction of travel (the centre->point dot is >= 0.1)
    // A degenerate segment is refused.
    CHECK(!vehicle->FindPathIntersection(Vec(1, 1, 1), Vec(1, 1, 1), Vec(0, 0, 0), 5.0f, hit, 0.1f));

    // The lot edges: an edge crossing a circle of radius 6 around (5, 5).
    float best = FLT_MAX;
    cS3DVector3 out = Vec(0, 0, 0);
    vehicle->Check2DIntersection(0.0f, 0.0f, 10.0f, 0.0f, Vec(5, 0, 5), 6.0f, out, best);
    CHECK_NEAR(best, 6.0f);                    // every intersection is exactly the radius from the centre
    CHECK_NEAR(std::fabs(out.x - 5.0f), 3.3166f);   // x = 5 +- sqrt(36 - 25)
    CHECK_NEAR(out.z, 0.0f);
    CHECK_NEAR(TestDistance(out, Vec(5, 0, 5)), 6.0f);
    // A parallel edge that misses leaves the answer alone.
    float untouched = FLT_MAX;
    cS3DVector3 untouchedPoint = Vec(0, 0, 0);
    vehicle->Check2DIntersection(0.0f, 100.0f, 10.0f, 100.0f, Vec(5, 0, 5), 6.0f, untouchedPoint, untouched);
    CHECK(untouched == FLT_MAX);

    vehicle->Release();
}

void TestAnimationAndHeadlights() {
    World world;
    cSC4Vehicle* vehicle = MakeVehicle(world);
    vehicle->Init();
    // Motion blur: one ghost model is created from the primary key and the cursor advances.
    vehicle->SetAutomataFlag(kAutomataFlagMotionBlur, true);
    vehicle->UpdateMotionBlur(0.0f);
    CHECK(world.automatonHost.modelsCreated == 1);
    CHECK(vehicle->MotionBlurModel(0) != nullptr);
    // The cursor advanced to slot 3; the next call fills it with a second ghost.
    vehicle->UpdateMotionBlur(0.0f);
    CHECK(world.automatonHost.modelsCreated == 2);
    CHECK(vehicle->MotionBlurModel(1) != nullptr);
    // The headlight timer crossing zero toggles the lights; the ghost update runs at the end of the tick.
    vehicle->SetHeadlightTimer(0.05f);
    world.automatonHost.simSpeedModifier = 1.0f;
    vehicle->DoAnimationTick(0, 100.0f);
    CHECK_NEAR(vehicle->HeadlightTimer(), 0.0f);
    // A negative timer crossing zero asks for the lights off (nothing to release here).
    vehicle->SetHeadlightTimer(-0.05f);
    vehicle->DoAnimationTick(0, 100.0f);
    CHECK_NEAR(vehicle->HeadlightTimer(), 0.0f);
    // ShowMotionBlur(false) clears the ghosts.
    vehicle->ShowMotionBlur(false);
    CHECK(vehicle->MotionBlurModel(0) == nullptr);
    CHECK(!vehicle->CheckAutomataFlag(kAutomataFlagMotionBlur));
    vehicle->ShowMotionBlur(true);
    CHECK(vehicle->CheckAutomataFlag(kAutomataFlagMotionBlur));
    vehicle->ShowMotionBlur(true);
    CHECK(vehicle->CheckAutomataFlag(kAutomataFlagMotionBlur));
    vehicle->Shutdown();
    vehicle->Release();
}

void TestTweensAndSpeed() {
    World world;
    cSC4Vehicle* vehicle = MakeVehicle(world);
    vehicle->Init();
    tAutomataStatePlayerDrive state;
    tAutomataPlayerDriveParams& params = world.vehicleHost.params;
    params.SetF(0x08, 10.0f);    // maximum speed
    params.SetF(0x0c, -5.0f);    // minimum speed (reverse)
    params.SetF(0x18, 1.0f);     // off-network scale
    params.SetF(0x1c, 0.0f);     // reverse drag
    params.SetF(0x48, 10.0f);    // collision speed divisor

    // Forward acceleration is clamped to the maximum.
    state.value3c = 0.0f;
    state.value38 = 1.0f;
    state.onRoad254 = 1;
    CHECK_NEAR(vehicle->PlayerDriveUpdateSpeed(&state, &params, true, false, 100.0f), 10.0f);
    CHECK_NEAR(state.value3c, 10.0f);

    // Reversing from a standstill is clamped to the minimum.
    state.value3c = 0.0f;
    state.value38 = -1.0f;
    CHECK_NEAR(vehicle->PlayerDriveUpdateSpeed(&state, &params, true, false, 100.0f), -5.0f);
    CHECK_NEAR(state.value3c, -5.0f);

    // A negative speed only survives off the network.
    state.value3c = -3.0f;
    state.value38 = 0.0f;
    state.onRoad254 = 0;
    CHECK_NEAR(vehicle->PlayerDriveUpdateSpeed(&state, &params, true, true, 10.0f), 0.0f);
    state.onRoad254 = 1;
    state.value3c = -3.0f;
    CHECK(vehicle->PlayerDriveUpdateSpeed(&state, &params, true, true, 10.0f) < 0.0f);

    // TranslatePlayerDriveCommands: forward command adds the parameter speed at +0x10; the steering clamps at +-pi/2.
    params.SetF(0x10, 50.0f);
    params.SetF(0x14, 10.0f);
    params.SetF(0x20, 0.7f);
    params.SetF(0x24, 0.5f);
    params.SetF(0x28, 1.5f);
    params.SetF(0x2c, 1.0f);
    params.SetF(0x30, 2.0f);
    state.history[0] = 1.0f;
    state.history[1] = -1.0f;
    state.history[2] = -1.0f;
    state.history[3] = -1.0f;
    state.value38 = 0.0f;
    state.steering6c = 0.0f;
    vehicle->SetSpeed(1.0f, 0.0f);   // the eased speed decides between the +0x10 / +0x14 command speeds
    vehicle->TranslatePlayerDriveCommands(&state, 100.0f);
    CHECK_NEAR(state.value38, 5.0f);   // 50 * 0.1
    // Turn right for a long time: the steering stops at +pi/2 (paths not ignored).
    state.history[3] = 1.0f;
    for (int i = 0; i < 100; ++i) vehicle->TranslatePlayerDriveCommands(&state, 100.0f);
    CHECK_NEAR(state.steering6c, -1.5707964f);
    // Turn left: the opposite clamp.
    state.history[3] = -1.0f;
    state.history[2] = 1.0f;
    for (int i = 0; i < 100; ++i) vehicle->TranslatePlayerDriveCommands(&state, 100.0f);
    CHECK_NEAR(state.steering6c, 1.5707964f);
    // No group: the translation answers false.
    world.vehicleHost.hasGroup = false;
    CHECK(!vehicle->TranslatePlayerDriveCommands(&state, 16.0f));
    world.vehicleHost.hasGroup = true;
    vehicle->Shutdown();
    vehicle->Release();
}

void TestStateMachine() {
    World world;
    cSC4Vehicle* vehicle = MakeVehicle(world);
    vehicle->Init();
    // HandleEnterState for the drive state copies the group's parameter word +8 into the group speed.
    world.vehicleHost.params.SetF(0x08, 3.5f);
    CHECK(vehicle->PushState(kTrafficStateDrive, nullptr, 0.0f, true, 0, 0, nullptr));
    tAutomataState* drive = vehicle->GetState();
    CHECK(drive && drive->stateType == kTrafficStateDrive);
    auto* playerDrive = static_cast<tAutomataStatePlayerDrive*>(drive);
    vehicle->HandleEnterState(drive);
    CHECK_NEAR(playerDrive->value40, 0.0f);
    CHECK_NEAR(playerDrive->groupSpeed, 3.5f);
    CHECK(vehicle->FindState(kTrafficStateDrive) == drive);

    // DoStateTick dispatches the plummet state and its timer counts down.
    vehicle->ClearStateStack();
    CHECK(vehicle->PushState(kStatePlummet, nullptr, 0.0f, true, 0, 0, nullptr));
    auto* plummet = static_cast<tAutomataStatePlummet*>(vehicle->GetState());
    plummet->value44 = 100.0f;
    plummet->value40 = 0.0f;
    plummet->value3c = 1.0f;
    CHECK(vehicle->DoStateTick(20.0f));
    CHECK_NEAR(plummet->value44, 80.0f);
    CHECK(plummet->value3c < 0.0f);   // the spin bounced at +0.05

    // The pull-over state: value38 grows by the frame time each tick.
    vehicle->ClearStateStack();
    CHECK(vehicle->PushState(kStatePullOver, nullptr, 0.0f, true, 0, 0, nullptr));
    auto* pullOver = static_cast<tAutomataStatePullOver*>(vehicle->GetState());
    CHECK(vehicle->DoStateTick(16.0f));
    CHECK_NEAR(pullOver->value38, 16.0f);
    CHECK(vehicle->DoStateTick(16.0f));
    CHECK_NEAR(pullOver->value38, 32.0f);
    // SkipTown rolls a cell lifespan in 0..49 and answers nothing.
    vehicle->SkipTown();
    CHECK(vehicle->CheckAutomataFlag(0x2000000));
    CHECK(vehicle->GetCellLifespan() <= 49);
    CHECK(!vehicle->DoBumpedTick(16.0f));
    vehicle->Shutdown();
    vehicle->Release();
}

void TestPaths() {
    World world;
    cSC4Vehicle* vehicle = MakeVehicle(world);
    vehicle->Init();
    cSC4PathInfo* info = MakePathInfo();
    const std::vector<cS3DVector3> points = {Vec(0, 0, 0), Vec(0, 0, 16), Vec(16, 0, 16)};
    const std::uint32_t key = AddPath(info, 1, 0, 2, points);
    FakeOccupant* occupant = MakeOccupant(info);

    vehicle->SetPath(occupant, key, nullptr, false);
    CHECK(vehicle->CurrentPath() == &info->GetPath(key, nullptr)->points);
    CHECK(vehicle->GetPathKey() == key);
    CHECK_NEAR(vehicle->GetPathPos().x, 0.0f);
    CHECK_NEAR(vehicle->GetPathPos().z, 0.0f);
    CHECK(vehicle->GetNetworkOccupant() == occupant);

    // The safe distance measures the path end from the path position (the 0x10 flag gates the path term).
    vehicle->SetAutomataFlag(0x10, true);
    vehicle->SetPathPos(Vec(16, 0, 15.0f));   // the last path point is (16, 0, 16)
    CHECK_NEAR(vehicle->GetSafeDistance(), 1.0f);
    // Far from the end the path term is 16; without the 0x10 flag it stays at the 16 default.
    vehicle->SetPathPos(Vec(0, 0, 0));
    CHECK_NEAR(vehicle->GetSafeDistance(), 16.0f);
    vehicle->SetAutomataFlag(0x10, false);
    vehicle->SetPathPos(Vec(16, 0, 15.0f));
    CHECK_NEAR(vehicle->GetSafeDistance(), 16.0f);

    // SavePathHistory stores the occupant / key pair and trims to `keep`.
    std::deque<tLookaheadNode> history;
    vehicle->SavePathHistory(history, 4);
    CHECK(history.size() == 1);
    CHECK(history.back().occupant.get() == occupant);
    CHECK(history.back().key == key);
    vehicle->SavePathHistory(history, 4);
    CHECK(history.size() == 1);   // the same occupant replaces the last entry
    vehicle->SavePathHistory(history, 1);
    CHECK(history.size() == 1);

    // RecalculatePathOffset uses the tuning range.
    world.vehicleHost.tuning.pathOffsetMin = 1.5f;
    world.vehicleHost.tuning.pathOffsetMax = 1.5f;
    vehicle->RecalculatePathOffset();
    CHECK_NEAR(vehicle->GetPathOffset(), 1.5f);

    // A transition to a tunnel occupant restarts the tunnel audio event (only with the driving state active).
    CHECK(vehicle->PushState(kTrafficStateDrive, nullptr, 0.0f, true, 0, 0, nullptr));
    const int startsBefore = world.host.audioStarts;
    FakeOccupant* tunnel = MakeOccupant(info);
    tunnel->networkFlags |= kVehicleNetworkFlagTunnelInterface;
    vehicle->SetPath(tunnel, key, nullptr, true);
    CHECK(world.host.audioStarts == startsBefore);   // the old occupant is not a tunnel: the vehicle stops the event
    vehicle->SetPath(occupant, key, nullptr, true);
    CHECK(world.host.audioStarts == startsBefore + 1);

    vehicle->ClearStateStack();
    vehicle->SetPath(nullptr, 0, nullptr, false);
    CHECK(vehicle->CurrentPath() == nullptr);
    CHECK(vehicle->GetNetworkOccupant() == nullptr);

    history.clear();   // the saved entries hold references to the occupants
    tunnel->Release();
    occupant->Release();
    delete tunnel;
    delete occupant;
    info->Release();
    vehicle->Shutdown();
    vehicle->Release();
}

void TestStartTripAndPathTick() {
    World world;
    cSC4Vehicle* vehicle = MakeVehicle(world);
    vehicle->Init();
    cSC4PathInfo* info = MakePathInfo();
    const std::vector<cS3DVector3> points = {Vec(0, 0, 0), Vec(0, 0, 16), Vec(16, 0, 16)};
    const std::uint32_t key = AddPath(info, 1, 0, 2, points);
    FakeOccupant* occupant = MakeOccupant(info);
    vehicle->SetRadius(1.0f);
    vehicle->SetAutomataFlag(kAutomataFlagSelfControlling, true);
    vehicle->SetSpeed(1.0f, 0.0f);
    vehicle->SetDefaultSpeed(1.0f);
    CHECK(vehicle->StartTrip(occupant, key, false));
    CHECK(vehicle->CurrentPath() != nullptr);
    // StartTrip with no remaining distance snaps onto the path.
    CHECK(vehicle->PathRemaining() == 0.0f);

    // DoPathTick advances along the path and stores the actual speed.
    const cS3DVector3 before = vehicle->GetPathPos();
    vehicle->SetPathPos(Vec(0, 0, 0));
    vehicle->DoPathTick(100.0f);
    CHECK(vehicle->GetPathPos().z > 0.0f);
    CHECK(vehicle->GetActualSpeed() >= 0.0f);
    (void)before;

    vehicle->Shutdown();   // drops the occupant reference before the fake is deleted
    occupant->Release();
    delete occupant;
    info->Release();
    vehicle->Release();
}

void TestCollisionAndDamage() {
    World world;
    cSC4Vehicle* vehicle = MakeVehicle(world);
    vehicle->Init();
    tAutomataStatePlayerDrive state;
    world.vehicleHost.params.SetF(0x48, 10.0f);
    world.vehicleHost.params.SetF(0x5c, 20.0f);

    // HandleCollision posts the message, turns the heading, marks the automaton removed and pushes the collide state.
    vehicle->SetSpeed(1.0f, 0.0f);
    CHECK(vehicle->HandleCollision(nullptr, Vec(0, 0, 0)));
    CHECK(world.vehicleHost.collisionMessages == 1);
    CHECK(vehicle->CheckAutomataFlag(kAutomataFlagRemoved));
    CHECK(vehicle->GetState() && vehicle->GetState()->stateType == kStateCollide);
    CHECK_NEAR(vehicle->GetState()->timer, 5000.0f);
    // A second collision while in the collide state answers true without pushing another state.
    CHECK(vehicle->HandleCollision(nullptr, Vec(0, 0, 0)));
    CHECK(vehicle->GetState() && vehicle->GetState()->stateType == kStateCollide);

    // ResolveCollision: a hard impact at the damage threshold creates the accident, disables the car and updates the
    // crash effect.
    vehicle->ClearStateStack();
    state.damage70 = 0;
    vehicle->ResolveCollision(Vec(0.0f, 0.0f, -1.0f), 30.0f, &state, &world.vehicleHost.params);
    CHECK(state.damage70 > 0);
    CHECK(world.vehicleHost.accidents == 1);
    CHECK(world.vehicleHost.effectCreations >= 1);
    CHECK(world.vehicleHost.effects.size() == 1);
    CHECK(world.vehicleHost.effects[0]->transforms >= 1);
    CHECK(world.vehicleHost.effects[0]->flags == 1);
    CHECK(vehicle->DriveEffect() == world.vehicleHost.effects[0]);
    CHECK(vehicle->Accident() != nullptr);
    CHECK(vehicle->CheckAutomataFlag(kAutomataFlagDisabled));

    // A light impact only creates the injury effect (half the threshold).
    World world2;
    FakeVehicleHost& host2 = world2.vehicleHost;
    cSC4Vehicle* vehicle2 = MakeVehicle(world2);
    vehicle2->Init();
    host2.params.SetF(0x48, 10.0f);
    host2.params.SetF(0x5c, 20.0f);
    tAutomataStatePlayerDrive state2;
    state2.damage70 = 11;
    vehicle2->ResolveCollision(Vec(0.0f, 0.0f, -0.001f), 1.0f, &state2, &host2.params);
    CHECK(host2.accidents == 0);
    CHECK(host2.effects.size() == 1);   // the injury effect
    vehicle2->Shutdown();
    vehicle2->Release();

    vehicle->Shutdown();
    vehicle->Release();
}

void TestPersistence() {
    World world;
    cSC4Vehicle* vehicle = MakeVehicle(world);
    vehicle->Init();
    const cS3DVector3 startPosition = Vec(1, 2, 3);
    const cS3DVector3 heading = Vec(0, 0, 1);
    vehicle->SetPosition(&startPosition.x);
    vehicle->SetPathPos(Vec(4, 5, 6));
    vehicle->SetPathRemaining(7.5f);
    vehicle->SetOrientation(&heading.x);
    vehicle->SetDefaultSpeed(0.5f);
    PortMemoryOStream out;
    CHECK(vehicle->Write(out));
    PortMemoryIStream in(out.bytes().data(), out.bytes().size());
    auto* copy = new cSC4Vehicle(&world.host, &world.automatonHost);
    copy->AddRef();
    CHECK(copy->Read(in));
    CHECK(copy->Write(out));
    CHECK_NEAR(copy->PathRemaining(), 7.5f);
    CHECK_NEAR(copy->GetPathPos().x, 4.0f);
    CHECK_NEAR(copy->GetPathPos().z, 6.0f);
    // A wrong version is refused.
    const std::uint8_t bad[2] = {9, 0};
    PortMemoryIStream badStream(bad, sizeof(bad));
    CHECK(!copy->Read(badStream));
    copy->Release();
    vehicle->Shutdown();
    vehicle->Release();
}

void TestLotIntersection() {
    World world;
    cSC4Vehicle* vehicle = MakeVehicle(world);
    vehicle->Init();
    // An empty lot crossing from cell 0 to cell 1: only the start cell is drivable.
    world.vehicleHost.canDrive = [](int cellX, int) { return cellX == 0; };
    cS3DVector3 position = Vec(20.0f, 0.0f, 8.0f);
    cS3DVector2 point;
    cS3DVector2 direction;
    CHECK(vehicle->GetPlayerDriveLotIntersection(Vec(8.0f, 0.0f, 8.0f), position, &point, &direction));
    CHECK_NEAR(point.x, 15.9f);
    CHECK_NEAR(point.y, 8.0f);
    CHECK_NEAR(direction.x, -1.0f);
    CHECK_NEAR(direction.y, 0.0f);
    // The same cell answers false.
    cS3DVector3 same = Vec(8.0f, 0.0f, 8.0f);
    CHECK(!vehicle->GetPlayerDriveLotIntersection(Vec(8.0f, 0.0f, 8.0f), same, &point, &direction));

    // The position is clamped to the lot border: from (1,1) to (20,20) over the lot cells 0..1.
    world.vehicleHost.hasLot = true;
    world.vehicleHost.lotRect[0] = 0;
    world.vehicleHost.lotRect[1] = 0;
    world.vehicleHost.lotRect[2] = 1;
    world.vehicleHost.lotRect[3] = 1;
    cS3DVector3 target = Vec(20.0f, 0.0f, 20.0f);
    vehicle->RestrictPlayerDrivePosition(Vec(1.0f, 0.0f, 1.0f), target);
    CHECK(target.z == 0.0f || target.z == 32.0f || target.x == 0.0f || target.x == 32.0f);
    CHECK(target.x >= 0.0f && target.x <= 32.0f && target.z >= 0.0f && target.z <= 32.0f);
    vehicle->Shutdown();
    vehicle->Release();
}

void TestPlayerDriveTickAndOffRoad() {
    World world;
    cSC4Vehicle* vehicle = MakeVehicle(world);
    vehicle->Init();
    cSC4PathInfo* info = MakePathInfo();
    const std::vector<cS3DVector3> points = {Vec(0, 0, 0), Vec(0, 0, 16)};
    AddPath(info, 1, 0, 2, points);
    FakeOccupant* occupant = MakeOccupant(info);
    vehicle->SetRadius(1.0f);
    vehicle->SetSpeed(0.5f, 0.0f);
    vehicle->SetDefaultSpeed(0.5f);
    const cS3DVector3 driveStart = Vec(4, 0, 4);
    vehicle->SetPosition(&driveStart.x);
    vehicle->SetPathPos(driveStart);
    world.vehicleHost.params.SetF(0x08, 10.0f);
    world.vehicleHost.params.SetF(0x0c, -5.0f);
    world.vehicleHost.params.SetF(0x18, 1.0f);
    world.vehicleHost.params.SetF(0x1c, 0.0f);
    world.vehicleHost.params.SetF(0x20, 1.0f);
    world.vehicleHost.params.SetF(0x2c, 1.0f);
    world.vehicleHost.params.SetF(0x30, 2.0f);
    world.vehicleHost.params.SetF(0x34, 0.5f);
    world.vehicleHost.params.SetF(0x38, 0.5f);
    world.vehicleHost.params.SetF(0x48, 10.0f);
    world.vehicleHost.params.SetF(0x5c, 100.0f);
    CHECK(vehicle->PushState(kTrafficStateDrive, nullptr, 0.0f, true, 0, 0, nullptr));
    vehicle->HandleEnterState(vehicle->GetState());
    // No occupant in the cell: the off-road branch runs, keeps the vehicle alive and sets the ignore-paths flag.
    world.vehicleHost.cellOccupants.clear();
    CHECK(vehicle->DoPlayerDriveTick(50.0f));
    CHECK(vehicle->CheckAutomataFlag(kAutomataFlagIgnorePaths));
    CHECK(vehicle->GetState() != nullptr);

    // With a drivable occupant the network branch runs and the callback reports the on-road state.
    FakeOccupant* other = MakeOccupant(info);
    other->networkFlags = kVehicleNetworkFlagDriveable | kVehicleNetworkFlagOnRoad;
    other->cellX = 0;
    other->cellZ = 0;
    world.vehicleHost.cellOccupants.push_back(other);
    vehicle->SetPathPos(Vec(4, 0, 4));
    CHECK(vehicle->DoPlayerDriveTick(33.0f));
    CHECK(vehicle->GetState() != nullptr);

    // The off-road tick moves the state's driving position.
    tAutomataStatePlayerDrive* drive = static_cast<tAutomataStatePlayerDrive*>(vehicle->GetState());
    drive->drivePos54 = Vec(4, 0, 4);
    drive->driveDir60 = Vec(0, 0, 1);
    drive->steering6c = 0.5f;
    drive->value3c = 1.0f;
    drive->value38 = 0.0f;
    CHECK(vehicle->DoPlayerDriveOffRoadTick(0.0f, drive, 2.0f, &world.vehicleHost.params, other));
    CHECK(drive->drivePos54.x != 4.0f || drive->drivePos54.z != 4.0f);

    vehicle->ClearStateStack();
    vehicle->Shutdown();
    other->Release();
    delete other;
    occupant->Release();
    delete occupant;
    info->Release();
    vehicle->Release();
}

}  // namespace

// ---- cSC4EmergencyVehicle (the recovered part) ----------------------------------------------------------------------------
struct FakeEmergencyHost final : IEmergencyVehicleHost {
    int stopped = 0, moved = 0, lightsOn = 0, lightsOff = 0;
    int lastX = 0, lastY = 0, lastZ = 0;
    void SirenStopped(const cSC4EmergencyVehicle&) override { ++stopped; }
    void SirenMoved(const cSC4EmergencyVehicle&, int x, int y, int z) override { ++moved; lastX = x; lastY = y; lastZ = z; }
    void SetEmergencyLights(cSC4EmergencyVehicle&, bool on) override { on ? ++lightsOn : ++lightsOff; }
};

void TestEmergencyVehicle() {
    World world;
    FakeEmergencyHost emergencyHost;
    auto* ev = new cSC4EmergencyVehicle(&world.host, &world.automatonHost, nullptr, &emergencyHost);
    ev->AddRef();
    CHECK(ev->GetCallNumber() == 0 && ev->GetDispatchState() == 0 && ev->RepathCount() == 0);
    ev->SetCallNumber(0x1234);
    ev->SetDispatchDestination(Vec(1, 2, 3));
    ev->SetDispatchStateRaw(3);
    ev->SetRepathCount(2);
    CHECK(ev->GetCallNumber() == 0x1234 && ev->GetDispatchState() == 3 && ev->GetDispatchDestination().z == 3.0f);

    // the stream: version 2, the vehicle record, destination, the (null) dispatch source, repath count, call number, state
    PortMemoryOStream out;
    CHECK(ev->Write(out));
    PortMemoryIStream in(out.bytes().data(), out.bytes().size());
    auto* copy = new cSC4EmergencyVehicle(&world.host, &world.automatonHost, nullptr, &emergencyHost);
    copy->AddRef();
    CHECK(copy->Read(in));
    CHECK(copy->GetCallNumber() == 0x1234 && copy->GetDispatchState() == 3 && copy->RepathCount() == 2 &&
          copy->GetDispatchDestination().x == 1.0f && copy->GetDispatchDestination().y == 2.0f && copy->DispatchSource() == nullptr);
    const std::uint8_t badVersion[2] = {9, 0};
    PortMemoryIStream bad(badVersion, sizeof(badVersion));
    CHECK(!copy->Read(bad));

    // lights key: released toggles the lights (on while there is no light model), pressed goes to the vehicle
    CHECK(ev->HandlePlayerDriveCommand(kEmergencyPlayerDriveLightsCommand, false));
    CHECK(emergencyHost.lightsOn == 1 && emergencyHost.lightsOff == 0);
    ev->ShowEmergencyLights(false);                                  // already off: nothing to do
    CHECK(emergencyHost.lightsOff == 0);

    // collisions: ordinary vehicles use the base rule; emergency mode ignores everything that is not also an emergency car
    cSC4Vehicle* other = MakeVehicle(world);
    CHECK(ev->ShouldCollide(other) == static_cast<const cSC4Automaton*>(ev)->cSC4Automaton::ShouldCollide(other));
    ev->SetAutomataFlag(0x80, true);
    CHECK(!ev->ShouldCollide(nullptr) && !ev->ShouldCollide(ev) && !ev->ShouldCollide(other));
    other->SetAutomataFlag(0x80, true);
    CHECK(ev->ShouldCollide(other));                                   // two emergency cars that are not queueing collide
    other->SetAutomataFlag(0x20, true);
    CHECK(!ev->ShouldCollide(other) || ev->Route().empty());           // a queueing one only when this car has nothing routed
    other->Release();

    // attraction: emergency mode drops the attraction controller instead of updating it
    ev->UpdateAttraction();

    // shutdown silences the siren and releases the light model
    const int stoppedBefore = emergencyHost.stopped;
    ev->Shutdown();
    CHECK(emergencyHost.stopped == stoppedBefore + 1 && ev->LightModel() == nullptr);
    copy->Release();
    ev->Release();
}

int main() {
    TestConstructionAndQueries();
    TestGeometry();
    TestAnimationAndHeadlights();
    TestTweensAndSpeed();
    TestStateMachine();
    TestPaths();
    TestStartTripAndPathTick();
    TestCollisionAndDamage();
    TestPersistence();
    TestLotIntersection();
    TestPlayerDriveTickAndOffRoad();
    TestEmergencyVehicle();
    if (failures == 0) {
        std::printf("sc4_vehicle: ok\n");
        return 0;
    }
    std::printf("sc4_vehicle: %d failure(s)\n", failures);
    return 1;
}
