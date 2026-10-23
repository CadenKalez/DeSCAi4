// cSTEOverlayManager — the terrain-overlay manager: it owns every decal / shadow / height-map overlay the 3-D view
// draws over the terrain (the zone, power/water, traffic and coverage data views, plus the text decals and the
// building shadows) and paints them into the terrain mesh through the terrain view. Recovered from SimCity 4
// Deluxe Editionsub 1.2.1 (class group 0x100aea310, functions 0x100727300..0x10072c4d5).
//
// The original implements three interfaces: cISTEOverlayManager (iid 0x895d6544, the big vtable at 0x100aea320),
// cSC4ViewObject3D (vtable at 0x100aea438) and cIGZMessageTarget2 (vtable at 0x100aea4a8). The port is one class with
// the 33 cISTEOverlayManager slots in vtable order; the two secondary vtables and their adjustor thunks do not exist
// in a single-inheritance layout (see the SKIPPED list in notes/ledger_pending/cSTEOverlayManager.txt).
//
// Original layout (0x160 bytes; offsets below are original ones):
//   +0x00/+0x08/+0x10/+0x20  vtable pointers (this class, cSC4ViewObject3D, cRZUnknown, cIGZMessageTarget2)
//   +0x28 bool inited                      +0x29 bool dynamic (no spatial index, update decals eagerly)
//   +0x2a bool drawWaterPass (the overlay paints the water-surface mesh instead of the terrain mesh)
//   +0x2c int quality (render properties +0x30 -> +0x398)   +0x30 int second-texture-stage (+0x158)
//   +0x34 bool spatial index valid (render properties +0x18 -> +0x6f8; the byte's meaning is unresolved)
//   +0x38 std::vector<cSC4PackedRenderState> five draw modes (+0x38 begin, +0x40 end, +0x48 cap)
//   +0x58/+0x64/+0x70 cGDCombiner {word0,word1,word2}: rough-shadow, shadow stage 0, shadow stage 1
//   +0x7c int cell count x   +0x80 int cell count z    +0x84 float cell size   +0x88 float 1/cell size
//   +0x8c/+0x90 float city extent x (twice)            +0x94 float city extent z
//   +0x98 cRZRect visible terrain rect (left/top 0, right/bottom the cell counts)
//   +0xa8 cSTETerrain* (spTerrain)          +0xb0 cSTETerrainView3D* (the view Init was given)
//   +0xb8 cIS3DTextureBindingFactory* (framework service 0x1ac0e11a / 0xfac0e219)
//   +0xc0 cSTEVertBufExtn* (borrowed for one DrawOverlays call)
//   +0xc8 cSC4SlotArray<cOverlay>   (+0xe0 free-list head, +0xe4 tail)
//   +0xe8 cSC4SlotArray<cDecal>     (+0x100 head, +0x104 tail)
//   +0x108 cSC4SlotArray<cRingDecal>(+0x120 head, +0x124 tail)
//   +0x128 cS3DTextureBinding* shadow texture (factory slot 0x28, key 0xaa592b77)
//   +0x130 int decals drawn last pass       +0x134 int shadows drawn last pass
//   +0x138 u16 spatial-index frame stamp    +0x13c int sds cell shift (0)  +0x140 int sds width (cells x)
//   +0x144 int sds height (cells z)         +0x148 std::vector<std::forward_list<u32>> spatial index buckets
//   +0x160 end
//
// cOverlay (200 bytes): +0 free link / overlay slot, +4 unused, +8 u64 flags, +0x10 u8 draw mode, +0x12 u16 frame
// stamp, +0x14 cRZRect rect, +0x24 cS3DMatrix4 texture transform, +0x64 cS3DMatrix4 shadow transform, +0xa4 float
// alpha, +0xa8 float rgb[3], +0xb8 cS3DTextureBinding*, +0xc0 cISC4CachedStringTexture*.
// Flag bits: 1 enabled, 2 "texture transform is rotated/scaled" (set by UpdateDecal), 8 shadow, 0x10 decal,
// 0x20 height map, 0x40 self-lit shade, 0x80 "smooth decal" (AddDecal with a decal, DrawDecals' +0x91 mask).
// cDecal (0x30): +0 free link, +4 x, +8 z, +0xc width, +0x10 turns (0..1), +0x14 aspect adjust, +0x18 u repeat,
// +0x1c v repeat, +0x20 uv offset, +0x24 user value, +0x28 texture aspect (height/width), +0x2c overlay slot.
// A negative +0x2c is a cRingDecal index | 0x80000000. cRingDecal (0x24): +0 free link, +4 .. +0x20 eight overlays.
//
// The draw/paint entry the view calls is Update() -> DrawOverlays(): it inverts the model-view matrix once, asks the
// spatial index (or the whole slot array when the manager is dynamic) for the visible overlays, then runs the decal
// pass and the shadow pass, each of which sets up one pack of render states and blits the overlay rectangles through
// the terrain view's mesh-subset draws.
//
// Neighbours that are not recovered go through seams (one method per original call; see docs/SEAMS.md):
//   IOverlayHost          — SL::spMS2, SL::spRenderProperties, SL::spTerrain, SL::spRenderer's shadow layer, the
//                           texture-binding factory, cSTETerrainView3D (culling info + mesh subset draws) and
//                           cSTETerrain::RedisplayTerrain (0x100740000 range, not recovered).
//   IOverlayDrawContext   — SC4DrawContext (recovered as a class, but the production adapter does not exist yet).
//   IOverlayTexture       — cS3DTextureBinding, IOverlayStringTexture — cISC4CachedStringTexture.
//   IOverlayShadowService — the shadow-projection object behind SL::spRenderer +0x48.
#pragma once
#include <array>
#include <cstdint>
#include <forward_list>
#include <vector>

