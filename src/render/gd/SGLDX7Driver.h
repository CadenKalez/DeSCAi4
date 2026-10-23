// nSGLDX7::cGDriver — the Aspyr build's Direct3D-7-shaped graphics driver: a 105-slot core interface at object +0x0, a
// colour-buffer query interface at +0x8, a lighting query interface at +0x10 and cIGZGDriver at +0x18. This is a faithful
// recovery of the driver's observable
// surface: the interface methods, the state mirror (render states, texture stages, lights, materials, viewport, buffer
// regions, video modes) and the capability/format bookkeeping. Nothing here emulates D3D7 or OpenGL: every device,
// surface, context, texture-manager and primitive-manager call goes through a narrow seam below, with the original vtable
// slot (byte offset) in the comment so the real neighbour can be hooked up later. The shipping renderer is Metal
// (cMetalGDriver), which implements cIGZGDriver directly; this class is the reference the Metal paths are checked against.
//
// Layout notes (byte offsets in the original object): +0x20 refcount, +0x30 cTextureManager, +0x38 cPrimitiveManager,
// +0x40 device description, +0x138 video modes (0x38 each), +0x150 window, +0x158 error code, +0x15c current texture
// stage, +0x160 stage count, +0x168 stage records (0x80 each), +0x180 stage-dirty flag, +0x188 texture params valid,
// +0x18c unpack row length, +0x1e0 clear colour / +0x1e4 clear depth / +0x1e8 clear stencil, +0x1ec window rect,
// +0x20c vertex format / +0x210 stride / +0x214 standard stride / +0x220 vertex data, +0x228 mode count,
// +0x23c current video mode (0x34), +0x270 matrix target, +0x2b0 device viewport, +0x2c8 viewport mirror,
// +0x2e0 D3DX context, +0x2ec in-scene, +0x2ed device lost, +0x2ee initialised, +0x2ef textures ready,
// +0x300 current bits per pixel, +0x308 buffer regions (0x20 each), +0x324 material (0x44), +0x368 lights (0x68 each),
// +0x380 lighting / vertex-colour flags.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "render/GDIndexOps.h"
#include "render/SimGL.h"
#include "render/TextureStageData.h"
#include "render/gd/GDriverBase.h"
#include "render/gd/IGZGDriver.h"
#include "render/gd/VideoModes.h"

