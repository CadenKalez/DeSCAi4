// cSTETerrain — the city terrain model: a (cells+1)^2 vertex grid of 16 m cells with altitude,
// water depth, vertex normals and per-vertex property flags. Recovered from Sim City 4 Deluxe
// Editionsub 1.2.1 (vtable 0x100aea6c0): the data model, point/cell queries, lighting-free setters,
// the three save/load subfiles, the four edge-altitude subfiles and the stream Read/Write wrapper;
// then the COM identity (QueryInterface/AddRef/Release plus the three thunks), the message posts,
// the display/accumulation state, the vertex editing helpers, the mark/rect/span helpers, the
// temporary water-level copies, the brush tuning queries and the lighting row walk.
// Part 3 adds the first-boot path through the host seams (Init/Shutdown/StaticInit/StaticShutdown/
// DoPostCityInit/DoMessage/RedisplayTerrain/SetTerrainType/SetAltitude), the water proximity /
// land-type-ratio surface helpers, the shoreline helpers and the display altitude range.
// Part 4 adds the exemplar/texture pipeline: the four data readers (ReadTextureInfos/ReadTextureInfo/
// ReadTerrainIniFile/ReadTuningParameters plus the ini callbacks), DetermineTextureFamilyMembersForGridCells,
// DecideSurfaceCover, the completed AssignSurfaceTexturesAndColors, and the two-triangle normal pass
// CalculateNormalsAndAssignCliffTextures with MarkCliff.
// Still pending in notes/ledger.csv: the brush application functions (they need the
// cISTETerrain::tBrushParameters / tBrushContext layouts and ReadBrushDescriptions), the erosion and flood
// kernels (DoWaterErosion/DoWindErosion/DoFloodFlowStep), the volcanic ash and wave producers, Create and the
// shoreline shaping functions, and the neighbour-city edge reconcile.
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
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "gz/cIGZPersistDBSegment.h"
#include "rz/cRZString.h"

class cIGZMessageServer2;
class cIGZMessage2;
class cIGZMessage2Standard;
class cIGZSerializable;
class cIGZMessageTarget2;
class cIGZUnknown;
class cIGZCommandServer;
class cIGZPersistResourceManager;
class cIGZOStream;
class cIGZIStream;

