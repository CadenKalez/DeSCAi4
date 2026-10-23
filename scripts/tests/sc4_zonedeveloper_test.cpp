// cSC4ZoneDeveloper — unit test. Exercises the recovered data layout / COM plumbing, the boundary
// texture rules, the lot map and its queries, the network and occupant seams, the lot creation and
// deletion paths, the save stream and the message dispatch.
//
// The engine services are fakes behind IZoneDeveloperHost; the lots are real cSC4Lot objects from
// Lot.cpp (with no ILotHost attached, so their terrain/zone hooks stay inert).

#include "sc4/ZoneDeveloper.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gz/StreamOps.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/cSC4SimGrid.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);     \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (false)

using namespace sc4;

#define FAKE_SERVICE_BOILERPLATE                    \
    int refs = 0;                                   \
    void AddRef() override { ++refs; }              \
    void Release() override { if (refs > 0) --refs; } \
    void Init() override { ++inits; }               \
    void Shutdown() override { ++shutdowns; }       \
    int inits = 0;                                  \
    int shutdowns = 0;

//----------------------------------------------------------------------------------------------- services

struct FakeCity : IZoneDeveloperCityApi {
    FAKE_SERVICE_BOILERPLATE
    int cellsX = 16, cellsZ = 16;
    int CellCountX() override { return cellsX; }
    int CellCountZ() override { return cellsZ; }
    void PositionToCell(float x, float z, int& cellX, int& cellZ) override {
        cellX = static_cast<int>(std::floor(x * 0.0625f));
        cellZ = static_cast<int>(std::floor(z * 0.0625f));
    }
    bool LocationIsInBounds(float x, float z) override {
        int cx = 0, cz = 0;
        PositionToCell(x, z, cx, cz);
        return cx >= 0 && cz >= 0 && cx < cellsX && cz < cellsZ;
    }
};

struct FakeTerrain : IZoneDeveloperTerrainApi {
    FAKE_SERVICE_BOILERPLATE
    float baseAltitude = 5.0f;
    std::set<std::pair<int, int>> water;
    std::map<std::pair<int, int>, float> cellAltitudes;
    bool IsCellWater(int x, int z) override { return water.count({x, z}) != 0; }
    float Altitude(float, float) override { return baseAltitude; }
    float CellAltitude(int x, int z) override {
        auto it = cellAltitudes.find({x, z});
        return it != cellAltitudes.end() ? it->second : baseAltitude;
    }
};

struct FakeZoneManager : IZoneDeveloperZoneManagerApi {
    FAKE_SERVICE_BOILERPLATE
    std::map<std::pair<int, int>, int> zones;
    int ZoneTypeAt(int x, int z) override {
        auto it = zones.find({x, z});
        return it != zones.end() ? it->second : 0;
    }
    bool IsZoneTypeValid(int zoneType) override { return zoneType >= 1 && zoneType <= 9; }
};

struct FakeNetwork : IZoneDeveloperNetworkApi {
    FAKE_SERVICE_BOILERPLATE
    std::map<std::pair<int, int>, std::uint32_t> cells;
    std::map<std::pair<std::uint32_t, int>, bool> connections;
    std::uint32_t typeFlags = 0;
    std::uint32_t connectionFlags = 0;
    int clearHighlightCalls = 0;
    bool HasFlag(std::uint32_t flag) override { return (typeFlags & flag) != 0; }
    bool GetCellFlags(int x, int z, std::uint32_t flag, std::uint32_t& out) override {
        auto it = cells.find({x, z});
        if (it == cells.end() || (it->second & flag) == 0) return false;
        out = it->second;
        return true;
    }
    std::uint32_t GetTypeFlags() override { return typeFlags; }
    std::uint32_t GetConnectionFlags() override { return connectionFlags; }
    bool IsConnected(std::uint32_t direction, int networkType) override {
        auto it = connections.find({direction, networkType});
        return it != connections.end() && it->second;
    }
    void ClearHighlight() override { ++clearHighlightCalls; }
};

struct FakeTerrainView : IZoneDeveloperTerrainViewApi {
    FAKE_SERVICE_BOILERPLATE
    int clearHighlightCalls = 0;
    void ClearHighlight() override { ++clearHighlightCalls; }
};

struct FakeLotDeveloper : IZoneDeveloperLotDeveloperApi {
    FAKE_SERVICE_BOILERPLATE
    std::vector<cSC4Lot*> removed;
    void RemoveLot(cSC4Lot* lot) override { removed.push_back(lot); }
};

struct FakeBuildingDevelopment : IZoneDeveloperBuildingDevelopmentApi {
    FAKE_SERVICE_BOILERPLATE
    std::map<cSC4Lot*, std::uint32_t> units;
    std::vector<cSC4Lot*> added;
    std::vector<cSC4Lot*> forgotten;
    void ForgetLot(cSC4Lot* lot) override { forgotten.push_back(lot); }
    bool GetLotUnitId(cSC4Lot* lot, std::uint32_t& unitId) override {
        auto it = units.find(lot);
        if (it == units.end()) return false;
        unitId = it->second;
        return true;
    }
    void AddLot(cSC4Lot* lot) override { added.push_back(lot); }
};

struct FakeRenderer : IZoneDeveloperService {
    FAKE_SERVICE_BOILERPLATE
};

struct FakeSafeList : IZoneDeveloperSafeListApi {
    FAKE_SERVICE_BOILERPLATE
    std::vector<cSC4Lot*> added;
    void AddAfter(cSC4Lot* lot) override { added.push_back(lot); }
};

//----------------------------------------------------------------------------------------------- occupants

