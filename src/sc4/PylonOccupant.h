// cSC4PylonOccupant — the dispatch "pylon": the call-box occupant the dispatch tool places at a fire / police /
// MySim call site. Despite the name it has nothing to do with the power-line pylons (those are
// cSC4PowerPoleOccupant, recovered in sc4/PowerLineGeometry.*); this class is the marker the dispatch manager hands
// to cSC4MySim and tracks in cSC4DispatchVehicleView. 98 function symbols at 0x10022def8..0x100230554 (48
// hand-written functions plus the 50 compiler-generated this-adjusting thunks; the primary vtable is 0x100a87598,
// the other six views are the ones QueryInterface answers below).
//
// Class id 0x49b6d69b, interface cISC4PylonOccupant 0xc9b6d7d6, occupant type 0xa9b6d7de (spelled kOccupantTypePylon
// in sc4/MySimFilters.h, whose cMySimPylonFilter is the other user of the interface). Record version 5.
//
// It is a cSC4Occupant (at +0x10 of the original object) that also is a cIGZMessageTarget2 (+0x08), a
// cS3DModelInstanceBasic (+0x70, the model-instance view the renderer calls) and the cISC4PylonOccupant interface
// (+0x00). Original layout (0x228 bytes):
//   +0x00 cISC4PylonOccupant vptr         +0x08 cIGZMessageTarget2 vptr
//   +0x10 cSC4Occupant (0x60 bytes: +0x48 type = 0xa9b6d7de, +0x52 state, +0x53 flags)
//   +0x70 cS3DModelInstanceBasic (owner +0x78, custom data +0x80, refcount +0x88, properties +0x8c = 3,
//          draw attributes +0x90)
//   +0xa4 u8   mDirty                    RebuildModel must run before the next Draw
//   +0xa8 i32  mUIState                  tUIState, 1 = the default "call box" look (ctor sets 1)
//   +0xac i32  mDispatchState            cISC4DispatchManager::tDispatchState (0 idle, 3 dispatched, 5 active)
//   +0xb0 i32  mDispatchType             cISC4DispatchManager::tDispatchType (0 police .. 6 police heli, 4 MySim)
//   +0xb8 cISC4Occupant* mSourceOccupant  the station the dispatch came from (counted)
//   +0xc0 cS3DTransform mTransform
//   +0xf8 float mBoundsMin[3]            local space
//   +0x104 float mBoundsMax[3]
//   +0x110 float mWorldMin[3]            after the composed camera-facing yaw
//   +0x11c float mWorldMax[3]
//   +0x128 sVF_V3F_T2F mQuad[4]          the billboard: (x, y, 0) + (u, v)
//   +0x178 cSC4PackedRenderState         the pass-1 state (Init packs it)
//   +0x180 cSC4PackedRenderState         the pass-0 state
//   +0x188 IPylonTextureBinding*         the current texture's binding (owned; released by Shutdown only)
//   +0x190 u32 mCurrentTexture           the bound texture id (also GetSortValue's value)
//   +0x194 u16 mCallNumber
//   +0x198 u32 mCallNumberTexture        the rendered "%d" texture (released by Shutdown)
//   +0x19c sVF_V3F_T2F mCallNumberQuad[4]
//   +0x1ec u8  mBlink                    the flash tick's toggle (states 1 and 4 blink)
//   +0x1ed u8  mDragState                the dispatch tool's drag flag (persisted)
//   +0x1f0 i32 mBitmapId                 0 = use mBitmapName, else the {0x856ddbac, 0x46a006b0, id} bitmap
//   +0x1f8 cRZString mBitmapName
//   +0x220 cIGZUnknown* mPylonOwner       counted
//
// Quirks kept (addresses are the original functions):
//  * the constructor never initialises mDispatchType (+0xb0); the port starts it at 0 (the police value) so the
//    object is never read uninitialised.
//  * GetSortValue answers mCurrentTexture, not a distance (0x10022f55a).
//  * Pick only tests a 50-world-unit slab just under the top of the world box: the box minimum y is
//    max.y - ViewportUnitsToWorldUnits(50) / cos(pitch), the stored minimum y is not read (0x10022fed8).
//  * ReleaseCurrentTexture releases the binding (or, without a binding, the raw texture id) but does not clear
//    mCurrentTexture (0x10022e79c); Shutdown clears only mCallNumberTexture, and the destructor releases neither
//    the binding nor either texture (0x10022e0ec) — they leak unless Shutdown runs.
//  * SetUIState / SetDispatchState / SetDispatchType clear the blink flag only when the value actually changed.
//  * DoAnimationTick's 500-second accumulator is a function-local static, so every pylon in the city shares it
//    (0x10022ec28).
//  * Init sets a function-local "static initialised" byte that nothing ever reads again (0x10022e497).
//  * Read accepts versions 3 and up; version 3 reads the legacy transform and stops after the drag flag (the
//    bitmap and the owner are only read for version > 4). The source occupant is read with the cISC4Occupant iid
//    and the owner with iid 1 (0x10022e92a).
//  * Write stores the dirty flag, the blink flag and the drag flag as part of the record.
//  * RebuildModel's world box is the box of only two of the billboard's four corners, (-w/2, 0) and (+w/2, h);
//    the other two corners can stick out of it when the rotation is not a yaw (0x10022f037).
//  * the pylon composes the camera-facing yaw into its transform exactly like cSC4SignpostOccupant, using the
//    transform's rotation flag 4 as "this rotation was built here".
//
// PARTIAL / seams (docs/SEAMS.md): the neighbours the original reaches through SL::sp* globals and the unrecovered
// font / texture-upload code are IPylonOccupantHost / IPylonFont / IPylonTextureBinding. Every method mirrors one
// original call and carries its slot / address in the comment.
#pragma once
#include <cstdint>