#include "gz/StreamOps.h"
#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZMessage2.h"
#include "rz/cRZString.h"
#include "sc4/DrawContext.h"
#include "sc4/PackedRenderState.h"
#include "sc4/SlotArray.h"

namespace sc4 {

class ITerrainVertBufExtn;

// ---------------------------------------------------------------------------------------------------------------
// Seam types
// ---------------------------------------------------------------------------------------------------------------

// cS3DTextureBinding as the overlay manager uses it: a refcounted binding whose first word points at the array of GL
// texture ids (DrawDecals 0x10072aa56 reads *(int*)*binding).
class IOverlayTexture {
public:
    virtual ~IOverlayTexture() = default;
    virtual void AddRef() = 0;                                     // cS3DTextureBinding::AddRef 0x1000d9bec
    virtual void Release() = 0;                                    // cS3DTextureBinding::Release 0x1000d9bfa
    virtual int GlTextureId() const = 0;                           // *(int*)(*(void**)binding)
};

// cISC4CachedStringTexture (the rendered text a text decal shows). vslot 0x18 GetSize, vslot 0x20 texture id.
class IOverlayStringTexture {
public:
    virtual ~IOverlayStringTexture() = default;
    virtual void AddRef() = 0;
    virtual void Release() = 0;
    virtual void GetSize(std::int32_t& width, std::int32_t& height) const = 0;   // [3] +0x18
    virtual int GlTextureId() const = 0;                                          // [4] +0x20
};

// cIS3DTextureBindingFactory (framework service 0x1ac0e11a / 0xfac0e219).
class IOverlayTextureFactory {
public:
    virtual ~IOverlayTextureFactory() = default;
    virtual void AddRef() = 0;                                                    // cIS3DTextureBindingFactory is refcounted
    virtual void Release() = 0;
    virtual IOverlayTexture* GetTexture(const cGZPersistResourceKey& key) = 0;    // +0x20 (the key is passed by pointer)
    virtual IOverlayTexture* GetTexture(std::uint32_t instance) = 0;              // +0x28 (instance only, out-parameter form)
    virtual void GetTextureSize(const IOverlayTexture* texture, std::int32_t& width, std::int32_t& height) = 0;  // +0x40
    virtual std::uint32_t ShadowTextureGroup() = 0;                               // +0x58 (group of the 0x7ab50e44 keys)
};

// The shadow-projection object behind SL::spRenderer +0x48 (cSC4Renderer::GetShadowLayer, unrecovered).
class IOverlayShadowService {
public:
    virtual ~IOverlayShadowService() = default;
    // +0x88: project the shadow of `pointCount` terrain points through `transform`; fills `out`.
    virtual bool ProjectShadow(int pointCount, const cS3DVector3* points, const cS3DVector2* texCoords,
                               const cS3DTransform& transform, cS3DMatrix4& out) = 0;
    // +0x68: the current light direction (world space, unit).
    virtual void GetShadowDirection(cS3DVector3& out) = 0;
    // +0x90: exact screen/terrain-space rect of the shadow of a bounding box.
    virtual bool CalcShadowRect(int pointCount, const cS3DVector3* points, const cS3DBoundingBox& box, cRZRect& out) = 0;
    // +0x70: shadow colour and the two intensity scales (colour, alpha scale, intensity).
    virtual void GetShadowColor(cS3DVector4& colour, float& scale, float& intensity) = 0;
    // +0x78: how much of the shadow reaches a world position (0 = fully shadowed/off, 1 = full).
    virtual float ShadowFalloff(const cS3DVector2& worldXZ) = 0;
};

// Everything the manager reaches that is not recovered yet. Every method is one original call; the address or
// vtable slot is in the comment.
class IOverlayHost {
public:
    virtual ~IOverlayHost() = default;
    // ---- services ------------------------------------------------------------------------------------------
    virtual cIGZMessageServer2* Messages() = 0;                                  // SL::spMS2
    virtual IOverlayTextureFactory* TextureFactory() = 0;                        // framework service 0x1ac0e11a/0xfac0e219
    virtual IOverlayShadowService* ShadowService() = 0;                          // SL::spRenderer vslot 0x48
    virtual int RenderQuality() = 0;                                             // spRenderProperties+0x30 -> +0x398
    virtual int RenderSecondTextureStage() = 0;                                  // spRenderProperties+0x30 -> +0x158
    virtual bool RenderSpatialIndexValid() = 0;                                  // spRenderProperties+0x18 -> +0x6f8
    // ---- terrain (SL::spTerrain) -----------------------------------------------------------------------------
    virtual int TerrainCellCountX() = 0;                                         // terrain vslot 0x38
    virtual int TerrainCellCountZ() = 0;                                         // terrain vslot 0x40
    virtual float TerrainCellSize() = 0;                                         // terrain vslot 0x30
    virtual int TerrainCellCountInRect(const cRZRect& rect) = 0;                  // terrain vslot 0x1b0
    virtual void RedisplayTerrain(int drawWaterPass, const cRZRect& rect) = 0;    // terrain vslot 0x228 (unrecovered)
    // ---- terrain view (cSTETerrainView3D) ---------------------------------------------------------------------
    // GetCullingInfo @0x10074e162. colStart/colEnd point at arrays indexed by terrain row (z), at least cellCountZ long.
    virtual void GetTerrainCullingInfo(int& rowBegin, int& rowEnd, const std::int32_t*& colStart,
                                       const std::int32_t*& colEnd) = 0;
    virtual void DrawTerrainMeshSubset(bool viewFrustum, const cRZRect& rect, ITerrainVertBufExtn* vb) = 0;  // 0x100749f40 / 0x10074a56e (+ the *WithVertBufExtension variants)
    virtual void DrawWaterSurfaceSubset(bool viewFrustum, const cRZRect& rect) = 0;                           // 0x10074c300 / 0x10074c446
    virtual void DrawMixedMeshSubset(bool viewFrustum, const cRZRect& rect) = 0;                              // 0x10074ab9e / 0x10074adca
};
IOverlayHost* OverlayHost();
void SetOverlayHost(IOverlayHost* host);

// SC4DrawContext as the overlay code drives it. The SC4DrawContext class is recovered, but nothing implements
// ITerrainDrawContext for it yet, so the overlay manager takes this seam (one method per original call).
class IOverlayDrawContext {
public:
    virtual ~IOverlayDrawContext() = default;
    // render-properties flags (SC4DrawContext +0x50 -> +0x18 -> ...)
    virtual bool SmoothOverlays() const = 0;             // +0x738 (SC4DrawContext::GridSuppressed in the terrain port)
    virtual bool DecalPassEnabled() const = 0;           // +0x2b8
    virtual bool ShadowPassEnabled() const = 0;          // +0x278
    virtual bool ShadowProjectionAccurate() const = 0;   // +0x2f8
    virtual int DepthOffset() const = 0;                 // +0x114
    // SC4DrawContext methods (address in the comment)
    virtual void ResetModelViewTransform() = 0;                       // 0x1007726ee
    virtual void GetModelViewMatrix(float m[16]) const = 0;           // 0x100772908
    virtual void ClearTexTransform(int stage) = 0;                    // 0x1007733c6
    virtual void SetDepthOffset(int offset) = 0;                      // 0x1007741b8
    virtual void SetTransparency(float alpha) = 0;                    // 0x100773704
    virtual void ResetTransparency() = 0;                             // 0x10077371a
    virtual void SetShade(const float rgb[3]) = 0;                    // 0x10077358e
    virtual void SetSelfLitShade(const float rgb[3]) = 0;             // 0x1007735d8
    virtual void ResetShade() = 0;                                    // 0x1007736be
    virtual void SetRenderState(const cSC4PackedRenderState& state) = 0;   // 0x100773906
    virtual void SetTexture(int texture, int stage) = 0;              // 0x100772d0e
    virtual void SetTexFiltering(int minFilter, int magFilter, int stage = 0) = 0;   // 0x100773108
    virtual void SetTexWrapModes(int wrapU, int wrapV, int stage = 0) = 0;          // 0x10077302a
    virtual void SetTexTransform4(const cS3DMatrix4& m, int stage) = 0;            // 0x1007732be
    virtual void SetTexColor(const cS3DVector4& colour) = 0;           // 0x100773296
    virtual void SetTexCombiner(const gd::cGDCombiner& combiner, int stage = 0) = 0;   // 0x100773212
    virtual void EnableAlphaTestFlag(bool on) = 0;                     // 0x1007729c2
    virtual void SetAlphaFunc(int func, float ref) = 0;                // 0x100772e20
    virtual void EnableTextureStateFlag(bool on, int stage) = 0;       // 0x100772ad4
    // cIGZGDriver calls the manager makes directly (driver = SC4DrawContext +0x40)
    virtual void DriverSetState(int a, int b) = 0;                     // driver vslot 0x110
    virtual bool DriverStage7Begin() = 0;                              // driver vslot 0x170 (7) -> previous state
    virtual void DriverStage7Apply() = 0;                              // driver vslot 0x160 (7)
    virtual void DriverStage7End() = 0;                                // driver vslot 0x168 (7)
    virtual ITerrainVertBufExtn* GetVertexBufferExtension() = 0;       // cSTEVertBufExtn::GetVertexBufferExtension 0x100751874
    virtual void ReleaseVertexBufferExtension() = 0;                   // cSTEVertBufExtn::ReleaseVertexBufferExtension 0x100751a26
};

// ---------------------------------------------------------------------------------------------------------------
// Records of the overlay manager (original layouts; see the file comment)
// ---------------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kOverlayTextureShadow = 0xaa592b77;        // Init: the shadow decal texture
constexpr std::uint32_t kOverlayShadowKeyType = 0x7ab50e44;        // AddShadowDecal(uint): type of the shadow keys
constexpr std::uint32_t kOverlayMessageRenderProperties = 0xc9f775bb;   // Init registers it, DoMessage handles it
constexpr std::uint32_t kOverlayIID = 0x895d6544;                  // cISTEOverlayManager

// cOverlay::flags (+0x08). The original has no enum; the names say where each bit is tested.
constexpr std::uint32_t kOverlayFlagEnabled = 0x01;         // SetOverlayEnabled / DrawShadows (flags & 9) == 9
constexpr std::uint32_t kOverlayFlagTransformed = 0x02;     // UpdateDecal: the transform carries a turn or a scale
constexpr std::uint32_t kOverlayFlagShadow = 0x08;          // AddShadowDecal
constexpr std::uint32_t kOverlayFlagDecal = 0x10;           // AddDecal / AddRingDecal / DrawDecals' mask
constexpr std::uint32_t kOverlayFlagHeightMap = 0x20;       // SetHeightMapParams
constexpr std::uint32_t kOverlayFlagSelfLit = 0x40;         // SetOverlayFlags(bit 0) -> SetSelfLitShade
constexpr std::uint32_t kOverlayFlagSmoothDecal = 0x80;     // AddDecal; DrawDecals' 0x91 mask

// cISTEOverlayManager::tDrawMode: an index into the five packed render states (0..4); 4 is the shadow pass.
enum tOverlayDrawMode : std::uint8_t {
    kOverlayDrawMode0 = 0, kOverlayDrawMode1 = 1, kOverlayDrawMode2 = 2, kOverlayDrawMode3 = 3, kOverlayDrawModeShadow = 4
};

struct cOverlay {
    static constexpr std::size_t kSize = 0xc8;
    std::int32_t freeLink = -1;                       // +0x00 free-list link (-1 = in use)
    std::int32_t reserved4 = 0;                       // +0x04 never written by the manager
    std::uint64_t flags = kOverlayFlagEnabled;        // +0x08
    std::uint8_t drawMode = 0;                        // +0x10
    std::uint16_t frameStamp = 0;                     // +0x12 spatial-index stamp (SDSFindOverlaysToDraw)
    cRZRect rect;                                     // +0x14
    cS3DMatrix4 texMatrix;                            // +0x24
    cS3DMatrix4 shadowMatrix;                         // +0x64
    float alpha = 1.0f;                               // +0xa4
    float color[3] = {1.0f, 1.0f, 1.0f};              // +0xa8 (nS3D::kColorWhite and the two white constants)
    IOverlayTexture* texture = nullptr;               // +0xb8
    IOverlayStringTexture* stringTexture = nullptr;   // +0xc0

