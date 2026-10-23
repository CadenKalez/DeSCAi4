// cSTETerrain parts 2 and 3: COM identity, the message posts, the display/accumulation state, the vertex
// editing helpers, the mark/rect/span helpers, the temporary water-level copies, the brush tuning
// queries, the lighting row walk, the edge-altitude subfiles and the stream Read/Write wrapper; then the
// first-boot path (Init/Shutdown/StaticInit/StaticShutdown/DoPostCityInit/DoMessage/DoAnimationTick), the
// redisplay + altitude range, the water-proximity/land-type surface helpers, the shoreline helpers and the
// effect-id property. The engine services (message server, lighting manager, view, city, surface water,
// simulator, command registry, flood fill) are fakes here; the scripting payloads use the small
// parameter-set / property-holder fakes below so the test keeps the registered link set.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "gz/CommandServer.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZPersistDBSegment.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZVariant.h"
#include "rz/cRZString.h"
#include "sc4/DBSegment.h"
#include "sc4/SCProperty.h"
#include "sc4/cSTETerrain.h"

using namespace sc4;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

namespace {

// ---------------------------------------------------------------------- fake message server
struct PostedMessage {
    std::uint32_t type = 0;
    std::uint64_t data[4] = {0, 0, 0, 0};
};

class FakeMessageServer final : public cIGZMessageServer2 {
public:
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override { return --refs_; }
    bool MessageSend(cIGZMessage2* message) override { Record(message); return true; }
    bool MessagePost(cIGZMessage2* message, bool) override { Record(message); return true; }
    bool AddNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool RemoveNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool GeneralMessagePostToTarget(cIGZMessage2* message, cIGZMessageTarget2* target) override {
        Record(message);
        lastTarget = target;
        return true;
    }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return static_cast<std::uint32_t>(posts.size()); }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return refs_; }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }

    void Record(cIGZMessage2* message) {
        PostedMessage post;
        post.type = message->GetType();
        void* raw = nullptr;
        if (message->QueryInterface(kGZIID_cIGZMessage2Standard, &raw) && raw) {
            auto* standard = static_cast<cIGZMessage2Standard*>(raw);
            post.data[0] = standard->GetData1();
            post.data[1] = standard->GetData2();
            post.data[2] = standard->GetData3();
            post.data[3] = standard->GetData4();
            standard->Release();
        }
        posts.push_back(post);
    }

    std::vector<PostedMessage> posts;
    cIGZMessageTarget2* lastTarget = nullptr;
    std::uint32_t refs_ = 0;
};

// ------------------------------------------------------------------ fake engine services
class FakeService final : public cIGZUnknown {
public:
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    int refs = 0;
};

// The command registry: records the seven terrain script commands.
class FakeCommandServer final : public cIGZCommandServer {
public:
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 0; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    void CreateCommandParameterSet(cIGZCommandParameterSet**) override {}
    bool RegisterCommand(std::uint32_t id, const char* name, const char* help) override {
        registered.emplace_back(id, name ? name : "", help ? help : "");
        return true;
    }
    bool RegisterCommands(const cGZCommandInfoEntry*, std::uint32_t) override { return true; }
    bool RegisterCommandParameterInfo(std::uint32_t, const char*) override { return true; }
    bool UnregisterCommand(std::uint32_t id) override { unregistered.push_back(id); return true; }
    bool UnregisterCommands(const cGZCommandInfoEntry*, std::uint32_t) override { return true; }
    bool GetCommandInfo(std::uint32_t, cIGZString&, cIGZString*) const override { return false; }
    std::uint32_t GetCommandID(const cIGZString&) const override { return 0; }
    std::uint32_t GetCommandIDFromCommandString(const char*, std::uint32_t) const override { return 0; }
    void EnumerateCommands(std::vector<std::uint32_t>&) const override {}
    bool RegisterCommandGenerator(cIGZCommandGenerator*) override { return true; }
    bool UnregisterCommandGenerator(cIGZCommandGenerator*) override { return true; }
    cIGZCommandDispatcher* GetCommandDispatcher() const override { return nullptr; }
    bool SetCommandDispatcher(cIGZCommandDispatcher*) override { return true; }
    std::uint32_t ExecuteCommand(std::uint32_t, cIGZCommandParameterSet*, cIGZCommandParameterSet*) override { return 0; }
    std::uint32_t ExecuteCommand(const char*, std::uint32_t, cIGZCommandParameterSet*) override { return 0; }
    std::uint32_t ExecuteCommandFormatted(const char*, cIGZCommandParameterSet*, ...) override { return 0; }
    bool ConvertStringToCommand(const char*, std::uint32_t, std::uint32_t&, cIGZCommandParameterSet*&) override { return false; }
    bool ConvertCommandToString(std::uint32_t, cIGZCommandParameterSet*, cIGZString&) override { return false; }
    bool ConvertCommandOutputToString(cIGZCommandParameterSet*, cIGZString&) override { return false; }

    struct Command {
        std::uint32_t id;
        std::string name, help;
    };
    std::vector<Command> registered;
    std::vector<std::uint32_t> unregistered;
};

class FakeView final : public ITerrainView {
public:
    cIGZSerializable* Serializable() override { return nullptr; }
    bool Init(cSTETerrain* terrain) override {
        ++inits;
        initTerrain = terrain;
        return true;
    }
    void Shutdown() override { ++shutdowns; }
    void MarkTextureSpanInfoDirty(std::int32_t firstRow, std::int32_t lastRow) override {
        dirtyRows.emplace_back(firstRow, lastRow);
    }
    void ChangeTerrainType(std::int32_t type) override { changedTypes.push_back(type); }
    void SetUpTexturesForCurrentTerrainTypeAndZoom(std::int32_t zoom) override { textureZooms.push_back(zoom); }
    float MaxDisplayAltitude() const override { return maxAltitude; }
    float MinDisplayAltitude() const override { return minAltitude; }
    cIGZUnknown* OverlayManager(std::uint32_t which) override {
        overlayRequests.push_back(which);
        return which < 3 ? &overlays[which] : nullptr;
    }

    int inits = 0, shutdowns = 0;
    cSTETerrain* initTerrain = nullptr;
    float minAltitude = 0.0f, maxAltitude = 50.0f;
    std::vector<std::pair<std::int32_t, std::int32_t>> dirtyRows;
    std::vector<std::int32_t> changedTypes, textureZooms, overlayRequests;
    FakeService overlays[3];
};

// The scripting payload of a command message: a sparse set of cRZVariant values (the real class lives in
// src/gz/CommandParameterSet.cpp, which is kept out of this test's link set).
class FakeParameterSet final : public cIGZCommandParameterSet {
public:
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 0; }
    std::uint32_t GetParameterCount() const override { return static_cast<std::uint32_t>(values.size()); }
    void SetParameterCount(std::uint32_t) override {}
    cIGZVariant& operator[](std::uint32_t id) override { return values[id]; }
    cIGZVariant* GetParameter(std::uint32_t id) override {
        auto it = values.find(id);
        return it == values.end() ? nullptr : &it->second;
    }
    void SetParameter(std::uint32_t id, const cIGZVariant& value) override { values[id].CopyFrom(value); }
    void AppendParameter(const cIGZVariant&) override {}
    void RemoveParameter(std::uint32_t id) override { values.erase(id); }
    void SetStatusParameterValue(std::int32_t value) override {
        status = value;
        values[kGZStatusParameterID].SetValSint32(value);
    }
    bool CompareParameterString(std::uint32_t, const char*, bool) override { return false; }
    bool ValidateParameter(std::uint32_t, std::uint32_t) override { return true; }
    std::uint32_t GetFirstParameterID() const override { return 0; }
    std::uint32_t GetNextParameterID(std::uint32_t) const override { return 0; }

    std::map<std::uint32_t, cRZVariant> values;
    std::int32_t status = 0;
};

// A property holder with one two-dword array property (GetEffectIDProperty's input).
class FakeProperty final : public sc4::cISCProperty {
public:
    explicit FakeProperty(const cRZVariant& value) { value_.CopyFrom(value); }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override { return --refs_; }
    std::uint32_t GetPropertyID() const override { return 0; }
    void SetPropertyID(std::uint32_t) override {}
    const cIGZVariant* GetPropertyValue() const override { return &value_; }
    cIGZVariant* GetPropertyValue() override { return &value_; }
    bool SetPropertyValue(const cIGZVariant& value) override { return value_.CopyFrom(value); }
    bool Write(cIGZOStream&) const override { return false; }
    bool Read(cIGZIStream&) override { return false; }

private:
    int refs_ = 0;
    cRZVariant value_;
};