struct FakeNetworkOccupant : IZoneDeveloperNetworkOccupant {
    int refs = 0;
    std::uint32_t flags = 0;
    std::uint32_t networkFlags = 0;
    std::uint32_t connectionFlags = 0;
    std::map<std::pair<std::uint32_t, int>, bool> connections;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 1 || iid == 0xa821ef94) {
            *out = static_cast<IZoneDeveloperNetworkOccupant*>(this);
            AddRef();
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return refs > 0 ? static_cast<std::uint32_t>(--refs) : 0u; }
    bool HasNetworkFlags(std::uint32_t value) override { return (flags & value) != 0; }
    std::uint32_t GetNetworkFlags() override { return networkFlags; }
    std::uint32_t GetConnectionFlags() override { return connectionFlags; }
    bool IsConnected(std::uint32_t direction, int networkType) override {
        auto it = connections.find({direction, networkType});
        return it != connections.end() && it->second;
    }
};

struct FakeOccupant : cISC4Occupant {
    int refs = 0;
    std::uint32_t occupantType = 0;
    float position[3] = {0, 0, 0};
    FakeNetworkOccupant* network = nullptr;

    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 0xa821ef94 && network) {
            network->AddRef();
            *out = network;
            return true;
        }
        if (iid == 1) {
            *out = static_cast<cISC4Occupant*>(this);
            AddRef();
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return refs > 0 ? static_cast<std::uint32_t>(--refs) : 0u; }

    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool IsInitialized() const override { return true; }
    cISCPropertyHolder* AsPropertyHolder() override { return nullptr; }
    std::uint32_t GetType() const override { return occupantType; }
    void GetPosition(float* xyz) override { xyz[0] = position[0]; xyz[1] = position[1]; xyz[2] = position[2]; }
    void SetPosition(const float* xyz) override { std::memcpy(position, xyz, sizeof(position)); }
    void GetBoundingBox(float*, float*) override {}
    void GetBoundingCityCells(SC4CellRect&) override {}
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
    std::uint32_t GetFlags() override { return 0; }
};

struct FakeOccupantManager : IZoneDeveloperOccupantManagerApi {
    FAKE_SERVICE_BOILERPLATE
    std::map<std::pair<int, int>, std::vector<FakeOccupant*>> occupants;
    std::vector<std::uint32_t> removed;
    void ListOccupants(std::vector<cISC4Occupant*>& out, int x, int z, std::uint32_t typeId,
                       int maxCount) override {
        auto it = occupants.find({x, z});
        if (it == occupants.end()) return;
        for (FakeOccupant* occupant : it->second) {
            if (typeId != 0 && occupant->occupantType != typeId) continue;
            if (maxCount >= 0 && static_cast<int>(out.size()) >= maxCount) break;
            occupant->AddRef();
            out.push_back(occupant);
        }
    }
    void RemoveOccupant(std::uint32_t occupantId, int, int) override { removed.push_back(occupantId); }
};

//----------------------------------------------------------------------------------------------- framework

struct FakeMessageServer : cIGZMessageServer2 {
    int refs = 0;
    std::vector<std::uint32_t> notificationsAdded;
    std::vector<std::uint32_t> notificationsRemoved;
    struct Posted {
        std::uint32_t type;
        bool hasData1;
        std::uint64_t data1;
    };
    std::vector<Posted> posted;

    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 1 || iid == 0x652294c7) {
            *out = static_cast<cIGZMessageServer2*>(this);
            AddRef();
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return refs > 0 ? static_cast<std::uint32_t>(--refs) : 0u; }
    bool MessageSend(cIGZMessage2*) override { return true; }
    bool MessagePost(cIGZMessage2* message, bool) override {
        if (auto* standard = dynamic_cast<cIGZMessage2Standard*>(message)) {
            posted.push_back({standard->GetType(), standard->GetHasData1(), standard->GetData1()});
        }
        return true;
    }
    bool AddNotification(cIGZMessageTarget2*, std::uint32_t messageType) override {
        notificationsAdded.push_back(messageType);
        return true;
    }
    bool RemoveNotification(cIGZMessageTarget2*, std::uint32_t messageType) override {
        notificationsRemoved.push_back(messageType);
        return true;
    }
    bool GeneralMessagePostToTarget(cIGZMessage2*, cIGZMessageTarget2*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return static_cast<std::uint32_t>(refs); }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }
};

struct FakeSimGridCity : ISimGridCity {
    std::int32_t CitySizeXInCells() const override { return 64; }
    std::int32_t CitySizeZInCells() const override { return 64; }
    float CellSizeXMeters() const override { return 16.0f; }
    float CellSizeZMeters() const override { return 16.0f; }
};

struct FakeHost : IZoneDeveloperHost {
    FakeMessageServer server;
    FakeRenderer renderer;
    FakeLotDeveloper lotDeveloper;
    FakeBuildingDevelopment buildingDevelopment;
    FakeCity city;
    FakeOccupantManager occupantManager;
    FakeTerrain terrain;
    FakeTerrainView terrainView;
    FakeZoneManager zoneManager;
    FakeNetwork street;
    FakeNetwork road;
    FakeNetwork rail;
    FakeSafeList safeList;

    bool provideCity = true;
    bool provideTerrain = true;
    bool useSafeList = false;
    bool cheatCodesEnabled = false;
    int rendererScale = 64;
    std::vector<std::uint32_t> randomValues;
    std::size_t randomIndex = 0;
    std::vector<std::pair<std::string, std::uint32_t>> registeredCheats;
    std::vector<std::uint32_t> unregisteredCheats;
    std::vector<cSC4Lot*> writtenLots;

