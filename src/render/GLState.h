// The GL state-cache types of the DX7 shim: per-texture-unit state (CTextureStage + GLStageState), the client vertex
// arrays (CClientArray family) and the default-state fills (GetDefaultOpenGLState / GetDefaultOpenGLTexState).
// Byte layouts follow the original because CDirect3DDevice7 embeds them at fixed offsets (see D3DDevice7.h).
// 0xcccccccc is the "never set" marker the device writes everywhere at construction (a fresh debug fill).
#pragma once
#include <cstdint>

#include "render/DX7Types.h"
#include "render/GLApi.h"

namespace sgl {

constexpr std::uint32_t kUnset = 0xcccccccc;

// _GetDefaultOpenGLTexState (0x1001e3cc5): wrap s/t/r, mag, min filter, anisotropy 1.0, max level 1000. 36 bytes.
struct GLTexState {
    std::uint32_t wrapS, wrapT, wrapR, magFilter, minFilter;
    float anisotropy;       // +0x14
    std::uint32_t pad18;    // +0x18
    std::uint32_t maxLevel; // +0x1c  (1000)
    std::uint32_t pad20;
};
void GetDefaultOpenGLTexState(GLenum target, GLTexState* out);   // @ 0x1001e3cc5

// What the surface keeps in its private data for textures (0x38 bytes): the GL texture object, its upload bookkeeping and
// the filter / wrap state last pushed to it.
struct OpenGLTextureState {
    std::uint32_t id;               // +0   GL texture object (0 = none yet)
    std::uint32_t dirty;            // +4   pixels changed since the last upload
    std::uint32_t uploaded;         // +8   the full image has been sent once
    std::uint32_t internalFormat;   // +0xc
    GLTexState tex;                 // +0x10
    std::uint32_t pixelBuffer;      // +0x34  pixel-unpack buffer object used for uploads
};
static_assert(sizeof(OpenGLTextureState) == 0x38);

// Opaque default-initialised blobs: the device never reads them back, it only fills them at construction.
struct GLStageState { std::uint8_t bytes[0x114]; };
struct GLState {                                   // global fixed-function mirror (0x198 bytes); +0 blend, +0x20 alpha test
    std::uint8_t bytes[0x198];
    std::uint32_t& blendEnabled() { return *reinterpret_cast<std::uint32_t*>(bytes); }
    std::uint32_t& alphaTestEnabled() { return *reinterpret_cast<std::uint32_t*>(bytes + 0x20); }
};
void GetDefaultOpenGLState(GLStageState* state);   // @ 0x1001e3bde
void GetDefaultOpenGLState(GLState* state);        // @ 0x1001e3aa5 (the free function _GetDefaultOpenGLState)

// Client array mirrors. Layout: +0 vptr, +8 enabled, +0xc size, +0x10 type, +0x14 stride, +0x18 pointer.
class CClientArray {
public:
    virtual void SetEnable(GLenum array, std::int32_t enable);                                      // @ 0x10063ad3e
    virtual void SetPointer(std::int32_t size, GLenum type, std::int32_t stride, const void* p) = 0;
    virtual ~CClientArray() = default;
    std::int32_t enabled = static_cast<std::int32_t>(kUnset);
    std::int32_t size = static_cast<std::int32_t>(kUnset);
    std::uint32_t type = kUnset;
    std::int32_t stride = static_cast<std::int32_t>(kUnset);
    const void* pointer = nullptr;
protected:
    bool SameAs(std::int32_t s, GLenum t, std::int32_t st, const void* p) const {
        return size == s && type == t && stride == st && pointer == p;
    }
    void Remember(std::int32_t s, GLenum t, std::int32_t st, const void* p) { size = s; type = t; stride = st; pointer = p; }
};
class CTextureCoordArray final : public CClientArray {
public:
    void SetPointer(std::int32_t size, GLenum type, std::int32_t stride, const void* p) override;   // @ 0x10063ad68
};
class CVertexArray final : public CClientArray {
public:
    void SetPointer(std::int32_t size, GLenum type, std::int32_t stride, const void* p) override;   // @ 0x10063ada0
};
class CColorArray final : public CClientArray {
public:
    void SetPointer(std::int32_t size, GLenum type, std::int32_t stride, const void* p) override;   // @ 0x10063add8
};

// One texture unit of the device (0x218 bytes).
struct CTextureStage {
    std::int32_t enabled = static_cast<std::int32_t>(kUnset);   // +0   GL_TEXTURE_2D enabled
    IDirectDrawSurface7* texture = nullptr;                     // +8
    std::int32_t cameraSpace = static_cast<std::int32_t>(kUnset);   // +0x10  TCI camera-space flag
    std::int32_t matrixDirty = static_cast<std::int32_t>(kUnset);   // +0x14
    std::uint32_t matrix[16];                                   // +0x18  texture transform (floats, 0xcc-filled when unset)
    CTextureCoordArray coords;                                  // +0x58
    float rgbScale = 0, alphaScale = 0;                         // +0x78 / +0x7c  last TexEnv scale pushed
    GLStageState gl{};                                          // +0x80
    std::uint32_t state[32];                                    // +0x194  D3DTSS values (0xcccccccc = never set)
    std::uint32_t pad = 0;

    void SetEnabled(std::int32_t on);                                          // @ 0x10063834c
    void SetTexture(OpenGLTextureState& state, IDirectDrawSurface7* surface);   // @ 0x100637a42
};

}  // namespace sgl
