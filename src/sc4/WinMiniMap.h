// cSC4WinMiniMap — the corner minimap of the city view (0x1005c4664..0x1005c772c): the downsampled picture of
// the city (terrain shading with the two colour ramps, the water overlay, the zone overlay and the network
// occupants painted on top), the camera's view rectangle drawn over it, the marker of the active city-situation
// target, and the click / drag that recentres the camera.
//
// Vtable groups (the original object is 0xa10 bytes and has three vtable pointers): +0 the cGZWin/cIGZWin view
// (cGZWin's 0x150 bytes), +0x150 the cISC4WinMiniMap view (iid 0xca318385: AsIGZWin / SetOverlay /
// SetDefaultZoneColor / SetDefaultZoneColorToTrans / SetZoneColor / SetFlags / Update), +0x158 the
// cIGZMessageTarget2 view (DoMessage(cIGZMessage2*)). QueryInterface answers 1 / 0x22ba0121 / 0xe98b2f57 with the
// window view and 0xca318385 with the minimap view (0x1005c48c6).
//
// Original layout:
//   +0x150 cISC4WinMiniMap vptr                 +0x158 cIGZMessageTarget2 vptr
//   +0x160 u32 flags (7 at construction; 1 = zone overlay, 2 = occupant pass, 4 = view rectangle,
//          8 = overlay replaces the terrain, 0x10 = city-situation target marker)
//   +0x164 int map size (largest power of two <= min(window width, height), picked by SetArea)
//   +0x168 int city cell count X                +0x16c int city cell count Z
//   +0x170 cIGZBuffer* map buffer (map size square, ARGB8888, created by Init from RZ::GS())
//   +0x178 cIGZBuffer* overlay buffer (SetOverlay; owned, one reference)
//   +0x180 float animation-tick accumulator (ms; the picture is redrawn every 1000)
//   +0x184 bool city initialised                +0x185 bool terrain map dirty
//   +0x186 bool picture needs update            +0x187 bool repaint requested
//   +0x188 bool left button dragging            +0x189 bool occupant messages hooked
//   +0x18a bool situation-marker blink phase    +0x18c int zoom shift (log2(map size / city cells))
//   +0x190 std::vector<u32> zone colours (16 entries, ARGB; index = zone grid value)
//   +0x1a8 cTZMap terrain/zone map (pixels, width, height — a 16-byte {u32*, int, int})
//   +0x1b8 u32 dirty blocks[16] (bit x = the 16x16-cell block column x of block row y was invalidated)
//   +0x1f8 u32 terrain ramp[256] (height -> colour, alpha cleared)   +0x5f8 u32 water ramp[256]
//   +0x9f8 std::vector<u32> per-cell flags (2 bits per city cell: bit 0 = "occupant queried",
//          bit 1 = "occupied"; the picture's network colour is cached here so the occupant manager is only
//          asked once per cell until an occupant notification clears the query bit)
//
// The three ramps come from the exemplar {0x6534284a, 0x690f693f, 0x8a356f2c}: property 0xea3573af (the terrain
// colour ramp, alpha cleared after interpolation) and property 0x6b733233 (the water ramp, indexed by water
// depth; its alpha is the blend weight over the shaded terrain). Both are uint32 stop arrays interpolated to
// 256 entries by nSC4UI::InterpolateRampToPalette (LoadTunableValues, 0x1005c4c5c).
//
// The picture is built block by block in UpdateTerrainAndZoneMap: each dirty 16x16-cell block is turned into a
// 16x16 colour block (altitude shade * terrain ramp, water ramp blended by its alpha, then the zone colour when
// the zone overlay is on) and resampled into the map with the zoom-shift table Upsample16 / Upsample4 /
// Downsample1 / Downsample4 / Downsample16 (0x100ad12e0, indexed by zoomShift + 2; an index outside 0..4 skips
// the block). Update's occupant pass paints every cell that holds a surface-network occupant with 0xffcfcfc5,
// then RotateBlt (0x1005c6266) copies the map into the map buffer through the camera rotation, or the overlay
// is composited over it first.
//
// Quirks kept (asserted in scripts/tests/sc4_winminimap_test.cpp):
//  * SetArea tails into OnCityInit only when the terrain map already exists, so the call is dead in practice
//    (OnCityInit returns at once once the city is initialised; 0x1005c4ff7).
//  * SetDefaultZoneColor makes every entry opaque and then forces entry 0 transparent; SetZoneColor forces the
//    alpha of a non-zero colour to 0xff and stores 0 as 0 (0x1005c58d4 / 0x1005c5956).
//  * The animation tick reads the low 32 bits of the message's data3 as a float (0x1005c5528); the original's
//    tick source is not recovered, the port keeps the access as the binary does it.
//  * InitTZMap takes the zoom shift from the X cell count only (0x1005c5a6a).
//  * The zone overlay reads a signed zone value straight into the colour vector without a range check
//    (0x1005c68e3); the port keeps the sign but skips values outside 0..15 (the original read out of bounds).
//  * The ARGB4444 dither path uses (height & 3) for the dither row of every row and (width - x) & 3 for the
//    column (0x1005c6b6a..0x1005c6bd7). The 16-bit format check is the draw target's format == 7.
//  * GZPaint rebuilds the terrain map (InitTZMap + Update) only while the picture is missing (0x1005c6a5d); it
//    reads the camera rotation even when neither the view rectangle nor the marker is enabled (0x1005c6c8a).
//  * The 3D view's screen corners are projected to the ground, normalised by the city size, rotated with the
//    camera rotation, scaled by (map size - 1) and rounded with the add-12582912 trick.
//
// Neighbours that are not recovered go through IMiniMapHost (one method per original call, the slot / address in
// the comment): the city, the terrain, the zone grid, the occupant manager, the renderer, the 3D window, the
// animation tick manager, the resource manager's exemplar ramps and the free function DrawLineAntialiased
// (0x100390498). The message server (cIGZMessageServer2) and the buffers (cIGZBuffer) are recovered classes.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/Buffer.h"
#include "gz/StreamOps.h"
#include "gz/cIGZMessage2.h"
#include "gz/Win.h"
#include "rz/cRZPoint.h"