    cIGZMessageServer2* MessageServer() override { return &server; }
    int RendererHighlightScale() override { return rendererScale; }
    IZoneDeveloperService* Renderer() override { return &renderer; }
    IZoneDeveloperLotDeveloperApi* LotDeveloper() override { return &lotDeveloper; }
    IZoneDeveloperBuildingDevelopmentApi* BuildingDevelopment() override { return &buildingDevelopment; }
    IZoneDeveloperCityApi* City() override { return provideCity ? &city : nullptr; }
    IZoneDeveloperOccupantManagerApi* OccupantManager() override { return &occupantManager; }
    IZoneDeveloperTerrainApi* Terrain() override { return provideTerrain ? &terrain : nullptr; }
    IZoneDeveloperTerrainViewApi* TerrainView() override { return &terrainView; }
    IZoneDeveloperService* TrafficSim() override { return &renderer; }   // any service placeholder
    IZoneDeveloperZoneManagerApi* ZoneManager() override { return &zoneManager; }
    IZoneDeveloperNetworkApi* NetworkSubnet(int index) override {
        if (index == 0) return &street;
        if (index == 1) return &road;
        return &rail;
    }
    IZoneDeveloperSafeListApi* SafeListIterator() override { return useSafeList ? &safeList : nullptr; }
    bool CheatCodesEnabled() override { return cheatCodesEnabled; }
    void RegisterCheatCode(const char* text, std::uint32_t id, cIGZMessageTarget2*) override {
        registeredCheats.emplace_back(text, id);
    }
    void UnregisterCheatCode(std::uint32_t id) override { unregisteredCheats.push_back(id); }
    std::uint32_t RandomBelow(std::uint32_t range) override {
        if (randomValues.empty() || range == 0) return 0;
        const std::uint32_t value = randomValues[randomIndex++ % randomValues.size()];
        return value % range;
    }
    void WriteLot(cIGZOStream& stream, cSC4Lot* lot) override {
        writtenLots.push_back(lot);
        stream.SetUint32(static_cast<std::uint32_t>(writtenLots.size()));
    }
    cSC4Lot* ReadLot(cIGZIStream& stream) override {
        std::uint32_t id = 0;
        stream.GetUint32(id);
        auto* lot = new cSC4Lot(0, 0, 1, 1, 0);
        lot->AddRef();
        lot->AddRef();   // the stream and the lot list both reference the object, like CreateLot
        return lot;
    }
};

//----------------------------------------------------------------------------------------------- streams

struct FakeOStream : cIGZOStream {
    std::vector<std::uint8_t> bytes;
    bool error = false;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    void Put(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        bytes.insert(bytes.end(), p, p + size);
    }
    void Flush() override {}
    bool SetSint8(std::int8_t v) override { Put(&v, sizeof v); return true; }
    bool SetUint8(std::uint8_t v) override { Put(&v, sizeof v); return true; }
    bool SetSint16(std::int16_t v) override { Put(&v, sizeof v); return true; }
    bool SetUint16(std::uint16_t v) override { Put(&v, sizeof v); return true; }
    bool SetSint32(std::int32_t v) override { Put(&v, sizeof v); return true; }
    bool SetUint32(std::uint32_t v) override { Put(&v, sizeof v); return true; }
    bool SetSint64(std::int64_t v) override { Put(&v, sizeof v); return true; }
    bool SetUint64(std::uint64_t v) override { Put(&v, sizeof v); return true; }
    bool SetFloat32(float v) override { Put(&v, sizeof v); return true; }
    bool SetFloat64(double v) override { Put(&v, sizeof v); return true; }
    bool SetRZCharStr(const char* v) override { SetUint32(static_cast<std::uint32_t>(std::strlen(v))); Put(v, std::strlen(v)); return true; }
    bool SetGZStr(const cIGZString&) override { return true; }
    bool SetGZSerializable(const cIGZSerializable&) override { return true; }
    bool SetVoid(const void* source, std::uint32_t count) override { Put(source, count); return true; }
    bool GetError() const override { return error; }
    void SetUserData(cIGZVariant*) override {}
    cIGZVariant* GetUserData() override { return nullptr; }
};

struct FakeIStream : cIGZIStream {
    std::vector<std::uint8_t> bytes;
    std::size_t cursor = 0;
    bool error = false;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    template <typename T>
    bool Get(T& out) {
        if (cursor + sizeof(T) > bytes.size()) {
            error = true;
            return false;
        }
        std::memcpy(&out, bytes.data() + cursor, sizeof(T));
        cursor += sizeof(T);
        return true;
    }
    bool Skip(std::uint32_t count) override {
        if (cursor + count > bytes.size()) { error = true; return false; }
        cursor += count;
        return true;
    }
    bool GetSint8(std::int8_t& v) override { return Get(v); }
    bool GetUint8(std::uint8_t& v) override { return Get(v); }
    bool GetSint16(std::int16_t& v) override { return Get(v); }
    bool GetUint16(std::uint16_t& v) override { return Get(v); }
    bool GetSint32(std::int32_t& v) override { return Get(v); }
    bool GetUint32(std::uint32_t& v) override { return Get(v); }
    bool GetSint64(std::int64_t& v) override { return Get(v); }
    bool GetUint64(std::uint64_t& v) override { return Get(v); }
    bool GetFloat32(float& v) override { return Get(v); }
    bool GetFloat64(double& v) override { return Get(v); }
    bool GetRZCharStr(char* buffer, std::uint32_t maxLength) override {
        std::uint32_t length = 0;
        if (!Get(length)) return false;
        for (std::uint32_t i = 0; i < length; ++i) {
            std::uint8_t c = 0;
            if (!Get(c)) return false;
            if (i < maxLength) buffer[i] = static_cast<char>(c);
        }
        if (length <= maxLength) buffer[length] = '\0';
        return true;
    }
    bool GetGZStr(cIGZString&) override { return true; }
    bool GetGZSerializable(cIGZSerializable&) override { return true; }
    bool GetVoid(void* destination, std::uint32_t count) override {
        if (cursor + count > bytes.size()) { error = true; return false; }
        std::memcpy(destination, bytes.data() + cursor, count);
        cursor += count;
        return true;
    }
    bool GetError() const override { return error; }
    void SetUserData(cIGZVariant*) override {}
    cIGZVariant* GetUserData() override { return nullptr; }
};

