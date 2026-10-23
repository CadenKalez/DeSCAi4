// cS3DModelInstanceAnimatedSprite (42 functions incl. thunks, 0x1001a455c..0x1001a7120, iid 0xa818edd8): a billboard
// model instance whose picture is picked from a pre-rendered sprite sheet by the viewing angle. It sits on the
// cS3DModelInstanceBasic base (owner / custom data / draw attributes) and implements the animated-instance interface
// (placement, frame sets, play modes, opacity, highlight) with its own transform instead of a loaded model.
//
// Sprite sheets: a cIS3DResSpriteAnim names five cIS3DResSpriteAnimTables (one per zoom level, wrapped by
// cAnimatedSpriteTableData) and the texture binding that holds the sheets' GL texture ids. For a view direction
// (elevation, azimuth) the table gives an 8 byte record {texture, log2 size, x, y, width, height, offsetX, offsetY}
// which GetTextureData turns into the four texture coordinate pairs and the quad size; UpdateView then turns the quad
// size into the four corner offsets (and the local bounds) for the current camera.
//
// Original layout (object start): +0 animated interface vptr, +8 cS3DModelInstanceBasic (draw attribute flags +0x28:
// bit 0 highlighted, bit 1 translucent, bit 2 self-lit; highlight value +0x29; shade +0x2c; opacity +0x38),
// +0x40 frame set info, +0x48 transform, +0x80 local bounds (min, max), +0x98 zoom, +0x99 rotation, +0x9c packed render
// state, +0xa8 current texture data, +0xb0 cached table record, +0xb8 frame dirty, +0xb9 view dirty, +0xba transform
// counter at the last corner update, +0xbb frame, +0xbc auto frame rate (-100 unset), +0xc0 frame accumulator,
// +0xc4 play mode, +0xc5 direction, +0xc6 catch up, +0xc8 frame count, +0xcc[5] scale per zoom, +0xe0 texture binding,
// +0xe8[5] table data per zoom, +0x110 four vertices (24 bytes: position, colour, u, v), +0x170 four corner offsets.
//
// Quirks kept: the frame set info (+0x40) is never assigned by this class, GetAnimInfo returns null (0x1001a5f00);
// DoAutoFrameUpdate resets the accumulator to exactly 1 when it skips frames without catch up (0x1001a5dc4);
// SetOrientation(angles) always reports 0; UpdateView also mirrors the x / z extents about the origin (0x1001a67f8).
// Compile without FMA contraction.
#pragma once
#include <array>
#include <cstdint>
#include <memory>

#include "gz/cGZPersistResourceKey.h"
#include "s3d/AnimatedInstance.h"
#include "s3d/Camera.h"
#include "s3d/ModelInstance.h"
#include "sc4/MiscTypes.h"
#include "sc4/PackedRenderState.h"

