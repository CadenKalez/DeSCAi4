// cSTETerrain — the city terrain model: a (cells+1)^2 vertex grid of 16 m cells with altitude,
// water depth, vertex normals and per-vertex property flags. Recovered from Sim City 4 Deluxe
// Editionsub 1.2.1 (vtable 0x100aea6c0): the data model, point/cell queries, lighting-free setters,
// the three save/load subfiles, the four edge-altitude subfiles and the stream Read/Write wrapper;
// then the COM identity (QueryInterface/AddRef/Release plus the three thunks), the message posts,
// the display/accumulation state, the vertex editing helpers, the mark/rect/span helpers, the
// temporary water-level copies, the brush tuning queries and the lighting row walk.
// Still pending in notes/ledger.csv: the brush application functions, the surface-texture/exemplar
// pipeline (ReadTextureInfos/ReadTerrainIniFile/ReadTuningParameters/DecideSurfaceCover/Assign-
// SurfaceTexturesAndColors), erosion and floods, the cliff/shore tracking, the volcanic ash and wave
// producers, the life cycle (Init/Shutdown/DoPostCityInit/DoMessage) and the neighbour-city edge
// reconcile.
//
// Vertex (36 bytes): +0 altitude, +4/+8/+0xc normal, +0x10..0x21 texture/colour data, +0x22 u16
// property flags:
//   0x0020 even triangle hidden   0x0040 odd triangle hidden   (0x60 = whole cell hidden)
//   0x0200 cell flag 4            0x1000 internal triangulation flipped
//   0x2000 external triangulation flipped / cell flag 8         0x4000 altitude locked
// GetTerrainCellFlags packs them as bit0-1 = (flags>>5)&3, bit2 = (flags>>7)&4, bit3 = (flags>>10)&8.
//
// Water depth lives in the surface-water object in the original (cSTESurfaceWater, same 36-byte
// stride); the model keeps the depth array itself and hands it out through WaterDepth(). A vertex
// is "land" when its water depth is <= 0, "water" when > 0 and "sea" when its altitude is below
// the sea level.
//
// Saved subfiles (group 0xe98f9525): 0x8a91e7e3/3 {u32 vertsX, u32 vertsZ, s32 terrainType,
// f32 minHeight, f32 maxHeight, f32 maxNormalHeight} (version 2), 0xa9dd6ff4/1 {f32 altitude per
// vertex, row-major} (version 2), 0x8a91e7e0/2 {u16 flags per vertex} (version 1).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "gz/cIGZPersistDBSegment.h"

class cIGZMessageServer2;
class cIGZMessage2Standard;
class cIGZSerializable;
class cIGZMessageTarget2;
class cIGZUnknown;
class cIGZOStream;
class cIGZIStream;