//----------------------------------------------------------------------------------------------- helpers

cSC4Lot* CreateLotChecked(cSC4ZoneDeveloper* developer, int x, int z, int sizeX, int sizeZ, int facing) {
    cSC4Lot* lot = nullptr;
    CHECK(developer->CreateLot(x, z, sizeX, sizeZ, facing, lot));
    CHECK(lot != nullptr);
    return lot;
}

void ClearLotList(std::list<cRZAutoRefCount<cSC4Lot>>& lots) { lots.clear(); }

//----------------------------------------------------------------------------------------------- tests

void TestLayoutAndCom() {
    CHECK(sizeof(cSC4ZoneDeveloper) == 0x1c0);
    auto* developer = new cSC4ZoneDeveloper();
    CHECK((char*)static_cast<cISC4LotManager*>(developer) - (char*)developer == 8);
    CHECK((char*)static_cast<cIGZMessageTarget2*>(developer) - (char*)developer == 0x10);
    CHECK((char*)static_cast<cIGZSerializable*>(developer) - (char*)developer == 0x18);

    void* result = nullptr;
    CHECK(developer->QueryInterface(1, &result) && result == static_cast<cISC4ZoneDeveloper*>(developer));
    CHECK(developer->QueryInterface(0xe818ed2b, &result) && result == static_cast<cISC4ZoneDeveloper*>(developer));
    CHECK(developer->QueryInterface(0x681905bd, &result) &&
          (char*)result - (char*)developer == 8);
    CHECK(developer->QueryInterface(0x452294aa, &result) &&
          (char*)result - (char*)developer == 0x10);
    CHECK(developer->QueryInterface(0xe4fda3d4, &result) &&
          (char*)result - (char*)developer == 0x18);
    CHECK(!developer->QueryInterface(0xdeadbeef, &result));
    CHECK(developer->GetGZCLSID() == 0x498f9b01);
    // Five successful QIs incremented the shared count to 5 (the last four after the iid-1 success).
    CHECK(developer->AddRef() == 6);
    CHECK(developer->Release() == 5);
    CHECK(developer->Release() == 4);
    CHECK(developer->Release() == 3);
    CHECK(developer->Release() == 2);
    CHECK(developer->Release() == 1);
    CHECK(developer->Release() == 0);   // count < 2 deletes the object
}

void TestInitShutdown() {
    FakeHost host;
    SetZoneDeveloperHost(&host);

    {
        FakeHost noCity;
        noCity.provideCity = false;
        SetZoneDeveloperHost(&noCity);
        auto* developer = new cSC4ZoneDeveloper();
        developer->AddRef();
        CHECK(!developer->Init());
        developer->Release();
        SetZoneDeveloperHost(&host);
    }

    auto* developer = new cSC4ZoneDeveloper();
    developer->AddRef();
    CHECK(developer->Init());
    CHECK(developer->IsInitialized());
    CHECK(developer->GetMaximumCapacityGrid() != nullptr);
    CHECK(developer->GetMaximumCapacityGrid()->GetTractSize() == 4);
    CHECK(developer->GetMaximumCapacityGrid()->GetInstanceID() == 0x69d5c456);
    CHECK(developer->GetLotCount() == 0);
    CHECK(developer->MessageServerForTest() == &host.server);
    CHECK(host.server.notificationsAdded.size() == 3);
    CHECK(host.server.notificationsAdded[0] == 0x99ef1142);
    CHECK(host.server.notificationsAdded[1] == 0x26d31ec1);
    CHECK(host.server.notificationsAdded[2] == 0xa6b79602);
    CHECK(host.registeredCheats.empty());   // cheats disabled in this host
    CHECK(!developer->CheatCodesWereRegistered());
    CHECK(developer->RendererHighlightScale() == 64);
    // Second Init is a no-op.
    CHECK(developer->Init());
    CHECK(host.server.notificationsAdded.size() == 3);

    CHECK(developer->Shutdown());
    CHECK(!developer->IsInitialized());
    CHECK(host.server.notificationsRemoved.size() == 3);
    CHECK(developer->GetMaximumCapacityGrid() == nullptr);
    CHECK(host.city.refs == 0);
    CHECK(host.terrain.refs == 0);
    developer->Release();

    // Cheat registration path.
    host.cheatCodesEnabled = true;
    host.server.notificationsAdded.clear();
    auto* cheats = new cSC4ZoneDeveloper();
    cheats->AddRef();
    CHECK(cheats->Init());
    CHECK(cheats->CheatCodesWereRegistered());
    CHECK(host.registeredCheats.size() == 2);
    CHECK(host.registeredCheats[0].first == "PlopAllLots");
    CHECK(host.registeredCheats[0].second == 0x8929e68e);
    CHECK(host.registeredCheats[1].first == "PlopAllBuildings");
    CHECK(host.registeredCheats[1].second == 0x4ab569c9);
    CHECK(cheats->Shutdown());
    CHECK(!cheats->CheatCodesWereRegistered());
    CHECK(host.unregisteredCheats.size() == 2);
    cheats->Release();

    SetZoneDeveloperHost(nullptr);
}

void TestBoundaryRules() {
    FakeHost host;
    SetZoneDeveloperHost(&host);
    auto* developer = new cSC4ZoneDeveloper();
    developer->AddRef();
    CHECK(developer->Init());

    auto checkRule = [&](BoundaryType boundary, std::uint32_t expectedTexture,
                         std::uint8_t expectedShape) {
        std::uint32_t texture = 0xffffffff;
        std::uint8_t shape = 0xff;
        bool flag = true;
        developer->GetBoundaryLotTexture(boundary, texture, shape, flag);
        CHECK(texture == expectedTexture);
        CHECK(shape == expectedShape);
        CHECK(!flag);
    };
    checkRule(BoundaryType{{0, 0, 0, 0}}, 0x3000, 0);
    checkRule(BoundaryType{{1, 0, 0, 0}}, 0x2000, 0);
    checkRule(BoundaryType{{0, 2, 1, 0}}, 0x2400, 3);       // key 0x210, 16-bit rule 0x103 truncated
    checkRule(BoundaryType{{0, 0, 0, 2}}, 0x2600, 3);       // key 2
    checkRule(BoundaryType{{5, 5, 5, 5}}, 0x3000, 0);        // falls back to key 0

    developer->Shutdown();
    developer->Release();
    SetZoneDeveloperHost(nullptr);
}

