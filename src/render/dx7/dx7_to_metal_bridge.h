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
    kFlagHasDiffuse = 1u << 4, kFlagHasTexMatrix = 1u << 5,
};

// Uploaded verbatim as a constant buffer (setVertexBytes / setFragmentBytes at index 1). Plain floats and uint32 only, so the
// layout is identical in C++ and MSL and passes the Metal API validation layer's buffer-size check.
struct alignas(16) MetalRenderState {
    float world[16];            // +0    row-major as D3D hands it over
    float view[16];             // +64
    float projection[16];       // +128
    float texture0[16];         // +192
    float ambient[4];           // +256  rgba 0..1
    float textureFactor[4];     // +272
    float lightDirection[4];    // +288  direction the light travels, world space (w unused)
    float lightDiffuse[4];      // +304
    std::uint32_t flags;        // +320
    std::uint32_t alphaFunc;    // +324  D3DCMP 1..8
    std::uint32_t alphaRef;     // +328  0..255
    std::uint32_t fvf;          // +332
};
static_assert(sizeof(MetalRenderState) == 336, "MetalRenderState must match the MSL struct");
static_assert(offsetof(MetalRenderState, flags) == 320, "MetalRenderState layout");

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