    cOverlay();                                       // 0x100727300
    cOverlay(const cOverlay& other);                  // 0x10072cf78
    cOverlay& operator=(const cOverlay& other);       // 0x10072ccc8
    ~cOverlay();                                      // inlined in cSC4SlotArray<cOverlay>::Clear 0x100727cac
};
static_assert(sizeof(cOverlay) == cOverlay::kSize, "cOverlay must keep the original 200-byte stride");

// cISTEOverlayManager::cDecalInfo (0x24 bytes): the caller-visible half of a cDecal.
struct cDecalInfo {
    float x = 0, z = 0;               // +0x00 / +0x04
    float width = 0;                  // +0x08
    float turns = 0;                  // +0x0c
    float aspectAdjust = 1;           // +0x10 -> decal +0x14
    float uRepeat = 1;                // +0x14 -> decal +0x18
    float vRepeat = 1;                // +0x18 -> decal +0x1c
    float uvOffset = 0;               // +0x1c -> decal +0x20
    float userValue = 0;              // +0x20 -> decal +0x24
};

struct cDecal {
    static constexpr std::size_t kSize = 0x30;
    std::int32_t freeLink = -1;       // +0x00
    float x = 0, z = 0;               // +0x04 / +0x08
    float width = 0;                  // +0x0c
    float turns = 0;                  // +0x10 fraction of a full turn, 0 = axis aligned
    float aspectAdjust = 1;           // +0x14
    float uRepeat = 1;                // +0x18
    float vRepeat = 1;                // +0x1c
    float uvOffset = 0;               // +0x20
    float userValue = 0;              // +0x24
    float aspect = 0;                 // +0x28 texture height / width
    std::int32_t overlay = -1;        // +0x2c overlay slot, or ring slot | 0x80000000
};
static_assert(sizeof(cDecal) == cDecal::kSize, "cDecal must keep the original 48-byte stride");

struct cRingDecal {
    static constexpr std::size_t kSize = 0x24;
    std::int32_t freeLink = -1;       // +0x00
    std::uint32_t overlay[8] = {};    // +0x04 .. +0x20
};
static_assert(sizeof(cRingDecal) == cRingDecal::kSize, "cRingDecal must keep the original 36-byte stride");

// ---------------------------------------------------------------------------------------------------------------
// The port keeps cIGZMessageTarget2 as a real base so the message server can hold the manager as a target (the
// original's second vtable group); cISTEOverlayManager is not modelled as a separate base, its 33 slots are the
// class's own virtual methods.
class cSTEOverlayManager : public cIGZMessageTarget2 {
public:
    explicit cSTEOverlayManager(IOverlayHost* host = nullptr);
    virtual ~cSTEOverlayManager();

