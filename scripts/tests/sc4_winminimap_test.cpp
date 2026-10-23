// cSC4WinMiniMap test: a fake minimap host (city, terrain, zone grid, occupant manager, renderer, 3D window, tick
// manager, tuning ramps, line drawing), the real cGZBuffer for the map and overlay buffers, the real message class
// and the fake window manager / draw context.
//
// Covers the constructor's field set-up, the three-interface QueryInterface, SetArea's power-of-two map size, the
// zone colour quirks (entry 0 transparent, alpha forced), SetFlags' bit arithmetic, Init (buffer creation / fill /
// notifications / zone colours / tuning ramps), InitTZMap's zoom shift and dirty mask, the click-to-jump
// (CenterOnMapPoint for every camera rotation, the mouse drag / capture path), the message dispatch (tick, the
// terrain rectangles, the camera notifications, the tuning reload, city init / shutdown, occupant insert / remove),
// the terrain + water + zone picture build with every image op of the 0x100ad12e0 table, the occupant pass, the
// overlay replacement and composite paths, the viewport-rectangle projection, the situation marker and GZPaint's
// ARGB4444 dither and blit paths.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fake_drawcontext.h"
#include "fake_winmgr.h"
#include "rz/cRZMessage2Standard.h"
#include "s3d/Camera.h"
#include "sc4/WinMiniMap.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {

constexpr std::uint32_t kGrey = 0xff808080;     // the terrain ramp used by the tests
constexpr std::uint32_t kWater = 0x800000ff;    // the water ramp: half-transparent blue

// ---------------------------------------------------------------------------------------------------------------
// Fakes
// ---------------------------------------------------------------------------------------------------------------
struct FakeRamp final : IMiniMapRamp {
    std::vector<std::uint32_t> data{0xff808080};
    const std::uint32_t* Data() override { return data.data(); }
    int Count() override { return static_cast<int>(data.size()); }
};

struct FakeTerrain final : IMiniMapTerrain {
    int cellsX = 256, cellsZ = 256;
    float sea = 0.0f;
    float defaultAltitude = 0.0f, defaultDepth = 0.0f;
    cS3DVector3 normal{0.0f, 1.0f, 0.0f};
    int altitudeCalls = 0, normalCalls = 0;
    int CellCountX() override { return cellsX; }
    int CellCountZ() override { return cellsZ; }
    float SeaLevel() override { return sea; }
    float Altitude(float, float) override { return defaultAltitude; }
    void Altitudes(const cRZRect&, float* out, long rowStride, long colStride) override {
        ++altitudeCalls;
        for (long row = 0; row < 16; ++row)
            for (long col = 0; col < 16; ++col) out[row * rowStride + col * colStride] = defaultAltitude;
    }
    void WaterDepths(const cRZRect&, float* out, long rowStride, long colStride) override {
        for (long row = 0; row < 16; ++row)
            for (long col = 0; col < 16; ++col) out[row * rowStride + col * colStride] = defaultDepth;
    }
    void VertexNormal(int, int, cS3DVector3& out) override {
        ++normalCalls;
        out = normal;
    }
};

struct FakeZoneGrid final : IMiniMapZoneGrid {
    int zone = 0;
    int queries = 0;
    int ZoneAt(int, int) override {
        ++queries;
        return zone;
    }
};

struct FakeSituation final : IMiniMapSituation {
    bool active = false;
    bool hasTarget = false;
    float position[3] = {0.0f, 0.0f, 0.0f};
    bool HasActiveSituation() override { return active; }
    bool CurrentTargetPosition(float xyz[3]) override {
        if (!hasTarget) return false;
        std::memcpy(xyz, position, sizeof(position));
        return true;
    }
};

struct FakeCameraControl final : IMiniMapCameraControl {
    int rotation = 0;
    int sets = 0;
    cS3DVector3 target;
    int Rotation() const override { return rotation; }
    bool SetViewTargetPosition(const cS3DVector3& position) override {
        ++sets;
        target = position;
        return true;
    }
};

struct FakeRenderer final : IMiniMapRenderer {
    FakeCameraControl* control = nullptr;
    s3d::cS3DCamera* camera = nullptr;
    std::vector<cS3DVector3> picks;
    std::size_t pickIndex = 0;
    int pickCalls = 0;
    IMiniMapCameraControl* CameraControl() override { return control; }
    s3d::cS3DCamera* Camera() override { return camera; }
    bool PickTerrainAlways(const float*, const float*, float* out, bool) override {
        ++pickCalls;
        if (pickIndex < picks.size()) {
            out[0] = picks[pickIndex].x;
            out[1] = picks[pickIndex].y;
            out[2] = picks[pickIndex].z;
            ++pickIndex;
        } else {
            out[0] = out[1] = out[2] = 0.0f;
        }
        return true;
    }
};

struct FakeView3D final : IMiniMapView3D {
    int width = 100, height = 80;
    int Width() override { return width; }
    int Height() override { return height; }
};