void TestLotCreationAndQueries() {
    FakeHost host;
    SetZoneDeveloperHost(&host);
    auto* developer = new cSC4ZoneDeveloper();
    developer->AddRef();
    CHECK(developer->Init());

    cSC4Lot* a = CreateLotChecked(developer, 2, 3, 2, 1, 0);
    CHECK(developer->GetLotCount() == 1);
    CHECK(developer->GetLot(2, 3, false) == a);
    CHECK(developer->GetLot(3, 3, false) == a);
    CHECK(developer->GetLot(2, 3, true) == nullptr);   // facing 0 names the corner cell (3, 3)
    CHECK(developer->GetLot(3, 3, true) == a);
    CHECK(developer->GetLot(-1, 0, false) == nullptr);
    CHECK(developer->GetLot(16, 0, false) == nullptr);
    CHECK(developer->GetLot(0, 16, false) == nullptr);
    CHECK(developer->DoesCellHaveLot(2, 3));
    CHECK(developer->DoesCellHaveLot(3, 3));
    CHECK(!developer->DoesCellHaveLot(4, 3));
    CHECK(host.buildingDevelopment.added.size() == 1);
    CHECK(host.buildingDevelopment.added[0] == a);

    CreateLotChecked(developer, 6, 2, 1, 2, 0);   // lot B (x 6, z 2..3)
    CHECK(developer->GetLotCount() == 2);

    // --- GetLotsInCellRect ---------------------------------------------------------------------
    std::list<cRZAutoRefCount<cSC4Lot>> lots;
    CHECK(developer->GetLotsInCellRect(lots, 2, 3, 3, 3, false) == 1);
    CHECK(lots.size() == 1 && lots.front().get() == a);
    ClearLotList(lots);
    CHECK(developer->GetLotsInCellRect(lots, 2, 3, 3, 3, true) == 1);
    CHECK(lots.front().get() == a);
    ClearLotList(lots);
    CHECK(developer->GetLotsInCellRect(lots, 2, 2, 6, 3, false) == 2);
    ClearLotList(lots);
    CHECK(developer->GetLotsInCellRect(lots, 4, 4, 5, 5, false) == 0);
    CHECK(developer->GetLotsInCellRect(lots, -5, -5, 30, 30, false) == 2);
    ClearLotList(lots);

    // --- surrounding ring ----------------------------------------------------------------------
    cSC4Lot* big = CreateLotChecked(developer, 3, 5, 3, 1, 0);   // footprint 3..5 x 5
    const SC4Rect<int> oneCell{4, 5, 4, 5};
    CHECK(developer->GetLotsSurroundingRect(oneCell, lots, 0xf) == 1);
    CHECK(lots.front().get() == big);
    ClearLotList(lots);
    CHECK(developer->GetLotsSurroundingRect(oneCell, lots, 0x7) == 1);   // east/north/south only
    CHECK(lots.front().get() == big);
    ClearLotList(lots);
    CHECK(developer->GetLotsSurroundingRect(oneCell, lots, 0x5) == 0);   // north/south only
    ClearLotList(lots);
    CHECK(developer->GetLotsSurroundingLot(4, 5, lots, 0xf) == 0);   // the lot at (4,5) is `big`; its ring is empty
    ClearLotList(lots);
    CHECK(developer->GetLotsSurroundingLot(big, lots, 0xf) == 0);
    ClearLotList(lots);

    // --- vertices and neighbouring cells --------------------------------------------------------
    const SC4Rect<int> rect{2, 3, 4, 5};
    SC4Rect<int> out{};
    developer->GetVerticesAtEdgeOfLot(rect, 1, out);
    CHECK(out.x0 == 2 && out.z0 == 3 && out.x1 == 5 && out.z1 == 3);
    developer->GetVerticesAtEdgeOfLot(rect, 2, out);
    CHECK(out.x0 == 5 && out.z0 == 3 && out.x1 == 5 && out.z1 == 6);
    developer->GetVerticesAtEdgeOfLot(rect, 4, out);
    CHECK(out.x0 == 2 && out.z0 == 6 && out.x1 == 5 && out.z1 == 6);
    developer->GetVerticesAtEdgeOfLot(rect, 8, out);
    CHECK(out.x0 == 2 && out.z0 == 3 && out.x1 == 2 && out.z1 == 6);
    out = {7, 7, 7, 7};
    developer->GetVerticesAtEdgeOfLot(rect, 16, out);
    CHECK(out.x0 == 7 && out.z0 == 7 && out.x1 == 7 && out.z1 == 7);   // untouched

    CHECK(developer->GetNeighborCellsAtEdgeOfLot(rect, 1, out));
    CHECK(out.x0 == 2 && out.z0 == 2 && out.x1 == 4 && out.z1 == 2);
    CHECK(!developer->GetNeighborCellsAtEdgeOfLot(SC4Rect<int>{0, 0, 3, 3}, 1, out));
    CHECK(out.x0 == 0 && out.z0 == 0 && out.x1 == -1 && out.z1 == -1);
    CHECK(developer->GetNeighborCellsAtEdgeOfLot(SC4Rect<int>{12, 0, 14, 3}, 2, out));
    CHECK(out.x0 == 15 && out.z0 == 0 && out.x1 == 15 && out.z1 == 3);
    CHECK(!developer->GetNeighborCellsAtEdgeOfLot(SC4Rect<int>{13, 0, 15, 3}, 2, out));
    CHECK(developer->GetNeighborCellsAtEdgeOfLot(rect, 4, out));
    CHECK(out.x0 == 2 && out.z0 == 6 && out.x1 == 4 && out.z1 == 6);
    CHECK(developer->GetNeighborCellsAtEdgeOfLot(rect, 8, out));
    CHECK(out.x0 == 1 && out.z0 == 3 && out.x1 == 1 && out.z1 == 5);

    // --- altitude helpers ----------------------------------------------------------------------
    a->SetFlags(1);   // kLotFlagLocked
    a->SetAltitude(12.5f);
    bool found = false;
    CHECK(developer->GetLotSavvyCellAltitude(2, 3, &found) == 12.5f);
    CHECK(found);
    CHECK(developer->GetLotSavvyCellAltitude(3, 3, &found) == 12.5f);
    CHECK(found);
    CHECK(developer->GetLotSavvyCellAltitude(12, 12, &found) == 5.0f);
    CHECK(!found);
    CHECK(developer->GetLotSavvyTerrainVertexAltitude(3, 3, nullptr, &found) == 12.5f);
    CHECK(found);
    CHECK(developer->GetLotSavvyTerrainVertexAltitude(3, 4, nullptr, &found) == 12.5f);
    CHECK(found);
    CHECK(developer->GetLotSavvyTerrainVertexAltitude(3, 4, a, &found) == 5.0f);
    CHECK(!found);
    CHECK(developer->GetLotSavvyTerrainVertexAltitude(12, 12, nullptr, &found) == 5.0f);
    CHECK(!found);
    CHECK(std::fabs(developer->GetLotSavvyTerrainAltitude(3 * 16.0f + 8.0f, 3 * 16.0f + 8.0f, &found) -
                    12.5f) < 0.001f);
    CHECK(found);
    CHECK(std::fabs(developer->GetLotSavvyTerrainAltitude(8.0f, 8.0f, &found) - 5.0f) < 0.001f);
    CHECK(!found);

    float minimum = 0, maximum = 0;
    developer->GetLotVertexAltitudeAdjustmentRange(3, 3, minimum, maximum);
    CHECK(minimum == FLT_MAX && maximum == FLT_MAX);
    a->SetState(2, 0, 0);
    developer->GetLotVertexAltitudeAdjustmentRange(3, 3, minimum, maximum);
    CHECK(minimum == 32.0f && maximum == 32.0f);
    developer->GetLotVertexAltitudeAdjustmentRange(15, 15, minimum, maximum);
    CHECK(minimum == FLT_MAX && maximum == FLT_MAX);

    // --- occupant query ------------------------------------------------------------------------
    FakeOccupant occupant;
    occupant.position[0] = 3 * 16.0f + 8.0f;
    occupant.position[2] = 3 * 16.0f + 8.0f;
    CHECK(developer->GetOccupantLot(&occupant) == a);
    occupant.position[0] = 12 * 16.0f + 8.0f;
    CHECK(developer->GetOccupantLot(&occupant) == nullptr);

    // --- 3D-vector query -----------------------------------------------------------------------
    cS3DVector3 position{3 * 16.0f + 8.0f, 0.0f, 3 * 16.0f + 8.0f};
    CHECK(developer->GetLot(position) == a);
    position = cS3DVector3{-16.0f, 0.0f, 0.0f};
    CHECK(developer->GetLot(position) == nullptr);

    // --- random lot ----------------------------------------------------------------------------
    host.randomValues = {3, 3};
    CHECK(developer->GetRandomLot() == a);
    host.randomValues = {0, 0};   // (0,0) is empty
    CHECK(developer->GetRandomLot() == nullptr);

    // --- create-lot-if-possible ----------------------------------------------------------------
    cSC4Lot* placed = nullptr;
    CHECK(developer->CreateLotIfPossible(9, 9, 1, 1, 0, placed, true, true, nullptr));
    CHECK(placed != nullptr);
    CHECK(developer->GetLot(9, 9, false) == placed);
    CHECK(developer->GetLotCount() == 4);
    CHECK(!developer->CreateLotIfPossible(-1, 0, 1, 1, 0, placed, true, true, nullptr));
    CHECK(placed == nullptr);

    cSC4Lot* shared = nullptr;
    CHECK(developer->CreateLot(10, 10, 1, 1, 0, shared));
    CHECK(host.useSafeList == false);
    CHECK(host.buildingDevelopment.added.back() == shared);

    // --- delete --------------------------------------------------------------------------------
    const std::size_t removedBefore = host.lotDeveloper.removed.size();
    developer->DeleteLots(9, 9, 9, 9);
    CHECK(developer->GetLot(9, 9, false) == nullptr);
    CHECK(developer->GetLotCount() == 4);
    CHECK(host.lotDeveloper.removed.size() == removedBefore + 1);
    developer->DeleteLot(shared);
    CHECK(developer->GetLot(10, 10, false) == nullptr);
    CHECK(developer->GetLotCount() == 3);

    // Deleting a lot with a building unit removes the occupant id.
    host.buildingDevelopment.units[a] = 0x1234;
    developer->DeleteLots(2, 3, 3, 3);
    CHECK(host.occupantManager.removed.size() == 1 && host.occupantManager.removed[0] == 0x1234);
    CHECK(host.buildingDevelopment.forgotten.empty());   // the lot has no unit, so it is not forgotten

    // --- ClearLotMap through Shutdown ----------------------------------------------------------
    developer->Shutdown();
    CHECK(developer->GetLotCount() == 0);
    developer->Release();
    SetZoneDeveloperHost(nullptr);
}