    // cISTEOverlayManager (vtable slots, in order)
    bool QueryInterface(std::uint32_t iid, void** out) override;               // [0]  0x100727d2c
    std::uint32_t AddRef() override;                                           // [1]  0x100727dc0
    std::uint32_t Release() override;                                          // [2]  0x100727df4 (deletes at 0)
    virtual std::uint32_t AddDecal(std::uint32_t instance, const cS3DVector2& position, float width, float turns);  // [3] 0x10072832c
    virtual std::uint32_t AddDecal(const cGZPersistResourceKey& key, const cS3DVector2& position, float width, float turns);  // [4] 0x1007282a2
    virtual std::uint32_t AddDecal(const IOverlayTexture* texture, const cS3DVector2& position, float width, float turns);   // [5] 0x1007283b6
    virtual std::uint32_t AddDecal(const IOverlayStringTexture* text, const cS3DVector2& position, float width, float turns); // [6] 0x1007285a6
    // The shared body of the four overloads (not a vtable slot: 0x1007283b6 / 0x1007285a6 tail-jump here).
    std::uint32_t AddDecal(const IOverlayStringTexture* text, const IOverlayTexture* texture, const cS3DVector2& position,
                           float width, float turns);   // 0x1007283ce
    virtual std::uint32_t AddRingDecal(std::uint32_t instance, const cS3DVector2& position, float width, float turns);        // [7] 0x100728f32
    virtual std::uint32_t AddHeightMap(std::uint32_t instance);               // [8]  0x1007295c8
    virtual std::uint32_t AddShadowDecal(std::uint32_t instance, const cS3DVector2& position, float width, float turns, bool mirror);   // [9] 0x10072924e
    virtual std::uint32_t AddShadowDecal(const cGZPersistResourceKey& key, const cS3DVector2& position, float width, float turns, bool mirror);   // [10] 0x1007292c6
    virtual std::uint32_t AddShadow(const IOverlayTexture* texture, int pointCount, const cS3DVector3* points, const cS3DVector2* texCoords,
                                    const cS3DBoundingBox& box, const cS3DTransform& transform);   // [11] 0x100729916
    virtual void MoveDecal(std::uint32_t id, const cS3DVector2& position);     // [12] 0x100729d16
    virtual void UpdateDecalInfo(std::uint32_t id, const cDecalInfo& info);    // [13] 0x100729dca
    virtual void DecalInfo(std::uint32_t id, cDecalInfo* out) const;           // [14] 0x100729e30
    virtual void RemoveOverlay(std::uint32_t id);                              // [15] 0x100729e80
    virtual void SetOverlayEnabled(std::uint32_t id, bool enabled);            // [16] 0x10072a136
    virtual void SetOverlayAlpha(std::uint32_t id, float alpha);               // [17] 0x10072a1f6
    virtual void SetOverlayColor(std::uint32_t id, const cS3DVector3& rgb);    // [18] 0x10072a2f2
    virtual void SetOverlayDrawMode(std::uint32_t id, tOverlayDrawMode mode);  // [19] 0x10072a3b4
    virtual void SetOverlayFlags(std::uint32_t id, std::uint32_t flags);       // [20] 0x10072a45c
    virtual void SetHeightMapParams(std::uint32_t id, float scale, float offset);   // [21] 0x10072a4f2
    virtual bool OverlayIsEnabled(std::uint32_t id) const;                     // [22] 0x10072a62c
    virtual float OverlayAlpha(std::uint32_t id) const;                        // [23] 0x10072a688
    virtual void OverlayColor(std::uint32_t id, cS3DVector3* out) const;       // [24] 0x10072a6e8
    virtual tOverlayDrawMode OverlayDrawMode(std::uint32_t id) const;          // [25] 0x10072a76a
    virtual bool OverlayFlags(std::uint32_t id) const;                         // [26] 0x10072a7c4
    virtual void GetStatsString(cRZString& out);                               // [27] 0x10072a824
    bool DoMessage(cIGZMessage2* message) override;                            // [28] 0x100727e28 (cIGZMessageTarget2)
    virtual bool Update(IOverlayDrawContext& dc);                              // [29] 0x100727ed0
    virtual bool Pick(const cS3DVector3& origin, const cS3DVector3& direction, IOverlayDrawContext* dc, float* out);  // [30] 0x100728292