namespace sc4 {

class cISCPropertyHolder;

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
constexpr std::uint16_t kVertexCliffMark = 0x0008;                        // MarkCliff: vertex touches a cliff
constexpr std::uint32_t kGZCLSID_cSTETerrain = 0xe98f9525;

// The class's mutable globals. The original keeps them as individual symbols:
//  * the initialised ones live in __data (0x100b175c0..0x100b17698) and are read by ReadTerrainIniFile /
//    ReadTuningParameters; the defaults below are the bytes in the binary (scripts/macho_read.py on the
//    __data section), not guesses.
//  * the zero-filled ones live in __common (0x100b713d0..0x100b71e30) and are filled when the terrain
//    exemplar / ini resources exist (the two demo defaults that keep the un-tuned port usable are marked).
// All of them belong to the class in the original; the struct is this port's storage for them.
struct TerrainStatics {
    // ---- __data: the state the terrain-ini reader fills (defaults = the binary's initialised bytes) ----------
    bool simulationEnabled = true;                              // 0x100b175c0
    float waterSourceCapacity = 5.5f;                           // 0x100b175c4
    float waterSinkCapacity = 5.5f;                             // 0x100b175c8
    float surfaceWaterThreshold = 10.0f;                        // 0x100b175cc
    float vertexHeightRaiseLowerAmount = 10.5f;                 // 0x100b175d0
    float minSeaLevel = -10.1f;                                 // 0x100b175d4
    float maxSeaLevel = 200.1f;                                 // 0x100b175d8
    float seaRaiseLowerAmount = 20.0f;                          // 0x100b175dc
    float terrainRaiseLowerAmount = 20.0f;                      // 0x100b175e0
    std::uint8_t maxBeachWidth = 4;                             // 0x100b175e4
    float maxBeachAltitude = 7.0f;                              // 0x100b175e8
    std::int32_t numWindErosionIterationsPerUserClick = 5;      // 0x100b175ec
    std::int32_t numWindErosionIterationsPerSimMonth = 1;       // 0x100b175f0
    float talusAngle = 0.79f;                                   // 0x100b175f4 (radians; the ini's degrees * pi/180)
    float diagonalHeightDifferenceThreshold = 22.624f;          // 0x100b175f8 (= tan(talusAngle) * 16 * sqrt 2)
    float crossHeightDifferenceThreshold = 16.0f;               // 0x100b175fc (= tan(talusAngle) * 16)
    std::int32_t numWaterErosionIterationsPerPlayerClick = 1;   // 0x100b17600
    std::int32_t numFlowStepsForGlobalWaterErosion = 35;        // 0x100b17604
    float rainFallFactor = 0.01f;                               // 0x100b17608
    float maxSedimentPerUnitWater = 5.0f;                       // 0x100b1760c
    float hydraulicDepositRate = 0.2f;                          // 0x100b17610
    float hydraulicDepositRateUnderSea = 0.02f;                 // 0x100b17614
    float minHydraulicErosionRate = 0.02f;                      // 0x100b17618
    float maxHydraulicErosionRate = 0.06f;                      // 0x100b1761c
    float hydraulicErosionRateRampBegin = 5.128f;               // 0x100b17620
    float hydraulicErosionRateRampEnd = 38.14f;                 // 0x100b17624
    float sedimentInfluenceOnWaterColor = 0.1f;                 // 0x100b17628
    float maxSedimentAlpha = 0.5f;                              // 0x100b1762c
    std::uint8_t sedimentColorRed = 118;                        // 0x100b17630
    std::uint8_t sedimentColorGreen = 113;                      // 0x100b17631
    std::uint8_t sedimentColorBlue = 104;                       // 0x100b17632
    float minWaterDepthForDeepSeaBedTexture = 15.0f;            // 0x100b17634
    float maxWaterDepthForDeepSeaBedTexture = 30.0f;            // 0x100b17638
    float maxTerrainHeight = 200.0f;                            // 0x100b1763c
    float minTerrainHeight = 5.0f;                              // 0x100b17640
    float defaultTerrainHeight = 50.0f;                         // 0x100b17644
    float imageImportScaleFactor = 1.5f;                        // 0x100b17648
    std::int32_t numSmoothingIterationsAfterWaterErosion = 1;   // 0x100b1764c
    std::int32_t minCellsInUsefulFlatArea = 0;                  // 0x100b17650 (tuning exemplar only)
    float maxHeightChangeToFlattenFactor = 0.0f;                // 0x100b17654
    float minTerraceAltitudeFactor = 0.0f;                      // 0x100b17658
    float maxTerraceAltitudeFactor = 0.0f;                      // 0x100b1765c
    std::int32_t numSmoothingPassesAfterAutoFlattening = 0;     // 0x100b17660
    std::int32_t autoFlatAreaEdgeSmootheningRadius = 0;         // 0x100b17664
    bool shoreCliffsUponCreation = false;                       // 0x100b17668
    float minShoreCliffSize = 0.0f;                             // 0x100b1766c
    float maxShoreCliffSize = 0.0f;                             // 0x100b17670
    float minShoreCliffCandidateHeightSum = 0.0f;               // 0x100b17674 (x6 after the exemplar read)
    float minShoreCliffHeight = 0.0f;                           // 0x100b17678
    float maxShoreCliffHeight = 0.0f;                           // 0x100b1767c
    float maxNormalYForCliff = 0.3f;                            // 0x100b17680
    float volcanicAshLifeTime = 0.0f;                           // 0x100b17684
    float volcanicAshUpdateInterval = 0.0f;                     // 0x100b17688
    std::int32_t edgeReconciliationSmoothingIterations = 0;     // 0x100b17694
    float edgeReconciliationHeightDifferenceThreshold = 0.0f;   // 0x100b17698