namespace s3d {

constexpr std::uint32_t kGZIID_cIS3DModelInstanceAnimated = 0xa818edd8;
constexpr std::uint32_t kGZIID_cIS3DResSpriteAnim = 0x29a5d1ec;        // resource type asked of the resource manager
constexpr std::uint32_t kGZIID_cIS3DResSpriteAnimTable = 0x69b6f01c;
constexpr std::uint32_t kGZSID_TextureBindingFactory = 0x1ac0e11a;     // service id
constexpr std::uint32_t kGZIID_TextureBindingFactory = 0xfac0e219;

// One 8 byte record of a sprite table (cAnimatedSpriteTextureResourceData).
struct SpriteTextureRecord {
    std::uint8_t texture, sizeShift, x, y;
    std::int8_t width, height, offsetX, offsetY;   // a negative width / height mirrors the picture
};

// cAnimatedSpriteTextureData: what the quad needs for the current view.
struct cAnimatedSpriteTextureData {
    std::uint32_t textureIndex = 0;                // +0
    float width = 0, height = 0, offsetX = 0, offsetY = 0;   // +4..+0x10 (scaled by the zoom's scale)
};

struct SpriteVertex {            // 0x18 bytes
    float x = 0, y = 0, z = 0;
    std::uint32_t color = 0xffffffff;
    float u = 0, v = 0;
};

// cIS3DResSpriteAnim::GetData() (vslot 0x18).
struct SpriteAnimData {
    std::uint32_t textureKey[3];       // +0: type, group, instance of the texture binding
    std::uint32_t tableType;           // +0xc
    std::uint32_t tableGroup;          // +0x10
    std::uint32_t tableInstance[5];    // +0x14 per zoom
    float zoomScale[5];                // +0x28 per zoom
    std::uint32_t frameCount;          // +0x3c
};
class cIS3DResSpriteAnim : public cIGZUnknown {
public:
    virtual const SpriteAnimData* GetData() = 0;   // [3]
};

// cS3DTextureBinding as the sprite uses it: **binding is the array of GL texture ids (indexed by sheet).
class ISpriteTextureBinding {
public:
    virtual ~ISpriteTextureBinding() = default;
    virtual const std::uint32_t* TextureIds() = 0;
    virtual void Release() = 0;          // cS3DTextureBinding::Release @ 0x1000d9bfa
};

// The parts of SC4DrawContext the sprite talks to.
class ISpriteDrawContext {
public:
    virtual ~ISpriteDrawContext() = default;
    virtual int Zoom() = 0;                                  // +0x14
    virtual int Rotation() = 0;                              // +0x18
    virtual cS3DCamera* Camera() = 0;                        // +0x58
    virtual float CameraMagnification() = 0;                 // cSC4CameraControl::GetCustomMagnification(+0x60) @ 0x10076bd30
    virtual bool ViewPixelScale(float& scale) = 0;           // +0x28 object's +0x158; false when the pointer is null
    virtual std::uint32_t ViewportWidth() = 0;               // +8
    virtual void ResetModelViewTransform() = 0;              // 0x1007726ee
    virtual void SetModelShade(cIS3DModelInstance* instance, const float* shade) = 0;   // 0x10077360a
    virtual void SetSelfLitShade(const float* shade) = 0;    // 0x1007735d8
    virtual void SetRenderState(const cSC4PackedRenderState& state) = 0;   // 0x100773906
    virtual void SetTransparency(float opacity) = 0;         // 0x100773704
    virtual void SetAlphaFunc(int function, float reference) = 0;   // 0x100772e20
    virtual void SetTexture(std::uint32_t texture, int stage) = 0;  // 0x100772d0e
    virtual void DrawPrims(int primitive, int format, int count, const SpriteVertex* vertices) = 0;   // 0x100773462
    virtual void ResetShade() = 0;                           // 0x1007736be
    virtual void ResetTransparency() = 0;                    // 0x10077371a
};

class ISpriteInstanceHost {
public:
    virtual ~ISpriteInstanceHost() = default;
    // Wraps the SC4DrawContext handed to Draw / UpdateView.
    virtual ISpriteDrawContext* DrawContext(SC4DrawContext* context) = 0;
    // SL::spRM vslot 0x18 (key, 0x29a5d1ec, &out, 0, 0): the returned animation is AddRef'd (the caller Releases).
    virtual cIS3DResSpriteAnim* FindSpriteAnim(const cGZPersistResourceKey& key) = 0;
    // SL::spRM vslot 0x18 ({type, group, instance}, 0x69b6f01c, &out, 0, 0): AddRef'd table.
    virtual sc4::ISpriteAnimTable* FindSpriteAnimTable(std::uint32_t type, std::uint32_t group, std::uint32_t instance) = 0;
    // RZGetFramework()->GetSystemService(0x1ac0e11a, 0xfac0e219)->vslot 0x20(key, &binding); null if the service or the
    // texture is missing. The sprite owns the returned binding and Releases it.
    virtual ISpriteTextureBinding* CreateTextureBinding(const std::uint32_t key[3]) = 0;
    // SC4ViewUtilities::TrianglePick(context, a, b, c) @ 0x1001764d8
    virtual bool TrianglePick(cSC4PickContext* context, const float* a, const float* b, const float* c) = 0;
};
ISpriteInstanceHost* SpriteInstanceHost();
void SetSpriteInstanceHost(ISpriteInstanceHost* host);

// The animated-instance interface (vslots 19..39 of the primary vtable).
class cIS3DModelInstanceAnimated : public virtual cIS3DModelInstance {
public:
    virtual cIS3DModelInstance* AsModelInstance() = 0;
    virtual int SetPosition(const cS3DVector3& position) = 0;
    virtual int SetOrientation(const cS3DVector3& angles) = 0;
    virtual int SetOrientation(const cS3DVector3& target, const cS3DVector3& up) = 0;
    virtual void ModelPointToWorldPoint(cS3DVector3& point) = 0;
    virtual void ModelPointToWorldPoint(float* point) = 0;
    virtual void ModelVectorToWorldVector(cS3DVector3& vector) = 0;
    virtual void ModelVectorToWorldVector(float* vector) = 0;
    virtual int SetZoomAndRotation(int zoom, int rotation) = 0;
    virtual int SetFrameset(int index) = 0;
    virtual int SetFrame(int frame) = 0;
    virtual int GetFrame() const = 0;
    virtual int GetFrameCount() const = 0;
    virtual int IncrementFrame() = 0;
    virtual int SetAutoFrameRate(float rate) = 0;
    virtual int DoAutoFrameUpdate(float seconds) = 0;
    virtual void SetPlayMode(int mode) = 0;
    virtual const void* GetAnimInfo() const = 0;
    virtual int SetOpacity(bool translucent, float opacity) = 0;
    virtual int SetHighlight(int value) = 0;
    virtual int SetSelfIlluminated(bool on) = 0;
};

class cS3DModelInstanceAnimatedSprite : public cIS3DModelInstanceAnimated, public cS3DModelInstanceBasic {
public:
    cS3DModelInstanceAnimatedSprite();               // 0x1001a455c
    ~cS3DModelInstanceAnimatedSprite() override;     // 0x1001a47dc (+ deleting 0x1001a4900 and the thunks)

    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x1001a49ce
    std::uint32_t AddRef() override { return cS3DModelInstanceBasic::AddRef(); }   // 0x1001a4992
    std::uint32_t Release() override { return cS3DModelInstanceBasic::Release(); } // 0x1001a49b0