#include "gz/StreamOps.h"
#include "gz/cIGZMessage2.h"
#include "rz/cRZString.h"
#include "sc4/Occupant.h"
#include "sc4/OccupantManager.h"   // cISC4OccupantManager and kGZIID_cIGZMessageTarget2
#include "sc4/PackedRenderState.h"
#include "s3d/ModelInstance.h"

class cIGZBuffer;
class cSC4PickContext;
class SC4DrawContext;

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4PylonOccupant = 0x49b6d69b;
constexpr std::uint32_t kGZIID_cISC4PylonOccupant = 0xc9b6d7d6;
constexpr std::uint32_t kPylonOccupantType = 0xa9b6d7de;         // kOccupantTypePylon in sc4/MySimFilters.h
constexpr std::uint16_t kPylonRecordVersion = 5;

// messages (Init registers the first three with SL::spMS2 and the animation tick manager)
constexpr std::uint32_t kPylonMessageViewChanged = 0xa6b79602;    // camera / view moved: mark the model dirty
constexpr std::uint32_t kPylonMessageViewSet = 0x06cdb65b;        // data2 = address of the 3-D view-set pointer
constexpr std::uint32_t kPylonMessageRebuild = 0x26d31ec1;        // RebuildModel now
constexpr std::uint32_t kPylonMessageAnimationTick = 0x86ad10ee;   // data2 = elapsed ms (cAnimationTickManager)
constexpr int kPylonTickChannel = 0x80;                           // SL::ATM() notification channel

// u32 resource instances of the three pylon families, indexed by the dispatch state (0..6); the tables are
// contiguous at 0x100a1b4a0 (MySim), 0x100a1b4c0 (police) and 0x100a1b4e0 (fire), eight entries each.
constexpr std::uint32_t kMySimDispatchPylonResId[8] = {0x2bb075b4, 0x2bb06f3f, 0x2bb075b4, 0x2bb075b4,
                                                       0x2bb075b4, 0x2bb075b4, 0x2bb075b4, 0};
constexpr std::uint32_t kPoliceDispatchPylonResId[8] = {0xea3ee100, 0xea3ee110, 0xea3ee120, 0xea3ee130,
                                                        0xea3ee140, 0xea3ee150, 0xea3ee120, 0};
constexpr std::uint32_t kFireDispatchPylonResId[8] = {0x14315e00, 0x14315e10, 0x14315e20, 0x14315e30,
                                                      0x14315e40, 0x14315e50, 0x14315e20, 0};
