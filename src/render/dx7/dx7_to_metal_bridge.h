// dx7_to_metal_bridge.h — records Direct3D 7 state calls into a plain, GPU-visible MetalRenderState. No rendering happens here.
#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace dx7 {

// D3DTRANSFORMSTATETYPE
enum : std::uint32_t { kTransformView = 2, kTransformProjection = 3, kTransformTexture0 = 16, kTransformWorld = 256 };

// D3DRENDERSTATETYPE (the subset the bridge maps; anything else is logged with its hexadecimal id)
enum : std::uint32_t {
    kRenderZEnable = 7, kRenderFillMode = 8, kRenderShadeMode = 9, kRenderZWriteEnable = 14, kRenderAlphaTestEnable = 15,
    kRenderSrcBlend = 19, kRenderDestBlend = 20, kRenderCullMode = 22, kRenderZFunc = 23, kRenderAlphaRef = 24,
    kRenderAlphaFunc = 25, kRenderAlphaBlendEnable = 27, kRenderFogEnable = 28, kRenderTextureFactor = 60,
    kRenderLighting = 137, kRenderAmbient = 139,
};

// D3DFVF flags
enum : std::uint32_t { kFvfXyz = 0x002, kFvfXyzRhw = 0x004, kFvfNormal = 0x010, kFvfDiffuse = 0x040, kFvfTex1 = 0x100 };

// MetalRenderState::flags
enum : std::uint32_t {
    kFlagLighting = 1u << 0, kFlagTexture = 1u << 1, kFlagAlphaTest = 1u << 2, kFlagHasNormal = 1u << 3,
    kFlagHasDiffuse = 1u << 4,
};

// Uploaded verbatim as a constant buffer (setVertexBytes / setFragmentBytes at index 1). Plain floats and uint32 only, so the
// layout is identical in C++ and MSL and passes the Metal API validation layer's buffer-size check.
// texMode bits: 0/1 stage 0/1 has a texture matrix, 2/3 stage 0/1 takes the camera-space position as coordinates, 0x100+ debug views.
struct alignas(16) MetalRenderState {
    float world[16];            // +0
    float view[16];             // +64   the game's modelview
    float projection[16];       // +128
    float texMat0[16];          // +192
    float texMat1[16];          // +256
    float textureFactor[4];     // +320
    float colorMul[4];          // +336  ColorMultiplier / AlphaMultiplier
    float ambient[4];           // +352
    float lightDirection[4];    // +368  direction the light travels, world space (w unused)
    float lightDiffuse[4];      // +384
    std::uint32_t stride;       // +400  bytes per vertex of the raw-buffer path
    std::uint32_t alphaRef;     // +404  0..255
    std::uint32_t texMode;      // +408
    std::uint32_t flags;        // +412
    std::uint32_t alphaFunc;    // +416  D3DCMP 1..8
    std::uint32_t fvf;          // +420
    std::uint32_t pad0, pad1;   // +424
};
static_assert(sizeof(MetalRenderState) == 432, "MetalRenderState must match the MSL struct");
static_assert(offsetof(MetalRenderState, stride) == 400 && offsetof(MetalRenderState, flags) == 412, "MetalRenderState layout");

// State the encoder applies outside the shader (pipeline / depth-stencil / rasteriser).
struct MetalFixedState {
    std::uint32_t zEnable = 1, zWrite = 1, zFunc = 4, cullMode = 3, blendEnable = 0, srcBlend = 2, dstBlend = 1, fogEnable = 0;
};

class StateRecorder {
public:
    StateRecorder();
    bool SetTransform(std::uint32_t state, const float* matrix16);
    bool SetRenderState(std::uint32_t state, std::uint32_t value);
    bool SetLight(const float direction3[3], const float diffuse4[4]);
    bool SetFVF(std::uint32_t fvf);
    void SetTextureBound(bool bound);
    // The two-stage path the game's driver uses: per-stage texture matrix / camera-space coordinates, colour multiplier, factor.
    bool SetStageTransform(std::uint32_t stage, const float* matrix16);   // null clears the stage's matrix
    void SetStageCameraSpaceCoords(std::uint32_t stage, bool on);
    void SetTextureFactor(const float rgba[4]);
    void SetColorMultiplier(const float rgba[4]);
    void SetDebugMode(std::uint32_t bits);
    MetalRenderState Snapshot() const;
    MetalFixedState SnapshotFixed() const;
    std::uint32_t UnmappedCount() const;

private:
    mutable std::mutex mutex_;
    MetalRenderState state_;
    MetalFixedState fixed_;
    std::uint32_t unmapped_ = 0;
};

}  // namespace dx7