    // ---- __common: the terrain-tuning exemplar's statics (zero until the exemplar is read) -------------------
    float seaLevel = 250.0f;                                    // 0x100b71d48 (the port's demo default)
    std::vector<float> maxNormalTerrainHeight;                  // 0x100b71d30 (indexed by terrain type)
    std::vector<float> minRegularHeightFactor, maxRegularHeightFactor;      // 0x100b71d50 / d68
    std::vector<float> minMountainHeightFactor, maxMountainHeightFactor;    // 0x100b71d80 / d98
    float temperatureRangeFactor = 0.0f;                        // 0x100b71db0
    float waterProximityRangeFactor = 10.0f;                    // 0x100b71db4 (the port's demo default)
    std::int32_t numInitialErosionPasses = 0;                   // 0x100b71db8
    std::int32_t numHydraulicIterationsPerInitialErosionPass = 0;   // 0x100b71dbc
    std::int32_t numWindIterationsPerInitialErosionPass = 0;        // 0x100b71dc0
    std::vector<float> seaLevelChangeDueToWaterSlider;          // 0x100b71dc8
    bool autoFlattenUponCreation = false;                       // 0x100b71de0
    std::vector<float> typicalBrushAngleIncrement;              // 0x100b71de8 (radians after the read)
    std::vector<std::uint32_t> numFlowStepsPerWaterErosionBrushApplication;   // 0x100b71e00
    float windErosionFactor = 0.0f;                             // 0x100b71d28
    float sedimentSpreadRateUnderSea = 0.0f;                    // 0x100b71d2c
    std::int32_t maxSurfaceTextureSpread[8] = {0, 0, 0, 0, 0, 0, 0, 0};   // 0x100b71d10 (snMaxSurfaceTextureSpread)
    std::vector<std::uint32_t> textureRandomizationGridSize;    // 0x100b713f0 (one entry per zoom)
};

// cSTETerrain::tSurfaceCoverInfo: the per-texture surface-cover tuning, keyed by texture id in
// sSurfaceCoverTextures. The original's layout (0xd8-byte map value): +0x08 weight, +0x10+zoom*0x20 five
// cSC4ResponseCurve objects, +0xa8 five int spread limits, +0xbc five float curve maxima. The port keeps the
// curve points directly (EvaluateCurve in cSTETerrain.cpp) because cSC4ResponseCurve is not part of this
// test's link set; the evaluation mirrors its GetOutputValue.
struct TerrainSurfaceCoverInfo {
    float weight = 0.0f;                                   // +0x08 (the value base is the pair's second half)
    std::vector<TerrainPoint> curves[5];                   // +0x10 .. +0xb8 (one curve per zoom)
    std::int32_t maxSpread[5] = {0, 0, 0, 0, 0};           // +0xb8
    float curveMaxInput[5] = {0, 0, 0, 0, 0};              // +0xcc
};

// The ten message types Init subscribes the terrain to (0x26d31ec1 .. 0xc4a6af6).
constexpr std::uint32_t kTerrainMessageTypes[10] = {0x26d31ec1, 0x470bd214, 0x6716a4e6, 0xa6b79602, 0xc4a6af0,
                                                    0xc4a6af1, 0xc4a6af2, 0xc4a6af4, 0xc4a6af5, 0xc4a6af6};
// The ten types Shutdown removes (the original also removes 0xc4a6af3, which Init does not add: kept quirk).
constexpr std::uint32_t kTerrainShutdownMessageTypes[10] = {0x26d31ec1, 0xa6b79602, 0x470bd214, 0x6716a4e6,
                                                            0xc4a6af0, 0xc4a6af1, 0xc4a6af2, 0xc4a6af3,
                                                            0xc4a6af4, 0xc4a6af5};
// The seven scripting commands (name strings from the .rodata the registration in Init walks).
struct TerrainScriptCommand {
    std::uint32_t id;
    const char* name;
};
constexpr TerrainScriptCommand kTerrainScriptCommands[7] = {
    {0xc4a6af0, "GetAltitude"}, {0xc4a6af1, "SetAltitude"}, {0xc4a6af2, "GetCellAltitude"},
    {0xc4a6af3, "SetCellAltitude"}, {0xc4a6af4, "GetSeaLevel"}, {0xc4a6af5, "SetSeaLevel"},
    {0xc4a6af6, "ApplyTerrainBrush"}};

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

class cSTETerrain;
class ITerrainView;

// cRZIniFile as cSTETerrain uses it (ReadEntry / EnumEntries). The real class is recovered in
// src/rz/cRZIniFile.cpp; the seam exists so a test (and a host adapter) can supply the terrain ini without
// pulling the whole reader into every link set. The callback signature mirrors cRZIniFile::EnumCallback.
class ITerrainIniFile {
public:
    using EnumCallback = void (*)(const cRZString& key, const cRZString& value, void* context);
    virtual ~ITerrainIniFile() = default;
    // cRZIniFile::ReadEntry (vslot 0x18): false when the section/key is absent.
    virtual bool ReadEntry(const cRZString& section, const cRZString& key, cRZString& value) = 0;
    // cRZIniFile::EnumEntries (vslot 0x38).
    virtual bool EnumEntries(const cRZString& section, EnumCallback callback, void* context) = 0;
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