namespace nSGLDX7 {

// The driver's interface speaks the gd (cIGZGDriver) vocabulary; the original used these names unqualified.
using gd::cGDCombiner;
using gd::cIGZGDriver;
using gd::eGDBlend;
using gd::eGDDriverState;
using gd::eGDElementType;
using gd::eGDFogParam;
using gd::eGDFogParamType;
using gd::eGDInternalTextureFormat;
using gd::eGDMatrixTarget;
using gd::eGDParameter;
using gd::eGDPrimType;
using gd::eGDShade;
using gd::eGDStencilOp;
using gd::eGDTexCoordSource;
using gd::eGDTexMatFlags;
using gd::eGDTestFunc;
using gd::eGDTextureEnvModeParam;
using gd::eGDTextureEnvParamType;
using gd::eGDTextureEnvTarget;
using gd::eGDTextureFormat;
using gd::eGDTextureHintFlags;
using gd::eGDTextureParam;
using gd::eGDTextureParamType;
using gd::eGDTextureStageCombineModeParam;
using gd::eGDTextureStageCombineOperandParamType;
using gd::eGDTextureStageCombineParamType;
using gd::eGDTextureStageCombineScaleParam;
using gd::eGDTextureStageCombineScaleParamType;
using gd::eGDTextureStageCombineSourceParam;
using gd::eGDTextureStageCombineSourceParamType;
using gd::eGDTextureTarget;
using gd::eGDType;
using gd::eGDVertexFormat;
using gd::sGDMode;

// ---------------------------------------------------------------------------------------------------------------------
// Small records whose layouts are observable in the original.

// D3DLIGHT7 as kept in the driver's light vector (0x68 bytes each).
struct sGDLight {
    std::uint32_t type = 0;          // +0x00
    float diffuse[4] = {0, 0, 0, 0}; // +0x04
    float specular[4] = {0, 0, 0, 0};// +0x14
    float ambient[4] = {0, 0, 0, 0}; // +0x24
    float position[3] = {0, 0, 0};   // +0x34
    float direction[3] = {0, 0, 0};  // +0x40
    float range = 0;                 // +0x4c
    float falloff = 0;               // +0x50
    float attenuation0 = 0;          // +0x54
    float attenuation1 = 0;          // +0x58
    float attenuation2 = 0;          // +0x5c
    float theta = 0;                 // +0x60
    float phi = 0;                   // +0x64
};
static_assert(sizeof(sGDLight) == 0x68);

// D3DMATERIAL7 as kept at +0x324.
struct sGDMaterial {
    float diffuse[4] = {0, 0, 0, 0};  // +0x00
    float ambient[4] = {0, 0, 0, 0};  // +0x10
    float specular[4] = {0, 0, 0, 0}; // +0x20
    float emissive[4] = {0, 0, 0, 0}; // +0x30
    float power = 0;                  // +0x40
};
static_assert(sizeof(sGDMaterial) == 0x44);

// One surface format enumerated by the device (0x28 bytes in the original vector).
struct cDXTextureFormatInfo {
    std::uint32_t alphaMask = 0;    // +0x00
    std::uint32_t redMask = 0;      // +0x04
    std::uint32_t greenMask = 0;    // +0x08
    std::uint32_t blueMask = 0;     // +0x0c
    std::uint32_t flags = 0;        // +0x10 (DDPIXELFORMAT.dwFlags)
    std::uint32_t fourCC = 0;       // +0x14
    std::uint32_t bitsPerPixel = 0; // +0x18 (DDPIXELFORMAT.dwRGBBitCount)
    std::uint32_t colorBits = 0;    // +0x1c popcount(R|G|B)
    std::uint32_t alphaBits = 0;    // +0x20 popcount(A)
    std::uint32_t formatIndex = 0;  // +0x24 table index (0..8) or DXT index (9/0xa/0xb/0xc/0xd)
};
static_assert(sizeof(cDXTextureFormatInfo) == 0x28);

// DDPIXELFORMAT as the device hands it to the enumeration callback (0x20 bytes).
struct sGDPixelFormat {
    std::uint32_t dwSize = 0x20;    // +0x00
    std::uint32_t dwFlags = 0;      // +0x04
    std::uint32_t dwFourCC = 0;     // +0x08
    std::uint32_t dwRGBBitCount = 0;// +0x0c
    std::uint32_t dwRBitMask = 0;   // +0x10
    std::uint32_t dwGBitMask = 0;   // +0x14
    std::uint32_t dwBBitMask = 0;   // +0x18
    std::uint32_t dwRGBAlphaBitMask = 0;   // +0x1c
};
static_assert(sizeof(sGDPixelFormat) == 0x20);

// Context record SurfaceFormatEnumerator receives (a pointer to the vector plus a "compressed format seen" byte, as
// the original's stack context at 0x10060e085).
struct sGDTextureFormatContext {
    std::vector<cDXTextureFormatInfo>* formats = nullptr;
    std::uint8_t compressed = 0;   // +0x08
    std::uint8_t pad[7] = {};
};

// The device description (0xf4 bytes) the driver keeps at +0x40, with the fields the driver reads.
namespace caps {
constexpr std::size_t kDeviceDescSize = 0xf4;
constexpr std::size_t kRenderBitDepth = 0x44;   // DDBD_* of the colour buffer
constexpr std::size_t kZBufferBitDepth = 0x5c;  // DDBD_* of the depth buffer
constexpr std::size_t kHasStencil = 0xac;       // non-zero when a stencil buffer exists
constexpr std::size_t kStageCount = 0xb8;       // u16: texture stages
constexpr std::size_t kMaxTextureSize = 0xba;   // u16
constexpr std::size_t kFog = 0xe0;              // bit 2: table fog supported
}  // namespace caps

// Deferred-driver render states (values as the shim passes them; see SetDefaultD3DDeviceState).
namespace rs {
enum : std::uint32_t {
    TexturePerspective = 4, FillMode = 8, ShadeMode = 9, ZWriteEnable = 0xe, AlphaTestEnable = 0xf,
    SrcBlend = 0x13, DestBlend = 0x14, CullMode = 0x16, ZFunc = 0x17, AlphaRef = 0x18, AlphaFunc = 0x19,
    DitherEnable = 0x1a, AlphaBlendEnable = 0x1b, FogEnable = 0x1c, SpecularEnable = 0x1d, FogColor = 0x22,
    FogTableMode = 0x8c, FogStart = 0x24, FogEnd = 0x25, FogDensity = 0x26, ZBias = 0x2f,
    StencilEnable = 0x34, StencilFail = 0x35, StencilZFail = 0x36, StencilPass = 0x37, StencilFunc = 0x38,
    StencilRef = 0x39, StencilMask = 0x3a, StencilWriteMask = 0x3b, TextureFactor = 0x3c,
    Lighting = 0x89, Ambient = 0x8b, ColorVertex = 0x8d, DiffuseMaterialSource = 0x91,
    SpecularMaterialSource = 0x92, AmbientMaterialSource = 0x93,
};
}  // namespace rs

// The value tables of the shim (read from the binary: 0x100a26820 .. 0x100a268c0).
namespace lookup {
constexpr std::uint32_t kPrimitiveType[8] = {4, 5, 6, 1, 2, 3, 4, 4};   // vLookupPrimitiveType
constexpr std::uint32_t kCompareFunction[8] = {1, 2, 3, 4, 5, 6, 7, 8};
constexpr std::uint32_t kStencilOperation[6] = {2, 1, 3, 4, 5, 6};
constexpr std::uint32_t kBlendMode[11] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::uint32_t kShadeModel[2] = {1, 2};
constexpr std::uint32_t kFogMode[3] = {1, 2, 3};
constexpr std::uint32_t kDriverMode[6] = {0xf, 0x7, 0x34, 0x16, 0x1b, 0};   // AlphaTest..Texturing (Fog is special-cased)
constexpr std::uint32_t kCombineSource[4] = {2, 1, 3, 0};                    // texture, current, factor, diffuse
constexpr std::uint32_t kTexCoordSource[4] = {0x00000, 0x20000, 0x10000, 0x30000};   // @ 0x100a26750
// SetCombiner's two 4-entry operand tables (@ 0x100a26770 / 0x100a26780); the shim only defines nibbles 0..3.
constexpr std::uint32_t kColorOperand[4] = {2, 3, 4, 5};
constexpr std::uint32_t kAlphaOperand[4] = {4, 5, 4, 5};
}  // namespace lookup

// ---------------------------------------------------------------------------------------------------------------------
// Seams. Each method mirrors the original call noted in its comment.

// The DirectDraw7 surface calls the driver makes (surface vtable offsets from the binary).
class ID3D7Surface {
public:
    virtual ~ID3D7Surface() = default;
    virtual long GetPixelFormat(sGDPixelFormat& out) = 0;                                            // +0xa8
    virtual long GetSurfaceDesc(void* desc) = 0;                                                    // +0xb0
    virtual long Blt(const void* dstRect, ID3D7Surface* src, const void* srcRect, std::uint32_t flags,
                     void* fx) = 0;                                                                  // +0x28
    virtual long IsLost() = 0;                                                                       // +0xb8
    virtual long Lock(const void* rect, void* desc, std::uint32_t flags, void* event) = 0;           // +0xc8
    virtual long Restore() = 0;                                                                      // +0xe0
    virtual long Unlock(const void* rect) = 0;                                                       // +0x100
};

// The Direct3D7 device calls the driver makes (device vtable offsets from the binary).
class ID3D7Device {
public:
    virtual ~ID3D7Device() = default;
    virtual long GetCaps(std::uint8_t* desc) = 0;                                                    // +0x18, 0xf4 bytes
    virtual long EnumTextureFormats(int (*callback)(sGDPixelFormat*, void*), void* context) = 0;     // +0x20
    virtual long BeginScene() = 0;                                                                   // +0x28
    virtual long EndScene() = 0;                                                                     // +0x30
    virtual ID3D7Surface* GetRenderTarget() = 0;                                                     // +0x48
    virtual long Clear(const void* rects, std::uint32_t flags, std::uint32_t color, float z,
                       std::uint32_t stencil) = 0;                                                   // +0x50
    virtual long SetTransform(std::uint32_t state, const float* matrix) = 0;                         // +0x58
    virtual long GetViewport(std::uint32_t out[4]) = 0;                                              // +0x78
    virtual long SetMaterial(const sGDMaterial& material) = 0;                                       // +0x80
    virtual long SetLight(std::uint32_t index, const sGDLight& light) = 0;                           // +0x90
    virtual long SetRenderState(std::uint32_t state, std::uint32_t value) = 0;                       // +0xa0
    virtual long GetRenderState(std::uint32_t state, std::uint32_t* value) = 0;                      // +0xa8
    virtual long SetTexture(std::uint32_t stage, std::uint32_t texture) = 0;                         // +0x118
    virtual long SetTextureStageState(std::uint32_t stage, std::uint32_t type, std::uint32_t value) = 0;  // +0x128
    virtual long LightEnable(std::uint32_t index, bool enable) = 0;                                  // +0x160
};

// cISGLDX7D3DX: device/context creation, presentation and the driver-identifier query.
struct sGDDriverInfo {
    std::string driverName;         // DDDEVICEIDENTIFIER2.szDriver
    std::uint32_t versionLow = 0;   // liDriverVersion low dword
    std::uint32_t versionHigh = 0;  // liDriverVersion high dword
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t subSysId = 0;
    std::uint32_t revision = 0;
    std::uint8_t guid[16] = {};     // guidDeviceIdentifier
};
class ID3D7Environment {
public:
    virtual ~ID3D7Environment() = default;
    virtual bool CreateContext(void* window, bool windowed, bool vsync, std::uint32_t bitsPerPixel,
                               std::int32_t width, std::int32_t height) = 0;   // CreateSGLD3DXContext @ 0x1006247ef
    virtual void DestroyContext() = 0;
    virtual bool CreateContextInfoOnly() = 0;                                 // CreateSGLD3DXContextInfoOnly @ 0x1006246b0
    virtual bool GetDriverInfo(sGDDriverInfo& info) = 0;                      // IDirectDraw7::GetDeviceIdentifier, +0xd8
    virtual ID3D7Device* Device() = 0;
    virtual ID3D7Surface* RenderTarget() = 0;
    virtual ID3D7Surface* BackBuffer() = 0;
    virtual ID3D7Surface* DepthStencil() = 0;
    virtual ID3D7Surface* CreateRegionSurface(std::int32_t type, std::int32_t width, std::int32_t height) = 0;
    // The surface a buffer region blits to/from for a region type (0 render target, 1 back buffer, 2 third surface).
    virtual ID3D7Surface* BufferRegionTarget(std::int32_t type) = 0;
    virtual void ReleaseDisplay() = 0;
    virtual bool TestCooperativeLevel() = 0;                                  // IDirectDraw7::TestCooperativeLevel, +0xd0
    virtual bool RestoreDevice() = 0;                                         // lost-device restore path
    virtual bool SetDisplayMode(bool windowed, std::int32_t width, std::int32_t height) = 0;
    virtual void GetWindowRect(std::int32_t rect[4]) = 0;
};

// cPrimitiveManager: batching of vertices/indices into vertex buffers (34 functions, neighbour not recovered).
class IPrimitiveManagerHost {
public:
    virtual ~IPrimitiveManagerHost() = default;
    virtual bool IsStarted() = 0;                                            // @ 0x100609670
    virtual long Flush() = 0;                                                // @ 0x100609f14
    virtual long FlushScene() = 0;                                           // @ 0x10060a4fc
    virtual long Queue(std::uint32_t vertexFormat, std::uint32_t primitiveType, std::uint32_t stride,
                       std::uint32_t baseVertex, std::uint32_t vertexCount, const void* vertices,
                       std::uint32_t indexCount, const std::uint16_t* indices) = 0;   // @ 0x10060a0ec
    virtual std::uint16_t* GetIndices(int count) = 0;                        // @ 0x10060a420
    virtual long Restart(void* driver, ID3D7Device* device) = 0;            // @ 0x10060a574
    virtual void Stop() = 0;                                                 // @ 0x100609d88
    virtual bool Init(void* driver, ID3D7Device* device) = 0;               // @ 0x100609bcc
    virtual void Shutdown() = 0;                                             // @ 0x100609d32
};

// cTextureManager: handle table, sampler parameters, alpha bits and uploads (recovered separately as cTextureTable;
// this seam is the call surface cGDriver uses).
struct sGDTextureParams {
    std::uint32_t magFilter = 0, minFilter = 0, wrapS = 3, wrapT = 3;
};
class ITextureManagerHost {
public:
    virtual ~ITextureManagerHost() = default;
    virtual bool GetTextureParams(std::uint32_t texture, sGDTextureParams& out) = 0;   // @ 0x10060bedc
    virtual bool HasAlpha(std::uint32_t texture) = 0;                                  // @ 0x10060bd20
    virtual std::uint32_t NewTexture() = 0;                                            // @ 0x10060bbc4
    virtual bool CreateTexture(std::uint32_t texture, std::uint32_t hint, std::uint32_t format, std::uint32_t width,
                               std::uint32_t height, std::uint32_t mipLevels) = 0;      // @ 0x10060b5bc
    virtual bool LoadTextureLevel(std::uint32_t texture, std::uint32_t level, std::uint32_t x, std::uint32_t y,
                                  std::uint32_t width, std::uint32_t height, std::uint32_t format, std::uint32_t type,
                                  std::uint32_t rowLength, const void* pixels) = 0;    // @ 0x10060b8aa
    virtual bool HasTexture(std::uint32_t texture) = 0;                                // @ 0x10060ba5a
    virtual std::uint32_t TextureHint(std::uint32_t texture) = 0;                      // cTextureHandle +0x18 @ 0x10060bda6
    virtual void FreeTexture(std::uint32_t texture) = 0;                               // @ 0x10060b452
    virtual bool IsValid(std::uint32_t texture) = 0;                                   // @ 0x10060b756
    virtual bool IsResident(std::uint32_t texture) = 0;                                // @ 0x10060bd66
    virtual void SetPriority(std::uint32_t texture, float priority) = 0;               // @ 0x10060bdec
    virtual bool SetTextureParam(std::uint32_t texture, std::uint32_t type, std::uint32_t value) = 0;   // @ 0x10060be44
    virtual void InitDevice(ID3D7Device* device) = 0;                                  // @ 0x10060b4fc
    virtual void ShutdownDevice() = 0;                                                 // @ 0x10060b6e4
    virtual void Init(void* driver, ID3D7Device* device, const std::vector<cDXTextureFormatInfo>& formats) = 0;   // @ 0x10060b26e
    virtual void Shutdown() = 0;                                                       // @ 0x10060b3a0
};

// The four direct GL entry points the driver makes (glViewport / glScissor / glEnable / glDisable). A Metal backend
// implements these as render-target/scissor state; the recovered cGDriver is backend-neutral through this seam.
class IDriverGL {
public:
    virtual ~IDriverGL() = default;
    virtual void Viewport(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) = 0;
    virtual void Scissor(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) = 0;
    virtual void Enable(std::uint32_t cap) = 0;
    virtual void Disable(std::uint32_t cap) = 0;
};

// ---------------------------------------------------------------------------------------------------------------------
// The extension interfaces cGDriver implements beside cIGZGDriver (parts of vtable group +0x0, plus +0x8 and +0x10).

// cIGZGBufferRegionExtension (object +0x0, slots 3..10 and 94). Region ids are 1-based indexes.
class IGDDriverBufferRegions {
public:
    virtual ~IGDDriverBufferRegions() = default;
    virtual bool BufferRegionEnabled() = 0;                                                    // +0x018 slot 3
    virtual int NewBufferRegion(int type) = 0;                                                 // +0x020 slot 4
    virtual bool DeleteBufferRegion(std::uint32_t id) = 0;                                     // +0x028 slot 5
    virtual bool ReadBufferRegion(std::uint32_t id, int x, int y, int width, int height, int dx, int dy) = 0;   // +0x030 slot 6
    virtual bool DrawBufferRegion(std::uint32_t id, int x, int y, int width, int height, int dx, int dy) = 0;   // +0x038 slot 7
    virtual bool IsBufferRegion(std::uint32_t id) const = 0;                                   // +0x040 slot 8
    virtual bool CanDoPartialRegionWrites() const = 0;                                         // +0x048 slot 9
    virtual bool CanDoOffsetReads() const = 0;                                                 // +0x050 slot 10
    virtual bool DeleteAllBufferRegions() = 0;                                                 // +0x2f0 slot 94
};

// A copy of the colour buffer into a cIGZBuffer (object +0x8, slot 3). The cIGZBuffer neighbour is not recovered, so
// the destination is opaque here; see CopyColorBuffer.
class IGDDriverColorBuffer {
public:
    virtual ~IGDDriverColorBuffer() = default;
    virtual void* CopyColorBuffer(int x, int y, int width, int height, void* destination) = 0;  // +0x018 slot 3
};

// Lighting/material extension (object +0x10, slots 3..11).
class IGDDriverLighting {
public:
    virtual ~IGDDriverLighting() = default;
    virtual void EnableLighting(bool enable) = 0;                                              // +0x018 slot 3
    virtual void EnableLight(std::uint32_t index, bool enable) = 0;                            // +0x020 slot 4
    virtual void LightModelAmbient(float r, float g, float b, float a) = 0;                    // +0x028 slot 5
    virtual void LightColor(std::uint32_t index, std::uint32_t component, const float* color) = 0;   // +0x030 slot 6
    virtual void LightColor(std::uint32_t index, const float* ambient, const float* diffuse,
                            const float* specular) = 0;                                        // +0x038 slot 7
    virtual void LightPosition(std::uint32_t index, const float* position) = 0;                // +0x040 slot 8
    virtual void LightDirection(std::uint32_t index, const float* direction) = 0;              // +0x048 slot 9
    virtual void MaterialColor(std::uint32_t component, const float* color) = 0;               // +0x050 slot 10
    virtual void MaterialColor(const float* ambient, const float* diffuse, const float* specular, const float* emissive,
                               float power) = 0;                                               // +0x058 slot 11
};

// ---------------------------------------------------------------------------------------------------------------------

class cGDriver final : public gd::cIGZGDriver,
                       public IGDDriverBufferRegions,
                       public IGDDriverColorBuffer,
                       public IGDDriverLighting {
public:
    // The seams are not owned by the driver (they outlive it); the original owned the texture and primitive managers.
    cGDriver(ID3D7Environment& environment, gd::IModeSource& modeSource, IPrimitiveManagerHost& primitiveManager,
             ITextureManagerHost& textureManager, IDriverGL& gl);
    ~cGDriver() override;

    // Allocates a driver with refcount 1; Release() to zero deletes it (FinalRelease path).
    static cGDriver* Create(ID3D7Environment& environment, gd::IModeSource& modeSource,
                            IPrimitiveManagerHost& primitiveManager, ITextureManagerHost& textureManager, IDriverGL& gl);

    // ---- cIGZGDriver (object +0x18, 83 slots) ----
    bool QueryInterface(std::uint32_t iid, void** out) override;                       // @ 0x10060401e
    void AddRef() override;                                                            // @ 0x1006040b8
    std::int32_t Release() override;                                                   // @ 0x1006040fa
    void DrawArrays(eGDPrimType mode, int first, int count) override;                  // @ 0x10060421a
    void DrawElements(eGDPrimType mode, int count, eGDType indexType, const void* indices) override;   // @ 0x1006044a8
    void InterleavedArrays(eGDVertexFormat format, int stride, const void* data) override;   // @ 0x100604ce4
    eGDVertexFormat MakeVertexFormat(std::uint32_t count, eGDElementType* elements) override;   // @ 0x100604da0
    eGDVertexFormat MakeVertexFormat(eGDVertexFormat format) override;                 // @ 0x100604dc0
    int VertexFormatStride(eGDVertexFormat format) const override;                     // @ 0x100604dd8
    int VertexFormatElementOffset(eGDVertexFormat format, eGDElementType element, std::uint32_t index) const override;   // @ 0x100604df0
    int VertexFormatNumElements(eGDVertexFormat format, eGDElementType element) const override;   // @ 0x100604e10
    void Clear(std::uint32_t mask) override;                                           // @ 0x100604e2c
    void ClearColor(float r, float g, float b, float a) override;                      // @ 0x100604e9e
    void ClearDepth(double depth) override;                                            // @ 0x100604f2e
    void ClearStencil(int value) override;                                             // @ 0x100604f52
    void ColorMask(bool enable) override;                                              // @ 0x100604fea
    void DepthFunc(eGDTestFunc func) override;                                         // @ 0x100604f6a
    void DepthMask(bool enable) override;                                              // @ 0x100605006
    void StencilFunc(eGDTestFunc func, int ref, std::uint32_t mask) override;          // @ 0x100605072
    void StencilMask(std::uint32_t mask) override;                                     // @ 0x10060515a
    void StencilOp(eGDStencilOp fail, eGDStencilOp zfail, eGDStencilOp zpass) override;   // @ 0x1006051c6
    void BlendFunc(eGDBlend src, eGDBlend dst) override;                               // @ 0x1006052b6
    void AlphaFunc(eGDTestFunc func, float ref) override;                              // @ 0x10060532c
    void ShadeModel(eGDShade model) override;                                          // @ 0x1006053bc
    void BindTexture(eGDTextureTarget target, std::uint32_t texture) override;         // @ 0x10060543c
    void TexImage2D(eGDTextureTarget target, int level, eGDInternalTextureFormat internalFormat, int width, int height,
                    int border, eGDTextureFormat format, eGDType type, const void* pixels) override;   // @ 0x1006056a4
    void PixelStore(eGDParameter pname, int value) override;                           // @ 0x1006057d8
    void TexEnv(eGDTextureEnvTarget target, eGDTextureEnvParamType param, eGDTextureEnvModeParam mode) override;   // @ 0x1006057f8
    using cIGZGDriver::TexEnv;
    void TexEnv(eGDTextureEnvTarget target, eGDTextureEnvParamType param, const float* values) override;   // @ 0x10060584c
    void TexParameter(eGDTextureTarget target, eGDTextureParamType param, eGDTextureParam value) override;   // @ 0x10060592c
    void SetTexture(std::uint32_t texture, std::uint32_t stage) override;              // @ 0x100605514
    std::uint32_t GetTexture(std::uint32_t stage) const override;                      // @ 0x10060556e
    std::uint32_t CreateTexture(eGDInternalTextureFormat format, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                eGDTextureHintFlags hint) override;                    // @ 0x10060559e
    void LoadTextureLevel(std::uint32_t texture, std::uint32_t level, std::uint32_t x, std::uint32_t y,
                          std::uint32_t width, std::uint32_t height, eGDTextureFormat format, eGDType type,
                          std::uint32_t rowLength, const void* pixels) override;       // @ 0x10060561a
    void SetCombiner(const cGDCombiner& combiner, std::uint32_t stage) override;       // @ 0x100605a32
    void Fog(eGDFogParamType param, eGDFogParam value) override;                       // @ 0x100605b80
    using cIGZGDriver::Fog;
    void Fog(eGDFogParamType param, const float* values) override;                     // @ 0x100605c20
    void ColorMultiplier(float r, float g, float b) override;                          // @ 0x100605d8a
    void AlphaMultiplier(float alpha) override;                                        // @ 0x100605ed2
    void EnableVertexColors(bool color, bool alpha) override;                          // @ 0x100605fe6
    void GenTextures(int count, std::uint32_t* textures) override;                     // @ 0x100606080
    void DeleteTextures(int count, const std::uint32_t* textures) override;            // @ 0x1006060ec
    bool IsTexture(std::uint32_t texture) override;                                    // @ 0x10060613e
    void PrioritizeTextures(int count, const std::uint32_t* textures, const float* priorities) override;   // @ 0x10060615a
    bool AreTexturesResident(int count, const std::uint32_t* textures, bool* residence) override;   // @ 0x1006061de
    void MatrixMode(eGDMatrixTarget target) override;                                  // @ 0x100604162
    void LoadMatrix(const float* matrix) override;                                     // @ 0x10060417a
    void LoadIdentity() override;                                                      // @ 0x1006041e4
    void Flush() override;                                                             // @ 0x100606266
    void Enable(eGDDriverState state) override;                                        // @ 0x100606734
    void Disable(eGDDriverState state) override;                                       // @ 0x10060683a
    bool IsEnabled(eGDDriverState state) override;                                     // @ 0x10060696a
    void GetBoolean(eGDParameter pname, bool* out) override;                           // @ 0x100606a0e
    void GetInteger(eGDParameter pname, int* out) override;                            // @ 0x100606a1a
    void GetFloat(eGDParameter pname, float* out) override;                            // @ 0x100606a36
    int GetError() override;                                                           // @ 0x100606a42
    void TexStage(std::uint32_t stage) override;                                       // @ 0x100606a6e
    void TexStageCoord(eGDTexCoordSource source) override;                             // @ 0x100606aae
    void TexStageMatrix(const float* matrix, std::uint32_t a, std::uint32_t b, eGDTexMatFlags flags) override;   // @ 0x100606b5c
    void TexStageCombine(eGDTextureStageCombineParamType param, eGDTextureStageCombineModeParam mode) override;   // @ 0x100606d9e
    void TexStageCombine(eGDTextureStageCombineSourceParamType param, eGDTextureStageCombineSourceParam source) override;   // @ 0x100606e06
    void TexStageCombine(eGDTextureStageCombineOperandParamType param, eGDBlend operand) override;   // @ 0x100606e9e
    void TexStageCombine(eGDTextureStageCombineScaleParamType param, eGDTextureStageCombineScaleParam scale) override;   // @ 0x100606f4e
    void PolygonOffset(std::uint32_t units) override;                                  // @ 0x100606fb6
    bool Punt(std::uint32_t code, void* data) override;                                // @ 0x10060702c
    void BitBlt(int x, int y, int w, int h, eGDTextureFormat format, eGDType type, const void* pixels, bool flip,
                const void* extra) override;                                           // @ 0x10060703c
    void StretchBlt(int x, int y, int w, int h, int dw, int dh, eGDTextureFormat format, eGDType type,
                    const void* pixels, bool flip, const void* extra) override;        // @ 0x10060705c
    void BitBltAlpha(int x, int y, int w, int h, eGDTextureFormat format, eGDType type, const void* pixels, bool flip,
                     const void* extra, std::uint32_t alpha) override;                 // @ 0x10060707c
    void StretchBltAlpha(int x, int y, int w, int h, int dw, int dh, eGDTextureFormat format, eGDType type,
                         const void* pixels, bool flip, const void* extra, std::uint32_t alpha) override;   // @ 0x10060709c
    void BitBltAlphaModulate(int x, int y, int w, int h, eGDTextureFormat format, eGDType type, const void* pixels,
                             bool flip, const void* extra, std::uint32_t alpha) override;   // @ 0x1006070bc
    void StretchBltAlphaModulate(int x, int y, int w, int h, int dw, int dh, eGDTextureFormat format, eGDType type,
                                 const void* pixels, bool flip, const void* extra, std::uint32_t alpha) override;   // @ 0x1006070dc
    void SetViewport() override;                                                       // @ 0x1006070fc
    void SetViewport(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) override;   // @ 0x100607176
    void GetViewport(std::uint32_t out[4]) override;                                   // @ 0x10060721c
    const void* GetDriverInfo() override;                                              // @ 0x10060cc36
    std::uint32_t GetGZCLSID() const override;                                         // @ 0x10060cc6e
    bool Init() override;                                                              // @ 0x100603f52
    bool Shutdown() override;                                                          // @ 0x100603fe2
    bool IsDeviceReady() override;                                                     // @ 0x100609188
    bool FinalRelease();                                                               // @ 0x10060413c
    int CountVideoModes() override;                                                    // @ 0x10060cd4a
    bool GetVideoModeInfo(std::uint32_t index, sGDMode& mode) override;                // @ 0x10060d094
    bool GetVideoModeInfo(sGDMode& mode) override;                                     // @ 0x10060d158
    bool SetVideoMode(int mode, void* window, bool windowed, bool vsync) override;     // @ 0x10060d1f8

    // ---- IGDDriverBufferRegions ----
    bool BufferRegionEnabled() override;                                               // @ 0x10060727c
    int NewBufferRegion(int type) override;                                            // @ 0x100607284
    bool DeleteBufferRegion(std::uint32_t id) override;                                // @ 0x100607548
    bool ReadBufferRegion(std::uint32_t id, int x, int y, int width, int height, int dx, int dy) override;   // @ 0x1006075ce
    bool DrawBufferRegion(std::uint32_t id, int x, int y, int width, int height, int dx, int dy) override;   // @ 0x100607714
    bool IsBufferRegion(std::uint32_t id) const override;                              // @ 0x10060787a
    bool CanDoPartialRegionWrites() const override;                                    // @ 0x1006078ae
    bool CanDoOffsetReads() const override;                                            // @ 0x1006078b6
    bool DeleteAllBufferRegions() override;                                            // @ 0x1006078be

    // ---- IGDDriverColorBuffer ----
    void* CopyColorBuffer(int x, int y, int width, int height, void* destination) override;   // @ 0x1006079b8

    // ---- IGDDriverLighting ----
    void EnableLighting(bool enable) override;                                         // @ 0x1006081ba
    void EnableLight(std::uint32_t index, bool enable) override;                       // @ 0x100608248
    void LightModelAmbient(float r, float g, float b, float a) override;               // @ 0x1006082cc
    void LightColor(std::uint32_t index, std::uint32_t component, const float* color) override;   // @ 0x10060839a
    void LightColor(std::uint32_t index, const float* ambient, const float* diffuse,
                    const float* specular) override;                                   // @ 0x10060852a
    void LightPosition(std::uint32_t index, const float* position) override;           // @ 0x100608696
    void LightDirection(std::uint32_t index, const float* direction) override;         // @ 0x1006086a2
    void MaterialColor(std::uint32_t component, const float* color) override;          // @ 0x1006086ae
    void MaterialColor(const float* ambient, const float* diffuse, const float* specular, const float* emissive,
                       float power) override;                                          // @ 0x1006087ee

    // ---- driver helpers (not in the vtable) ----
    bool HasStencilBuffer() const;             // currentMode byte +0x21
    bool HasFog() const;                       // deviceDescription+0xe0 bit 2
    bool HasCombine() const;                   // currentMode byte +0x29
    void ApplyTextureState();                                                          // @ 0x100608914
    bool BeginScene();                                                                 // @ 0x10060e80e (not virtual)
    void EndScene();                                                                   // @ 0x10060d322
    bool GetDeviceProperties(std::vector<cDXTextureFormatInfo>& formats);              // @ 0x10060df46
    void SetDefaultD3DDeviceState();                                                   // @ 0x10060e0ea
    bool Initialize3DEnvironment();                                                    // @ 0x10060dbfe
    bool SetupViewport();                                                              // @ 0x10060d3f4
    void DestroyViewport();                                                            // @ 0x10060d36e
    void ShutdownViewport();                                                           // @ 0x10060db0c
    void Cleanup3DEnvironment();                                                       // @ 0x10060e51c
    bool SetupD3DX();                                                                  // @ 0x10060e682
    void InitViewport();                                                               // @ 0x10060d54a

    // ---- observable state (offsets of the original object; public so the layout can be checked) ----
    std::string driverInfo;                                   // global std::string @ 0x100b6ed18
    sGDMode currentMode{};                                    // +0x23c (id starts at -1 after Init)
    std::uint32_t clearColor = 0;                             // +0x1e0
    float clearDepth = 1.0f;                                  // +0x1e4
    std::int32_t clearStencil = 0;                            // +0x1e8
    std::int32_t windowRect[4] = {0, 0, 0, 0};                // +0x1ec left/top/right/bottom
    std::uint32_t deviceViewport[4] = {0, 0, 0, 0};           // +0x2b0 (from IDirect3DDevice7::GetViewport)
    std::uint32_t viewport[4] = {0, 0, 0, 0};                 // +0x2c8 mirror (x, y, width, height)
    std::uint32_t matrixTarget = 0;                           // +0x270
    std::vector<cTextureStageData> stages;                    // +0x168
    std::vector<sGDLight> lights;                             // +0x368
    sGDMaterial material;                                     // +0x324
    std::uint32_t currentStage = 0;                           // +0x15c
    std::uint32_t stageCount = 0;                            // +0x160
    bool stagesDirty = false;                                 // +0x180
    bool textureParamsValid = false;                          // +0x188
    std::uint32_t unpackRowLength = 0;                        // +0x18c (GL_UNPACK_ROW_LENGTH, PixelStore(0))
    std::uint32_t vertexFormat = 0;                           // +0x20c
    std::uint32_t vertexStride = 0;                           // +0x210
    std::uint32_t standardStride = 0;                         // +0x214
    const void* vertexData = nullptr;                         // +0x220
    bool interleavedSet = false;                              // +0x21c
    std::int32_t error = 0;                                   // +0x158
    std::int32_t modeIndex = -1;                              // +0x23c
    void* window = nullptr;                                   // +0x150 (main window handle)
    std::uint32_t currentBitsPerPixel = 0;                    // +0x300
    bool inScene = false;                                     // +0x2ec
    bool deviceLost = false;                                  // +0x2ed
    bool initialized = false;                                 // +0x2ee
    bool texturesReady = false;                               // +0x2ef
    bool windowed = false;                                    // +0x2ea
    bool vsync = false;                                       // +0x2eb
    bool lightingEnabled = false;                             // +0x380
    bool clearOnBeginScene = false;                           // +0x381
    bool vertexColorEnabled = true;                           // +0x382
    bool vertexAlphaEnabled = true;                           // +0x383
    bool copyColorChecked = false;                            // +0x320
    bool copyColorAvailable = false;                          // +0x321
    std::uint32_t reserved184 = 7;                            // +0x184
    std::uint8_t deviceDescription[caps::kDeviceDescSize] = {};   // +0x40

private:
    struct BufferRegionData {                                 // 0x20 bytes
        std::int32_t index = -1;                              // +0x00
        std::uint32_t type = 0;                               // +0x04
        ID3D7Surface* surface = nullptr;                      // +0x08
        std::int32_t width = 0;                               // +0x18
        std::int32_t height = 0;                              // +0x1c
    };
    class TextureStageHost;   // adapts ID3D7Device + ITextureManagerHost to ITextureStageHost
    friend class TextureStageHost;

    std::uint32_t StageCountFromCaps() const;
    std::uint32_t CapU32(std::size_t offset) const;
    std::uint16_t CapU16(std::size_t offset) const;
    void MarkStageDirty(std::uint32_t stage);

    ID3D7Environment& environment_;
    gd::cVideoModeList modes_;
    IPrimitiveManagerHost& primitiveManager_;
    ITextureManagerHost& textureManager_;
    IDriverGL& gl_;
    std::vector<BufferRegionData> bufferRegions_;              // +0x308
    std::int32_t refCount_ = 1;                                // cRZRefCount at +0x20
    std::uint32_t capStageCount_ = 0;                          // cached from the device description
};

// Free helpers recovered with the class.
int SurfaceFormatEnumerator(sGDPixelFormat* format, void* context);   // @ 0x10060e919
bool IsIdentityMatrix(const float* m);                                 // used by TexStageMatrix

}  // namespace nSGLDX7