    // ---- life cycle (original non-virtual methods) ----------------------------------------------------------
    void Init(class cSTETerrainView3D* view, bool dynamic, bool drawWaterPass);   // 0x100727792
    void InitDrawStates();                                                        // 0x100727a22
    void Shutdown();                                                              // 0x100727bfe

    // ---- internals (original private methods) ---------------------------------------------------------------
    void DrawOverlays(IOverlayDrawContext& dc);                                   // 0x100727ede
    void DrawDecals(const cS3DMatrix4& inverseView, IOverlayDrawContext& dc, const std::vector<std::uint32_t>& ids);        // 0x10072aa56
    void DrawShadowsRough(const cS3DMatrix4& inverseView, IOverlayDrawContext& dc, const std::vector<std::uint32_t>& ids);  // 0x10072acae
    void DrawShadows(const cS3DMatrix4& inverseView, IOverlayDrawContext& dc, const std::vector<std::uint32_t>& ids);       // 0x10072aee0
    void DrawRect(IOverlayDrawContext& dc, const cRZRect& rect);                   // 0x10072b342
    void DrawMixedWaterAndLandRect(IOverlayDrawContext& dc, const cRZRect& rect);  // 0x10072b3ee
    void CalcShadowTerrainRect(const cS3DVector3& lightDir, int pointCount, const cS3DVector3* points,
                               const cS3DBoundingBox& box, cRZRect* out);          // 0x1007296f0
    void UpdateDecal(const cDecal& decal, bool staticOnly);                        // 0x1007286ac
    void UpdateRingDecalOverlays(const cRingDecal& ring, const std::array<float, 4>& rect, bool staticOnly);   // 0x10072b442
    void UpdateStaticOverlayRect(std::uint32_t slot, const cRZRect& rect);         // 0x10072c246
    void AddOverlay(std::uint32_t slot);                                           // 0x100728dda
    std::uint32_t AddOverlay(const IOverlayTexture* texture, const cRZRect& rect, const cS3DMatrix4& matrix,
                             std::uint64_t flags);                                 // 0x100729bd6
    std::uint32_t AddOverlay(std::uint32_t instance, const cRZRect& rect, const cS3DMatrix4& matrix, std::uint64_t flags);  // 0x100729668
    void SDSAdd(std::uint32_t slot, const cRZRect& rect);                          // 0x10072a852
    void SDSRemove(std::uint32_t slot, const cRZRect& rect);                       // 0x10072a034
    void SDSFindOverlaysToDraw(std::vector<std::uint32_t>& out);                   // 0x10072a90e
    void RedisplayStaticOverlay(std::uint32_t slot);                               // 0x10072a0cc