    // ---- the city / surface water / engine singletons Init and Shutdown take (Init 0x100739cb0) ---------
    // The port keeps the raw pointers and does the original's AddRef / Release pair on them; the slot of the
    // original object is in each comment. Every accessor below mirrors one original call.
    virtual cIGZUnknown* City() = 0;                                  // SL::spCity, this + 0x28
    virtual std::uint32_t CityCellCountX() = 0;                       // spCity vslot 0x2e8
    virtual std::uint32_t CityCellCountZ() = 0;                       // spCity vslot 0x2f0
    virtual std::int32_t CityTerrainType() = 0;                       // spCity vslot 0x2b8 (+0x70)
    virtual cIGZUnknown* CityLandSimulator() = 0;                     // spCity vslot 0x230 (+0x140)
    virtual cIGZUnknown* CityFloraSimulator() = 0;                    // spCity vslot 0x1d8 (+0x148)
    virtual cIGZUnknown* CityEffectsManager() = 0;                    // spCity vslot 0x2a8 (+0x138)
    virtual cIGZUnknown* Simulator() = 0;                             // SL::spSimulator, this + 0x30
    virtual cIGZUnknown* SurfaceWater() = 0;                          // SL::spSurfaceWater, this + 0x38
    virtual bool SurfaceWaterInit() = 0;                              // spSurfaceWater vslot 0x18
    // spSimulator (this + 0x30) and SL::ATM(): the two schedulers Init hooks the terrain into. The exact
    // argument lists of slots 0x98 / 0xa8 are only partly recovered (a 6-entry registration set and the
    // "cSTETerrain" debug name ride in the call), so the seam takes the terrain itself.
    virtual void RegisterWithSimulator(cSTETerrain* terrain) = 0;     // spSimulator vslot 0x98
    virtual void UnregisterFromSimulator(cSTETerrain* terrain) = 0;   // spSimulator vslot 0xa8
    virtual void AddToAutomataManager(cSTETerrain* terrain) = 0;      // SL::ATM() vslot 0x28
    virtual void RemoveFromAutomataManager(cSTETerrain* terrain) = 0; // SL::ATM() vslot 0x30
    // RZGetFramework()->GetSystemService(0xeb903a32, 0xcb90253a, &out): the command registry the seven
    // script commands (0xc4a6af0..6) are registered on.
    virtual cIGZCommandServer* CommandServer() = 0;
    // spMS2 AddNotification / RemoveNotification (vslots 0x28 / 0x30) for the ten terrain messages. The
    // terrain is not a cIGZMessageTarget2 in the port (no multiple inheritance), so the adapter wraps it.
    virtual bool AddTerrainNotification(cSTETerrain* terrain, std::uint32_t type) = 0;
    virtual bool RemoveTerrainNotification(cSTETerrain* terrain, std::uint32_t type) = 0;
    // +0x140 vslot 0x80 / vslot 0x28: UpdateMoistureValues (0x100733d98) / DoPostCityInit's reset.
    virtual void UpdateLandSimulator(cIGZUnknown* landSimulator) = 0;
    virtual void ResetLandSimulator(cIGZUnknown* landSimulator) = 0;
    // RZ::WM() vslot 0x150 (DoMessage 0x2a612643): true while the world manager cannot take the reconcile.
    virtual bool WorldManagerBusy() = 0;
    // SL::spPreferences +0x4ec (DoAnimationTick 0x10073759e): false posts the city-changed message, true
    // re-posts 0x2a612643 to the terrain. The preference's label is not recovered.
    virtual bool PreferencesFlag4ec() = 0;
    // SL::spViewUtilities +0x14 / cSC4ViewUtilities::GetVisibleEdges @ 0x10017376a.
    virtual std::uint32_t ViewZoomLevel() = 0;
    virtual void VisibleEdges(std::int32_t& horizontal, std::int32_t& vertical) = 0;
    // GZCOM()->CreateInstance(0xc9b84e10 /*cSTETerrainView3D*/, 0x6771477d) in DoPostCityInit. The host
    // owns the object; the terrain only calls Init/Shutdown on it.
    virtual ITerrainView* CreateTerrainView() = 0;
    // SL::spSC4 vslots 0x218 / 0x220: the static shutdown hook the class registers in StaticInit.
    virtual void RegisterStaticShutdown() = 0;
    virtual void UnregisterStaticShutdown() = 0;
    // SL::spRM, taken in StaticInit and released in StaticShutdown (the texture readers use it).
    virtual cIGZPersistResourceManager* AcquireResourceManager() = 0;
    virtual void ReleaseResourceManager() = 0;
    // cSC4MapUtilities::FloodFillDistance @ 0x1000d82b4 (ComputeWaterProximityValues).
    virtual void FloodFillDistance(std::vector<std::vector<std::uint8_t>>& grid, std::int32_t source,
                                   std::int32_t threshold) = 0;