class FakePropertyHolder final : public sc4::cISCPropertyHolder {
public:
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 0; }
    bool HasProperty(std::uint32_t id) const override { return properties.count(id) != 0; }
    bool GetPropertyList(cIGZUnknownList**) const override { return false; }
    sc4::cISCProperty* GetProperty(std::uint32_t id) const override {
        auto it = properties.find(id);
        return it == properties.end() ? nullptr : it->second;
    }
    bool GetProperty(std::uint32_t, std::uint32_t&) const override { return false; }
    bool GetProperty(std::uint32_t, cIGZString&) const override { return false; }
    bool GetProperty(std::uint32_t, std::uint32_t, void**) const override { return false; }
    bool GetProperty(std::uint32_t, void*, std::uint32_t&) const override { return false; }
    void AddProperty(sc4::cISCProperty*, bool) override {}
    void AddProperty(std::uint32_t, const cIGZVariant*, bool) override {}
    void AddProperty(std::uint32_t, std::uint32_t, bool) override {}
    void AddProperty(std::uint32_t, const cIGZString&, bool) override {}
    void AddProperty(std::uint32_t, cIGZUnknown*, bool) override {}
    void AddProperty(std::uint32_t, const void*, std::uint32_t, bool) override {}
    void CopyAddProperty(sc4::cISCProperty*, bool) override {}
    bool RemoveProperty(std::uint32_t, bool) override { return false; }
    void RemoveAllProperties(bool) override {}
    void EnumProperties(void (*)(sc4::cISCProperty*, void*), void*) const override {}
    void EnumProperties(bool (*)(sc4::cISCProperty*, void*), void (*)(sc4::cISCProperty*, void*), void*) const override {}
    void CompactProperties() override {}

    std::map<std::uint32_t, sc4::cISCProperty*> properties;
};

// ---------------------------------------------------------------------------- fake terrain host
class FakeHost final : public ITerrainHost {
public:
    struct LightRow {
        std::int32_t z, x, endX, normalStride, colourStride;
        float* normals;
        void* colours;
    };

    cIGZMessageServer2* Messages() override { return &messages; }
    // The model's `new cRZMessage2Standard`; the real class also implements every payload setter.
    cIGZMessage2Standard* NewMessage() override {
        auto* message = new cRZMessage2Standard();
        message->AddRef();
        return message;
    }
    bool WriteGZSerializable(cIGZOStream& stream, const cIGZUnknown* object) override {
        return sc4::SC4WriteGZSerializable(stream, object);
    }
    bool ReadGZSerializable(cIGZIStream& stream, std::uint32_t iid, void** out) override {
        return sc4::SC4ReadGZSerializable(stream, iid, out, false);
    }
    double RandomDoubleRangeUniform(double lo, double hi) override {
        // a deterministic stand-in for the class's seeded sRand
        randomState = randomState * 1664525u + 1013904223u;
        const double unit = static_cast<double>(randomState >> 8) / 16777216.0;
        return lo + (hi - lo) * unit;
    }
    void ApplyTerrainLightingRow(std::int32_t z, std::int32_t x, std::int32_t endX, float* normals,
                                 std::int32_t normalStride, void* colours, std::int32_t colourStride) override {
        rows.push_back({z, x, endX, normalStride, colourStride, normals, colours});
    }

    // ---- the life-cycle seams ----
    cIGZUnknown* City() override { return &city; }
    std::uint32_t CityCellCountX() override { return cellCountX; }
    std::uint32_t CityCellCountZ() override { return cellCountZ; }
    std::int32_t CityTerrainType() override { return terrainType; }
    cIGZUnknown* CityLandSimulator() override { return &landSim; }
    cIGZUnknown* CityFloraSimulator() override { return &floraSim; }
    cIGZUnknown* CityEffectsManager() override { return &effects; }
    cIGZUnknown* Simulator() override { return &simulator; }
    cIGZUnknown* SurfaceWater() override { return &surfaceWater; }
    bool SurfaceWaterInit() override { ++surfaceWaterInits; return true; }
    void RegisterWithSimulator(cSTETerrain*) override { ++simulatorRegistrations; }
    void UnregisterFromSimulator(cSTETerrain*) override { ++simulatorUnregistrations; }
    void AddToAutomataManager(cSTETerrain*) override { ++automataAdds; }
    void RemoveFromAutomataManager(cSTETerrain*) override { ++automataRemoves; }
    cIGZCommandServer* CommandServer() override { return &commands; }
    bool AddTerrainNotification(cSTETerrain*, std::uint32_t type) override {
        notifications.push_back(type);
        return true;
    }
    bool RemoveTerrainNotification(cSTETerrain*, std::uint32_t type) override {
        removedNotifications.push_back(type);
        return true;
    }
    void UpdateLandSimulator(cIGZUnknown*) override { ++landUpdates; }
    void ResetLandSimulator(cIGZUnknown*) override { ++landResets; }
    bool WorldManagerBusy() override { return worldBusy; }
    bool PreferencesFlag4ec() override { return preferencesFlag; }
    std::uint32_t ViewZoomLevel() override { return viewZoom; }
    void VisibleEdges(std::int32_t& horizontal, std::int32_t& vertical) override {
        horizontal = visibleHorizontal;
        vertical = visibleVertical;
    }
    ITerrainView* CreateTerrainView() override { return view; }
    void RegisterStaticShutdown() override { ++staticShutdownAdds; }
    void UnregisterStaticShutdown() override { ++staticShutdownRemoves; }
    cIGZPersistResourceManager* AcquireResourceManager() override { ++resourceManagerAcquires; return nullptr; }
    void ReleaseResourceManager() override { ++resourceManagerReleases; }
    void FloodFillDistance(std::vector<std::vector<std::uint8_t>>& grid, std::int32_t source,
                           std::int32_t threshold) override {
        ++floodFills;
        lastFloodSource = source;
        lastFloodThreshold = threshold;
        lastFloodGrid = grid;
    }

    FakeMessageServer messages;
    std::vector<LightRow> rows;
    std::uint32_t randomState = 12345;
    FakeService city, landSim, floraSim, effects, simulator, surfaceWater;
    FakeCommandServer commands;
    std::vector<std::uint32_t> notifications, removedNotifications;
    FakeView* view = nullptr;
    std::uint32_t cellCountX = 8, cellCountZ = 6, viewZoom = 2;
    std::int32_t terrainType = 2;
    std::int32_t visibleHorizontal = 0, visibleVertical = 1;
    int surfaceWaterInits = 0, simulatorRegistrations = 0, simulatorUnregistrations = 0;
    int automataAdds = 0, automataRemoves = 0, landUpdates = 0, landResets = 0;
    int staticShutdownAdds = 0, staticShutdownRemoves = 0;
    int resourceManagerAcquires = 0, resourceManagerReleases = 0;
    int floodFills = 0;
    std::int32_t lastFloodSource = 0, lastFloodThreshold = 0;
    std::vector<std::vector<std::uint8_t>> lastFloodGrid;
    bool worldBusy = false, preferencesFlag = false;
};


class FakeMessageTarget final : public cIGZMessageTarget2 {
public:
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 0; }
    bool DoMessage(cIGZMessage2*) override { return true; }
};

// The records the edge subfiles live in are opened on a real package file.
cGZDBSegmentPackedFile* Open(const std::string& path, bool write) {
    auto* segment = new cGZDBSegmentPackedFile();
    segment->AddRef();
    segment->Init();
    cRZString p(path.c_str());
    segment->SetPath(p);
    CHECK(segment->Open(true, write));
    return segment;
}

void Close(cGZDBSegmentPackedFile* segment) {
    segment->Close();
    segment->Shutdown();
    segment->Release();
}

}  // namespace