// texture resource key of the dispatch pylon graphics: {0x7ab50e44, 0x1abe787d, instance}
constexpr std::uint32_t kPylonTextureType = 0x7ab50e44;
constexpr std::uint32_t kPylonTextureGroup = 0x1abe787d;
// the MySim portrait bitmaps: {0x856ddbac, 0xab7e5421, kMySimDispatchPylonResId[state]} and
// {0x856ddbac, 0x46a006b0, mBitmapId}; colour type 9 = ARGB8888.
constexpr std::uint32_t kPylonBitmapType = 0x856ddbac;
constexpr std::uint32_t kPylonBaseBitmapGroup = 0xab7e5421;
constexpr std::uint32_t kPylonBitmapGroup = 0x46a006b0;
constexpr std::int32_t kPylonBitmapColorType = 9;
// the call-number texture's font is RZ::WCM()'s default style; the digits are drawn at size 3
constexpr std::uint32_t kPylonCallNumberFontStyle = 0x68963c4c;
constexpr int kPylonCallNumberFontSize = 3;
// the billboard's textured quad table @ 0x100a1b488 (v extent) and @ 0x100a1b490 (u span), indexed by
// (dispatch type == 4)
constexpr float kPylonQuadV[2] = {0.5859375f, 0.6924715638160706f};
constexpr float kPylonQuadUSpan[2] = {0.171875f, 0.203125f};
// the two texture-look states BuildCallNumberTexture picks (0x100a165b8)
constexpr float kPylonBlinkStateFactor = 3.0f;

// ---- vertices -----------------------------------------------------------------------------------------------
// Draw submits sVF_V3F_T2F (format 2, 0x14 bytes) quads.
struct PylonVertexT2F {
    float x = 0, y = 0, z = 0;   // +0
    float u = 0, v = 0;          // +0xc
};
static_assert(sizeof(PylonVertexT2F) == 0x14, "sVF_V3F_T2F stride");

// ---- seams --------------------------------------------------------------------------------------------------
// cS3DTextureBinding as the pylon reads it (vslot 0x10 Release, the first word is the texture id) — the same shape
// sc4/SignpostOccupant.h uses.
class IPylonTextureBinding {
public:
    virtual ~IPylonTextureBinding() = default;
    virtual std::uint32_t TextureId() const = 0;
    virtual void Release() = 0;   // 0x1000d9bfa
};

// The font object RZ::WCM() vslot 0x28 returns for style 0x68963c4c, as BuildCallNumberTexture uses it.
class IPylonFont {
public:
    virtual ~IPylonFont() = default;
    virtual void AddRef() = 0;                                          // vslot 0x08
    virtual void Release() = 0;                                         // vslot 0x10
    virtual int GetFontSize() = 0;                                      // vslot 0x78
    virtual void SetFontSize(int size) = 0;                             // vslot 0x80
    virtual int TextHeight(const char* text, std::uint32_t length) = 0;   // vslot 0x128
    virtual int TextWidth(const char* text, std::uint32_t length) = 0;    // vslot 0x180
    // vslot 0x198: (target buffer, x, y, text, length, colour, shadow colour, clip, flags)
    virtual void DrawText(cIGZBuffer* target, int x, int y, const char* text, std::uint32_t length,
                          std::uint32_t color, std::uint32_t color2, const void* clip, int flags) = 0;
};

// The 3-D view set whose "contains occupant" test DoMessage runs (the pointer cIGZMessage2Standard::GetVoid2
// points at).
class IPylonViewSet : public cIGZUnknown {
public:
    virtual bool ContainsOccupant(cISC4Occupant* occupant) = 0;   // [3] 0x18
};