    // ---- the exemplar / texture readers (ReadTextureInfos / ReadTextureInfo / ReadTuningParameters) ----
    // SL::spRM vslot 0x18 GetResource({0x6534284a, 0x88cd66e9, 1} or a texture key, 0xa52160f5, &out, 0, 0)
    // followed by resource vslot 0x30 (the object's cISCPropertyHolder). The returned holder is AddRef'd and
    // the caller releases it. Null when the resource is absent.
    virtual cISCPropertyHolder* GetPropertyHolder(const cGZPersistResourceKey& key) = 0;
    // SL::spRM vslot 0x88 GetAvailableResourceList(list, filter) where the filter is
    // cSCResFilterFixedGroupAndTypeId(0x6534284a, 0x89c2a517): every texture-info resource key.
    virtual void ListTerrainTextureResources(std::vector<cGZPersistResourceKey>& out) = 0;
    // SC4GetResourceAsFile({0x8a5971c5, 0, 0xaa597172}) opened as a cRZIniFile (ReadTerrainIniFile). The
    // caller deletes it. Null when the resource is absent (the original skips the whole read).
    virtual ITerrainIniFile* OpenTerrainIni() = 0;

    // ---- the surface-cover pass (DecideSurfaceCover) --------------------------------------------------
    // cSTETerrainView3D::ChangeTerrainType @ 0x1007467ca / SetUpTexturesForCurrentTerrainTypeAndZoom
    // @ 0x10074688c: the original calls the two view statics unconditionally from DecideSurfaceCover.
    virtual void ChangeTerrainType(std::int32_t terrainType) = 0;
    virtual void SetUpTexturesForCurrentTerrainTypeAndZoom(std::int32_t zoom) = 0;
    // cSTETerrainView3D::sTerrainTextureBinding[zoom][texture] + 8: the number of sub-textures the loaded
    // texture has (the original divides the family index by it). 0 when the texture is not loaded.
    virtual std::uint32_t TerrainTextureSubTextureCount(std::int32_t zoom, std::uint32_t textureId) = 0;
    // The land simulator's climate/moisture query: +0x140 vslot 0x98, called with (cellX * 16.0f). The
    // returned 0..15-ish index is the surface-cover map's moisture column.
    virtual std::int32_t MoistureIndexAtX(float worldX) = 0;
};
ITerrainHost* TerrainHost();
void SetTerrainHost(ITerrainHost* host);

// cISTETerrainView: the subobject GetView() returns (the view object at +0x110 plus 0x18). Only the
// calls this class makes are modelled, so the interface is not the full cISTETerrainView yet.
class ITerrainView {
public:
    virtual ~ITerrainView() = default;
    virtual cIGZSerializable* Serializable() = 0;   // the view object's +0x28 subobject (Write 0x10073efda)
    virtual bool Init(cSTETerrain* terrain) = 0;                                    // cSTETerrainView3D::Init @0x10074607c
    virtual void Shutdown() = 0;                                                    // @0x1007484de
    virtual void MarkTextureSpanInfoDirty(std::int32_t firstRow, std::int32_t lastRow) = 0;   // @0x10074d7d4
    virtual void SetLightingUpdateRect(const TerrainRect* r) = 0;                   // vslot 0x88 (RedisplayTerrain)
    virtual void ChangeTerrainType(std::int32_t terrainType) = 0;                   // @0x1007467ca
    virtual void SetUpTexturesForCurrentTerrainTypeAndZoom(std::int32_t zoom) = 0;  // @0x10074688c
    // The view's current altitude range: +0x94 is the top and +0x98 the bottom of what it displays.
    virtual float MaxDisplayAltitude() const = 0;                                   // the view object's +0x94
    virtual float MinDisplayAltitude() const = 0;                                   // the view object's +0x98
    // DoPostCityInit takes three overlay managers out of the view: the two fields at +0xa8 / +0xb0 and
    // vslot 0xb0 with argument 0. They are stored at +0x120 / +0x128 / +0x130 and only released again.
    virtual cIGZUnknown* OverlayManager(std::uint32_t which) = 0;
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