    cIS3DModelInstance* AsModelInstance() override { return static_cast<cIS3DModelInstance*>(this); }   // 0x1001a707a
    int SetPosition(const cS3DVector3& position) override;                       // 0x1001a4a1c
    int SetOrientation(const cS3DVector3& angles) override;                      // 0x1001a4a40
    int SetOrientation(const cS3DVector3& target, const cS3DVector3& up) override;   // 0x1001a57d8
    void ModelPointToWorldPoint(cS3DVector3& point) override;                    // 0x1001a5926
    void ModelPointToWorldPoint(float* point) override;                          // 0x1001a59e2
    void ModelVectorToWorldVector(cS3DVector3& vector) override;                 // 0x1001a5a82
    void ModelVectorToWorldVector(float* vector) override;                       // 0x1001a5b02
    int SetZoomAndRotation(int zoom, int rotation) override;                     // 0x1001a5b82
    int SetFrameset(int index) override;                                         // 0x1001a5bba
    int SetFrame(int frame) override;                                            // 0x1001a5c14
    int GetFrame() const override { return frame_; }                             // 0x1001a5c80
    int GetFrameCount() const override { return std::int32_t(frameCount_); }     // 0x1001a5c8e
    int IncrementFrame() override;                                               // 0x1001a5c9a
    int IncrementFrameSimple();                                                  // 0x1001a5ce8
    int SetAutoFrameRate(float rate) override;                                   // 0x1001a5db0
    int DoAutoFrameUpdate(float seconds) override;                               // 0x1001a5dc4
    void SetPlayMode(int mode) override { playMode_ = std::uint8_t(mode); }      // 0x1001a5ef2
    const void* GetAnimInfo() const override { return nullptr; }                 // 0x1001a5f00
    int SetOpacity(bool translucent, float opacity) override;                    // 0x1001a5f08
    int SetHighlight(int value) override;                                        // 0x1001a5f5a
    int SetSelfIlluminated(bool on) override;                                    // 0x1001a5f96
    bool LoadSpriteResource(const cGZPersistResourceKey& key);                   // 0x1001a5fc6
    bool LoadSpriteResource(cIS3DResSpriteAnim* anim);                           // 0x1001a6028