namespace sc4 {

struct TerrainRect {                // SC4Rect<int>: inclusive vertex bounds
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
};

struct TerrainVector3 { float x = 0, y = 0, z = 0; };

struct TerrainPoint { float x = 0, z = 0; };   // SC4Point<float> (x at +0, z at +4)

struct TerrainTuning {              // statics read from the terrain ini / exemplars at startup
    float seaLevel = 250.0f;
    float minSeaLevel = -10.1f, maxSeaLevel = 200.1f;
    float seaRaiseLowerAmount = 20.0f;
    float maxTerrainHeight = 200.0f;
};

constexpr float kTerrainCellWidth = 16.0f;
constexpr std::uint16_t kVertexHiddenEven = 0x0020, kVertexHiddenOdd = 0x0040, kVertexHiddenWhole = 0x0060;
constexpr std::uint16_t kVertexFlipInternal = 0x1000, kVertexFlipExternal = 0x2000, kVertexLocked = 0x4000;
constexpr std::uint16_t kVertexMark1 = 0x0001, kVertexMark3 = 0x0004;      // scratch marks of MarkRegion
constexpr std::uint16_t kVertexCliffFlip = 0x0800;                        // cliff flip-triangulation mark

// Statics of the binary (read from .rodata / the data segment):
constexpr float kMinTerrainHeight = 5.0f;        // 0x100b17640 (MinAllowedHeight)
constexpr float kMaxTerrainHeight = 200.0f;      // 0x100b1763c (MaxAllowedHeight)
constexpr float kTerrainRaiseLowerAmount = 20.0f;  // 0x100b175e0 (GetDefaultTerrainRaiseLowerAmount)
constexpr std::uint32_t kGZCLSID_cSTETerrain = 0xe98f9525;

// cISTETerrain::tUIElementsForBrush: five 8-byte pairs (min/max of the brush's UI elements); the
// original copies 0x28 bytes straight out of the brush description at +0x5c.
struct TerrainBrushUIElements {
    std::uint32_t values[10] = {};
};

// std::map<unsigned int, tBrushDescription> value: one brush's tuning, read from its exemplar by
// ReadBrushDescriptions (pending, TERRAIN-5). The original keeps a *pointer* to the per-zoom radii at
// +0x18; the port keeps the array inline. +0x48 is the per-zoom cost, +0x5c the UI elements.
struct TerrainBrushDescription {
    float radius[8] = {};
    float cost[5] = {};
    TerrainBrushUIElements ui;
};

// The engine services the model reaches for (seams; see docs/SEAMS.md). One method per original call.
class ITerrainHost {
public:
    virtual ~ITerrainHost() = default;
    virtual cIGZMessageServer2* Messages() = 0;                      // SL::spMS2 (PostMessageA / PostMessageToTarget)
    // `new cRZMessage2Standard` (+ its constructor at 0x1000412ce); the model fills in the payload.
    virtual cIGZMessage2Standard* NewMessage() = 0;
    // SC4WriteGZSerializable @ 0x1000435e3 / SC4ReadGZSerializable @ 0x100043267 (DBSegment unit).
    virtual bool WriteGZSerializable(cIGZOStream& stream, const cIGZUnknown* object) = 0;
    virtual bool ReadGZSerializable(cIGZIStream& stream, std::uint32_t iid, void** out) = 0;
    // cRZRandom::RandomDoubleRangeUniform (the class's seeded sRand, call at 0x10003d3e4).
    virtual double RandomDoubleRangeUniform(double lo, double hi) = 0;
    // SL::spLightingManager vslot 0xe0 (DoLighting 0x100737cba): light one row of terrain vertices.
    virtual void ApplyTerrainLightingRow(std::int32_t z, std::int32_t x, std::int32_t endX, float* normals,
                                         std::int32_t normalStride, void* colours, std::int32_t colourStride) = 0;
};
ITerrainHost* TerrainHost();
void SetTerrainHost(ITerrainHost* host);

// cISTETerrainView: the subobject GetView() returns (the view object at +0x110 plus 0x18). Only the
// calls this class makes are modelled, so the interface is not the full cISTETerrainView yet.
class ITerrainView {
public:
    virtual ~ITerrainView() = default;
    virtual cIGZSerializable* Serializable() = 0;   // the view object's +0x28 subobject (Write 0x10073efda)
};

class cSTETerrain {
public:
    struct Vertex {
        float altitude = 0;
        float normal[3] = {0, 0, 0};
        std::uint8_t surface[0x12] = {};
        std::uint16_t flags = 0;
    };
    static_assert(sizeof(Vertex) == 0x24);

    cSTETerrain() = default;

    // ---- COM identity (vtable groups at +0x0 / +0x8 / +0x10) --------------------------------
    bool QueryInterface(std::uint32_t iid, void** out);               // 0x10073aa92
    std::uint32_t AddRef();                                           // 0x1007313ee
    std::uint32_t Release();                                          // 0x10073e0ba (destroys at <= 1)
    std::uint32_t GetGZCLSID() const { return kGZCLSID_cSTETerrain; }  // 0x100738ffa
    // The three non-virtual thunks. A class with the original's three bases gets them from the
    // compiler; the port has no multiple inheritance, so they are plain forwarders.
    std::uint32_t AddRefThunk_13fc() { return AddRef(); }             // 0x1007313fc (this - 8)
    std::uint32_t AddRefThunk_140a() { return AddRef(); }             // 0x10073140a (this - 0x10)
    std::uint32_t ReleaseThunk_e0da() { return Release(); }           // 0x10073e0da (this - 8)
    std::uint32_t ReleaseThunk_e0fe() { return Release(); }           // 0x10073e0fe (this - 0x10)
    bool QueryInterfaceThunk_aae2(std::uint32_t iid, void** out) { return QueryInterface(iid, out); }  // 0x10073aae2
    bool QueryInterfaceThunk_aaf0(std::uint32_t iid, void** out) { return QueryInterface(iid, out); }  // 0x10073aaf0
    std::uint32_t GetGZCLSIDThunk() const { return GetGZCLSID(); }    // 0x100739006 (this - 0x10)