    // The original's constructor only stores the three vtables and zeroes the fields (the member initialisers
    // here) and its destructor (0x1007372a8) calls Shutdown and frees the three blocks. The port keeps both
    // implicit: a user-defined destructor would make every translation unit that includes this header call
    // into cSTETerrain.cpp, and the owner (the city / the view) calls Shutdown explicitly instead.
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

    // ---- life cycle (the first-boot path) ----------------------------------------------------
    static TerrainStatics& Statics();                                  // the 0x100b71xxx statics
    static bool StaticInit();                                          // 0x10073a346
    static bool StaticShutdown(void* unused = nullptr);                // 0x10073e9de
    bool Init();                                                       // 0x100739cb0 (city size + services)
    bool Shutdown();                                                   // 0x10073e5c6
    void DoPostCityInit();                                             // 0x10073847e (view + first surface pass)
    bool DoMessage(cIGZMessage2* message);                             // 0x100737d3e (+ thunk 0x10073846c)
    bool DoMessageThunk(cIGZMessage2* message) { return DoMessage(message); }   // 0x10073846c (this - 8)
    void DoAnimationTick(std::uint32_t elapsedMs);                     // 0x10073759e
    bool IsPostCityInitDone() const { return postCityInitDone_; }      // +0x20
    cIGZUnknown* City() const { return city_; }                        // +0x28
    cIGZUnknown* SurfaceWaterObject() const { return surfaceWater_; }  // +0x38
    std::int32_t CityTerrainTypeValue() const { return cityTerrainType_; }   // +0x70
    const std::vector<std::vector<std::uint8_t>>& SurfaceCoverGrid() const { return surfaceCover_; }   // +0x200

    // ---- redisplay (the view's dirty rows / altitude range) ----------------------------------
    // 0x10073de82: the corner pair is sorted and widened by one cell, then the rect variant runs.
    void RedisplayTerrain(bool adjustAltitudes, bool redisplayWater, const TerrainPoint& a,
                          const TerrainPoint& b, std::uint32_t flags);
    void RedisplayTerrain(bool adjustAltitudes, bool redisplayWater, const TerrainRect& r,
                          std::uint32_t flags);                     // 0x10073defa
    void SetTerrainType(std::int32_t type);                         // 0x10073e4e6
    // 0x100735a58: the altitude range the view must cover, including the city-edge special cases.
    void ComputeMinAndMaxAltitudeForTerrainRedisplay(const TerrainRect& r, float& lo, float& hi) const;
    // 0x10073e1f2: every vertex of the rect, then the water recompute and the redisplay.
    void SetAltitude(const TerrainRect& r, float altitude, bool recomputeWater, std::uint32_t flags);

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
    std::uint32_t ZoomLevel() const { return zoom_; }

    // ---- limits / counts --------------------------------------------------------------------
    static float MinAllowedHeight() { return Statics().minTerrainHeight; }             // 0x100740ec0
    static float MaxAllowedHeight() { return Statics().maxTerrainHeight; }             // 0x100740ece
    static float GetDefaultTerrainRaiseLowerAmount() { return Statics().terrainRaiseLowerAmount; }  // 0x100740f0a
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