// The services behind the SL::sp* globals the original reaches and the two unrecovered drawing packages
// (RZ::WCM()'s font, nsTexUtil). One method per original call.
class IPylonOccupantHost {
public:
    virtual ~IPylonOccupantHost() = default;
    // SL::spMS2.
    virtual cIGZMessageServer2* Messages() = 0;
    // SL::ATM() (the animation tick manager) vslot 0x28 / 0x30.
    virtual void AddTickNotification(cIGZMessageTarget2* target, int channel) = 0;
    virtual void RemoveTickNotification(cIGZMessageTarget2* target, int channel) = 0;
    // SL::spCity vslot 0x110: the occupant manager (borrowed).
    virtual cISC4OccupantManager* OccupantManager() = 0;
    // SL::spRenderer (0x100b214d0): presence, then [27] 0xd8 HasModel, [28] 0xe0 InsertModel, [29] 0xe8
    // RemoveModel, [30] 0xf0 MoveModel, [31] 0xf8 ChangeModel(instance, reason).
    virtual bool RendererPresent() = 0;
    virtual bool RendererHasModel(s3d::cIS3DModelInstance* model) = 0;
    virtual bool RendererInsertModel(s3d::cIS3DModelInstance* model) = 0;
    virtual bool RendererRemoveModel(s3d::cIS3DModelInstance* model) = 0;
    virtual bool RendererMoveModel(s3d::cIS3DModelInstance* model) = 0;
    virtual bool RendererChangeModel(s3d::cIS3DModelInstance* model, int reason) = 0;
    // SL::spRenderer vslot 0x50 GetDrawContext(); Init calls it but throws the result away.
    virtual void RendererGetDrawContext() = 0;
    // SC4ViewUtilities (SL::spViewUtilities 0x100b214d8): +0x20 pitch angle, +0x5c / +0x64 view direction x / z,
    // ViewportUnitsToWorldUnits @ 0x10017374c.
    virtual float ViewportUnitsToWorldUnits(float units) = 0;
    virtual float ViewPitchAngle() = 0;
    virtual void ViewDirection(float& x, float& z) = 0;
    // The draw-context render-properties chain (+0x50 -> +0x18 -> +0x738): when set, Draw does nothing.
    virtual bool OccupantDrawSuppressed() = 0;
    // RZ::GS() vslot 0x20: a new buffer (one reference); false when there is no graphics system.
    virtual bool CreateBuffer(cIGZBuffer** out) = 0;
    // RZ::WCM() vslot 0x28(0x68963c4c): the default font (borrowed; BuildCallNumberTexture takes a reference).
    virtual IPylonFont* CallNumberFont() = 0;
    // nsTexUtil::SetupTexture(buffer, SL::spGDriver, &handle, true, 0, 0, 0).
    virtual bool UploadTexture(cIGZBuffer* buffer, std::uint32_t& handle) = 0;
    // nsTexUtil::ReleaseTexture(SL::spGDriver, handle) — the original checks SL::spGDriver first.
    virtual void ReleaseTexture(std::uint32_t handle) = 0;
    // cIS3DTextureBindingFactory (service 0x1ac0e11a / iid 0xfac0e219) vslot 0x20: CreateTexture with the key
    // {0x7ab50e44, 0x1abe787d, instance} -> one reference, held by the caller (the pylon's +0x188).
    virtual IPylonTextureBinding* CreateTextureBinding(std::uint32_t instance) = 0;
    // RZ::GZCOM() vslot 0x18(0xa2ffb5d3, 0x02ffbb60) then vslot 0x20(path, &buffer, 8): a bitmap file into a buffer
    // (one reference). The MySim texture path of BuildMySimTexture.
    virtual bool LoadBitmapFromFile(const cIGZString& path, cIGZBuffer** out) = 0;
};
IPylonOccupantHost* PylonOccupantHost();
void SetPylonOccupantHost(IPylonOccupantHost* host);

// ---- the interface (its 18 slots are the primary vtable 0x100a87598, slots 3..20) ---------------------------
class cISC4PylonOccupant : public cIGZUnknown {
public:
    virtual cISC4Occupant* AsOccupant() = 0;                                    // [3]  0x18
    virtual bool Init() = 0;                                                    // [4]  0x20
    virtual bool Shutdown() = 0;                                                // [5]  0x28
    virtual void SetUIState(std::int32_t state) = 0;                            // [6]  0x30
    virtual std::int32_t GetUIState() const = 0;                                // [7]  0x38
    virtual void SetBitmapInfo(std::int32_t bitmapId, const cIGZString& name) = 0;   // [8]  0x40
    virtual void SetDispatchType(std::int32_t type) = 0;                        // [9]  0x48
    virtual std::int32_t GetDispatchType() const = 0;                           // [10] 0x50
    virtual void SetDispatchState(std::int32_t state) = 0;                      // [11] 0x58
    virtual std::int32_t GetDispatchState() const = 0;                          // [12] 0x60
    virtual void SetCallNumber(std::uint16_t callNumber) = 0;                   // [13] 0x68
    virtual std::uint16_t GetCallNumber() const = 0;                            // [14] 0x70
    virtual void SetDragState(bool dragging) = 0;                               // [15] 0x78
    virtual bool GetDragState() const = 0;                                      // [16] 0x80
    virtual void SetSourceOccupant(cISC4Occupant* occupant) = 0;                // [17] 0x88
    virtual cISC4Occupant* GetSourceOccupant() const = 0;                       // [18] 0x90
    virtual void SetPylonOwner(cIGZUnknown* owner) = 0;                         // [19] 0x98
    virtual cIGZUnknown* GetPylonOwner() const = 0;                             // [20] 0xa0
};