    // ---- long operations / messages ---------------------------------------------------------
    void BeginLongTerrainOperation();                                 // 0x100734b7a (posts 0xea3b0af1)
    void EndLongTerrainOperation();                                   // 0x1007386ec (posts 0x6a3b0afd)
    void PostMessageA(std::uint32_t type, std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d);  // 0x100734b92
    void PostMessageToTarget(std::uint32_t type, cIGZMessageTarget2* target);                                  // 0x100737684

    // ---- display state ----------------------------------------------------------------------
    bool GetWaterSurfaceDisplayState() const { return displayWater_; }  // 0x1007394b6 (+0xfd, default true)
    bool GetPlumbingInfoDisplayState() const;                           // 0x1007394c2 (static flag)
    void SetPlumbingInfoDisplayState(bool on);                          // 0x10073e418 (static flag)
    void SetWaterSurfaceDisplayState(bool on);                          // 0x10073e36c (posts 0x6ab97795)
    void StartAccumulatingTextureUpdates();                            // 0x10073e90e
    void StopAccumulatingTextureUpdates();                             // 0x10073ea34
    bool IsAccumulatingTextureUpdates() const { return accumulating_; }  // 0x100740f24 (+0xe0)
    ITerrainView* GetView() const { return view_; }                     // 0x100739a1e (+0x110 -> +0x18)
    void SetView(ITerrainView* view) { view_ = view; }                  // hook-up aid for +0x110
    bool DisplayEnabled() const { return displayEnabled_; }             // +0x118 (DoPostCityInit sets it)
    void SetDisplayEnabled(bool on) { displayEnabled_ = on; }
    void SetZoomLevel(std::uint32_t zoom) { zoom_ = zoom; }             // +0xdc (spViewUtilities +0x14)

    // ---- limits / counts --------------------------------------------------------------------
    static float MinAllowedHeight() { return kMinTerrainHeight; }             // 0x100740ec0
    static float MaxAllowedHeight() { return kMaxTerrainHeight; }             // 0x100740ece
    static float GetDefaultTerrainRaiseLowerAmount() { return kTerrainRaiseLowerAmount; }  // 0x100740f0a
    std::uint32_t NumberOfWaterVertices() const { return waterVertexCount_; }  // 0x100740f18 (+0x108)
    int NumberOfWaterVertices(const TerrainRect& r) const;                    // 0x10073aa02
    bool AreSeaVertices(const TerrainRect& r) const;                          // 0x100733b16
    float GetAltitudeCheckSum(const TerrainRect& r) const;                     // 0x100738b4a

    // ---- vertex editing ---------------------------------------------------------------------
    void LowerVertex(std::uint32_t index, float amount);             // 0x100734c7c (clamps to 5 m)
    void RaiseVertex(std::uint32_t index, float amount);             // 0x100734cde (clamps to 200 m)
    void SetAltitudeAtVertex(std::int32_t x, std::int32_t z, float altitude);  // 0x10073e2b2
    void ClearVertexPropertyFlags(std::uint16_t mask);               // 0x100735810
    void ClearVertexPropertyFlags(const TerrainRect& r, std::uint16_t mask);   // 0x100735288
    void ClearVertexCliffFlipTriangulationFlags(const TerrainRect& r);        // 0x1007352f0
    bool IsVertexWithMark1(std::int32_t index) const;                // 0x100736930 (flags & 1)
    bool IsLandAndNotMark1(std::int32_t index) const;                // 0x10073694c
    bool IsVertexWithMark3(std::int32_t index) const;                // 0x100736fd4 (flags & 4)
    bool IsLandAndNotMark3(std::int32_t index) const;                // 0x100736ff2
    void PadRectangle(TerrainRect& r, int amount) const;             // 0x100733596
    void CreateVertRectAroundVertex(TerrainRect& r, int x, int z, int radius) const;  // 0x100736cf8
    float GetSedimentValue(float x, float z) const;                  // 0x1007394dc (surface water +0x1c)
    std::int32_t GetNeighborWithLowestWaterLevel(std::int32_t index) const;    // 0x100737b50

    // ---- temporary water-level copies -------------------------------------------------------
    void SaveWaterLevelsInTempArray(TerrainRect& r);                 // 0x10073e122
    void RestoreWaterLevelsFromTempArray(TerrainRect& r);            // 0x100737ada