class cIGZBuffer;
namespace gz { class cIGZDrawContext; }
namespace s3d { class cS3DCamera; }

namespace sc4 {

// Message types cSC4WinMiniMap registers / handles (the ids Init registers, 0x1005c4a66..).
constexpr std::uint32_t kMiniMapMsgTerrainChanged1 = 0x89171496;   // data1/data2 = four u16 cell bounds
constexpr std::uint32_t kMiniMapMsgTerrainChanged2 = 0x678e6e3a;   // the same, three more senders
constexpr std::uint32_t kMiniMapMsgTerrainChanged3 = 0x278e6003;
constexpr std::uint32_t kMiniMapMsgRectInvalid = 0x07699455;       // void2 = cRZRect*
constexpr std::uint32_t kMiniMapMsgCameraZoomRotationDone = 0xa6b79602;
constexpr std::uint32_t kMiniMapMsgCameraTargetDone = 0xa6b79612;
constexpr std::uint32_t kMiniMapMsgCityInit = 0x26d31ec1;
constexpr std::uint32_t kMiniMapMsgCityShutdown = 0x26d31ec2;
constexpr std::uint32_t kMiniMapMsgReloadTunables = 0xc53d10aa;    // data1 low == 0x690f693f, data2 low == 0x8a356f2c
constexpr std::uint32_t kMiniMapMsgOccupantInserted = 0x99ef1142;  // void1 = occupant, data3 = its type
constexpr std::uint32_t kMiniMapMsgOccupantRemoved = 0x99ef1143;
constexpr std::uint32_t kMiniMapMsgTick = 0x86ad10ee;              // data3 low 32 bits = float milliseconds

// Masks / ids the class compares.
constexpr std::uint32_t kMiniMapIID = 0xca318385;                  // cISC4WinMiniMap
constexpr std::uint32_t kMiniMapNetworkOccupantType = 0xc772bf98;  // cISC4Occupant type of network occupants
constexpr std::uint32_t kMiniMapNetworkOccupantIID = 0xa821ef94;   // cISC4NetworkOccupant
constexpr std::uint32_t kMiniMapTunableGroup = 0x690f693f;         // exemplar {0x6534284a, group, 0x8a356f2c}
constexpr std::uint32_t kMiniMapTunableInstance = 0x8a356f2c;
constexpr std::uint32_t kMiniMapTerrainRampProperty = 0xea3573af;
constexpr std::uint32_t kMiniMapWaterRampProperty = 0x6b733233;
constexpr std::uint32_t kMiniMapLockReadWrite = 0x8001;            // cIGZBuffer::Lock flags of Init
constexpr std::uint32_t kMiniMapLockWriteOnly = 0x8010;            // GZPaint / the blit path
constexpr std::uint32_t kMiniMapLockReadOnly = 0x8040;             // RotateBlt / the overlay composite
constexpr std::uint32_t kMiniMapNetworkCellColor = 0xffcfcfc5;     // painted for surface-network occupants
constexpr int kMiniMapZoneColorCount = 16;                         // the +0x190 vector's size
constexpr float kMiniMapTickPeriodMs = 1000.0f;
constexpr int kMiniMapImageOpsFirstShift = -2, kMiniMapImageOpsLastShift = 2;   // the 0x100ad12e0 table

// ---------------------------------------------------------------------------------------------------------------
// The outside world, one method per original call.
// ---------------------------------------------------------------------------------------------------------------
// SL::spTerrain (cSTETerrain) as the minimap uses it.
class IMiniMapTerrain {
public:
    virtual ~IMiniMapTerrain() = default;
    virtual int CellCountX() = 0;                                  // 0x38
    virtual int CellCountZ() = 0;                                  // 0x40
    virtual float SeaLevel() = 0;                                  // 0xe8
    virtual float Altitude(float x, float z) = 0;                  // 0x60 (click-to-jump ground height)
    virtual void Altitudes(const cRZRect& rect, float* out, long rowStride, long colStride) = 0;    // 0x78
    virtual void WaterDepths(const cRZRect& rect, float* out, long rowStride, long colStride) = 0;  // 0x80
    virtual void VertexNormal(int x, int z, cS3DVector3& out) = 0;  // 0xe0
};

// SL::spZoneManager 0x28 (GetZoneGrid) -> grid 0xc0 (GetGridData: the vector<vector<int8>> at grid + 0x40).
class IMiniMapZoneGrid {
public:
    virtual ~IMiniMapZoneGrid() = default;
    virtual int ZoneAt(int x, int z) = 0;                          // data[x][z], sign-extended int8
};

// The city situation manager as the minimap reads it.
class IMiniMapSituation {
public:
    virtual ~IMiniMapSituation() = default;
    virtual bool HasActiveSituation() = 0;                         // 0x30 GetActiveSituation() != null
    virtual bool CurrentTargetPosition(float xyz[3]) = 0;          // 0x48 GetCurrentTarget, occupant 0x40 Position
};

// The 3D view window (SL::spView3DWin, a cIGZWin) as the minimap uses it.
class IMiniMapView3D {
public:
    virtual ~IMiniMapView3D() = default;
    virtual int Width() = 0;                                       // cIGZWin 0x148
    virtual int Height() = 0;                                      // cIGZWin 0x150
};

// The renderer's camera control (cSC4CameraControl) as the minimap uses it.
class IMiniMapCameraControl {
public:
    virtual ~IMiniMapCameraControl() = default;
    virtual int Rotation() const = 0;                              // camera control +0x114
    virtual bool SetViewTargetPosition(const cS3DVector3& position) = 0;   // 0x10076bf2a
};

// SL::spRenderer (cSC43DRender) as the minimap uses it.
class IMiniMapRenderer {
public:
    virtual ~IMiniMapRenderer() = default;
    virtual IMiniMapCameraControl* CameraControl() = 0;            // 0x80
    virtual s3d::cS3DCamera* Camera() = 0;                         // 0x70
    virtual bool PickTerrainAlways(const float* origin, const float* direction, float* out, bool a) = 0;  // 0x1f0
};

class IMiniMapOccupantQuery {
public:
    virtual ~IMiniMapOccupantQuery() = default;
    virtual void Release() = 0;
    virtual bool GetFirstOccupant(int x, int z, cIGZUnknown*& out) = 0;   // occupant manager 0x108
};

// One colour ramp of the tuning exemplar: the property's uint32 array.
class IMiniMapRamp {
public:
    virtual ~IMiniMapRamp() = default;
    virtual const std::uint32_t* Data() = 0;                       // cRZVariant::RefUint32() (0x358)
    virtual int Count() = 0;                                       // cRZVariant::GetCount() (0x20)
};

class IMiniMapHost {
public:
    virtual ~IMiniMapHost() = default;
    virtual cIGZMessageServer2* MessageServer() = 0;               // SL::spMS2 (0x28 add / 0x30 remove)
    // RZ::GS() vslot 0x20: a fresh buffer with one reference (the class releases it).
    virtual cIGZBuffer* CreateBuffer() = 0;
    virtual int CityCellCountX() = 0;                              // city 0x2e8 (int)
    virtual int CityCellCountZ() = 0;                              // city 0x2f0
    virtual float CitySizeX() = 0;                                 // city 0x2c8 (float)
    virtual float CitySizeZ() = 0;                                 // city 0x2d0
    virtual IMiniMapSituation* CitySituationManager() = 0;         // city 0x188 (borrowed), may be null
    virtual IMiniMapTerrain* Terrain() = 0;                        // SL::spTerrain (borrowed), may be null
    virtual IMiniMapZoneGrid* ZoneGrid() = 0;                      // SL::spZoneManager -> grid (borrowed), may be null
    // The occupant filter + query the occupant pass installs: cSC4SurfaceNetworkOccupantFilter (a 0x18-byte
    // object created per pass, 0x1005c5cba) and cISC4OccupantManager vslot 0x108 GetFirstOccupant(x, z, filter).
    // `out` is the counted occupant; the query owns the filter's reference. Null when the manager is missing.
    virtual IMiniMapOccupantQuery* CreateSurfaceNetworkOccupantQuery() = 0;
    // nSC4UI::InterpolateRampToPalette @ 0x10030a7f0 (recovered in src/sc4/SC4UI.cpp, deliberately not linked
    // here so the minimap does not drag in the whole UI translation unit).
    virtual void InterpolateRamp(std::uint32_t* out, int count, const std::uint32_t* stops, int stopCount) = 0;
    virtual IMiniMapRenderer* Renderer() = 0;                      // SL::spRenderer (borrowed), may be null
    virtual IMiniMapView3D* View3DWindow() = 0;                   // SL::spView3DWin, may be null
    virtual void AddTickNotification(cIGZMessageTarget2* target) = 0;      // SL::ATM() 0x28(target, 0)
    virtual void RemoveTickNotification(cIGZMessageTarget2* target) = 0;   // SL::ATM() 0x30(target, 0)
    // SL::spRM 0x18({0x6534284a, 0x690f693f, 0x8a356f2c}, 0xa52160f5) -> exemplar 0x30 (holder) ->
    // 0x28 (property) -> 0x30 (variant) -> 0x358 RefUint32 / 0x20 GetCount. Null when the exemplar or the
    // property is missing.
    virtual IMiniMapRamp* FindRamp(std::uint32_t propertyId) = 0;
    // cISC4NetworkOccupant (QueryInterface 0xa821ef94) vslot 0x1d8: the occupant's city cell. False when the
    // occupant is not a network occupant.
    virtual bool NetworkOccupantCell(cIGZUnknown* occupant, int& cellX, int& cellZ) = 0;
    // The free function DrawLineAntialiased(cIGZBuffer*, int, int, int, int, unsigned int, int, cRZRect const*)
    // @ 0x100390498, always called with antialiasing = 1 and no clip rectangle. `target` is the window's draw
    // buffer (the original's cIGZBuffer, the port's IWinBuffer view of it).
    virtual bool DrawLineAntialiased(IWinBuffer* target, int x0, int y0, int x1, int y1, std::uint32_t color) = 0;
};
IMiniMapHost* MiniMapHost();
void SetMiniMapHost(IMiniMapHost* host);

// ---------------------------------------------------------------------------------------------------------------
// cISC4WinMiniMap: the interface view at +0x150 (iid 0xca318385).
// ---------------------------------------------------------------------------------------------------------------
class cISC4WinMiniMap : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                               // 0x18
    virtual void SetOverlay(cIGZBuffer* overlay) = 0;              // 0x20
    virtual void SetDefaultZoneColor(std::uint32_t color) = 0;     // 0x28
    virtual void SetDefaultZoneColorToTrans() = 0;                 // 0x30
    virtual void SetZoneColor(std::uint32_t index, std::uint32_t color) = 0;   // 0x38
    virtual std::uint32_t SetFlags(std::uint32_t mask, std::uint32_t value) = 0;   // 0x40
    virtual void Update() = 0;                                     // 0x48
};