    // ---- state accessors for the render integration and the tests -------------------------------------------
    bool IsInited() const { return inited_; }
    bool IsDynamic() const { return dynamic_; }
    bool DrawsWaterPass() const { return drawWaterPass_; }
    int Quality() const { return quality_; }
    int SecondTextureStage() const { return secondStage_; }
    const cRZRect& VisibleRect() const { return visible_; }
    float CellSize() const { return cellSize_; }
    float InvCellSize() const { return invCellSize_; }
    int CellCountX() const { return cellCountX_; }
    int CellCountZ() const { return cellCountZ_; }
    std::uint16_t FrameStamp() const { return frameStamp_; }
    int DecalsDrawn() const { return decalsDrawn_; }
    int ShadowsDrawn() const { return shadowsDrawn_; }
    const std::vector<cSC4PackedRenderState>& RenderStates() const { return renderStates_; }
    const gd::cGDCombiner& RoughShadowCombiner() const { return roughShadowCombiner_; }
    const gd::cGDCombiner& ShadowCombiner0() const { return shadowCombiner0_; }
    const gd::cGDCombiner& ShadowCombiner1() const { return shadowCombiner1_; }
    IOverlayTexture* ShadowTexture() const { return shadowTexture_; }
    cSC4SlotArray<cOverlay>& Overlays() { return overlays_; }
    cSC4SlotArray<cDecal>& Decals() { return decals_; }
    cSC4SlotArray<cRingDecal>& RingDecals() { return ringDecals_; }
    std::vector<std::forward_list<std::uint32_t>>& SpatialIndex() { return spatialIndex_; }
    IOverlayHost* Host() const { return host_; }