// ---- the occupant -------------------------------------------------------------------------------------------
class cSC4PylonOccupant : public cSC4Occupant,
                          public s3d::cS3DModelInstanceBasic,
                          public cIGZMessageTarget2,
                          public cISC4PylonOccupant {
public:
    cSC4PylonOccupant();                              // 0x10022def8
    ~cSC4PylonOccupant() override;                    // 0x10022e0ec (D1), 0x10022e22c (D0), 0x10022e280 (D2)

    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x10022e3a8
    std::uint32_t AddRef() override { return cSC4Occupant::AddRef(); }     // 0x10022e2e0
    std::uint32_t Release() override { return cSC4Occupant::Release(); }   // 0x10022e344

    // cISC4Occupant / cIGZSerializable
    bool Init() override;                             // 0x10022e476
    bool Shutdown() override;                         // 0x10022e5d6
    void GetPosition(float* xyz) override;            // 0x10022fd60
    void SetPosition(const float* xyz) override;      // 0x10022fd9c
    void GetBoundingBox(float* minXyz, float* maxXyz) override;   // 0x10022fe1a
    void SetVisibility(bool visible, bool postNow) override;      // 0x10022fe8e
    bool Write(cIGZOStream& stream) const override;   // 0x10022e7fc
    bool Read(cIGZIStream& stream) override;          // 0x10022e92a
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4PylonOccupant; }   // 0x100230508

    // cISC4PylonOccupant
    cISC4Occupant* AsOccupant() override;             // 0x1002304b4
    void SetUIState(std::int32_t state) override;     // 0x10022f24c
    std::int32_t GetUIState() const override { return uiState_; }      // 0x10022f26e
    void SetBitmapInfo(std::int32_t bitmapId, const cIGZString& name) override;   // 0x10023004a
    void SetDispatchType(std::int32_t type) override;                    // 0x10022f2a8
    std::int32_t GetDispatchType() const override { return dispatchType_; }   // 0x10022f2ca
    void SetDispatchState(std::int32_t state) override;                  // 0x10022f27a
    std::int32_t GetDispatchState() const override { return dispatchState_; }   // 0x10022f29c
    void SetCallNumber(std::uint16_t callNumber) override;               // 0x1002304be
    std::uint16_t GetCallNumber() const override { return callNumber_; }   // 0x1002304d2
    void SetDragState(bool dragging) override { dragState_ = dragging; }   // 0x1002304e0
    bool GetDragState() const override { return dragState_; }              // 0x1002304ee
    void SetSourceOccupant(cISC4Occupant* occupant) override;            // 0x10022f2d6
    cISC4Occupant* GetSourceOccupant() const override { return sourceOccupant_; }   // 0x1002304fa
    void SetPylonOwner(cIGZUnknown* owner) override;                     // 0x10022ffe8
    cIGZUnknown* GetPylonOwner() const override { return pylonOwner_; }  // 0x10023003c

    // cIGZMessageTarget2
    bool DoMessage(cIGZMessage2* message) override;   // 0x10022eaf6

    // cIS3DModelInstance (the +0x70 view)
    cIGZUnknown* GetOwner() override;                 // 0x100230514 (the occupant, not the model base's owner)
    void SetOwner(cIGZUnknown*) override {}           // 0x10023051e (does nothing at all)
    void GetBounds(cS3DBoundingBox& out) override;          // 0x10022f32a
    void GetWorldBounds(cS3DBoundingBox& out) override;     // 0x10022f3aa
    void GetTransform(cS3DTransform& out) override;         // 0x10022f42a
    void SetTransform(const cS3DTransform& value) override; // 0x10022f4c0
    void GetSortValue(std::uint32_t& out) const override;   // 0x10022f55a
    bool Pick(cSC4PickContext* context) override;           // 0x10022fed8
    int Draw(SC4DrawContext* context, bool highlight) override;   // 0x10022f580

    // the build / placement helpers (public: the messages, SetPosition and the texture path drive them)
    void DoAnimationTick(std::uint32_t seconds);      // 0x10022ec28
    void RebuildModel();                              // 0x10022ec78
    bool RebuildCurrentTexture();                     // 0x10022f744
    void BuildCallNumberTexture(float topY);          // 0x10022f858
    bool BuildMySimTexture();                         // 0x100230076
    bool ReleaseCurrentTexture();                     // 0x10022e79c
    bool PlaceInOccupantManager(bool place);           // 0x10022e6da
    bool PlaceInRenderer(bool place);                 // 0x10022fc98

    // recovered state, for tests and for the dispatch manager
    bool IsDirty() const { return dirty_; }                       // +0xa4
    bool Blink() const { return blink_; }                         // +0x1ec
    std::int32_t BitmapId() const { return bitmapId_; }           // +0x1f0
    const cRZString& BitmapName() const { return bitmapName_; }   // +0x1f8
    std::uint32_t CurrentTexture() const { return currentTexture_; }         // +0x190
    std::uint32_t CallNumberTexture() const { return callNumberTexture_; }   // +0x198
    IPylonTextureBinding* TextureBinding() const { return textureBinding_; } // +0x188
    const cS3DTransform& Transform() const { return transform_; }            // +0xc0
    const PylonVertexT2F* Quad() const { return quad_; }                    // +0x128
    const PylonVertexT2F* CallNumberQuad() const { return callNumberQuad_; } // +0x19c
    const cSC4PackedRenderState& RenderStatePass1() const { return renderStatePass1_; }   // +0x178
    const cSC4PackedRenderState& RenderStatePass0() const { return renderStatePass0_; }   // +0x180
    const float* LocalBoundsMin() const { return boundsMin_; }   // +0xf8
    const float* LocalBoundsMax() const { return boundsMax_; }   // +0x104
    const float* WorldBoundsMin() const { return worldMin_; }    // +0x110
    const float* WorldBoundsMax() const { return worldMax_; }    // +0x11c
    bool IsInOccupantManager() const { return (flags_ & 1) != 0; }   // flags bit 0
    bool IsInRenderer() const { return (flags_ & 2) != 0; }          // flags bit 1