struct FakeMessageServer2 final : cIGZMessageServer2 {
    std::vector<std::pair<cIGZMessageTarget2*, std::uint32_t>> added, removed;
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool MessageSend(cIGZMessage2*) override { return true; }
    bool MessagePost(cIGZMessage2*, bool) override { return true; }
    bool AddNotification(cIGZMessageTarget2* target, std::uint32_t type) override {
        added.emplace_back(target, type);
        return true;
    }
    bool RemoveNotification(cIGZMessageTarget2* target, std::uint32_t type) override {
        removed.emplace_back(target, type);
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

struct FakeDrawBuffer final : IWinBuffer {
    cRZRect bounds{0, 0, 16, 16};
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    const cRZRect* GetBounds() const override { return &bounds; }
    std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override {
        return 0xff000000u | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
    }
    void NativeToRGB(std::uint32_t native, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override {
        r = static_cast<std::uint8_t>(native >> 16);
        g = static_cast<std::uint8_t>(native >> 8);
        b = static_cast<std::uint8_t>(native);
    }
    void* GetDrawContext() override { return nullptr; }
    bool SetFadeAlpha(std::uint8_t) override { return true; }
};

struct LineCall {
    IWinBuffer* target;
    int x0, y0, x1, y1;
    std::uint32_t color;
};

struct Counted final : cIGZUnknown {
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
};

struct FakeHost;

struct FakeOccupantQuery final : IMiniMapOccupantQuery {
    FakeHost* host = nullptr;
    int queries = 0;
    bool released = false;
    void Release() override { released = true; }
    bool GetFirstOccupant(int x, int z, cIGZUnknown*& out) override;
};

struct FakeHost final : IMiniMapHost {
    FakeMessageServer2 server;
    FakeTerrain terrain;
    FakeZoneGrid zoneGrid;
    FakeSituation situation;
    FakeRenderer renderer;
    FakeView3D view3d;
    FakeCameraControl cameraControl;
    FakeRamp terrainRamp, waterRamp;
    FakeOccupantQuery query;
    std::vector<LineCall> lines;
    std::vector<cIGZBuffer*> created;
    int tickAdds = 0, tickRemoves = 0;
    std::uint32_t rampsRequested = 0;
    int cityCellX = 256, cityCellZ = 256;
    float citySizeX = 256.0f * 16.0f, citySizeZ = 256.0f * 16.0f;
    bool bufferCreateFails = false;
    int occupantX = -1, occupantZ = -1;
    int occupantCalls = 0;
    bool networkCellOk = true;
    int networkCellX = 0, networkCellZ = 0;
    Counted occupant;
    s3d::cS3DCamera camera;
    int rampCalls = 0;

    FakeHost() {
        renderer.control = &cameraControl;
        renderer.camera = &camera;
        query.host = this;
    }

    cIGZMessageServer2* MessageServer() override { return &server; }
    cIGZBuffer* CreateBuffer() override {
        if (bufferCreateFails) return nullptr;
        auto* buffer = new cGZBuffer();
        buffer->AddRef();   // the graphics system hands out a counted buffer
        created.push_back(buffer);
        return buffer;
    }
    int CityCellCountX() override { return cityCellX; }
    int CityCellCountZ() override { return cityCellZ; }
    float CitySizeX() override { return citySizeX; }
    float CitySizeZ() override { return citySizeZ; }
    IMiniMapSituation* CitySituationManager() override { return &situation; }
    IMiniMapTerrain* Terrain() override { return &terrain; }
    IMiniMapZoneGrid* ZoneGrid() override { return &zoneGrid; }
    IMiniMapOccupantQuery* CreateSurfaceNetworkOccupantQuery() override {
        query.queries = 0;
        return &query;
    }
    void InterpolateRamp(std::uint32_t* out, int count, const std::uint32_t* stops, int stopCount) override {
        ++rampCalls;
        for (int i = 0; i < count; ++i) {
            if (stopCount <= 0) { out[i] = 0; continue; }
            const int stop = stopCount == 1 ? 0 : i * stopCount / count;   // nearest stop (the fake's stand-in)
            out[i] = stops[stop];
        }
    }
    IMiniMapRenderer* Renderer() override { return &renderer; }
    IMiniMapView3D* View3DWindow() override { return &view3d; }
    void AddTickNotification(cIGZMessageTarget2*) override { ++tickAdds; }
    void RemoveTickNotification(cIGZMessageTarget2*) override { ++tickRemoves; }
    IMiniMapRamp* FindRamp(std::uint32_t propertyId) override {
        if (propertyId == kMiniMapTerrainRampProperty) {
            rampsRequested |= 1u;
            return &terrainRamp;
        }
        if (propertyId == kMiniMapWaterRampProperty) {
            rampsRequested |= 2u;
            return &waterRamp;
        }
        return nullptr;
    }
    bool NetworkOccupantCell(cIGZUnknown*, int& cellX, int& cellZ) override {
        if (!networkCellOk) return false;
        cellX = networkCellX;
        cellZ = networkCellZ;
        return true;
    }
    bool DrawLineAntialiased(IWinBuffer* target, int x0, int y0, int x1, int y1, std::uint32_t color) override {
        lines.push_back({target, x0, y0, x1, y1, color});
        return true;
    }
};

bool FakeOccupantQuery::GetFirstOccupant(int x, int z, cIGZUnknown*& out) {
    ++queries;
    ++host->occupantCalls;
    if (x != host->occupantX || z != host->occupantZ) return false;
    host->occupant.AddRef();
    out = &host->occupant;
    return true;
}

// A window that exposes the protected draw state the paint path uses.
struct TestWindow final : cSC4WinMiniMap {
    using cSC4WinMiniMap::cSC4WinMiniMap;
    void SetDrawState(IWinBuffer* buffer, gz::cIGZDrawContext* context) {
        drawBuffer_ = buffer;
        drawContext_ = context;
    }
};

std::uint32_t MapPixel(cSC4WinMiniMap& window, int x, int y) {
    return window.Map().pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(window.Map().width) +
                               static_cast<std::size_t>(x)];
}

std::uint32_t BufferPixel(cIGZBuffer* buffer, int x, int y) {
    const auto* bits = static_cast<const std::uint32_t*>(buffer->GetColorSurfaceBits());
    const std::uint32_t stride = buffer->GetColorSurfaceStride() >> 2;
    return bits[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x)];
}

// The block value for a flat grey (0x808080) terrain under shade `shade` (UpdateTerrainAndZoneMap's arithmetic).
std::uint32_t GreyShaded(int shade) {
    const std::uint32_t v = ((0x80u * static_cast<std::uint32_t>(shade) + 0x80u) >> 8) & 0xffu;
    return (v << 16) | (v << 8) | v;
}

// The dither table GZPaint inlines (0x100a25290).
constexpr std::uint32_t kDither[16] = {0x0000000, 0x8000000, 0x2000000, 0xa000000, 0xc000000, 0x4000000,
                                       0xe000000, 0x6000000, 0x3000000, 0xb000000, 0x1000000, 0x9000000,
                                       0xf000000, 0x7000000, 0xd000000, 0x5000000};

std::uint16_t DitherExpected(std::uint32_t pixel, unsigned width, unsigned height, unsigned col) {
    const std::uint32_t dither = kDither[(height & 3u) * 4u + ((width - col) & 3u)];
    const std::uint32_t r = ((((pixel >> 16) & 0xff) * 0xf1f1f1u + dither) >> 20) & 0xfu;
    const std::uint32_t g = ((((pixel >> 8) & 0xff) * 0xf1f1f1u + dither) >> 24) & 0xf0u;
    const std::uint32_t b = (((pixel & 0xff) * 0xf1f1f1u + dither) >> 28) & 0xfu;
    return static_cast<std::uint16_t>(0xf000u | (r << 8) | g | b);
}

struct Fixture {
    FakeMgr mgr;
    FakeHost host;
    FakeDrawBuffer drawBuffer;
    TestWindow* window = nullptr;