void TestNetworkAndOccupantSeams() {
    FakeHost host;
    SetZoneDeveloperHost(&host);
    auto* developer = new cSC4ZoneDeveloper();
    developer->AddRef();
    CHECK(developer->Init());

    FakeNetworkOccupant networkOccupant;
    networkOccupant.flags = (0x1f4f & ~0x104u) | 0x808;
    networkOccupant.connections[{2, 3}] = true;

    FakeOccupant occupant;
    occupant.occupantType = 0xc772bf98;
    occupant.network = &networkOccupant;
    host.occupantManager.occupants[{7, 7}].push_back(&occupant);

    CHECK(developer->GetNetwork(7, 7, 0x1f4f) == &networkOccupant);
    CHECK(developer->GetNetwork(7, 7, 0x40000) == nullptr);
    CHECK(developer->GetNetwork(8, 8, 0x1f4f) == nullptr);
    CHECK(developer->ExistsNetworkOfType(7, 7, 0x1f4f));
    CHECK(developer->ExistsNetworkOfType(7, 7, 0x808));
    CHECK(!developer->ExistsNetworkOfType(7, 7, 0x104));
    CHECK(developer->ExistsConnectionOfType(7, 7, 0x1f4f, 1, 3));
    CHECK(!developer->ExistsConnectionOfType(7, 7, 0x1f4f, 0, 3));

    // IsCellWater goes straight to the terrain.
    host.terrain.water.insert({2, 8});
    CHECK(developer->IsCellWater(2, 8));
    CHECK(!developer->IsCellWater(2, 7));

    // ExamineCell: a rail border costs 30 and reports a soft boundary.
    BoundaryCondition boundary = kBoundaryNone;
    int cost = 0;
    developer->ExamineCell(7, 7, boundary, cost);
    CHECK(boundary == kBoundarySoft);
    CHECK(cost == 30);

    // A street (0x104) is a blocked boundary and costs 5.
    networkOccupant.flags = 0x1f4f | 0x104;
    boundary = kBoundaryNone;
    cost = 0;
    developer->ExamineCell(7, 7, boundary, cost);
    CHECK(boundary == kBoundaryBlocked && cost == 5);

    // A plain cell costs 1.
    boundary = kBoundaryNone;
    cost = 0;
    developer->ExamineCell(12, 12, boundary, cost);
    CHECK(boundary == kBoundaryNone && cost == 1);

    // Water is blocked and costs 5.
    boundary = kBoundaryNone;
    cost = 0;
    developer->ExamineCell(2, 8, boundary, cost);
    CHECK(boundary == kBoundaryBlocked && cost == 5);

    // A building occupant also blocks; a non-building occupant does not.
    FakeOccupant building;
    building.occupantType = 0x278128a0;
    host.occupantManager.occupants[{5, 5}].push_back(&building);
    boundary = kBoundaryNone;
    cost = 0;
    developer->ExamineCell(5, 5, boundary, cost);
    CHECK(boundary == kBoundaryBlocked && cost == 5);

    FakeOccupant plain;
    plain.occupantType = 0x12345678;
    host.occupantManager.occupants[{6, 6}].push_back(&plain);
    boundary = kBoundaryNone;
    cost = 0;
    developer->ExamineCell(6, 6, boundary, cost);
    CHECK(boundary == kBoundaryNone && cost == 1);

    // CanCreateLotOnCell: out of bounds, water, network, existing lot, free.
    CHECK(developer->CanCreateLotOnCell(-1, 0, false, false) == 0);
    CHECK(developer->CanCreateLotOnCell(16, 0, false, false) == 0);
    CHECK(developer->CanCreateLotOnCell(2, 8, false, false) == 1);
    CHECK(developer->CanCreateLotOnCell(2, 8, false, true) == 8);
    networkOccupant.flags = 0x1f4f;
    CHECK(developer->CanCreateLotOnCell(7, 7, false, true) == 3);

    // CanCreateLot min over the footprint; a null configuration cannot allow water.
    CHECK(developer->CanCreateLot(0, 0, 2, 2, true, true, nullptr) == 8);
    CHECK(developer->CanCreateLot(2, 8, 1, 1, true, false, nullptr) == 1);
    cSC4Lot* lot = nullptr;
    CHECK(developer->CreateLot(1, 1, 1, 1, 0, lot));
    CHECK(developer->CanCreateLotOnCell(1, 1, false, true) == 7);
    CHECK(developer->CanCreateLotOnCell(3, 3, false, true) == 8);
    CHECK(developer->CanCreateLot(1, 1, 1, 1, true, true, nullptr) == 7);

    // Attach the network sub-managers (as the city-init message does) before checking highlight clears.
    cRZMessage2Standard cityInit;
    cityInit.SetType(0x26d31ec1);
    CHECK(developer->DoMessage(&cityInit));

    // ClearHighlight hits the road network, the street network and the terrain view.
    host.road.clearHighlightCalls = 0;
    host.street.clearHighlightCalls = 0;
    host.terrainView.clearHighlightCalls = 0;
    developer->ClearHighlight();
    CHECK(host.road.clearHighlightCalls == 1);
    CHECK(host.street.clearHighlightCalls == 1);
    CHECK(host.terrainView.clearHighlightCalls == 1);

    // SetOptions maps the three arguments onto the original bytes.
    developer->SetOptions(true, false, true);
    CHECK(developer->SwapOrientation());
    CHECK(!developer->ExtendAsStreets());
    CHECK(developer->ExamineBoundaries());
    developer->SetOptions(false, true, false);
    CHECK(!developer->SwapOrientation());
    CHECK(developer->ExtendAsStreets());
    CHECK(!developer->ExamineBoundaries());

    // The safe-list iterator reports "not recovered" as a null iterator.
    CHECK(developer->GetSafeLotIterator() == nullptr);

    developer->Shutdown();
    developer->Release();
    SetZoneDeveloperHost(nullptr);
}