private:
    void ComputeWorldBounds(float halfWidth, float height);
    void SetRenderStates();

    // cSC4Occupant's payload is at +0x10; the fields below start at +0xa4 of the original (the model-instance
    // subobject covers +0x70..+0xa4).
    bool dirty_ = true;                               // +0xa4
    std::int32_t uiState_ = 1;                        // +0xa8
    std::int32_t dispatchState_ = 0;                  // +0xac
    std::int32_t dispatchType_ = 0;                   // +0xb0 (the original leaves this uninitialised)
    cISC4Occupant* sourceOccupant_ = nullptr;         // +0xb8
    cS3DTransform transform_;                         // +0xc0
    float boundsMin_[3] = {1.0f, 1.0f, 1.0f};         // +0xf8 the ctor's "empty" box
    float boundsMax_[3] = {0.0f, 0.0f, 0.0f};         // +0x104
    float worldMin_[3] = {1.0f, 1.0f, 1.0f};          // +0x110
    float worldMax_[3] = {0.0f, 0.0f, 0.0f};          // +0x11c
    PylonVertexT2F quad_[4] = {};                     // +0x128
    cSC4PackedRenderState renderStatePass1_{0, 0};    // +0x178 (the ctor zeroes both words)
    cSC4PackedRenderState renderStatePass0_{0, 0};    // +0x180
    IPylonTextureBinding* textureBinding_ = nullptr;  // +0x188
    std::uint32_t currentTexture_ = 0;                // +0x190
    std::uint16_t callNumber_ = 0;                    // +0x194
    std::uint32_t callNumberTexture_ = 0;             // +0x198
    PylonVertexT2F callNumberQuad_[4] = {};           // +0x19c
    bool blink_ = false;                              // +0x1ec
    bool dragState_ = false;                          // +0x1ed
    std::int32_t bitmapId_ = 0;                       // +0x1f0
    cRZString bitmapName_;                            // +0x1f8
    cIGZUnknown* pylonOwner_ = nullptr;               // +0x220
};

}  // namespace sc4