// ---------------------------------------------------------------------------------------------------------------
// The window.
// ---------------------------------------------------------------------------------------------------------------
class cSC4WinMiniMap : public cGZWin, public cISC4WinMiniMap, public cIGZMessageTarget2 {
public:
    // The original's default constructor takes the window manager from the class manager; the port takes it
    // from the caller (the GZCOM factory passes the global window manager).
    explicit cSC4WinMiniMap(cIGZWinMgr* mgr);                    // 0x1005c4664
    ~cSC4WinMiniMap() override;                                  // 0x1005c474c (+0x1005c4832 alias, +0x1005c483c deleting)

    bool QueryInterface(std::uint32_t iid, void** out) override; // 0x1005c48c6 (+0x1005c48ea / 0x1005c4916 thunks)
    std::uint32_t AddRef() override { return cGZWin::AddRef(); } // 0x1005c486a (+0x1005c4874 / 0x1005c4886 thunks)
    std::uint32_t Release() override { return cGZWin::Release(); }   // 0x1005c4898 (+0x1005c48a2 / 0x1005c48b4)

    bool Init() override;                                        // 0x1005c4946
    bool Shutdown() override;                                    // 0x1005c4e66
    bool GZPaint() override;                                     // 0x1005c6a38
    void SetArea(int l, int t, int r, int b) override;           // 0x1005c4fda

    bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) override;   // 0x1005c5048
    bool GZOnMouseDownR(int x, int y, std::uint32_t modifiers) override;   // 0x1005c5258
    bool GZOnMouseUpL(int x, int y, std::uint32_t modifiers) override;     // 0x1005c520e
    bool GZOnMouseMove(int x, int y, std::uint32_t modifiers) override;    // 0x1005c5222
    bool GZOnMouseEnter(cIGZWin* from) override;                 // 0x1005c5238
    bool GZOnMouseExit(cIGZWin* to) override;                    // 0x1005c5248

    // --- cISC4WinMiniMap (the view at +0x150)
    cIGZWin* AsIGZWin() override { return this; }                // 0x1005c578e (+0x1005c5798 thunk)
    void SetOverlay(cIGZBuffer* overlay) override;               // 0x1005c57a6 (+0x1005c57f8 thunk)
    void SetDefaultZoneColor(std::uint32_t color) override;      // 0x1005c58d4 (+0x1005c591a thunk)
    void SetDefaultZoneColorToTrans() override;                  // 0x1005c5842 (+0x1005c588e thunk)
    void SetZoneColor(std::uint32_t index, std::uint32_t color) override;   // 0x1005c5956 (+0x1005c597a thunk)
    std::uint32_t SetFlags(std::uint32_t mask, std::uint32_t value) override;   // 0x1005c599a (+0x1005c59f6 thunk)
    void Update() override;                                      // 0x1005c5ba4 (+0x1005c6a26 thunk)

    // --- cIGZMessageTarget2 (the view at +0x158)
    using cGZWin::DoMessage;   // the original's vslot 0x18 is cGZWin::DoMessage(cGZMessage&)
    bool DoMessage(cIGZMessage2* message) override;              // 0x1005c5260 (+0x1005c577c thunk)

    // --- the class's own work
    void LoadTunableValues();                                    // 0x1005c4c5c
    void OnCityInit();                                           // 0x1005c4dbc
    void OnCityShutdown();                                       // 0x1005c568a
    void HookOccupantMessages();                                 // 0x1005c5acc
    void UnhookOccupantMessages();                               // 0x1005c4f76
    void InvalidateTZMapRect(const cRZRect& rect);               // 0x1005c56ea
    void InitTZMap();                                            // 0x1005c5a08
    void UpdateTerrainAndZoneMap();                              // 0x1005c637a
    void CenterOnMapPoint(int x, int y);                         // 0x1005c506e

    // The 16-byte terrain/zone map the picture is built in (cSC4WinMiniMap::cTZMap).
    struct cTZMap {
        bool Init(int width, int height);                        // 0x1005c5b3a
        std::uint32_t* pixels = nullptr;                         // +0
        int width = 0;                                           // +8
        int height = 0;                                          // +0xc
    };

    // --- test access (not part of the original API)
    std::uint32_t Flags() const { return flags_; }
    int MapSize() const { return mapSize_; }
    int ZoomShift() const { return zoomShift_; }
    bool CityInitialised() const { return cityInitialised_; }
    bool TerrainDirty() const { return terrainDirty_; }
    bool NeedsUpdate() const { return needsUpdate_; }
    bool Dragging() const { return dragging_; }
    bool OccupantMessagesHooked() const { return occupantMessagesHooked_; }
    bool MarkerBlink() const { return markerBlink_; }
    float TickAccumulator() const { return tickAccumulator_; }
    unsigned DirtyBlock(int block) const { return dirtyBlocks_[block & 15]; }
    std::uint32_t ZoneColor(int index) const { return zoneColors_[static_cast<std::size_t>(index)]; }
    std::uint32_t TerrainRampEntry(int index) const { return terrainRamp_[index & 0xff]; }
    std::uint32_t WaterRampEntry(int index) const { return waterRamp_[index & 0xff]; }
    cIGZBuffer* MapBuffer() const { return mapBuffer_; }
    cIGZBuffer* OverlayBuffer() const { return overlayBuffer_; }
    cTZMap& Map() { return map_; }
    const cTZMap& Map() const { return map_; }
    int CityCellsX() const { return cityCellsX_; }
    int CityCellsZ() const { return cityCellsZ_; }
    const std::vector<std::uint32_t>& CellFlags() const { return cellFlags_; }
    void SetDrawContextForTest(gz::cIGZDrawContext* context) { drawContext_ = context; }
    void SetDrawBufferForTest(IWinBuffer* buffer) { drawBuffer_ = buffer; }