    // ---- surface textures (the exemplar / texture pipeline) ---------------------------------
    void ClearSurfaceTextures(const TerrainRect& r);                 // 0x100733dd4 (three layer keys -> 0)
    void UpdateMoistureValues(const TerrainRect& r);                 // 0x100733d98 (land simulator +0x140 slot 0x80)
    // 0x100731338: keeps the three (texture key, weight) pairs of a vertex sorted by weight.
    void AddLandTypeAtVertex(std::uint32_t index, std::uint16_t textureKey, float weight);
    // 0x100733bc8: the whole surface pass (water proximity, moisture, cover decision, ash, ratios).
    void AssignSurfaceTexturesAndColors(TerrainRect& r);
    static bool GetEffectIDProperty(cISCPropertyHolder* holder, std::uint32_t propertyID,
                                    std::uint32_t out[2]);            // 0x100738dcc
    // 0x100733e48: picks one surface texture per vertex (altitude -> temperature, x -> moisture), spreads
    // each texture's response curve over the neighbourhood and adds the land-type weights.
    void DecideSurfaceCover(const TerrainRect& r);

    // ---- the data readers (StaticInit) -------------------------------------------------------
    static void ReadTextureInfos();                                  // 0x10073d8a4
    static void ReadTextureInfo(const cGZPersistResourceKey& key, void* unused);   // 0x10073d22a
    static void ReadTuningParameters();                              // 0x10073d95e
    static void ReadTerrainIniFile();                                // 0x10073bb96
    static void ReadMiscTexturesForTerrainTypes(ITerrainIniFile& ini);   // 0x10073b602
    // 0x100731418: the EnumEntries callback that fills sSurfaceCoverMap[terrainType][temperature][moisture].
    static void AddTemperatureAndMoistureToSurfaceCoverEntry(const cRZString& key, const cRZString& value,
                                                             void* terrainType);
    static void AddToTerrainTypeTextureList(std::int32_t terrainType, std::uint32_t textureId);   // 0x10073159e
    static void DetermineTextureFamilyMembersForGridCells();         // 0x100737430
    // The per-texture tuning map (sSurfaceCoverTextures). Tests read it to verify the exemplar parse.
    static std::map<std::uint32_t, TerrainSurfaceCoverInfo>& SurfaceCoverTextures();
    static std::vector<std::uint32_t>& TexturesUsedInTerrainType(std::int32_t terrainType);   // sTexturesUsedInTerrainType

    // ---- normals / cliffs --------------------------------------------------------------------
    // 0x100734dc8: the two-triangle normal pass over the rect; marks cliffs and flips the triangulation.
    void CalculateNormalsAndAssignCliffTextures(const TerrainRect& r);
    void MarkCliff(std::int32_t index, bool evenIsCliff, bool oddIsCliff);   // 0x100735398

    // ---- erosion / flood --------------------------------------------------------------------
    void DoErosion(std::uint32_t style, bool fromImage);             // 0x100736216 (style 0 = water, 1 = wind)
    void EnsureMinAllowedHeight();                                   // 0x1007361a0
    void ChangeTerrainLevel(bool raise, float amount);               // 0x100735746
    void StartFlood();                                               // 0x10073e932
    bool IsFloodActive() const { return floodActive_; }              // +0xf4
    // 0x100731a1c: the first sea vertex among the west / north-west / north / north-east neighbours.
    std::int32_t GetNeighboringSeaVertex(std::int32_t index) const;

    // ---- water proximity / land type ratios (the surface pass helpers) ----------------------
    void ComputeWaterProximityValues(const TerrainRect& r);          // 0x100733c62 (grid + flood fill)
    void AdjustLandTypeRatios(const TerrainRect& r);                 // 0x10073173a (weights -> at most 250)

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
    // SC4Rect<int>::UnionRect @ 0x100212e68, as the port needs it (the original is a method of SC4Rect).
    static void UnionRect(TerrainRect& dst, const TerrainRect& a, const TerrainRect& b);
    // Lays the grid out for a city of cellsX x cellsZ cells; shared by Init() and the test fixture overload.
    void LayOutGrid(std::uint32_t cellsX, std::uint32_t cellsZ);
    // The service pointers Init takes and Shutdown releases (each is a referenced cIGZUnknown).
    void ReleaseServices();
    void ReleaseOne(cIGZUnknown*& object);