void TestDeferAndMessages() {
    FakeHost host;
    SetZoneDeveloperHost(&host);
    auto* developer = new cSC4ZoneDeveloper();
    developer->AddRef();
    CHECK(developer->Init());

    developer->DeferOccupantOperations(true);
    CHECK(host.server.posted.size() == 1);
    CHECK(host.server.posted[0].type == 0x49d968c4);
    CHECK(host.server.posted[0].hasData1 && host.server.posted[0].data1 == 1);
    developer->DeferOccupantOperations(false);
    CHECK(host.server.posted.size() == 2 && host.server.posted[1].data1 == 0);

    // 0xa6b79602 carries the highlight scale in its second data word.
    cRZMessage2Standard scaleMessage;
    scaleMessage.SetType(0xa6b79602);
    int scale = 77;
    scaleMessage.SetVoid2(&scale);
    CHECK(developer->DoMessage(&scaleMessage));
    CHECK(developer->RendererHighlightScale() == 77);

    // 0x26d31ec1 attaches the three networks and the terrain view and re-posts the loaded lots.
    cRZMessage2Standard initMessage;
    initMessage.SetType(0x26d31ec1);
    CHECK(developer->DoMessage(&initMessage));
    CHECK(host.road.refs == 1);
    CHECK(host.rail.refs == 1);
    CHECK(host.street.refs == 1);
    CHECK(host.terrainView.refs == 1);
    CHECK(host.road.inits == 1 && host.rail.inits == 1 && host.street.inits == 1);
    CHECK(developer->DoMessage(&initMessage));
    CHECK(host.road.refs == 1);   // already attached

    // The occupant-removed message deletes the lot under the occupant and stops (the remnant/connection
    // passes are not recovered).
    cSC4Lot* lot = nullptr;
    CHECK(developer->CreateLot(4, 4, 1, 1, 0, lot));
    FakeOccupant occupant;
    occupant.position[0] = 4 * 16.0f + 8.0f;
    occupant.position[2] = 4 * 16.0f + 8.0f;
    cRZMessage2Standard removal;
    removal.SetType(0x99ef1142);
    removal.SetData3(0xc7729698);
    removal.SetVoid1(&occupant);
    removal.SetData4(1);
    CHECK(developer->DoMessage(&removal));
    CHECK(developer->GetLot(4, 4, false) == nullptr);
    CHECK(developer->GetLotCount() == 0);

    // The network-occupant skip flag leaves the lot alone.
    cSC4Lot* kept = nullptr;
    CHECK(developer->CreateLot(4, 4, 1, 1, 0, kept));
    FakeNetworkOccupant skipView;
    skipView.flags = 0x80200090;
    occupant.network = &skipView;
    CHECK(developer->DoMessage(&removal));
    CHECK(developer->GetLot(4, 4, false) == kept);
    occupant.network = nullptr;

    // Unknown messages are ignored.
    cRZMessage2Standard unknown;
    unknown.SetType(0x12345678);
    CHECK(developer->DoMessage(&unknown));

    developer->Shutdown();
    developer->Release();
    SetZoneDeveloperHost(nullptr);
}