int main() {
    FakeHost host;
    SetTerrainHost(&host);
    CHECK(TerrainHost() == &host);

    // ------------------------------------------------------------------ COM identity + thunks
    {
        auto* object = new cSTETerrain();
        CHECK(object->GetGZCLSID() == 0xe98f9525 && object->GetGZCLSIDThunk() == 0xe98f9525);
        void* out = nullptr;
        CHECK(object->QueryInterface(1, &out) && out == object);
        CHECK(object->QueryInterface(0xab953253, &out) && out == object);
        CHECK(object->QueryInterface(0xc70bd37e, &out) && out == object);
        CHECK(object->QueryInterface(0xe4fda3d4, &out) && out == object);
        CHECK(object->QueryInterface(0x452294aa, &out) && out == object);
        CHECK(!object->QueryInterface(0x12345678, &out));
        // the five successful QueryInterface calls each added a reference
        CHECK(object->AddRef() == 6);
        CHECK(object->AddRefThunk_13fc() == 7 && object->AddRefThunk_140a() == 8);
        CHECK(object->Release() == 7 && object->Release() == 6 && object->Release() == 5);
        CHECK(object->ReleaseThunk_e0da() == 4);
        CHECK(object->Release() == 3 && object->Release() == 2 && object->Release() == 1);
        object->ReleaseThunk_e0fe();                        // last reference: destroys
    }

    // ------------------------------------------------------------------------- the grid fixture
    cSTETerrain t;
    TerrainTuning tuning;
    tuning.seaLevel = 20.0f;
    t.Init(8, 6, tuning);
    CHECK(t.CellCountX() == 8 && t.CellCountZ() == 6);
    CHECK(t.VertexCountX() == 9 && t.VertexCountZ() == 7 && t.VertexCount() == 63);
    for (std::int32_t z = 0; z < 7; ++z)
        for (std::int32_t x = 0; x < 9; ++x) t.Vertices()[t.GetVertIndex(x, z)].altitude = 10.0f * x + 2.0f * z;
    t.RecomputeWaterLevel({0, 0, 8, 6});

    // --------------------------------------------------------------------- display + messages
    CHECK(t.GetWaterSurfaceDisplayState());
    host.messages.posts.clear();
    t.SetWaterSurfaceDisplayState(true);                   // unchanged: nothing is posted
    CHECK(host.messages.posts.empty());
    t.SetWaterSurfaceDisplayState(false);
    CHECK(!t.GetWaterSurfaceDisplayState());
    CHECK(host.messages.posts.size() == 1 && host.messages.posts[0].type == 0x6ab97795 &&
          host.messages.posts[0].data[0] == 0);
    t.SetWaterSurfaceDisplayState(true);
    CHECK(host.messages.posts.size() == 2 && host.messages.posts[1].data[0] == 1);

    CHECK(!t.GetPlumbingInfoDisplayState());
    t.SetPlumbingInfoDisplayState(true);
    CHECK(t.GetPlumbingInfoDisplayState());
    t.SetPlumbingInfoDisplayState(false);

    host.messages.posts.clear();
    t.BeginLongTerrainOperation();
    t.EndLongTerrainOperation();
    CHECK(host.messages.posts.size() == 2 && host.messages.posts[0].type == 0xea3b0af1 &&
          host.messages.posts[1].type == 0x6a3b0afd);

    t.PostMessageA(0xc922eb91, 7, 8, 9, 10);
    CHECK(host.messages.posts.size() == 3 && host.messages.posts[2].type == 0xc922eb91);
    CHECK(host.messages.posts[2].data[0] == 7 && host.messages.posts[2].data[1] == 8 &&
          host.messages.posts[2].data[2] == 9 && host.messages.posts[2].data[3] == 10);

    FakeMessageTarget target;
    t.PostMessageToTarget(0x2a612643, &target);
    CHECK(host.messages.posts.size() == 4 && host.messages.posts[3].type == 0x2a612643);
    CHECK(host.messages.lastTarget == &target);
    // a missing server posts nothing
    SetTerrainHost(nullptr);
    t.BeginLongTerrainOperation();
    CHECK(host.messages.posts.size() == 4);
    SetTerrainHost(&host);

    // -------------------------------------------------------------------- accumulate flag
    CHECK(!t.IsAccumulatingTextureUpdates());
    t.StartAccumulatingTextureUpdates();
    CHECK(t.IsAccumulatingTextureUpdates());
    t.StartAccumulatingTextureUpdates();                   // idempotent
    CHECK(t.IsAccumulatingTextureUpdates());
    t.StopAccumulatingTextureUpdates();
    CHECK(!t.IsAccumulatingTextureUpdates());
    t.StopAccumulatingTextureUpdates();                    // idempotent

    // ---------------------------------------------------------------------------- view
    CHECK(t.GetView() == nullptr);
    FakeView view;
    t.SetView(&view);
    CHECK(t.GetView() == &view);
    t.SetView(nullptr);

    // ---------------------------------------------------------------------- limits and counts
    CHECK(cSTETerrain::MinAllowedHeight() == 5.0f && cSTETerrain::MaxAllowedHeight() == 200.0f);
    CHECK(cSTETerrain::GetDefaultTerrainRaiseLowerAmount() == 20.0f);
    CHECK(t.NumberOfWaterVertices() == 0);                 // +0x108, filled by the water update
    CHECK(t.NumberOfWaterVertices({0, 0, 2, 2}) == 6);     // alt < 20 m: 0,10,20? 20 is not below the sea
    CHECK(t.GetAltitudeCheckSum({0, 0, 8, 6}) == 2898.0f);
    CHECK(t.GetAltitudeCheckSum({0, 0, 9, 6}) == 0.0f);    // x1 out of bounds
    CHECK(t.GetAltitudeCheckSum({0, 4, 2, 3}) == 0.0f);    // z1 < z0
    CHECK(t.AreSeaVertices({0, 0, 1, 1}));
    CHECK(!t.AreSeaVertices({0, 0, 2, 2}));
    CHECK(!t.AreSeaVertices({0, 0, 9, 6}));                // bounds
    CHECK(t.AreSeaVertices({0, 4, 2, 3}));                 // empty range: "all sea"

    // ------------------------------------------------------------------------ vertex editing
    // LowerVertex clamps at the 5 m floor and rewrites the surface-water depth.
    t.Vertices()[t.GetVertIndex(4, 4)].altitude = 100.0f;
    t.WaterDepth()[t.GetVertIndex(4, 4)] = 0.0f;
    t.LowerVertex(static_cast<std::uint32_t>(t.GetVertIndex(4, 4)), 10.0f);
    CHECK(t.GetAltitudeAtVertex(4, 4) == 90.0f && t.GetWaterDepthAtVertex(4, 4) == -70.0f);   // negative (quirk)
    t.LowerVertex(static_cast<std::uint32_t>(t.GetVertIndex(4, 4)), 1000.0f);
    CHECK(t.GetAltitudeAtVertex(4, 4) == 5.0f && t.GetWaterDepthAtVertex(4, 4) == 15.0f);
    // RaiseVertex clamps at 200 m.
    t.RaiseVertex(static_cast<std::uint32_t>(t.GetVertIndex(4, 4)), 1000.0f);
    CHECK(t.GetAltitudeAtVertex(4, 4) == 200.0f && t.GetWaterDepthAtVertex(4, 4) == -180.0f);
    // A locked vertex ignores both.
    t.Vertices()[t.GetVertIndex(4, 4)].flags |= 0x4000;
    t.RaiseVertex(static_cast<std::uint32_t>(t.GetVertIndex(4, 4)), 5.0f);
    CHECK(t.GetAltitudeAtVertex(4, 4) == 200.0f);
    t.Vertices()[t.GetVertIndex(4, 4)].flags &= static_cast<std::uint16_t>(~0x4000);
    // SetAltitudeAtVertex picks the direction and the amount.
    t.SetAltitudeAtVertex(4, 4, 30.0f);
    CHECK(t.GetAltitudeAtVertex(4, 4) == 30.0f && t.GetWaterDepthAtVertex(4, 4) == -10.0f);
    t.SetAltitudeAtVertex(4, 4, 30.0f);                    // no change, no message
    CHECK(t.GetAltitudeAtVertex(4, 4) == 30.0f);

    // ------------------------------------------------------------------ flags and mark helpers
    t.Vertices()[t.GetVertIndex(1, 1)].flags = 0xffff;
    t.Vertices()[t.GetVertIndex(2, 2)].flags = 0xffff;
    t.ClearVertexPropertyFlags(static_cast<std::uint16_t>(0x0003));
    CHECK((t.Vertices()[t.GetVertIndex(1, 1)].flags & 0x0003) == 0);
    t.Vertices()[t.GetVertIndex(1, 1)].flags = 0xffff;
    t.ClearVertexPropertyFlags({1, 1, 1, 1}, static_cast<std::uint16_t>(0x0018));
    CHECK((t.Vertices()[t.GetVertIndex(1, 1)].flags & 0x0018) == 0);
    CHECK((t.Vertices()[t.GetVertIndex(2, 2)].flags & 0x0018) == 0x0018);

    // The cliff flip flag also clears the internal triangulation flip.
    {
        const std::size_t i = static_cast<std::size_t>(t.GetVertIndex(3, 1));
        t.Vertices()[i].flags = static_cast<std::uint16_t>(t.Vertices()[i].flags | 0x0800);
        t.FlipInternalTriangulation(3, 1, true);
        CHECK(t.IsTriangulationFlipped(3, 1));
        t.ClearVertexCliffFlipTriangulationFlags({3, 1, 3, 1});
        CHECK((t.Vertices()[i].flags & 0x0800) == 0);
        CHECK((t.Vertices()[i].flags & 0x1000) == 0);
        CHECK(!t.IsTriangulationFlipped(3, 1));
    }

    // Marks 1 and 3 mark "land above the sea level" (strictly above it).
    {
        const std::int32_t seaLevelIndex = t.GetVertIndex(2, 0);   // altitude exactly 20 m
        const std::int32_t landIndex = t.GetVertIndex(3, 0);       // 30 m
        CHECK(!t.IsLandAndNotMark1(seaLevelIndex) && t.IsLandAndNotMark1(landIndex));
        CHECK(t.IsLandAndNotMark3(landIndex));
        t.Vertices()[static_cast<std::size_t>(landIndex)].flags |= 0x0001;
        CHECK(t.IsVertexWithMark1(landIndex) && !t.IsLandAndNotMark1(landIndex));
        t.Vertices()[static_cast<std::size_t>(landIndex)].flags |= 0x0004;
        CHECK(t.IsVertexWithMark3(landIndex) && !t.IsLandAndNotMark3(landIndex));
        CHECK(!t.IsVertexWithMark1(seaLevelIndex) && !t.IsVertexWithMark3(seaLevelIndex));
        t.Vertices()[static_cast<std::size_t>(landIndex)].flags &= static_cast<std::uint16_t>(~0x0005);
    }

    // ------------------------------------------------------------------------- rect helpers
    {
        TerrainRect r{1, 1, 6, 4};
        t.PadRectangle(r, 2);
        CHECK(r.left == 0 && r.top == 0 && r.right == 8 && r.bottom == 6);
        TerrainRect q{1, 1, 2, 2};
        t.PadRectangle(q, 100);
        CHECK(q.left == 0 && q.top == 0 && q.right == 8 && q.bottom == 6);
        TerrainRect v{};
        t.CreateVertRectAroundVertex(v, 5, 3, 2);
        CHECK(v.left == 3 && v.top == 1 && v.right == 7 && v.bottom == 5);
        t.CreateVertRectAroundVertex(v, 0, 0, 9);          // clamped to the grid
        CHECK(v.left == 0 && v.top == 0 && v.right == 8 && v.bottom == 6);
    }

    // ------------------------------------------------------------- temporary water-level copy
    {
        TerrainRect r{0, 0, 3, 3};
        t.SaveWaterLevelsInTempArray(r);
        const float before = t.WaterDepth()[t.GetVertIndex(2, 2)];
        t.WaterDepth()[t.GetVertIndex(2, 2)] = 123.0f;
        t.RestoreWaterLevelsFromTempArray(r);
        CHECK(t.WaterDepth()[t.GetVertIndex(2, 2)] == before);
        t.WaterDepth()[t.GetVertIndex(7, 6)] = 55.0f;       // outside the rect: untouched
        t.RestoreWaterLevelsFromTempArray(r);
        CHECK(t.WaterDepth()[t.GetVertIndex(7, 6)] == 55.0f);
        t.WaterDepth()[t.GetVertIndex(7, 6)] = 0.0f;
    }

    // ------------------------------------------------------------------------ sediment channel
    {
        t.Sediment().assign(t.VertexCount(), 0.0f);
        t.Sediment()[static_cast<std::size_t>(t.GetVertIndex(1, 2))] = 7.5f;   // cell-centred rounding
        CHECK(t.GetSedimentValue(24.0f, 40.0f) == 7.5f);
        CHECK(t.GetSedimentValue(24.0f, 39.9f) == 7.5f);
        CHECK(t.GetSedimentValue(-1.0f, 0.0f) == 0.0f);    // out of bounds
        CHECK(t.GetSedimentValue(8.0f, 8.0f) == 0.0f);
    }

    // ---------------------------------------------------------------- lowest water neighbour
    {
        cSTETerrain small;
        small.Init(2, 2, tuning);
        small.Vertices()[4].altitude = 100.0f;             // centre
        for (int i = 0; i < 9; ++i)
            if (i != 4) small.Vertices()[static_cast<std::size_t>(i)].altitude = 100.0f;
        small.RecomputeWaterLevel({0, 0, 2, 2});
        CHECK(small.GetNeighborWithLowestWaterLevel(4) == -1);   // flat: no lower neighbour
        small.Vertices()[3].altitude = 90.0f;              // direction 0 (index - 1): 10 m lower
        CHECK(small.GetNeighborWithLowestWaterLevel(4) == 0);
        small.Vertices()[0].altitude = 80.0f;              // direction 1 (index - 4 = 1 - vertsX): 20 m lower
        CHECK(small.GetNeighborWithLowestWaterLevel(4) == 1);
        small.Vertices()[0].altitude = 89.0f;              // 11 < 10 * sqrt(2): the straight one wins
        CHECK(small.GetNeighborWithLowestWaterLevel(4) == 0);
    }

    // ---------------------------------------------------------------------- brush tuning
    {
        auto& brushes = cSTETerrain::BrushDescriptions();
        brushes.clear();
        cSTETerrain brush;
        brush.SetZoomLevel(2);
        CHECK(brush.BrushRadius(3) == 32.0f && brush.GetCostPerBrushApplication(3) == 32);
        TerrainBrushUIElements ui{};
        CHECK(!brush.GetUIElementsForBrush(3, ui));
        CHECK(brushes.empty());

        TerrainBrushDescription description;
        description.radius[2] = 48.0f;
        description.radius[3] = 64.0f;
        description.cost[2] = 12.9f;
        for (int i = 0; i < 10; ++i) description.ui.values[i] = static_cast<std::uint32_t>(100 + i);
        brushes[3] = description;
        CHECK(brush.BrushRadius(3) == 48.0f);
        CHECK(brush.GetCostPerBrushApplication(3) == 12);           // truncated to a whole number
        CHECK(brush.GetUIElementsForBrush(3, ui) && ui.values[0] == 100 && ui.values[9] == 109);
        CHECK(brush.BrushRadius(4) == 32.0f);                       // unknown brush: the defaults
        cSTETerrain::ReleaseBrushDescriptions();
        CHECK(brushes.empty());
        CHECK(!brush.GetUIElementsForBrush(3, ui));
    }
    {
        cSTETerrain brush;
        std::uint32_t exemplar = 0;
        CHECK(brush.GetBrushExemplarID(0, exemplar) && exemplar == 0xea312ef9u);
        CHECK(brush.GetBrushExemplarID(6, exemplar) && exemplar == 0x2a31304eu);
        CHECK(!brush.GetBrushExemplarID(7, exemplar) && exemplar == 0u);
        // eight phases in [0, 2*pi)
        brush.InitBrushParameters();
        for (int i = 0; i < 8; ++i) CHECK(brush.BrushPhase(i) >= 0.0f && brush.BrushPhase(i) < 6.2831856f);
    }

    // --------------------------------------------------------------------------- lighting
    {
        host.rows.clear();
        t.DoLighting({2, 1, 5, 3});
        CHECK(host.rows.size() == 3);
        for (std::size_t i = 0; i < host.rows.size(); ++i) {
            const auto& row = host.rows[i];
            const std::size_t index = static_cast<std::size_t>(t.GetVertIndex(2, 1 + static_cast<int>(i)));
            CHECK(row.z == 1 + static_cast<std::int32_t>(i) && row.x == 2);
            CHECK(row.endX == (1 - 2) + 5);
            CHECK(row.normalStride == 0x24 && row.colourStride == 0x24);
            CHECK(row.normals == &t.Vertices()[index].normal[0]);
            CHECK(row.colours == t.Vertices()[index].surface);
        }
    }

    // ---------------------------------------------------------------- surface texture layers
    {
        const std::size_t i = static_cast<std::size_t>(t.GetVertIndex(2, 2));
        for (int k = 0; k < 0x12; ++k) t.Vertices()[i].surface[k] = static_cast<std::uint8_t>(k + 1);
        t.ClearSurfaceTextures({2, 2, 2, 2});
        CHECK(t.Vertices()[i].surface[6] == 0 && t.Vertices()[i].surface[0xa] == 0 &&
              t.Vertices()[i].surface[0xe] == 0);
        CHECK(t.Vertices()[i].surface[8] == 9 && t.Vertices()[i].surface[0xc] == 13);   // weights kept
    }

    // ---------------------------------------------------------------- marked-region bounds
    {
        for (std::size_t i = 0; i < t.Vertices().size(); ++i) t.Vertices()[i].flags = 0;
        t.Vertices()[static_cast<std::size_t>(t.GetVertIndex(2, 1))].flags = 0x0004;
        t.Vertices()[static_cast<std::size_t>(t.GetVertIndex(5, 4))].flags = 0x0004;
        TerrainRect bounds{};
        t.ComputeBoundsOfMarkedRegion(bounds, 0x0004);
        CHECK(bounds.left == 2 && bounds.top == 1 && bounds.right == 5 && bounds.bottom == 4);
        t.ComputeBoundsOfMarkedRegion(bounds, 0x0100);     // nothing marked
        CHECK(bounds.left == 0x7fffffff && bounds.right == -0x7fffffff - 1);
    }

    // -------------------------------------------------------- rotated rectangle + cell spans
    {
        TerrainPoint quad[4];
        t.ComputeRectAroundPt(quad, 32.0f, 24.0f, 64.0f, 48.0f, 0.0f);
        CHECK(Near(quad[0].x, 0.0f) && Near(quad[0].z, 0.0f));
        CHECK(Near(quad[1].x, 0.0f) && Near(quad[1].z, 48.0f));
        CHECK(Near(quad[2].x, 64.0f) && Near(quad[2].z, 48.0f));
        CHECK(Near(quad[3].x, 64.0f) && Near(quad[3].z, 0.0f));
        t.ComputeRectAroundPt(quad, 0.0f, 0.0f, 32.0f, 32.0f, 1.5707964f);   // 90 degrees
        CHECK(Near(quad[0].x, 16.0f) && Near(quad[0].z, -16.0f));
        CHECK(Near(quad[2].x, -16.0f) && Near(quad[2].z, 16.0f));

        t.ComputeRectAroundPt(quad, 32.0f, 24.0f, 64.0f, 48.0f, 0.0f);
        int spanMin[260];
        int spanMax[260];
        int firstRow = 0, lastRow = 0;
        t.ComputeSpansForRectangle(quad, spanMin, spanMax, firstRow, lastRow);
        CHECK(firstRow == 0 && lastRow == 3);
        // Rows 0..2 hold the quad's two vertical edges; each span is padded one cell on the right.
        for (int row = 0; row <= 2; ++row) CHECK(spanMin[row] == 0 && spanMax[row] == 5);
        // (sic) row 3 is the far boundary: no edge is half-open across it, so the minimum stays at
        // INT_MAX and the empty maximum becomes cellsX.
        CHECK(spanMin[3] == 0x7fffffff && spanMax[3] == 8);
        // every covered vertex is sea (the fixture's plane is below 20 m only up to x = 1):
        CHECK(t.ComputeBooleanFnForRect(quad, &cSTETerrain::IsSeaVertex, 0.0f));
        CHECK(!t.ComputeBooleanFnForRect(quad, &cSTETerrain::IsSeaVertex, 1.0f));
        CHECK(t.ComputeBooleanFnForRect(quad, &cSTETerrain::IsLandVertex, 0.5f));
    }

    // ------------------------------------------------------------------ edge altitude subfiles
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "sc4_steterrain_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "city.dat").string();
    {
        auto* segment = Open(path, true);
        // Save writes the four edge subfiles itself (0x10073f17a loops over them).
        CHECK(t.Save(segment));
        for (int edge = 0; edge < 4; ++edge) CHECK(t.SaveEdgeAltitudes(edge, segment));
        // SaveObliterated clears every flag except 0x18 (its mask argument is inverted by the
        // original: flags &= ~0xffe7 == flags & 0x18) before it saves.
        const std::size_t obliterated = static_cast<std::size_t>(t.GetVertIndex(1, 1));
        t.Vertices()[obliterated].flags = 0xffff;
        t.SaveObliterated(segment);
        CHECK(t.Vertices()[obliterated].flags == 0x0018);
        Close(segment);

        segment = Open(path, false);
        std::vector<float> edge;
        CHECK(t.LoadEdgeAltitudes(0, segment, edge) && edge.size() == 7);   // west column, x = 0
        CHECK(edge[0] == 0.0f && edge[1] == 2.0f && edge[6] == 12.0f);
        CHECK(t.LoadEdgeAltitudes(1, segment, edge) && edge.size() == 9);   // north row, z = 0
        CHECK(edge[0] == 0.0f && edge[8] == 80.0f);
        CHECK(t.LoadEdgeAltitudes(2, segment, edge) && edge.size() == 7);   // east column, x = 8
        CHECK(edge[0] == 80.0f && edge[6] == 92.0f);
        CHECK(t.LoadEdgeAltitudes(3, segment, edge) && edge.size() == 9);   // south row, z = 6
        CHECK(edge[0] == 12.0f && edge[8] == 92.0f);
        cSTETerrain loaded;
        loaded.LoadObliterated(segment);
        CHECK(loaded.LastLoadSucceeded() && loaded.VertexCountX() == 9 && loaded.VertexCountZ() == 7);
        Close(segment);
    }

    // ---------------------------------------------------------------------- stream Read/Write
    const std::string streamPath = (dir / "stream.dat").string();
    {
        auto* segment = new cSC4DBSegment();
        segment->AddRef();
        segment->Init();
        cRZString p(streamPath.c_str());
        segment->SetPath(p);
        CHECK(segment->Open(true, true));
        cISC4DBSegmentOStream* out = nullptr;
        CHECK(segment->OpenOStream({0xaaaabbbb, 0x11112222, 1}, &out, true) && out != nullptr);
        t.Write(*out);
        CHECK(segment->CloseOStream(out));
        out->Release();
        CHECK(segment->Close());
        segment->Release();                                 // the destructor calls Close() again

        auto* reopen = new cSC4DBSegment();
        reopen->AddRef();
        reopen->Init();
        cRZString p2(streamPath.c_str());
        reopen->SetPath(p2);
        CHECK(reopen->Open(true, false));
        cISC4DBSegmentIStream* in = nullptr;
        CHECK(reopen->OpenIStream({0xaaaabbbb, 0x11112222, 1}, &in) && in != nullptr);
        cSTETerrain back;
        if (in) {
            CHECK(back.Read(*in));
            CHECK(reopen->CloseIStream(in));
            in->Release();
        }
        CHECK(reopen->Close());
        reopen->Release();
        CHECK(back.LastLoadSucceeded());
        CHECK(back.VertexCountX() == 9 && back.VertexCountZ() == 7);
        CHECK(back.GetAltitudeAtVertex(8, 6) == t.GetAltitudeAtVertex(8, 6));
    }
    // =================================================================================== life cycle
    cSTETerrain::Statics().seaLevel = 100.0f;      // the runtime statics are otherwise the demo defaults
    cSTETerrain::Statics().maxNormalTerrainHeight[3] = 175.0f;
    {
        // ---- StaticInit / StaticShutdown: the flag, the shutdown hook, the resource manager hold
        host.staticShutdownAdds = host.staticShutdownRemoves = 0;
        host.resourceManagerAcquires = host.resourceManagerReleases = 0;
        auto& brushes = cSTETerrain::BrushDescriptions();
        brushes.clear();
        brushes[7] = TerrainBrushDescription{};
        CHECK(cSTETerrain::StaticInit());
        CHECK(host.staticShutdownAdds == 1 && host.resourceManagerAcquires == 1);
        CHECK(cSTETerrain::StaticInit());                        // already inited: nothing happens again
        CHECK(host.staticShutdownAdds == 1 && host.resourceManagerAcquires == 1);
        bool called = false;
        CHECK(cSTETerrain::StaticShutdown(&called));
        CHECK(host.staticShutdownRemoves == 1 && host.resourceManagerReleases == 1);
        CHECK(brushes.empty());                                  // ReleaseBrushDescriptions
        CHECK(cSTETerrain::StaticShutdown(&called));             // idempotent
        CHECK(host.staticShutdownRemoves == 1);
    }
    {
        // ---- Init / Shutdown: the city sizes the grid, every service is taken and released again
        host.city.refs = host.simulator.refs = host.surfaceWater.refs = 0;
        host.landSim.refs = host.floraSim.refs = host.effects.refs = 0;
        host.notifications.clear();
        host.removedNotifications.clear();
        host.commands.registered.clear();
        host.commands.unregistered.clear();
        host.surfaceWaterInits = 0;
        host.simulatorRegistrations = host.simulatorUnregistrations = 0;
        host.automataAdds = host.automataRemoves = 0;
        host.cellCountX = 8;
        host.cellCountZ = 6;
        host.terrainType = 3;

        cSTETerrain city;
        CHECK(!city.IsInitialized());
        CHECK(city.Init());
        CHECK(city.IsInitialized());
        CHECK(city.CellCountX() == 8 && city.CellCountZ() == 6);
        CHECK(city.VertexCountX() == 9 && city.VertexCountZ() == 7 && city.VertexCount() == 63);
        CHECK(city.City() == &host.city && city.SurfaceWaterObject() == &host.surfaceWater);
        CHECK(city.CityTerrainTypeValue() == 3);
        CHECK(city.GetSeaLevel() == 100.0f);                 // copied from kSeaLevel in Init
        CHECK(city.MaxNormalTerrainHeight() == 175.0f);      // kMaxNormalTerrainHeight[3]
        CHECK(city.SurfaceCoverGrid().size() == 7 && city.SurfaceCoverGrid()[0].size() == 9);
        CHECK(host.city.refs == 1 && host.simulator.refs == 1 && host.surfaceWater.refs == 1);
        CHECK(host.landSim.refs == 1 && host.floraSim.refs == 1 && host.effects.refs == 1);
        CHECK(host.surfaceWaterInits == 1);                      // Create() itself is not recovered
        CHECK(host.simulatorRegistrations == 1 && host.automataAdds == 1);
        CHECK(host.notifications.size() == 10);
        CHECK(host.notifications[0] == 0x26d31ec1 && host.notifications[9] == 0xc4a6af6);
        CHECK(host.commands.registered.size() == 7);
        CHECK(host.commands.registered[0].id == 0xc4a6af0 &&
              host.commands.registered[0].name == "GetAltitude");
        CHECK(host.commands.registered[3].name == "SetCellAltitude");
        CHECK(host.commands.registered[6].id == 0xc4a6af6 &&
              host.commands.registered[6].name == "ApplyTerrainBrush");

        CHECK(city.Shutdown());
        CHECK(!city.IsInitialized());
        CHECK(host.city.refs == 0 && host.simulator.refs == 0 && host.surfaceWater.refs == 0);
        CHECK(host.landSim.refs == 0 && host.floraSim.refs == 0 && host.effects.refs == 0);
        CHECK(host.simulatorUnregistrations == 1 && host.automataRemoves == 1);
        CHECK(host.removedNotifications.size() == 10);
        // (sic) Shutdown removes 0xc4a6af3 (never added by Init) and leaves 0xc4a6af6 subscribed.
        CHECK(host.removedNotifications[7] == 0xc4a6af3);
        for (std::uint32_t removed : host.removedNotifications) CHECK(removed != 0xc4a6af6);
        CHECK(host.commands.unregistered.size() == 7);
        CHECK(city.Vertices().empty());                          // the vertex block is freed (+0x88)
        CHECK(city.Shutdown());                                  // idempotent
    }
    {
        // ---- DoPostCityInit: the view is created, initialised and its overlays are taken
        FakeView cityView;
        host.view = &cityView;
        for (FakeService& overlay : cityView.overlays) overlay.refs = 0;
        host.landResets = 0;
        host.viewZoom = 4;
        host.messages.posts.clear();

        cSTETerrain city;
        CHECK(city.Init());
        CHECK(!city.IsPostCityInitDone() && !city.DisplayEnabled());
        city.DoPostCityInit();
        CHECK(city.IsPostCityInitDone() && city.DisplayEnabled());
        CHECK(city.GetView() == &cityView);
        CHECK(cityView.inits == 1 && cityView.initTerrain == &city);
        CHECK(city.ZoomLevel() == 4);                            // spViewUtilities +0x14
        CHECK(host.landResets == 1);                             // the land simulator reset (+0x140 slot 0x28)
        CHECK(!cityView.dirtyRows.empty());                      // AssignSurfaceTexturesAndColors ran
        // rows 0 .. cellsZ-1 (the last cell row is cellsZ - 1, not cellsZ)
        CHECK(cityView.dirtyRows[0].first == 0 && cityView.dirtyRows[0].second == 5);
        CHECK(cityView.overlayRequests.size() == 3);
        CHECK(cityView.overlays[0].refs == 1 && cityView.overlays[1].refs == 1 && cityView.overlays[2].refs == 1);
        city.DoPostCityInit();                                   // idempotent
        CHECK(cityView.inits == 1 && cityView.overlayRequests.size() == 3);
        CHECK(city.Shutdown());
        CHECK(cityView.shutdowns == 1);
        CHECK(cityView.overlays[0].refs == 0 && cityView.overlays[1].refs == 0 && cityView.overlays[2].refs == 0);
        host.view = nullptr;

        // the city-up message runs DoPostCityInit too
        host.view = &cityView;
        cRZMessage2Standard message;
        message.AddRef();
        message.SetType(0x26d31ec1);
        cSTETerrain city2;
        CHECK(city2.Init());
        CHECK(city2.DoMessage(&message) && city2.IsPostCityInitDone());
        CHECK(cityView.inits == 2);
        CHECK(city2.Shutdown());
        host.view = nullptr;
    }
    {
        // ---- DoMessage: the scripting commands (0xc4a6af0..6)
        host.view = nullptr;
        cSTETerrain city;
        CHECK(city.Init());
        for (cSTETerrain::Vertex& vertex : city.Vertices()) vertex.altitude = 42.0f;
        city.RecomputeWaterLevel({0, 0, 8, 6});

        cRZMessage2Standard message;
        message.AddRef();                           // the DoMessage QueryInterface/Release pair
        FakeParameterSet params, result;
        message.SetVoid1(&params);
        message.SetVoid2(&result);

        // GetAltitude(32, 16) -- a flat 42 m grid
        params.SetParameter(0, cRZVariant(32.0f));
        params.SetParameter(1, cRZVariant(16.0f));
        message.SetType(0xc4a6af0);
        CHECK(city.DoMessage(&message));
        CHECK(Near(result[0].GetValFloat32(), 42.0f));
        // out of bounds sets status 2
        params.SetParameter(0, cRZVariant(-1.0f));
        CHECK(city.DoMessage(&message));
        CHECK(result.status == 2);

        // SetAltitude(32, 16, 7.5) -- (sic) the original uses the x coordinate as the row too
        params.SetParameter(0, cRZVariant(32.0f));
        params.SetParameter(1, cRZVariant(16.0f));
        params.SetParameter(2, cRZVariant(7.5f));
        message.SetType(0xc4a6af1);
        CHECK(city.DoMessage(&message));
        CHECK(city.GetAltitudeAtVertex(2, 2) == 7.5f);
        CHECK(city.GetAltitudeAtVertex(2, 1) == 42.0f);          // the z argument is ignored (quirk)

        // GetCellAltitude(2, 1) -> 42
        params.SetParameter(0, cRZVariant(static_cast<std::uint32_t>(2)));
        params.SetParameter(1, cRZVariant(static_cast<std::uint32_t>(1)));
        message.SetType(0xc4a6af2);
        CHECK(city.DoMessage(&message));
        CHECK(Near(result[0].GetValFloat32(), 42.0f));

        // SetCellAltitude(1, 1, 9) -- three parameters: one cell
        FakeParameterSet single;
        single.SetParameter(0, cRZVariant(static_cast<std::uint32_t>(1)));
        single.SetParameter(1, cRZVariant(static_cast<std::uint32_t>(1)));
        single.SetParameter(2, cRZVariant(9.0f));
        message.SetVoid1(&single);
        message.SetType(0xc4a6af3);
        CHECK(city.DoMessage(&message));
        CHECK(city.GetAltitudeAtVertex(1, 1) == 9.0f);

        // SetSeaLevel(30) then GetSeaLevel() -> 30
        result.SetStatusParameterValue(0);
        params.SetParameter(0, cRZVariant(30.0f));
        message.SetVoid1(&params);
        message.SetType(0xc4a6af5);
        CHECK(city.DoMessage(&message));
        CHECK(result.status == 0);
        CHECK(city.GetSeaLevel() == 30.0f);
        message.SetType(0xc4a6af4);
        CHECK(city.DoMessage(&message));
        CHECK(Near(result[0].GetValFloat32(), 30.0f));
        // an out-of-range sea level fails with status 2
        params.SetParameter(0, cRZVariant(100000.0f));
        message.SetType(0xc4a6af5);
        CHECK(city.DoMessage(&message));
        CHECK(result.status == 2);
        // ApplyTerrainBrush reports failure (the brush payload is not modelled yet)
        message.SetType(0xc4a6af6);
        CHECK(city.DoMessage(&message));
        CHECK(result.status == 2);

        // an unknown type is accepted and ignored
        message.SetType(0x12345678);
        CHECK(city.DoMessage(&message));
        CHECK(city.Shutdown());
    }
    {
        // ---- the zoom and erosion messages, the animation tick and the redisplay posts
        FakeView cityView;
        host.view = &cityView;
        host.messages.posts.clear();
        host.landUpdates = 0;
        host.preferencesFlag = false;
        cityView.dirtyRows.clear();

        cSTETerrain city;
        CHECK(city.Init());
        city.DoPostCityInit();
        cityView.dirtyRows.clear();
        host.landUpdates = 0;                                    // the post-city-init surface pass

        // zoom changed: data 2 points at the new zoom
        std::int32_t newZoom = 3;
        cRZMessage2Standard message;
        message.AddRef();
        message.SetType(0xa6b79602);
        message.SetVoid2(&newZoom);
        CHECK(city.DoMessage(&message));
        CHECK(city.ZoomLevel() == 3);
        CHECK(host.landUpdates == 1);                            // UpdateMoistureValues in the surface pass
        CHECK(!cityView.dirtyRows.empty());
        cityView.dirtyRows.clear();
        CHECK(city.DoMessage(&message));                         // same zoom: nothing happens
        CHECK(cityView.dirtyRows.empty() && host.landUpdates == 1);

        // erosion message (data 1 = style 1, wind only) redisplays the whole grid
        host.messages.posts.clear();
        message.SetType(0x470bd214);
        message.SetData1(1);
        CHECK(city.DoMessage(&message));
        CHECK(host.messages.posts.size() == 3);
        CHECK(host.messages.posts[0].type == 0x278e6003 && host.messages.posts[0].data[0] == 0x00000008 &&
              host.messages.posts[0].data[1] == 0x00000006);
        CHECK(host.messages.posts[1].type == 0x678e6e3a);
        CHECK(host.messages.posts[2].type == 0x870bc918);

        // animation tick: the second tick posts the city-changed message (preference off)
        host.messages.posts.clear();
        city.DoAnimationTick(100);
        CHECK(host.messages.posts.empty());
        city.DoAnimationTick(100);
        CHECK(host.messages.posts.size() == 1 && host.messages.posts[0].type == 0xca63d517);
        city.DoAnimationTick(100);                               // the arming happens only once
        CHECK(host.messages.posts.size() == 1);
        // with the preference set the second tick posts nothing (it re-posts 0x2a612643 to itself)
        cSTETerrain other;
        CHECK(other.Init());
        host.preferencesFlag = true;
        host.messages.posts.clear();
        other.DoAnimationTick(1);
        other.DoAnimationTick(1);
        CHECK(host.messages.posts.empty());
        host.preferencesFlag = false;
        CHECK(other.Shutdown());
        CHECK(city.Shutdown());
        host.view = nullptr;
    }
    {
        // ---- RedisplayTerrain(point pair): sort, widen by one cell, clamp to the grid
        FakeView cityView;
        host.view = &cityView;
        cSTETerrain city;
        CHECK(city.Init());
        city.DoPostCityInit();
        city.Vertices()[city.GetVertIndex(0, 0)].altitude = 10.0f;
        city.Vertices()[city.GetVertIndex(4, 3)].altitude = 30.0f;
        city.FindMinAndMaxHeights();
        host.messages.posts.clear();
        city.RedisplayTerrain(true, true, TerrainPoint{3.0f, 2.0f}, TerrainPoint{1.0f, 4.0f}, 5);
        CHECK(host.messages.posts.size() == 3);
        CHECK(host.messages.posts[0].type == 0x278e6003);
        CHECK(host.messages.posts[0].data[0] == ((1u << 16) | 4u));      // left 1, right 3 + 1
        CHECK(host.messages.posts[0].data[1] == ((2u << 16) | 5u));      // top 2, bottom 4 + 1
        CHECK(host.messages.posts[0].data[3] == 5);
        CHECK(host.messages.posts[1].type == 0x678e6e3a);
        CHECK(host.messages.posts[2].type == 0x870bc918);
        // the altitude range: the minimum in the high half and the maximum in the low one (sic)
        const std::uint32_t packed = static_cast<std::uint32_t>(host.messages.posts[2].data[2]);
        CHECK((packed >> 16) == 0u);                              // lo = 0
        CHECK((packed & 0xffffu) == 100u);                        // hi = the view's +0x94
        // a view top above kMaxTerrainHeight is clamped to it
        cityView.maxAltitude = 500.0f;
        host.messages.posts.clear();
        city.RedisplayTerrain(true, true, TerrainPoint{3.0f, 2.0f}, TerrainPoint{1.0f, 4.0f}, 0);
        CHECK(host.messages.posts.size() == 3);
        const std::uint32_t clamped = static_cast<std::uint32_t>(host.messages.posts[2].data[2]);
        CHECK((clamped & 0xffffu) == 200u);                       // kMaxTerrainHeight
        CHECK((clamped >> 16) == 0u);
        CHECK(city.Shutdown());
        host.view = nullptr;
    }
    {
        // ---- SetAltitude(rect), EnsureMinAllowedHeight and ChangeTerrainLevel
        FakeView cityView;
        host.view = &cityView;
        host.messages.posts.clear();
        cSTETerrain city;
        TerrainTuning tuning2;
        tuning2.seaLevel = 0.0f;
        city.Init(4, 4, tuning2);
        city.SetDisplayEnabled(true);
        city.SetView(&cityView);
        city.SetAltitude(TerrainRect{1, 1, 2, 2}, 12.0f, true, 3);
        for (int z = 1; z <= 2; ++z)
            for (int x = 1; x <= 2; ++x) CHECK(city.GetAltitudeAtVertex(x, z) == 12.0f);
        CHECK(city.GetAltitudeAtVertex(0, 0) == 0.0f);
        CHECK(host.messages.posts.size() == 3);                  // the redisplay of the rect
        CHECK(host.messages.posts[0].data[3] == 3);

        // EnsureMinAllowedHeight raises everything to the 5 m floor when the minimum is below it
        for (std::size_t i = 0; i < city.Vertices().size(); ++i)
            city.Vertices()[i].altitude = static_cast<float>(i % 4);
        city.FindMinAndMaxHeights();
        CHECK(city.MinHeight() == 0.0f);
        city.EnsureMinAllowedHeight();
        CHECK(city.MinHeight() == 5.0f);
        for (const cSTETerrain::Vertex& vertex : city.Vertices()) CHECK(vertex.altitude >= 5.0f);

        // ChangeTerrainLevel(raise, 20) raises the whole grid and redisplays it
        host.messages.posts.clear();
        const float before = city.GetAltitudeAtVertex(2, 2);
        city.ChangeTerrainLevel(true, 20.0f);
        CHECK(city.GetAltitudeAtVertex(2, 2) == before + 20.0f);
        CHECK(host.messages.posts.size() == 3);
        city.ChangeTerrainLevel(false, 1000.0f);                 // clamps at the 5 m floor
        CHECK(city.GetAltitudeAtVertex(2, 2) == 5.0f);
        CHECK(city.Shutdown());
    }
    {
        // ---- water proximity grid, the 10 s sweep and the land-type ratios
        cSTETerrain city;
        TerrainTuning tuning2;
        tuning2.seaLevel = 50.0f;
        city.Init(16, 16, tuning2);
        for (std::int32_t z = 0; z < 17; ++z)
            for (std::int32_t x = 0; x < 17; ++x)
                city.Vertices()[static_cast<std::size_t>(city.GetVertIndex(x, z))].altitude =
                    (z == 0) ? 10.0f : 60.0f;                    // the first row is under the sea
        city.RecomputeWaterLevel({0, 0, 16, 16});
        host.floodFills = 0;
        city.ComputeWaterProximityValues({0, 0, 16, 16});
        CHECK(host.floodFills == 1);                             // 16 x 16 > 99 cells
        CHECK(host.lastFloodSource == 0);                        // water exists
        CHECK(host.lastFloodThreshold == 5);                     // kMaxBeachWidth + 1
        CHECK(host.lastFloodGrid.size() == 17 && host.lastFloodGrid[0].size() == 17);
        CHECK(host.lastFloodGrid[0][0] == 0);                    // wet
        CHECK(host.lastFloodGrid[1][0] == 6);                    // dry: kMaxBeachWidth + 2
        CHECK(city.NumberOfWaterVertices() == 17);
        // a small rect only accumulates into the empty rect
        host.floodFills = 0;
        city.ComputeWaterProximityValues({1, 1, 2, 2});
        CHECK(host.floodFills == 0);
        // the 10 s animation sweep calls it again
        host.floodFills = 0;
        city.ComputeWaterProximityValues({1, 1, 2, 2});
        city.DoAnimationTick(10000000);
        CHECK(host.floodFills == 1);

        // land type ratios: three weights above 250 are scaled down
        cSTETerrain::Vertex& vertex = city.Vertices()[static_cast<std::size_t>(city.GetVertIndex(4, 4))];
        for (int slot = 0; slot < 3; ++slot) {
            const std::uint16_t key = static_cast<std::uint16_t>(100 + slot);
            const std::uint16_t weight = 200;
            std::memcpy(&vertex.surface[6 + slot * 4], &key, sizeof key);
            std::memcpy(&vertex.surface[8 + slot * 4], &weight, sizeof weight);
        }
        city.AdjustLandTypeRatios({4, 4, 4, 4});
        for (int slot = 0; slot < 3; ++slot) {
            std::uint16_t weight = 0;
            std::memcpy(&weight, &vertex.surface[8 + slot * 4], sizeof weight);
            CHECK(weight == 83);                                 // 200 * 250 / 600
        }

        // AddLandTypeAtVertex: empty slot, matching key (keeps the larger weight), full (drops the smallest)
        cSTETerrain::Vertex& fresh = city.Vertices()[static_cast<std::size_t>(city.GetVertIndex(5, 5))];
        for (int k = 0; k < 0x12; ++k) fresh.surface[k] = 0;
        city.AddLandTypeAtVertex(static_cast<std::uint32_t>(city.GetVertIndex(5, 5)), 7, 0.5f);
        city.AddLandTypeAtVertex(static_cast<std::uint32_t>(city.GetVertIndex(5, 5)), 8, 0.25f);
        city.AddLandTypeAtVertex(static_cast<std::uint32_t>(city.GetVertIndex(5, 5)), 9, 0.75f);
        city.AddLandTypeAtVertex(static_cast<std::uint32_t>(city.GetVertIndex(5, 5)), 7, 0.9f);
        std::uint16_t keys[3], weights[3];
        for (int slot = 0; slot < 3; ++slot) {
            std::memcpy(&keys[slot], &fresh.surface[6 + slot * 4], sizeof(std::uint16_t));
            std::memcpy(&weights[slot], &fresh.surface[8 + slot * 4], sizeof(std::uint16_t));
        }
        CHECK(keys[0] == 7 && weights[0] == static_cast<std::uint16_t>(0.9f * 255.0f + 1.0f));
        CHECK(keys[1] == 8 && weights[1] == static_cast<std::uint16_t>(0.25f * 255.0f + 1.0f));
        CHECK(keys[2] == 9 && weights[2] == static_cast<std::uint16_t>(0.75f * 255.0f + 1.0f));
        city.AddLandTypeAtVertex(static_cast<std::uint32_t>(city.GetVertIndex(5, 5)), 10, 1.0f);
        std::memcpy(&keys[1], &fresh.surface[6 + 4], sizeof(std::uint16_t));
        CHECK(keys[1] == 10);                                    // the 0.25 slot was dropped
        CHECK(city.Shutdown());
    }
    {
        // ---- the shore helpers: IsShoreVertex skips the south neighbour (sic), GetNeighboringSeaVertex
        //      divides the row by the vertex count in Z (sic)
        cSTETerrain city;
        TerrainTuning tuning2;
        tuning2.seaLevel = 20.0f;
        city.Init(2, 2, tuning2);
        for (cSTETerrain::Vertex& vertex : city.Vertices()) vertex.altitude = 100.0f;
        // (1,0) is land; its only sea neighbour is to the south (direction 6): not seen
        city.Vertices()[static_cast<std::size_t>(city.GetVertIndex(1, 0))].altitude = 30.0f;
        city.Vertices()[static_cast<std::size_t>(city.GetVertIndex(1, 1))].altitude = 10.0f;
        CHECK(!city.IsShoreVertex(city.GetVertIndex(1, 0)));
        // the same sea neighbour to the east (direction 4) is seen
        city.Vertices()[static_cast<std::size_t>(city.GetVertIndex(1, 1))].altitude = 100.0f;
        city.Vertices()[static_cast<std::size_t>(city.GetVertIndex(2, 0))].altitude = 10.0f;
        CHECK(city.IsShoreVertex(city.GetVertIndex(1, 0)));
        // the vertex itself must be land
        city.Vertices()[static_cast<std::size_t>(city.GetVertIndex(1, 0))].altitude = 20.0f;
        CHECK(!city.IsShoreVertex(city.GetVertIndex(1, 0)));

        // GetNeighboringSeaVertex on a 2 x 4 cell grid (9 x 5 vertices): index 7 is (1, 1)
        cSTETerrain tall;
        tall.Init(2, 4, tuning2);
        for (cSTETerrain::Vertex& vertex : tall.Vertices()) vertex.altitude = 100.0f;
        tall.Vertices()[static_cast<std::size_t>(tall.GetVertIndex(0, 0))].altitude = 5.0f;   // north-west
        CHECK(tall.GetNeighboringSeaVertex(7) == tall.GetVertIndex(0, 0));
        tall.Vertices()[static_cast<std::size_t>(tall.GetVertIndex(0, 0))].altitude = 100.0f;
        tall.Vertices()[static_cast<std::size_t>(tall.GetVertIndex(2, 0))].altitude = 5.0f;   // north-east
        CHECK(tall.GetNeighboringSeaVertex(7) == tall.GetVertIndex(2, 0));
        tall.Vertices()[static_cast<std::size_t>(tall.GetVertIndex(2, 0))].altitude = 100.0f;
        CHECK(tall.GetNeighboringSeaVertex(7) == -1);
        // the quirk: the row is (index / vertsZ) = 7 / 5 = 1, not 7 / 3 = 2 (which is out of the grid)
        tall.Vertices()[static_cast<std::size_t>(tall.GetVertIndex(0, 1))].altitude = 5.0f;   // west
        CHECK(tall.GetNeighboringSeaVertex(7) == tall.GetVertIndex(0, 1));
    }
    {
        // ---- GetEffectIDProperty: the two-dword property, anything else fails
        cSTETerrain city;
        const std::uint32_t effect[2] = {0x12345678u, 0x9abcdef0u};
        cRZVariant effectValue;
        effectValue.RefUint32(effect, 2);
        FakeProperty effectProperty(effectValue);
        const std::uint32_t triple[3] = {1u, 2u, 3u};
        cRZVariant tripleValue;
        tripleValue.RefUint32(triple, 3);
        FakeProperty tripleProperty(tripleValue);
        FakePropertyHolder holder;
        holder.properties[0x42u] = &effectProperty;
        holder.properties[0x44u] = &tripleProperty;
        std::uint32_t out[2] = {0, 0};
        CHECK(cSTETerrain::GetEffectIDProperty(&holder, 0x42u, out));
        CHECK(out[0] == 0x12345678u && out[1] == 0x9abcdef0u);
        CHECK(!cSTETerrain::GetEffectIDProperty(&holder, 0x43u, out));       // missing property
        CHECK(!cSTETerrain::GetEffectIDProperty(&holder, 0x44u, out));       // not two dwords
        CHECK(!cSTETerrain::GetEffectIDProperty(nullptr, 0x42u, out));       // no holder
    }
    {
        // ---- StopAccumulatingTextureUpdates re-textures and redisplays the accumulated rectangle
        FakeView cityView;
        host.view = &cityView;
        host.messages.posts.clear();
        host.landUpdates = 0;
        cSTETerrain city;
        CHECK(city.Init());
        city.DoPostCityInit();
        cityView.dirtyRows.clear();
        host.messages.posts.clear();
        city.StartAccumulatingTextureUpdates();
        CHECK(city.IsAccumulatingTextureUpdates());
        city.RedisplayTerrain(true, true, TerrainRect{1, 1, 2, 2}, 0);   // accumulates only
        CHECK(host.messages.posts.size() == 3);                            // the redisplay posts still run
        cityView.dirtyRows.clear();
        host.messages.posts.clear();
        city.StopAccumulatingTextureUpdates();
        CHECK(!city.IsAccumulatingTextureUpdates());
        CHECK(!cityView.dirtyRows.empty());                                // AssignSurfaceTexturesAndColors
        CHECK(host.messages.posts.size() == 1);                            // only the altitude-range post
        // an empty accumulation rectangle does nothing
        city.StartAccumulatingTextureUpdates();
        cityView.dirtyRows.clear();
        city.StopAccumulatingTextureUpdates();
        CHECK(cityView.dirtyRows.empty());
        CHECK(city.Shutdown());
        host.view = nullptr;
    }

    std::filesystem::remove_all(dir);
    SetTerrainHost(nullptr);

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::puts("sc4_steterrain ok");
    return 0;
}