    // ---- surface textures (the part that needs no exemplar data) ----------------------------
    void ClearSurfaceTextures(const TerrainRect& r);                 // 0x100733dd4 (three layer keys -> 0)
    void UpdateMoistureValues(const TerrainRect& r);                 // 0x100733d98 (spLandsim seam)

    // ---- brushed-region helpers -------------------------------------------------------------
    void ComputeBoundsOfMarkedRegion(TerrainRect& r, std::uint16_t mask) const;   // 0x1007359a2
    // 0x10073255c: the four corners of a rotated rectangle around (x, z), in order.
    void ComputeRectAroundPt(TerrainPoint out[4], float x, float z, float width, float height, float angle);
    // 0x100732656: per-row left/right cell spans of the quad (rows indexed by z).
    void ComputeSpansForRectangle(const TerrainPoint* quad, int* spanMin, int* spanMax, int& firstRow,
                                  int& lastRow) const;
    // 0x100735846: whether at least `fraction` of the covered vertices satisfy `fn`.
    bool ComputeBooleanFnForRect(const TerrainPoint* quad, bool (cSTETerrain::*fn)(int) const,
                                 float fraction) const;

    // ---- brush tuning -----------------------------------------------------------------------
    float BrushRadius(std::uint32_t brushType) const;                // 0x100734d6e (default 32 m)
    std::int64_t GetCostPerBrushApplication(std::uint32_t brushType) const;  // 0x100738d76 (default 32)
    bool GetUIElementsForBrush(std::uint32_t brushType, TerrainBrushUIElements& out) const;  // 0x100739976
    bool GetBrushExemplarID(std::int32_t brushType, std::uint32_t& exemplarID) const;        // 0x100738cee
    void InitBrushParameters();                                      // 0x10073a3aa (per-brush phases)
    float BrushPhase(int index) const { return brushPhase_[index & 7]; }   // +0x248..+0x264
    static void ReleaseBrushDescriptions();                          // 0x10073b548
    static std::map<std::uint32_t, TerrainBrushDescription>& BrushDescriptions();   // sBrushDescriptionMap

    // ---- lighting ---------------------------------------------------------------------------
    void DoLighting(const TerrainRect& r);                           // 0x100737cba (spLightingManager 0xe0)

    // ---- persistence ------------------------------------------------------------------------
    void Write(cIGZOStream& stream) const;                           // 0x10073efda
    bool Read(cIGZIStream& stream);                                  // 0x10073f0a2
    bool WriteThunk(cIGZOStream& stream) const { Write(stream); return true; }   // 0x10073f094 (this - 0x10)
    bool ReadThunk(cIGZIStream& stream) { return Read(stream); }               // 0x10073f16c (this - 0x10)
    void SaveObliterated(cIGZPersistDBSegment* segment);             // 0x100740de0
    void LoadObliterated(cIGZPersistDBSegment* segment);             // 0x100740e12
    bool SaveEdgeAltitudes(int edge, cIGZPersistDBSegment* segment) const;    // 0x10073f3fc
    bool LoadEdgeAltitudes(int edge, cIGZPersistDBSegment* segment, std::vector<float>& out) const;  // 0x10073f85e

    // Lays out the grid for a city of cellsX x cellsZ cells (zeroed vertices, sea-level water).
    bool Init(std::uint32_t cellsX, std::uint32_t cellsZ, const TerrainTuning& tuning = {});
    bool IsInitialized() const { return initialized_; }
    // Called by view code that caches triangle indices: (cellZ, cellX) of a cell whose flags changed.
    void SetCellChangedCallback(std::function<void(std::uint32_t, std::uint32_t)> cb) { onCellChanged_ = std::move(cb); }