    cIGZUnknown* city_ = nullptr;                 // +0x28 (SL::spCity)
    cIGZUnknown* simulator_ = nullptr;            // +0x30 (SL::spSimulator)
    cIGZUnknown* surfaceWater_ = nullptr;         // +0x38 (SL::spSurfaceWater)
    cIGZUnknown* overlayManagers_[3] = {nullptr, nullptr, nullptr};   // +0x120 / +0x128 / +0x130
    cIGZUnknown* effectsManager_ = nullptr;       // +0x138
    cIGZUnknown* landSimulator_ = nullptr;        // +0x140
    cIGZUnknown* floraSimulator_ = nullptr;       // +0x148
    bool postCityInitDone_ = false;               // +0x20
    bool createdFromImage_ = false;               // +0x80 (set by Create, not recovered)
    bool initialized_ = false;
    std::uint32_t refCount_ = 0;                 // +0x1c
    TerrainTuning tuning_;
    std::uint32_t cellsX_ = 0, cellsZ_ = 0, lastCellX_ = 0, lastCellZ_ = 0;
    std::uint32_t vertsX_ = 0, vertsZ_ = 0, totalVerts_ = 0;
    float widthMeters_ = 0, depthMeters_ = 0;
    std::int32_t terrainType_ = 0;                // +0x24 (SetTerrainType / the saved type)
    std::int32_t cityTerrainType_ = 0;            // +0x70 (the city's type at Init)
    std::uint8_t waterProximityMax_ = 0;          // +0xc4 (clamped kWaterProximityRangeFactor * 64)
    float minHeight_ = 0, maxHeight_ = 0, maxNormalHeight_ = 0;   // +0x90 / +0x94 / +0x98
    bool loadOk_ = false;
    std::int32_t neighborOffsets_[8] = {};        // +0xa4..+0xc0
    TerrainRect gridRect_;                        // +0x58: the whole grid as a vertex rect
    TerrainRect emptyRect_ = {0, 0, -1, -1};      // +0xcc (kEmptyRect + the following dword pair)
    std::vector<Vertex> vertices_;                // +0x88
    std::vector<float> waterDepth_;               // cSTESurfaceWater vertex +0x0 (elided class)
    std::vector<float> sediment_;                 // cSTESurfaceWater vertex +0x1c
    std::vector<float> tempWaterLevels_;          // +0x100 (SaveWaterLevelsInTempArray)
    std::uint32_t waterVertexCount_ = 0;          // +0x108
    std::vector<std::vector<std::uint8_t>> surfaceCover_;   // +0x200 (per vertex land-type ratios)
    std::uint32_t zoom_ = 0;                      // +0xdc (indexes the brush radii)
    bool accumulating_ = false;                   // +0xe0
    TerrainRect accumulateRect_ = {0x7fffffff, 0x7fffffff, -0x7fffffff, -0x7fffffff};   // +0xe4
    bool floodActive_ = false;                    // +0xf4
    TerrainRect floodRect_ = {20, 20, 40, 40};    // +0x218
    std::uint32_t floodStep_ = 0;                 // +0x228 (DoFloodFlowStep's counter)
    bool displayWater_ = true;                    // +0xfd
    bool displayEnabled_ = false;                 // +0x118 (set by DoPostCityInit)
    std::uint32_t animationTick_ = 0;             // +0xf8
    std::uint32_t waterProximityTick_ = 0;        // +0xc8
    float brushPhase_[8] = {};                    // +0x248..+0x264
    // +0x170 / +0x188: the two per-row triangle-normal scratch buffers CalculateNormals... swaps. Each holds
    // two 12-byte normals per cell column. The original allocates them in Create (not recovered); the port
    // sizes them lazily on the first normal pass.
    std::vector<float> triangleNormals_[2];
    ITerrainView* view_ = nullptr;                // +0x110 (the port keeps the +0x18 interface)
    std::function<void(std::uint32_t, std::uint32_t)> onCellChanged_;
};

}  // namespace sc4