    bool GetTextureData(float elevation, float azimuth, float unused, int zoom, int rotation, int frame, int unused2,
                        cAnimatedSpriteTextureData* out);                        // 0x1001a6384
    const SpriteTextureRecord* GetTextureResourceData(float elevation, float azimuth, float unused, int zoom,
                                                      int rotation);             // 0x1001a63ee
    void GetTextureData(int zoom, int frame, int unused, const SpriteTextureRecord* record,
                        cAnimatedSpriteTextureData* out);                        // 0x1001a64ca

    void GetSortValue(std::uint32_t& out) const override;                        // 0x1001a66fa
    void GetWorldBounds(cS3DBoundingBox& out) override;                          // 0x1001a675a
    void UpdateView(SC4DrawContext* context);                                    // 0x1001a67f8
    int Draw(SC4DrawContext* context, bool flag) override;                       // 0x1001a6ba0
    bool Pick(cSC4PickContext* context) override;                                // 0x1001a6e24
    void GetBounds(cS3DBoundingBox& out) override;                               // 0x1001a701c
    void GetTransform(cS3DTransform& out) override;                              // 0x1001a7058
    void SetTransform(const cS3DTransform& transform) override;                  // 0x1001a706c

    // state access for the view code and the tests
    const cS3DTransform& Transform() const { return transform_; }
    const SpriteVertex* Vertices() const { return vertices_; }
    const float (*CornerOffsets() const)[3] { return corners_; }
    const cAnimatedSpriteTextureData& TextureData() const { return *textureData_; }
    const cS3DBoundingBox& LocalBounds() const { return bounds_; }
    ISpriteTextureBinding* TextureBinding() const { return binding_; }
    sc4::cAnimatedSpriteTableData* TableData(int zoom) const { return tables_[std::size_t(zoom)].get(); }
    bool FrameDirty() const { return frameDirty_; }
    bool ViewDirty() const { return viewDirty_; }
    void SetFramesetInfo(FramesetInfo* info) { framesets_ = info; }   // port-only: nothing in the original assigns +0x40
    float AutoFrameRate() const { return autoFrameRate_; }
    float FrameAccumulator() const { return accumulator_; }
    int Direction() const { return direction_; }
    int PlayMode() const { return playMode_; }
    float ZoomScale(int zoom) const { return zoomScale_[zoom]; }

private:
    SC4DrawAttributes Attributes() const { return GetDrawAttributes(); }

    FramesetInfo* framesets_ = nullptr;                      // +0x40
    cS3DTransform transform_;                                // +0x48
    cS3DBoundingBox bounds_{{0, 0, 0}, {1, 1, 1}};           // +0x80
    std::uint8_t zoom_ = 0xff, rotation_ = 0xff;             // +0x98 / +0x99
    cSC4PackedRenderState renderState_;                      // +0x9c
    std::unique_ptr<cAnimatedSpriteTextureData> textureData_;   // +0xa8
    const SpriteTextureRecord* record_ = nullptr;            // +0xb0
    bool frameDirty_ = true;                                 // +0xb8
    bool viewDirty_ = true;                                  // +0xb9
    std::uint8_t lastTransformCounter_ = 0xff;               // +0xba
    std::uint8_t frame_ = 0;                                 // +0xbb
    float autoFrameRate_ = 0.001f;                           // +0xbc
    float accumulator_ = 0.0f;                               // +0xc0
    std::uint8_t playMode_ = kPlayLoop;                      // +0xc4
    std::int8_t direction_ = 1;                              // +0xc5
    bool catchUp_ = false;                                   // +0xc6
    std::uint32_t frameCount_ = 0;                           // +0xc8
    float zoomScale_[5] = {};                                // +0xcc
    ISpriteTextureBinding* binding_ = nullptr;               // +0xe0
    std::array<std::unique_ptr<sc4::cAnimatedSpriteTableData>, 5> tables_;   // +0xe8
    SpriteVertex vertices_[4];                               // +0x110
    float corners_[4][3] = {};                               // +0x170
};

}  // namespace s3d