    explicit Fixture(int size = 64, int cityCellsX = 256, int cityCellsZ = 256) {
        SetMiniMapHost(&host);
        host.cityCellX = cityCellsX;
        host.cityCellZ = cityCellsZ;
        host.citySizeX = static_cast<float>(cityCellsX) * 16.0f;
        host.citySizeZ = static_cast<float>(cityCellsZ) * 16.0f;
        host.terrain.cellsX = cityCellsX;
        host.terrain.cellsZ = cityCellsZ;
        host.terrainRamp.data = {kGrey};
        host.waterRamp.data = {0x00000000};   // no water overlay by default
        window = new TestWindow(&mgr);
        window->SetArea(0, 0, size, size);
    }
    ~Fixture() {
        delete window;
        SetMiniMapHost(nullptr);
    }
    bool Init() { return window->Init(); }
    void Build() { window->Update(); }
};

const std::uint32_t kDefaultBlockPixel = GreyShaded(201);   // normal (0, 1, 0): |s|+s+127.75 = 201.36

}  // namespace

int main() {
    // -----------------------------------------------------------------------------------------------------------
    // Construction and the interfaces
    // -----------------------------------------------------------------------------------------------------------
    {
        FakeMgr mgr;
        auto* window = new cSC4WinMiniMap(&mgr);
        CHECK(window->Flags() == 7);
        CHECK(window->MapSize() == 0);
        CHECK(window->MapBuffer() == nullptr);
        CHECK(window->OverlayBuffer() == nullptr);
        CHECK(!window->CityInitialised());
        CHECK(!window->Dragging());
        CHECK(!window->OccupantMessagesHooked());
        CHECK(window->ZoneColor(0) == 0);
        CHECK(window->ZoneColor(15) == 0);
        CHECK(window->AddRef() == 1);
        CHECK(window->Release() == 0);   // cGZWin::Release deletes the window at zero (do not delete it again)
        window = nullptr;
    }
    {
        Fixture f;
        auto* base = static_cast<cIGZWin*>(f.window);
        void* out = nullptr;
        CHECK(f.window->QueryInterface(kMiniMapIID, &out));
        CHECK(out == static_cast<cISC4WinMiniMap*>(f.window));
        CHECK(f.window->AsIGZWin() == base);
        CHECK(f.window->QueryInterface(0x22ba0121, &out) && out == base);
        CHECK(f.window->QueryInterface(0xe98b2f57, &out) && out == base);
        CHECK(f.window->QueryInterface(1, &out) && out == base);
        out = reinterpret_cast<void*>(0x1);
        CHECK(!f.window->QueryInterface(0x12345678, &out));
        CHECK(out == reinterpret_cast<void*>(0x1));   // a failed query writes nothing
    }

    // -----------------------------------------------------------------------------------------------------------
    // SetArea: the largest power of two <= min(width, height)
    // -----------------------------------------------------------------------------------------------------------
    {
        Fixture f(64);
        CHECK(f.window->MapSize() == 64);
        f.window->SetArea(0, 0, 100, 80);
        CHECK(f.window->MapSize() == 64);
        f.window->SetArea(0, 0, 63, 40);
        CHECK(f.window->MapSize() == 32);
        f.window->SetArea(0, 0, 128, 128);
        CHECK(f.window->MapSize() == 128);
        f.window->SetArea(0, 0, 128, 100);   // min(128, 100) = 100 -> 64
        CHECK(f.window->MapSize() == 64);
        f.window->SetArea(0, 0, 1, 1);
        CHECK(f.window->MapSize() == 1);
        f.window->SetArea(0, 0, 0, 0);
        CHECK(f.window->MapSize() == 0);
    }

    // -----------------------------------------------------------------------------------------------------------
    // Zone colours: alpha forced, 0 stays transparent, entry 0 of the default set transparent
    // -----------------------------------------------------------------------------------------------------------
    {
        Fixture f;
        f.window->SetZoneColor(3, 0x112233);
        CHECK(f.window->ZoneColor(3) == 0xff112233u);
        f.window->SetZoneColor(3, 0);
        CHECK(f.window->ZoneColor(3) == 0);
        f.window->SetZoneColor(16, 0x445566);   // out of range: ignored
        f.window->SetDefaultZoneColor(0x00a0b0);
        CHECK(f.window->ZoneColor(0) == 0);
        CHECK(f.window->ZoneColor(1) == 0xff00a0b0u);
        CHECK(f.window->ZoneColor(15) == 0xff00a0b0u);
        f.window->SetDefaultZoneColorToTrans();
        for (int i = 0; i < 16; ++i) CHECK(f.window->ZoneColor(i) == 0);
    }

    // -----------------------------------------------------------------------------------------------------------
    // SetFlags: flags = (~mask & flags) ^ value, and the dirty mask is reset
    // -----------------------------------------------------------------------------------------------------------
    {
        Fixture f;
        CHECK(f.window->SetFlags(0x2, 0x8) == 13);        // (0xfffffffd & 7) ^ 8 = 5 ^ 8 = 13
        CHECK(f.window->Flags() == 13);
        CHECK(f.window->DirtyBlock(0) == 0xffffffffu);
        CHECK(f.window->SetFlags(0x0, 0x7) == 10);        // 13 ^ 7 = 10
        CHECK(f.window->Flags() == 10);
        CHECK(f.window->SetFlags(0xffffffff, 0x7) == 7);  // every bit masked: the value replaces the flags
    }

    // -----------------------------------------------------------------------------------------------------------
    // Mouse: the left button captures, moves while dragging recentre, release ends the drag
    // -----------------------------------------------------------------------------------------------------------
    {
        Fixture f(64, 16, 16);
        CHECK(f.Init());
        CHECK(!f.window->Dragging());
        CHECK(f.window->GZOnMouseMove(10, 10, 0));         // not dragging: nothing happens
        CHECK(f.host.cameraControl.sets == 0);
        CHECK(f.window->GZOnMouseDownL(32, 32, 0));
        CHECK(f.window->Dragging());
        CHECK(f.mgr.capture == f.window);
        CHECK(f.host.cameraControl.sets == 1);
        CHECK(std::fabs(f.host.cameraControl.target.x - f.host.citySizeX * 0.5f) < 0.5f);
        CHECK(std::fabs(f.host.cameraControl.target.z - f.host.citySizeZ * 0.5f) < 0.5f);
        CHECK(f.window->GZOnMouseMove(0, 0, 0));
        CHECK(f.host.cameraControl.sets == 2);
        CHECK(f.host.cameraControl.target.x == 0.0f);   // clamped to the top-left corner
        CHECK(std::fabs(f.host.cameraControl.target.z - f.host.citySizeZ) < 0.5f);
        CHECK(f.window->GZOnMouseUpL(0, 0, 0));
        CHECK(!f.window->Dragging());
        CHECK(f.mgr.capture == nullptr);
        CHECK(f.window->GZOnMouseDownR(1, 2, 3));
        CHECK(!f.window->Dragging());
        // Enter / exit clear the drag flag.
        CHECK(f.window->GZOnMouseDownL(4, 4, 0));
        CHECK(f.window->Dragging());
        CHECK(f.window->GZOnMouseExit(nullptr));
        CHECK(!f.window->Dragging());
        CHECK(f.window->GZOnMouseDownL(4, 4, 0));
        CHECK(f.window->GZOnMouseEnter(nullptr));
        CHECK(!f.window->Dragging());
    }

    // CenterOnMapPoint for the four camera rotations (64x64 window, city 16 cells = 256 world units).
    {
        for (int rotation = 0; rotation < 4; ++rotation) {
            Fixture f(64, 16, 16);
            CHECK(f.Init());
            f.host.cameraControl.rotation = rotation;
            f.window->GZOnMouseDownL(48, 8, 0);   // normalised (0.25, -0.375)
            float ex = 0.0f, ez = 0.0f;
            switch (rotation) {
                case 0: ex = 0.75f; ez = 0.875f; break;
                case 1: ex = 0.875f; ez = 0.75f; break;
                case 2: ex = 0.25f; ez = 0.125f; break;
                default: ex = 0.125f; ez = 0.25f; break;
            }
            const float size = f.host.citySizeX;
            CHECK(std::fabs(f.host.cameraControl.target.x - ex * size) < 0.5f);
            CHECK(std::fabs(f.host.cameraControl.target.z - ez * size) < 0.5f);
            CHECK(f.host.cameraControl.target.y == 0.0f);
        }
    }

    // -----------------------------------------------------------------------------------------------------------
    // Init: buffer, notifications, zone colours, tuning ramps, terrain map
    // -----------------------------------------------------------------------------------------------------------
    {
        Fixture f(64, 256, 256);
        CHECK(f.Init());
        CHECK(f.window->Flags() == 7);
        CHECK(f.window->CityInitialised());
        CHECK(f.window->MapBuffer() != nullptr);
        CHECK(f.window->Map().pixels != nullptr);
        CHECK(f.window->Map().width == 64);
        CHECK(f.window->Map().height == 64);
        CHECK(f.window->ZoomShift() == 2);                 // 256 cells -> 64 pixels
        CHECK(f.window->CityCellsX() == 256);
        CHECK(f.window->CityCellsZ() == 256);
        CHECK(f.host.created.size() == 1);
        cIGZBuffer* map = f.window->MapBuffer();
        CHECK(map->GetColorType().type == kBufferColorARGB8888);
        CHECK(map->Width() == 64 && map->Height() == 64);
        CHECK(!map->IsLocked());
        CHECK(BufferPixel(map, 0, 0) == 0xff64647fu);      // RGB(100, 100, 127) fill
        CHECK(f.host.server.added.size() == 11);
        const std::uint32_t expected[9] = {0x89171496, 0x678e6e3a, 0x278e6003, 0x07699455, 0xa6b79602,
                                           0xa6b79612, 0x26d31ec1, 0x26d31ec2, 0xc53d10aa};
        for (int i = 0; i < 9; ++i) {
            CHECK(f.host.server.added[static_cast<std::size_t>(i)].second == expected[i]);
            CHECK(f.host.server.added[static_cast<std::size_t>(i)].first ==
                  static_cast<cIGZMessageTarget2*>(f.window));
        }
        CHECK(f.host.server.added[9].second == kMiniMapMsgOccupantInserted);
        CHECK(f.host.server.added[10].second == kMiniMapMsgOccupantRemoved);
        CHECK(f.window->ZoneColor(0) == 0);
        CHECK(f.window->ZoneColor(10) == 0xffe57300u);
        CHECK(f.window->ZoneColor(1) == 0xff00ec00u);
        CHECK(f.window->ZoneColor(4) == 0xff7979ffu);
        CHECK(f.window->ZoneColor(9) == 0xffc7bc00u);
        CHECK(f.host.rampsRequested == 3);
        CHECK(f.window->TerrainRampEntry(0) == 0x00808080u);   // alpha masked off
        CHECK(f.window->WaterRampEntry(0) == 0);
        CHECK(f.window->CellFlags().size() == (256u * 256u * 2u + 31u) / 32u);
        CHECK(f.host.tickAdds == 1);
        CHECK(f.window->OccupantMessagesHooked());
        for (int i = 0; i < 16; ++i) CHECK(f.window->DirtyBlock(i) == 0xffffffffu);
        CHECK(f.window->TerrainDirty());
        CHECK(f.window->NeedsUpdate());
        CHECK(f.window->Init());   // idempotent
        CHECK(f.host.created.size() == 1);
    }

    // Init with a 0x0 window: the buffer cannot be created, so Init fails.
    {
        Fixture f(0);
        CHECK(f.window->MapSize() == 0);
        CHECK(!f.Init());
        CHECK(f.window->MapBuffer() == nullptr);
    }
    // SetArea after the terrain map exists only re-runs OnCityInit, which returns at once (quirk).
    {
        Fixture f(64, 256, 256);
        CHECK(f.Init());
        f.window->SetArea(0, 0, 32, 32);
        CHECK(f.window->MapSize() == 32);
        CHECK(f.window->Map().width == 64);   // the map was not re-created
        CHECK(f.window->ZoomShift() == 2);    // and the zoom shift was not recomputed
    }

    // Init when the graphics system cannot create a buffer.
    {
        Fixture f(64);
        f.host.bufferCreateFails = true;
        CHECK(!f.Init());
        CHECK(f.window->MapBuffer() == nullptr);
        CHECK(f.host.server.added.empty());
    }

    // -----------------------------------------------------------------------------------------------------------
    // The terrain picture: shade, terrain ramp, water ramp, zone overlay and every resampler
    // -----------------------------------------------------------------------------------------------------------
    {
        Fixture f(64, 16, 16);   // 16x16 cells, a 64-pixel map: one block, Upsample16
        CHECK(f.Init());
        CHECK(f.window->ZoomShift() == -2);
        f.Build();
        CHECK(!f.window->TerrainDirty());
        CHECK(!f.window->NeedsUpdate());
        for (int y = 0; y < 64; y += 21)
            for (int x = 0; x < 64; x += 23) CHECK(MapPixel(*f.window, x, y) == kDefaultBlockPixel);
        CHECK(BufferPixel(f.window->MapBuffer(), 0, 0) == (kDefaultBlockPixel | 0xff000000u));
        CHECK(BufferPixel(f.window->MapBuffer(), 5, 7) == (kDefaultBlockPixel | 0xff000000u));
    }
    {
        Fixture f(32, 16, 16);   // Upsample4
        CHECK(f.Init());
        CHECK(f.window->ZoomShift() == -1);
        f.Build();
        CHECK(MapPixel(*f.window, 31, 31) == kDefaultBlockPixel);
        CHECK(BufferPixel(f.window->MapBuffer(), 31, 31) == (kDefaultBlockPixel | 0xff000000u));
    }
    {
        Fixture f(16, 16, 16);   // Downsample1
        CHECK(f.Init());
        CHECK(f.window->ZoomShift() == 0);
        f.Build();
        CHECK(MapPixel(*f.window, 15, 15) == kDefaultBlockPixel);
    }
    {
        Fixture f(8, 16, 16);   // Downsample4
        CHECK(f.Init());
        CHECK(f.window->ZoomShift() == 1);
        f.Build();
        CHECK(MapPixel(*f.window, 7, 7) == kDefaultBlockPixel);
    }
    {
        Fixture f(4, 16, 16);   // Downsample16
        CHECK(f.Init());
        CHECK(f.window->ZoomShift() == 2);
        f.Build();
        // Downsample16's green round adds 8 (0x8000 against the 0xff000 mask, 0x1005c746c): 0x65 becomes 0x6d.
        CHECK(MapPixel(*f.window, 3, 3) == 0x00656d65u);
    }
    {
        // A map too small for the city: shift 3 is outside the image-op table, so no block is written (quirk).
        Fixture f(2, 16, 16);
        CHECK(f.Init());
        CHECK(f.window->ZoomShift() == 3);
        f.window->SetFlags(0xffffffff, 0);
        f.Build();
        CHECK(!f.window->TerrainDirty());
        CHECK(!f.window->NeedsUpdate());
    }
    {
        // Water: depth 5 picks water ramp entry 5; its alpha 0x80 blends over the shaded grey.
        Fixture f(16, 16, 16);
        f.host.waterRamp.data.assign(256, kWater);
        f.host.terrain.defaultDepth = 5.0f;
        CHECK(f.Init());
        f.Build();
        CHECK(MapPixel(*f.window, 3, 3) == 0x003333ffu);
        CHECK(BufferPixel(f.window->MapBuffer(), 3, 3) == 0xff3333ffu);
    }
    {
        // The water ramp's alpha 0 entries leave the terrain alone.
        Fixture f(16, 16, 16);
        f.host.waterRamp.data.assign(256, 0x00808080);
        f.host.terrain.defaultDepth = 5.0f;
        CHECK(f.Init());
        f.Build();
        CHECK(MapPixel(*f.window, 3, 3) == kDefaultBlockPixel);
    }
    {
        // The terrain ramp is selected by (altitude - sea level) * 0.2 + 0x40, clamped to 0..255.
        Fixture f(16, 16, 16);
        f.host.terrainRamp.data = {0xff000000, 0xffffffff};   // black below index 128, white above
        f.host.terrain.defaultAltitude = 400.0f;              // 400 * 0.2 = 80 -> index 144
        CHECK(f.Init());
        f.Build();
        // The white stop (0xffffff) shaded by 201: (0xff * 201 + 0x80) >> 8 = 0xc8 per channel.
        CHECK(MapPixel(*f.window, 0, 0) == 0x00c8c8c8u);
        // At or below sea level the normal is the fixed (0, 1, 0): the black stop stays black.
        f.host.terrain.sea = 400.0f;
        f.window->SetFlags(0xffffffff, f.window->Flags());
        f.Build();
        CHECK(MapPixel(*f.window, 0, 0) == 0x00000000u);
    }
    {
        // Zone overlay: flag 1 replaces the (alpha 0) terrain pixel with the zone colour of the grid value.
        Fixture f(16, 16, 16);
        f.host.zoneGrid.zone = 2;
        CHECK(f.Init());
        CHECK(f.window->SetFlags(0xffffffff, f.window->Flags() | 1) == 7);
        f.Build();
        CHECK(f.host.zoneGrid.queries == 256);
        CHECK(MapPixel(*f.window, 0, 0) == 0xff00ec00u);   // zone 2 = Init's 0x00ec00 with opaque alpha
        // Zone 0 is transparent in the default set, so the terrain shows through.
        f.host.zoneGrid.zone = 0;
        f.window->SetFlags(0xffffffff, f.window->Flags() & ~1u);
        f.Build();
        CHECK(MapPixel(*f.window, 0, 0) == kDefaultBlockPixel);
        // An out-of-range zone value is dropped instead of reading outside the colour vector.
        f.host.zoneGrid.zone = 100;
        f.window->SetFlags(0xffffffff, f.window->Flags() | 1);
        f.Build();
        CHECK(MapPixel(*f.window, 0, 0) == kDefaultBlockPixel);
    }

    // -----------------------------------------------------------------------------------------------------------
    // The occupant pass (flag 2): network occupants are painted grey, the flag caches the query
    // -----------------------------------------------------------------------------------------------------------
    {
        Fixture f(64, 256, 256);
        CHECK(f.Init());
        f.host.occupantX = 5;   // cell (5, 3) -> map pixel (5>>2, 3>>2) = (1, 0)
        f.host.occupantZ = 3;
        f.Build();
        const std::uint32_t cell = 3u * 256u + 5u;
        const unsigned shift = (cell * 2u) & 0x1eu;
        CHECK((f.window->CellFlags()[cell / 16u] & (1u << shift)) != 0);          // queried
        CHECK((f.window->CellFlags()[cell / 16u] & (2u << shift)) != 0);          // occupied
        CHECK(MapPixel(*f.window, 1, 0) == kMiniMapNetworkCellColor);
        CHECK(BufferPixel(f.window->MapBuffer(), 1, 0) == kMiniMapNetworkCellColor);
        const int calls = f.host.occupantCalls;
        f.window->SetFlags(0x2, 0x2);   // keep the occupant flag, force a repaint
        f.Build();
        CHECK(f.host.occupantCalls == calls);   // cached
        CHECK(MapPixel(*f.window, 1, 0) == kMiniMapNetworkCellColor);
        // With the occupant flag off the pass does not run at all.
        f.window->SetFlags(0xffffffff, f.window->Flags() & ~2u);
        f.host.occupantCalls = 0;
        f.Build();
        CHECK(f.host.occupantCalls == 0);
    }

    // -----------------------------------------------------------------------------------------------------------
    // Overlay: flag 8 replaces the picture, otherwise the overlay is composited over it
    // -----------------------------------------------------------------------------------------------------------
    {
        Fixture f(16, 16, 16);
        CHECK(f.Init());
        auto* overlay = new cGZBuffer();
        overlay->AddRef();
        CHECK(overlay->Init(16, 16, cGZBufferColorType{kBufferColorARGB8888, 32}));
        overlay->Lock(cIGZBuffer::kLockWrite);
        auto* bits = static_cast<std::uint32_t*>(overlay->GetColorSurfaceBits());
        const std::uint32_t stride = overlay->GetColorSurfaceStride() >> 2;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) bits[y * stride + x] = 0xff000000u | (std::uint32_t(x) << 8) | std::uint32_t(y);
        overlay->Unlock(cIGZBuffer::kLockWrite);
        const std::uint32_t overlayPixel = 0xff000000u | (2u << 8) | 3u;
        f.window->SetOverlay(overlay);
        CHECK(f.window->OverlayBuffer() == overlay);
        overlay->Release();   // the window keeps its own reference
        // Composite (flag 8 clear): the blend keeps the terrain colour where the overlay is transparent.
        f.window->SetFlags(0xffffffff, f.window->Flags() & ~8u);
        f.Build();
        CHECK(MapPixel(*f.window, 2, 3) == kDefaultBlockPixel);           // the terrain picture is untouched
        CHECK(BufferPixel(f.window->MapBuffer(), 2, 3) == overlayPixel);  // the blended buffer holds the overlay
        // A half-transparent overlay pixel blends over the shaded grey.
        overlay->Lock(cIGZBuffer::kLockWrite);
        bits = static_cast<std::uint32_t*>(overlay->GetColorSurfaceBits());
        bits[2 * stride + 1] = 0x80000000u;   // (1, 2): black at alpha 0x80
        overlay->Unlock(cIGZBuffer::kLockWrite);
        f.window->SetFlags(0xffffffff, f.window->Flags());   // request another composite
        f.Build();
        const std::uint32_t blended = BufferPixel(f.window->MapBuffer(), 1, 2);
        const std::uint32_t base = kDefaultBlockPixel;
        const std::uint32_t r = ((((base & 0xff0000) * 0x80 + 0x800000) >> 8) + 0) & 0x1ff0000;
        const std::uint32_t g = ((((base & 0xff00) * 0x80 + 0x8000) >> 8) + 0) & 0x7ff00;
        const std::uint32_t b = ((((base & 0xff) * 0x80 + 0x80) >> 8) + 0);
        CHECK(blended == (r | g | b | 0xff000000u));
        // Replacement (flag 8 set): the map buffer holds only the overlay.
        f.window->SetFlags(0xffffffff, f.window->Flags() | 8u);
        f.Build();
        CHECK(BufferPixel(f.window->MapBuffer(), 2, 3) == overlayPixel);
        // RotateBlt's quarter turns: dest (col, row) reads (size-1-row, col) for 1 and (row, size-1-col) for 3.
        f.host.cameraControl.rotation = 1;
        f.window->SetFlags(0xffffffff, f.window->Flags());
        f.Build();
        CHECK(BufferPixel(f.window->MapBuffer(), 2, 3) == (0xff000000u | (12u << 8) | 2u));
        f.host.cameraControl.rotation = 3;
        f.window->SetFlags(0xffffffff, f.window->Flags());
        f.Build();
        CHECK(BufferPixel(f.window->MapBuffer(), 2, 3) == (0xff000000u | (3u << 8) | 13u));
        f.host.cameraControl.rotation = 0;
        f.window->SetOverlay(nullptr);
        CHECK(f.window->OverlayBuffer() == nullptr);
    }

    // -----------------------------------------------------------------------------------------------------------
    // The message dispatch
    // -----------------------------------------------------------------------------------------------------------
    {
        Fixture f(16, 16, 16);
        CHECK(f.Init());
        cRZMessage2Standard tick;
        tick.SetType(kMiniMapMsgTick);
        tick.SetData3(std::bit_cast<std::uint32_t>(400.0f));
        CHECK(f.window->DoMessage(&tick));
        CHECK(f.window->TickAccumulator() == 400.0f);
        tick.SetData3(std::bit_cast<std::uint32_t>(300.0f));
        CHECK(f.window->DoMessage(&tick));
        CHECK(f.window->TickAccumulator() == 700.0f);
        f.window->InvalidateTZMapRect(cRZRect{0, 0, 255, 255});
        tick.SetData3(std::bit_cast<std::uint32_t>(400.0f));
        CHECK(f.window->DoMessage(&tick));
        CHECK(f.window->TickAccumulator() == 0.0f);
        CHECK(!f.window->NeedsUpdate());
        // A hidden window drops the picture.
        f.window->HideWindow();
        CHECK(f.window->DoMessage(&tick));
        CHECK(f.window->Map().pixels == nullptr);
        f.window->ShowWindow();
        CHECK(f.window->GZPaint());   // GZPaint rebuilds the map when it is missing
        CHECK(f.window->Map().pixels != nullptr);

        cRZMessage2Standard rect;
        rect.SetType(kMiniMapMsgTerrainChanged1);
        rect.SetData1((17u << 16) | 47u);
        rect.SetData2((33u << 16) | 50u);
        CHECK(f.window->DoMessage(&rect));
        CHECK((f.window->DirtyBlock(2) & (1u << 1)) != 0);   // rows 2..3, columns 1..2
        CHECK((f.window->DirtyBlock(3) & (1u << 1)) != 0);
        CHECK(f.window->TerrainDirty());
        for (std::uint32_t type : {kMiniMapMsgTerrainChanged2, kMiniMapMsgTerrainChanged3}) {
            f.window->SetFlags(0xffffffff, 0);
            rect.SetType(type);
            CHECK(f.window->DoMessage(&rect));
            CHECK(f.window->TerrainDirty());
        }
        cRZMessage2Standard rectMessage;
        rectMessage.SetType(kMiniMapMsgRectInvalid);
        cRZRect r{0x20, 0x30, 0x2f, 0x3f};
        rectMessage.SetVoid2(&r);
        f.window->SetFlags(0xffffffff, 0);
        CHECK(f.window->DoMessage(&rectMessage));
        CHECK((f.window->DirtyBlock(3) & (1u << 2)) != 0);
        cRZMessage2Standard camera;
        camera.SetType(kMiniMapMsgCameraZoomRotationDone);
        CHECK(f.window->DoMessage(&camera));
        camera.SetType(kMiniMapMsgCameraTargetDone);
        CHECK(f.window->DoMessage(&camera));
        cRZMessage2Standard reload;
        reload.SetType(kMiniMapMsgReloadTunables);
        reload.SetData1(0x65706069);
        reload.SetData2(0x8a356f2c);
        f.host.rampsRequested = 0;
        CHECK(f.window->DoMessage(&reload));
        CHECK(f.host.rampsRequested == 0);   // the group does not match
        reload.SetData1(0x690f693f);
        CHECK(f.window->DoMessage(&reload));
        CHECK(f.host.rampsRequested == 3);
        cRZMessage2Standard unknown;
        unknown.SetType(0x12345678);
        CHECK(!f.window->DoMessage(&unknown));
    }

    // City shutdown and init through messages.
    {
        Fixture f(16, 16, 16);
        CHECK(f.Init());
        cRZMessage2Standard message;
        message.SetType(kMiniMapMsgCityShutdown);
        CHECK(f.window->DoMessage(&message));
        CHECK(!f.window->CityInitialised());
        CHECK(f.window->Map().pixels == nullptr);
        CHECK(f.host.tickRemoves == 1);
        CHECK(f.window->CityCellsX() == 0);
        CHECK(f.host.server.removed.size() == 2);
        message.SetType(kMiniMapMsgCityInit);
        CHECK(f.window->DoMessage(&message));
        CHECK(f.window->CityInitialised());
        CHECK(f.window->Map().pixels != nullptr);
    }

    // Occupant notifications: insert marks the picture dirty, remove invalidates the cell's block.
    {
        Fixture f(16, 16, 16);
        CHECK(f.Init());
        f.Build();
        cRZMessage2Standard message;
        message.SetType(kMiniMapMsgOccupantRemoved);
        message.SetVoid1(&f.host.occupant);
        message.SetData3(kMiniMapNetworkOccupantType);
        f.host.networkCellX = 20;   // outside the 16x16 city
        f.host.networkCellZ = 4;
        CHECK(f.window->DoMessage(&message));
        CHECK(!f.window->NeedsUpdate());
        f.host.networkCellX = 3;
        CHECK(f.window->DoMessage(&message));
        CHECK(f.window->TerrainDirty());
        CHECK((f.window->DirtyBlock(0) & 1u) != 0);
        // The query bit of the cell was cleared so the next pass asks again.
        const std::uint32_t cell = 4u * 16u + 3u;
        CHECK((f.window->CellFlags()[cell / 16u] & (1u << ((cell * 2u) & 0x1eu))) == 0);
        message.SetType(kMiniMapMsgOccupantInserted);
        f.Build();   // clears the dirty flags
        CHECK(f.window->DoMessage(&message));
        CHECK(f.window->NeedsUpdate());
        message.SetData3(0x278128a0);
        f.Build();
        CHECK(f.window->DoMessage(&message));
        CHECK(!f.window->NeedsUpdate());
        message.SetData3(kMiniMapNetworkOccupantType);
        f.host.networkCellOk = false;
        f.Build();
        CHECK(f.window->DoMessage(&message));
        CHECK(!f.window->NeedsUpdate());
    }

    // -----------------------------------------------------------------------------------------------------------
    // GZPaint: the ARGB4444 dither path writes the map into the locked target
    // -----------------------------------------------------------------------------------------------------------
    {
        Fixture f(16, 16, 16);
        CHECK(f.Init());
        f.Build();
        const std::uint32_t pixel = MapPixel(*f.window, 0, 0);
        std::vector<std::uint8_t> target(16 * 16 * 2, 0);
        fake_dc::Ctx context;
        context.lockResult = true;
        context.lockDesc.pixels = target.data();
        context.lockDesc.pitch = 16 * 2;
        context.lockDesc.width = 16;
        context.lockDesc.height = 16;
        context.lockDesc.format = 7;   // ARGB4444
        context.lockDesc.bytesPerPixel = 2;
        f.window->SetDrawState(&f.drawBuffer, &context);
        CHECK(f.window->GZPaint());
        const auto* words = reinterpret_cast<const std::uint16_t*>(target.data());
        CHECK(words[0] == DitherExpected(pixel, 16, 16, 0));
        CHECK(words[15] == DitherExpected(pixel, 16, 16, 15));
        CHECK(words[16 * 5 + 7] == DitherExpected(pixel, 16, 16, 7));
        CHECK(context.calls.size() >= 1);   // lock r + unlock
        CHECK(context.calls[0] == "lock r");
    }

    // GZPaint's dither path uses the window's accumulated (absolute) area as the destination rectangle.
    {
        Fixture f(16, 16, 16);
        CHECK(f.Init());
        f.Build();
        f.window->SetArea(4, 6, 20, 22);
        const std::uint32_t pixel = MapPixel(*f.window, 0, 0);
        std::vector<std::uint8_t> target(32 * 32 * 2, 0);
        fake_dc::Ctx context;
        context.lockDesc.pixels = target.data();
        context.lockDesc.pitch = 32 * 2;
        context.lockDesc.width = 32;
        context.lockDesc.height = 32;
        context.lockDesc.format = 7;
        f.window->SetDrawState(&f.drawBuffer, &context);
        CHECK(f.window->GZPaint());
        const auto* words = reinterpret_cast<const std::uint16_t*>(target.data());
        // accumulatedArea_ = area_ + its own top-left = (8, 12) .. (24, 28).
        CHECK(words[12 * 32 + 8] == DitherExpected(pixel, 16, 16, 0));
        CHECK(words[12 * 32 + 7] == 0);
        CHECK(words[11 * 32 + 8] == 0);
        CHECK(words[27 * 32 + 23] == DitherExpected(pixel, 16, 16, 15));
        CHECK(words[28 * 32 + 23] == 0);
    }

    // GZPaint's blit path (a target format that is not ARGB4444) draws through the draw context.
    {
        Fixture f(16, 16, 16);
        CHECK(f.Init());
        f.Build();
        fake_dc::Ctx context;
        context.lockResult = true;
        context.lockDesc.format = 9;   // ARGB8888
        f.window->SetDrawState(&f.drawBuffer, &context);
        CHECK(f.window->GZPaint());
        bool blit = false;
        for (const std::string& call : context.log)
            if (call.rfind("blt ", 0) == 0) {
                blit = true;
                CHECK(call == "blt 0,0,16,16>0,0,16,16");
            }
        CHECK(blit);
        CHECK(context.calls.size() == 1 && context.calls[0] == "lock r");
    }

    // GZPaint's view rectangle: four corner picks projected onto the map and closed with four lines.
    {
        Fixture f(16, 16, 16);
        CHECK(f.Init());
        f.Build();
        f.window->SetFlags(0xffffffff, f.window->Flags() | 4);
        f.host.renderer.picks = {{0.0f, 0.0f, 0.0f},
                                 {255.0f, 0.0f, 0.0f},
                                 {255.0f, 0.0f, 255.0f},
                                 {0.0f, 0.0f, 255.0f}};
        fake_dc::Ctx context;
        context.lockResult = false;   // skip the picture, the rectangle is still drawn
        f.window->SetDrawState(&f.drawBuffer, &context);
        f.host.lines.clear();
        CHECK(f.window->GZPaint());
        CHECK(f.host.renderer.pickCalls == 4);
        CHECK(f.host.lines.size() == 4);
        if (f.host.lines.size() == 4) {
            // The z axis is flipped by the projection (v = -normalised z): z = 0 lands on the bottom edge.
            CHECK(f.host.lines[0].x0 == 0 && f.host.lines[0].y0 == 15);
            CHECK(f.host.lines[0].x1 == 15 && f.host.lines[0].y1 == 15);
            CHECK(f.host.lines[1].x0 == 15 && f.host.lines[1].y0 == 15);
            CHECK(f.host.lines[1].x1 == 15 && f.host.lines[1].y1 == 0);
            CHECK(f.host.lines[2].x1 == 0 && f.host.lines[2].y1 == 0);
            CHECK(f.host.lines[3].x1 == 0 && f.host.lines[3].y1 == 15);
            CHECK(f.host.lines[0].color == 0xffffffffu);
            CHECK(f.host.lines[0].target == &f.drawBuffer);
        }
    }

    // The view rectangle with a rotated camera: the corners are mapped through the rotation.
    {
        Fixture f(16, 16, 16);
        CHECK(f.Init());
        f.Build();
        f.window->SetFlags(0xffffffff, f.window->Flags() | 4);
        f.host.cameraControl.rotation = 1;
        // normalised (x, z) values around the middle; rotation 1 swaps and negates.
        f.host.renderer.picks = {{128.0f, 0.0f, 64.0f},
                                 {64.0f, 0.0f, 64.0f},
                                 {64.0f, 0.0f, 192.0f},
                                 {192.0f, 0.0f, 192.0f}};
        fake_dc::Ctx context;
        context.lockResult = false;
        f.window->SetDrawState(&f.drawBuffer, &context);
        f.host.lines.clear();
        CHECK(f.window->GZPaint());
        CHECK(f.host.lines.size() == 4);
    }

    // GZPaint's situation marker: a blinking 3x3 fill at the target position.
    {
        Fixture f(16, 16, 16);
        CHECK(f.Init());
        f.Build();
        f.window->SetFlags(0xffffffff, f.window->Flags() | 0x10);
        f.host.situation.hasTarget = true;
        f.host.situation.position[0] = 64.0f;
        f.host.situation.position[2] = 64.0f;
        fake_dc::Ctx context;
        context.lockResult = false;
        f.window->SetDrawState(&f.drawBuffer, &context);
        CHECK(!f.window->MarkerBlink());
        CHECK(f.window->GZPaint());
        // 64 / 256 * 16 = 4: the marker is the rect (3, 3, 5, 5).
        bool found = false;
        for (const std::string& call : context.calls) {
            if (call.rfind("draw 3 ", 0) == 0) {
                found = true;
                CHECK(call == "draw 3 3,3,5,5 n=2");
            }
        }
        CHECK(found);
        CHECK(f.window->MarkerBlink());
        CHECK(!context.log.empty() && context.log.back() == "color 4290772992");   // 0xffc00000 while off
        context.log.clear();
        CHECK(f.window->GZPaint());
        CHECK(!f.window->MarkerBlink());
        CHECK(!context.log.empty() && context.log.back() == "color 4294918208");   // 0xffff4040 while on
        // The situation's active flag requests a repaint from the tick; without a target the marker is skipped.
        f.host.situation.hasTarget = false;
        context.calls.clear();
        CHECK(f.window->GZPaint());
        for (const std::string& call : context.calls) CHECK(call.rfind("draw 3 ", 0) != 0);
    }

    // -----------------------------------------------------------------------------------------------------------
    // Shutdown releases the buffer and unregisters the notifications
    // -----------------------------------------------------------------------------------------------------------
    {
        Fixture f(16, 16, 16);
        CHECK(f.Init());
        CHECK(f.window->Shutdown());
        CHECK(f.window->MapBuffer() == nullptr);
        CHECK(f.host.server.removed.size() == 11);
        CHECK(f.host.tickRemoves == 0);   // Shutdown leaves the animation-tick target registered (quirk)
        CHECK(!f.window->OccupantMessagesHooked());
        CHECK(f.window->Shutdown());   // idempotent
    }

    if (failures == 0) std::printf("all cSC4WinMiniMap checks passed\n");
    else std::printf("%d cSC4WinMiniMap checks failed\n", failures);
    return failures == 0 ? 0 : 1;
}