    // ---- dimensions -------------------------------------------------------------------------
    static float CellWidth() { return kTerrainCellWidth; }
    std::uint32_t CellCountX() const { return cellsX_; }
    std::uint32_t CellCountZ() const { return cellsZ_; }
    std::uint32_t VertexCountX() const { return vertsX_; }
    std::uint32_t VertexCountZ() const { return vertsZ_; }
    std::uint32_t VertexCount() const { return totalVerts_; }
    std::int32_t GetVertIndex(std::int32_t x, std::int32_t z) const { return z * static_cast<std::int32_t>(vertsX_) + x; }
    void GetVertex(std::int32_t index, std::int32_t& x, std::int32_t& z) const {
        z = static_cast<std::int32_t>(static_cast<std::uint32_t>(index) / vertsX_);
        x = static_cast<std::int32_t>(static_cast<std::uint32_t>(index) % vertsX_);
    }
    bool LocationIsInBounds(float x, float z) const { return x >= 0 && z >= 0 && x <= widthMeters_ && z <= depthMeters_; }
    std::int32_t GetTerrainGridIndex(float x, float z) const {
        return static_cast<std::int32_t>(x * 0.0625f) + static_cast<std::int32_t>(z * 0.0625f) * static_cast<std::int32_t>(vertsX_);
    }
    // Bit mask of the existing neighbours of cell/vertex (x, z): bit i = direction i of the table
    // E, NE, N... as the original packs them (0x7c edge pattern when x <= 0, etc.).
    std::uint8_t GetNeighbors(std::int32_t x, std::int32_t z) const;
    float MinHeight() const { return minHeight_; }
    float MaxHeight() const { return maxHeight_; }
    float MaxNormalTerrainHeight() const { return maxNormalHeight_; }
    std::int32_t TerrainType() const { return terrainType_; }
    void SetTerrainTypeRaw(std::int32_t type) { terrainType_ = type; }

    // ---- altitude ---------------------------------------------------------------------------
    float GetAltitude(float x, float z) const;                       // triangulated interpolation
    float GetAltitude(std::int32_t index) const;
    float GetAltitudeAtVertex(std::int32_t x, std::int32_t z) const;
    float GetAltitudeAtNearestGrid(float x, float z) const;
    void GetCellCornerAltitudes(std::uint32_t cx, std::uint32_t cz, float out[4]) const;
    void GetAltitudes(const TerrainRect& r, float* out, std::ptrdiff_t rowStride, std::ptrdiff_t colStride) const;
    void FindMinAndMaxHeights();                                      // whole grid
    void FindMinAndMaxHeights(const TerrainRect& r, float& lo, float& hi) const;
    void SetAltitude(std::int32_t index, float altitude);             // honours the lock flag
    void SetAltitude(const TerrainRect& r, float altitude, bool recomputeWater);
    void LockAltitude(const TerrainRect& r, bool lock);                // sets/clears the 0x4000 flag

    // ---- normals ----------------------------------------------------------------------------
    void GetFaceNormal(float x, float z, TerrainVector3& out) const;
    void GetLerpedNormal(float x, float z, TerrainVector3& out) const;
    void GetVertexNormal(std::uint32_t x, std::uint32_t z, TerrainVector3& out) const;

    // ---- water / sea ------------------------------------------------------------------------
    float GetSeaLevel() const { return tuning_.seaLevel; }
    bool SetSeaLevel(float level);                                    // range checked, resets the water
    void ChangeSeaLevel(bool raise) { SetSeaLevel(tuning_.seaLevel + (raise ? tuning_.seaRaiseLowerAmount : -tuning_.seaRaiseLowerAmount)); }
    float GetWaterDepthAtVertex(std::int32_t index) const;
    float GetWaterDepthAtVertex(std::int32_t x, std::int32_t z) const;
    float GetWaterLevel(std::int32_t index) const;                    // altitude + depth
    float GetWaterLevel(float x, float z) const;
    float GetLerpedWaterLevel(float x, float z) const;
    float GetMaxSurfaceLevel(std::uint32_t cx, std::uint32_t cz) const;
    void GetWaterDepths(const TerrainRect& r, float* out, std::ptrdiff_t rowStride, std::ptrdiff_t colStride) const;
    void RecomputeWaterLevel(const TerrainRect& r);                   // depth = sea - altitude where below
    bool IsSeaVertex(std::int32_t index) const;
    bool IsSeaVertex(std::uint32_t x, std::uint32_t z) const;
    bool IsWaterVertex(std::int32_t index) const;
    bool IsWaterVertex(std::uint32_t x, std::uint32_t z) const;
    bool IsLandVertex(std::int32_t index) const;
    bool IsLandVertex(std::uint32_t x, std::uint32_t z) const;
    bool IsShoreVertex(std::int32_t index) const;
    bool CellHasWater(std::uint32_t cx, std::uint32_t cz) const;
    bool CellHasLand(std::uint32_t cx, std::uint32_t cz) const;
    std::vector<float>& WaterDepth() { return waterDepth_; }
    const std::vector<float>& WaterDepth() const { return waterDepth_; }
    // cSTESurfaceWater's sediment channel (vertex +0x1c); the water simulation fills it.
    std::vector<float>& Sediment() { return sediment_; }
    const std::vector<float>& Sediment() const { return sediment_; }