private:
    std::uint32_t flags_ = 7;                  // +0x160
    int mapSize_ = 0;                          // +0x164
    int cityCellsX_ = 0;                       // +0x168
    int cityCellsZ_ = 0;                       // +0x16c
    cIGZBuffer* mapBuffer_ = nullptr;          // +0x170
    cIGZBuffer* overlayBuffer_ = nullptr;      // +0x178
    float tickAccumulator_ = 0.0f;             // +0x180
    bool cityInitialised_ = false;             // +0x184
    bool terrainDirty_ = false;                // +0x185
    bool needsUpdate_ = false;                 // +0x186
    bool repaintRequested_ = false;            // +0x187
    bool dragging_ = false;                    // +0x188
    bool occupantMessagesHooked_ = false;      // +0x189
    bool markerBlink_ = false;                 // +0x18a
    int zoomShift_ = 0;                        // +0x18c
    std::vector<std::uint32_t> zoneColors_;    // +0x190 (16)
    cTZMap map_;                               // +0x1a8
    std::uint32_t dirtyBlocks_[16] = {};       // +0x1b8
    std::uint32_t terrainRamp_[256] = {};      // +0x1f8
    std::uint32_t waterRamp_[256] = {};        // +0x5f8
    std::vector<std::uint32_t> cellFlags_;     // +0x9f8
};

}  // namespace sc4