    // The overlay a decal id / ring index resolves to (the original inlines this at every setter / getter). An
    // unresolvable id gives 0xffffffff; the original indexes the slot array unchecked, which the port does not.
    std::uint32_t ResolveOverlay(std::uint32_t id) const;

private:
    IOverlayHost* host_ = nullptr;
    std::uint32_t refCount_ = 1;

    bool inited_ = false;              // +0x28
    bool dynamic_ = false;             // +0x29
    bool drawWaterPass_ = false;       // +0x2a
    int quality_ = 2;                  // +0x2c
    int secondStage_ = 0;              // +0x30
    bool spatialIndexValid_ = true;    // +0x34
    std::vector<cSC4PackedRenderState> renderStates_;   // +0x38
    gd::cGDCombiner roughShadowCombiner_;               // +0x58
    gd::cGDCombiner shadowCombiner0_;                   // +0x64
    gd::cGDCombiner shadowCombiner1_;                   // +0x70
    int cellCountX_ = 0;               // +0x7c
    int cellCountZ_ = 0;               // +0x80
    float cellSize_ = 1.0f;            // +0x84
    float invCellSize_ = 1.0f;         // +0x88
    float extentX_ = 0, extentX2_ = 0; // +0x8c / +0x90
    float extentZ_ = 0;                // +0x94
    cRZRect visible_;                  // +0x98
    IOverlayTextureFactory* textureFactory_ = nullptr;   // +0xb8
    ITerrainVertBufExtn* vertexBufferExt_ = nullptr;     // +0xc0
    cSC4SlotArray<cOverlay> overlays_;                   // +0xc8
    cSC4SlotArray<cDecal> decals_;                       // +0xe8
    cSC4SlotArray<cRingDecal> ringDecals_;               // +0x108
    IOverlayTexture* shadowTexture_ = nullptr;           // +0x128
    int decalsDrawn_ = 0;              // +0x130
    int shadowsDrawn_ = 0;             // +0x134
    std::uint16_t frameStamp_ = 0;     // +0x138
    int sdsShift_ = 0;                 // +0x13c
    int sdsWidth_ = 0;                 // +0x140
    int sdsHeight_ = 0;                // +0x144
    std::vector<std::forward_list<std::uint32_t>> spatialIndex_;   // +0x148
};

// Free helper of the original's translation unit @0x10072c5fc: the cofactor/adjugate matrix of a 4x4 transform. The
// overlay manager divides it by the homogeneous w to get the inverse model-view matrix.
cS3DMatrix4 OverlayCofactors(const cS3DMatrix4& m);

}  // namespace sc4