    // ---- cell flags / triangulation ---------------------------------------------------------
    std::uint16_t GetTerrainCellFlags(std::uint32_t cx, std::uint32_t cz) const;
    bool SetTerrainCellFlags(std::uint32_t cx, std::uint32_t cz, std::uint32_t flags);
    void HideTerrainCell(std::uint32_t cx, std::uint32_t cz, bool hide);
    void HideEvenTriangle(std::uint32_t cx, std::uint32_t cz, bool hide);
    void HideOddTriangle(std::uint32_t cx, std::uint32_t cz, bool hide);
    bool IsEvenTriangleHidden(std::uint32_t cx, std::uint32_t cz) const;
    bool IsOddTriangleHidden(std::uint32_t cx, std::uint32_t cz) const;
    bool IsWholeCellHidden(std::uint32_t cx, std::uint32_t cz) const;
    bool IsPointHidden(float x, float z) const;
    bool IsTriangulationFlipped(std::uint32_t cx, std::uint32_t cz) const;
    void FlipInternalTriangulation(std::uint32_t cx, std::uint32_t cz, bool flip);
    void FlipExternalTriangulation(std::uint32_t cx, std::uint32_t cz, bool flip);

    // ---- persistence ------------------------------------------------------------------------
    bool Load(cIGZPersistDBSegment* segment);
    bool Save(cIGZPersistDBSegment* segment) const;
    bool LoadMiscTerrainData(cIGZPersistDBSegment* segment);
    bool LoadAltitudes(cIGZPersistDBSegment* segment);
    bool LoadVertexProperties(cIGZPersistDBSegment* segment);
    bool SaveMiscTerrainData(cIGZPersistDBSegment* segment) const;
    bool SaveAltitudes(cIGZPersistDBSegment* segment) const;
    bool SaveVertexProperties(cIGZPersistDBSegment* segment) const;
    bool LastLoadSucceeded() const { return loadOk_; }

    const std::vector<Vertex>& Vertices() const { return vertices_; }
    std::vector<Vertex>& Vertices() { return vertices_; }

private:
    std::uint32_t ClampCell(float coordinate, std::uint32_t limit) const;
    static std::uint32_t FloorCell(float coordinate);
    void CellChanged(std::uint32_t cx, std::uint32_t cz) { if (onCellChanged_) onCellChanged_(cz, cx); }
    std::uint16_t& Flags(std::int32_t index) { return vertices_[static_cast<std::size_t>(index)].flags; }
    std::uint16_t Flags(std::int32_t index) const { return vertices_[static_cast<std::size_t>(index)].flags; }

    bool initialized_ = false;
    std::uint32_t refCount_ = 0;                 // +0x1c
    TerrainTuning tuning_;
    std::uint32_t cellsX_ = 0, cellsZ_ = 0, lastCellX_ = 0, lastCellZ_ = 0;
    std::uint32_t vertsX_ = 0, vertsZ_ = 0, totalVerts_ = 0;
    float widthMeters_ = 0, depthMeters_ = 0;
    std::int32_t terrainType_ = 0;
    float minHeight_ = 0, maxHeight_ = 0, maxNormalHeight_ = 0;
    bool loadOk_ = false;
    std::int32_t neighborOffsets_[8] = {};        // +0xa4..+0xc0
    TerrainRect gridRect_;                        // +0x58: the whole grid as a vertex rect
    std::vector<Vertex> vertices_;
    std::vector<float> waterDepth_;               // cSTESurfaceWater vertex +0x0 (elided class)
    std::vector<float> sediment_;                 // cSTESurfaceWater vertex +0x1c
    std::vector<float> tempWaterLevels_;          // +0x100 (SaveWaterLevelsInTempArray)
    std::uint32_t waterVertexCount_ = 0;          // +0x108
    std::uint32_t zoom_ = 0;                      // +0xdc (indexes the brush radii)
    bool accumulating_ = false;                   // +0xe0
    TerrainRect accumulateRect_ = {0x7fffffff, 0x7fffffff, -0x7fffffff, -0x7fffffff};   // +0xe4
    bool displayWater_ = true;                    // +0xfd
    bool displayEnabled_ = false;                 // +0x118 (set by DoPostCityInit)
    float brushPhase_[8] = {};                    // +0x248..+0x264
    ITerrainView* view_ = nullptr;                // +0x110 (the port keeps the +0x18 interface)
    std::function<void(std::uint32_t, std::uint32_t)> onCellChanged_;
};

}  // namespace sc4