void TestSerialization() {
    FakeHost host;
    SetZoneDeveloperHost(&host);
    auto* developer = new cSC4ZoneDeveloper();
    developer->AddRef();
    CHECK(developer->Init());
    cSC4Lot* lot = nullptr;
    CHECK(developer->CreateLot(2, 2, 1, 1, 0, lot));

    FakeOStream out;
    CHECK(developer->Write(out));
    CHECK(out.bytes.size() == 2 + 4 + 4 + 4);   // version, rows, columns, one lot id
    std::uint16_t version = 0;
    std::uint32_t rows = 0, columns = 0, id = 0;
    std::memcpy(&version, out.bytes.data(), 2);
    std::memcpy(&rows, out.bytes.data() + 2, 4);
    std::memcpy(&columns, out.bytes.data() + 6, 4);
    std::memcpy(&id, out.bytes.data() + 10, 4);
    CHECK(version == 1 && rows == 16 && columns == 16 && id == 1);

    developer->Shutdown();
    developer->Release();

    // Read a one-cell map back into a fresh developer.
    FakeIStream in;
    auto put = [&](const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        in.bytes.insert(in.bytes.end(), p, p + size);
    };
    const std::uint16_t readVersion = 1;
    const std::uint32_t readRows = 1, readColumns = 1, readId = 1;
    put(&readVersion, 2);
    put(&readRows, 4);
    put(&readColumns, 4);
    put(&readId, 4);
    auto* reader = new cSC4ZoneDeveloper();
    reader->AddRef();
    CHECK(reader->Init());
    CHECK(reader->Read(in));
    CHECK(reader->GetLotCount() == 1);
    CHECK(reader->GetLot(0, 0, false) != nullptr);
    reader->Shutdown();
    reader->Release();

    // A bad version is refused.
    FakeIStream bad;
    const std::uint16_t badVersion = 2;
    const auto* p = reinterpret_cast<const std::uint8_t*>(&badVersion);
    bad.bytes.insert(bad.bytes.end(), p, p + 2);
    auto* badReader = new cSC4ZoneDeveloper();
    badReader->AddRef();
    CHECK(badReader->Init());
    CHECK(!badReader->Read(bad));
    badReader->Shutdown();
    badReader->Release();

    SetZoneDeveloperHost(nullptr);
}

}  // namespace

int main() {
    FakeSimGridCity simGridCity;
    SetSimGridCity(&simGridCity);

    TestLayoutAndCom();
    TestInitShutdown();
    TestBoundaryRules();
    TestLotCreationAndQueries();
    TestNetworkAndOccupantSeams();
    TestDeferAndMessages();
    TestSerialization();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("sc4_zonedeveloper: all checks passed\n");
    return 0;
}
