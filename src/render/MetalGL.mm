#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <simd/simd.h>
#include <vector>

#include "render/MetalGL.h"

namespace sgl {

// ---------------------------------------------------------------------------------------------------------------
// Shader. The constant block layout below must match the C++ structs further down.
// ---------------------------------------------------------------------------------------------------------------
static NSString* const kShader = @R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Stage {
    float4 envColor;
    float4x4 texMat;
    int enabled, rgbCombine, alphaCombine, pad0;
    int rgbSrc[3];   int pad1;
    int alphaSrc[3]; int pad2;
    int rgbOp[3];    int pad3;
    int alphaOp[3];  int pad4;
    float rgbScale, alphaScale, pad5, pad6;
};
struct Uniforms {
    float4x4 mv, proj;
    float4 current, matAmbient, matDiffuse, modelAmbient, fogColor, fogParams;
    int hasColor, lighting, colorMaterial, alphaFunc;
    float alphaRef; int alphaTest; int pad0, pad1;
    Stage st[2];
};
struct Vtx { packed_float3 pos; uint color; float4 uv0; float4 uv1; };
struct VOut { float4 position [[position]]; float4 color; float4 uv0; float4 uv1; float fog; };

vertex VOut ff_vs(uint vid [[vertex_id]], const device Vtx* v [[buffer(0)]], constant Uniforms& u [[buffer(1)]]) {
    Vtx in = v[vid];
    float4 p = float4(float3(in.pos), 1.0);
    float4 eye = u.mv * p;
    float4 clip = u.proj * eye;
    VOut o;
    o.position = float4(clip.xy, (clip.z + clip.w) * 0.5, clip.w);   // GL clip z [-w,w] -> Metal [0,w]
    float4 vc = u.hasColor != 0 ? unpack_unorm4x8_to_float(in.color) : u.current;
    if (u.lighting != 0) {
        float4 amb = (u.colorMaterial == 1 || u.colorMaterial == 3) ? vc : u.matAmbient;
        float4 dif = (u.colorMaterial == 2 || u.colorMaterial == 3) ? vc : u.matDiffuse;
        vc = float4(u.modelAmbient.rgb * amb.rgb, dif.a);
    }
    o.color = vc;
    o.uv0 = u.st[0].texMat * in.uv0;
    o.uv1 = u.st[1].texMat * in.uv1;
    o.fog = fabs(eye.z);
    return o;
}

static float3 pickRGB(int src, int op, float4 prim, float4 prev, float4 tex, float4 cst) {
    float4 c = src == 0x1702 ? tex : (src == 0x8576 ? cst : (src == 0x8577 ? prim : prev));
    if (op == 0x302) return c.aaa;
    if (op == 0x301) return 1.0 - c.rgb;
    if (op == 0x303) return 1.0 - c.aaa;
    return c.rgb;
}
static float pickA(int src, int op, float4 prim, float4 prev, float4 tex, float4 cst) {
    float4 c = src == 0x1702 ? tex : (src == 0x8576 ? cst : (src == 0x8577 ? prim : prev));
    return op == 0x303 ? 1.0 - c.a : c.a;
}
static float4 applyStage(constant Stage& s, float4 prim, float4 prev, float4 tex) {
    float3 a0 = pickRGB(s.rgbSrc[0], s.rgbOp[0], prim, prev, tex, s.envColor);
    float3 a1 = pickRGB(s.rgbSrc[1], s.rgbOp[1], prim, prev, tex, s.envColor);
    float3 a2 = pickRGB(s.rgbSrc[2], s.rgbOp[2], prim, prev, tex, s.envColor);
    float3 rgb;
    switch (s.rgbCombine) {
    case 0x1e01: rgb = a0; break;
    case 0x2100: rgb = a0 * a1; break;
    case 0x104: rgb = a0 + a1; break;
    case 0x8574: rgb = a0 + a1 - 0.5; break;
    case 0x8575: rgb = a0 * a2 + a1 * (1.0 - a2); break;
    case 0x84e7: rgb = a0 - a1; break;
    case 0x86ae: rgb = float3(4.0 * dot(a0 - 0.5, a1 - 0.5)); break;
    default: rgb = a0 * a1; break;
    }
    float b0 = pickA(s.alphaSrc[0], s.alphaOp[0], prim, prev, tex, s.envColor);
    float b1 = pickA(s.alphaSrc[1], s.alphaOp[1], prim, prev, tex, s.envColor);
    float b2 = pickA(s.alphaSrc[2], s.alphaOp[2], prim, prev, tex, s.envColor);
    float a;
    switch (s.alphaCombine) {
    case 0x1e01: a = b0; break;
    case 0x2100: a = b0 * b1; break;
    case 0x104: a = b0 + b1; break;
    case 0x8574: a = b0 + b1 - 0.5; break;
    case 0x8575: a = b0 * b2 + b1 * (1.0 - b2); break;
    case 0x84e7: a = b0 - b1; break;
    default: a = b0 * b1; break;
    }
    return clamp(float4(rgb * s.rgbScale, a * s.alphaScale), 0.0, 1.0);
}

fragment float4 ff_fs(VOut in [[stage_in]], constant Uniforms& u [[buffer(1)]],
                      texture2d<float> t0 [[texture(0)]], texture2d<float> t1 [[texture(1)]],
                      sampler s0 [[sampler(0)]], sampler s1 [[sampler(1)]]) {
    float4 prim = in.color;
    float4 c = prim;
    if (u.st[0].enabled != 0) {
        float2 uv = in.uv0.w != 0.0 && in.uv0.w != 1.0 ? in.uv0.xy / in.uv0.w : in.uv0.xy;
        c = applyStage(u.st[0], prim, c, t0.sample(s0, uv));
    }
    if (u.st[1].enabled != 0) {
        float2 uv = in.uv1.w != 0.0 && in.uv1.w != 1.0 ? in.uv1.xy / in.uv1.w : in.uv1.xy;
        c = applyStage(u.st[1], prim, c, t1.sample(s1, uv));
    }
    if (u.alphaTest != 0) {
        bool pass = true;
        switch (u.alphaFunc) {
        case 0x200: pass = false; break;
        case 0x201: pass = c.a < u.alphaRef; break;
        case 0x202: pass = c.a == u.alphaRef; break;
        case 0x203: pass = c.a <= u.alphaRef; break;
        case 0x204: pass = c.a > u.alphaRef; break;
        case 0x205: pass = c.a != u.alphaRef; break;
        case 0x206: pass = c.a >= u.alphaRef; break;
        default: break;
        }
        if (!pass) discard_fragment();
    }
    int mode = int(u.fogParams.w);
    if (mode != 0) {
        float d = in.fog, f = 1.0;
        if (mode == 1) f = clamp((u.fogParams.y - d) / (u.fogParams.y - u.fogParams.x), 0.0, 1.0);
        else if (mode == 2) f = exp(-u.fogParams.z * d);
        else f = exp(-(u.fogParams.z * d) * (u.fogParams.z * d));
        c.rgb = mix(u.fogColor.rgb, c.rgb, clamp(f, 0.0, 1.0));
    }
    return c;
}

// glDrawPixels: a pixel-space quad.
struct PxOut { float4 position [[position]]; float2 uv; };
vertex PxOut px_vs(uint vid [[vertex_id]], constant float4* q [[buffer(0)]]) {
    PxOut o;
    o.position = float4(q[vid].xy, 0.0, 1.0);
    o.uv = q[vid].zw;
    return o;
}
fragment float4 px_fs(PxOut in [[stage_in]], texture2d<float> t [[texture(0)]]) {
    constexpr sampler s(filter::nearest, address::clamp_to_edge);
    return t.sample(s, in.uv);
}
)MSL";

namespace {

struct StageU {
    simd_float4 envColor;
    simd_float4x4 texMat;
    int enabled, rgbCombine, alphaCombine, pad0;
    int rgbSrc[3];   int pad1;
    int alphaSrc[3]; int pad2;
    int rgbOp[3];    int pad3;
    int alphaOp[3];  int pad4;
    float rgbScale, alphaScale, pad5, pad6;
};
struct UniformsU {
    simd_float4x4 mv, proj;
    simd_float4 current, matAmbient, matDiffuse, modelAmbient, fogColor, fogParams;
    int hasColor, lighting, colorMaterial, alphaFunc;
    float alphaRef; int alphaTest; int pad0, pad1;
    StageU st[2];
};
struct Vtx { float pos[3]; std::uint32_t color; float uv0[4]; float uv1[4]; };
static_assert(sizeof(Vtx) == 48);

struct ClientArray {
    bool enabled = false;
    int size = 4;
    GLenum type = gl::FLOAT;
    int stride = 0;
    const void* pointer = nullptr;
    GLuint buffer = 0;   // bound ARRAY_BUFFER when the pointer was set (then `pointer` is an offset)
};

struct TexEnv {
    GLenum rgbCombine = 0x2100, alphaCombine = 0x2100;
    GLenum rgbSrc[3] = {0x1702, 0x8578, 0x8576}, alphaSrc[3] = {0x1702, 0x8578, 0x8576};
    GLenum rgbOp[3] = {0x300, 0x300, 0x302}, alphaOp[3] = {0x302, 0x302, 0x302};
    float rgbScale = 1, alphaScale = 1;
    float color[4] = {0, 0, 0, 0};
};

struct Level {
    int width = 0, height = 0;
    std::vector<std::uint8_t> bytes;   // BGRA8, or the raw S3TC blocks
};
struct TexObject {
    std::vector<Level> levels;
    bool compressed = false;
    MTLPixelFormat format = MTLPixelFormatBGRA8Unorm;
    GLenum minFilter = 0x2702, magFilter = 0x2601, wrapS = 0x2901, wrapT = 0x2901;
    GLint maxLevel = 1000;
    bool dirty = true;
    id<MTLTexture> texture = nil;
};

struct BufferObject { std::vector<std::uint8_t> bytes; };

// Everything glPushAttrib / glPopAttrib saves.
struct State {
    bool blend = false, alphaTest = false, depthTest = false, cull = false, lighting = false, fog = false,
         colorMaterial = false, scissor = false, stencilTest = false, dither = false, texture2D[2] = {false, false},
         polygonOffset = false, normalize = false, vertexProgram = false;
    GLenum blendSrc = 1, blendDst = 0, alphaFunc = 0x207, depthFunc = 0x201, frontFace = 0x901, cullFace = 0x405,
           polygonMode = 0x1b02, colorMaterialMode = 0x1602, matrixMode = 0x1700;
    float alphaRef = 0;
    bool depthMask = true;
    std::uint8_t colorMask[4] = {1, 1, 1, 1};
    GLuint stencilMask = 0xffffffff;
    float current[4] = {1, 1, 1, 1};
    float matAmbient[4] = {0.2f, 0.2f, 0.2f, 1}, matDiffuse[4] = {0.8f, 0.8f, 0.8f, 1}, modelAmbient[4] = {0.2f, 0.2f, 0.2f, 1};
    float fogColor[4] = {0, 0, 0, 0}, fogStart = 0, fogEnd = 1, fogDensity = 1;
    GLint fogMode = 0x800;
};

simd_float4x4 FromGL(const float* m) {
    simd_float4x4 r;
    std::memcpy(&r, m, sizeof r);
    return r;
}

std::uint32_t PackColor(const float* c) {
    auto q = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return q(c[0]) | q(c[1]) << 8 | q(c[2]) << 16 | q(c[3]) << 24;   // RGBA bytes in memory order
}

MTLBlendFactor BlendFactor(GLenum f) {
    switch (f) {
    case 0: return MTLBlendFactorZero;
    case 1: return MTLBlendFactorOne;
    case 0x300: return MTLBlendFactorSourceColor;
    case 0x301: return MTLBlendFactorOneMinusSourceColor;
    case 0x302: return MTLBlendFactorSourceAlpha;
    case 0x303: return MTLBlendFactorOneMinusSourceAlpha;
    case 0x304: return MTLBlendFactorDestinationAlpha;
    case 0x305: return MTLBlendFactorOneMinusDestinationAlpha;
    case 0x306: return MTLBlendFactorDestinationColor;
    case 0x307: return MTLBlendFactorOneMinusDestinationColor;
    case 0x308: return MTLBlendFactorSourceAlphaSaturated;
    default: return MTLBlendFactorOne;
    }
}

MTLCompareFunction Compare(GLenum f) {
    switch (f) {
    case 0x200: return MTLCompareFunctionNever;
    case 0x201: return MTLCompareFunctionLess;
    case 0x202: return MTLCompareFunctionEqual;
    case 0x203: return MTLCompareFunctionLessEqual;
    case 0x204: return MTLCompareFunctionGreater;
    case 0x205: return MTLCompareFunctionNotEqual;
    case 0x206: return MTLCompareFunctionGreaterEqual;
    default: return MTLCompareFunctionAlways;
    }
}

// Expand packed 16-bit pixels to BGRA8 (components "REV": the first-named component is in the low bits).
std::uint32_t Expand4444(std::uint16_t v) {
    const std::uint32_t b = (v & 0xf) * 17, g = (v >> 4 & 0xf) * 17, r = (v >> 8 & 0xf) * 17, a = (v >> 12 & 0xf) * 17;
    return b | g << 8 | r << 16 | a << 24;
}
std::uint32_t Expand1555(std::uint16_t v) {
    auto e5 = [](std::uint32_t x) { return (x << 3) | (x >> 2); };
    return e5(v & 0x1f) | e5(v >> 5 & 0x1f) << 8 | e5(v >> 10 & 0x1f) << 16 | (v & 0x8000 ? 0xff000000u : 0u);
}

// Converts client pixels (format/type as the game uploads them) into the BGRA8 store.
bool ToBGRA8(GLenum format, GLenum type, int w, int h, const void* data, std::vector<std::uint8_t>& out) {
    out.assign(static_cast<std::size_t>(w) * h * 4, 0);
    if (!data) return true;
    const auto* src = static_cast<const std::uint8_t*>(data);
    auto* dst = reinterpret_cast<std::uint32_t*>(out.data());
    const std::size_t n = static_cast<std::size_t>(w) * h;
    if (format == 0x80e1 && type == 0x8367) { std::memcpy(out.data(), src, n * 4); return true; }
    if (format == 0x80e1 && type == 0x8365) { for (std::size_t i = 0; i < n; ++i) { std::uint16_t v; std::memcpy(&v, src + i * 2, 2); dst[i] = Expand4444(v); } return true; }
    if (format == 0x80e1 && type == 0x8366) { for (std::size_t i = 0; i < n; ++i) { std::uint16_t v; std::memcpy(&v, src + i * 2, 2); dst[i] = Expand1555(v); } return true; }
    if (format == 0x1908 && type == 0x1401) {
        for (std::size_t i = 0; i < n; ++i) dst[i] = src[i * 4 + 2] | src[i * 4 + 1] << 8 | src[i * 4] << 16 | static_cast<std::uint32_t>(src[i * 4 + 3]) << 24;
        return true;
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------

struct MetalGL::Impl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLLibrary> library = nil;
    CAMetalLayer* layer = nil;
    id<MTLTexture> color = nil, depth = nil, whiteTexture = nil;
    std::uint32_t width = 0, height = 0;
    id<MTLCommandBuffer> cb = nil;
    id<MTLRenderCommandEncoder> enc = nil;
    bool clearColorPending = false, clearDepthPending = false;
    float clearColor[4] = {0, 0, 0, 0};
    double clearDepth = 1.0;

    static constexpr int kRing = 3;
    static constexpr std::size_t kRingBytes = 8u << 20;
    id<MTLBuffer> ring[kRing] = {nil, nil, nil};
    int ringIndex = 0;
    std::size_t ringOffset = 0;

    std::map<std::uint64_t, id<MTLRenderPipelineState>> pipelines;
    std::map<std::uint32_t, id<MTLDepthStencilState>> depthStates;
    std::map<std::uint64_t, id<MTLSamplerState>> samplers;
    id<MTLRenderPipelineState> pixelPipeline = nil;

    State s, saved[16];
    int savedCount = 0;
    std::vector<simd_float4x4> modelview{matrix_identity_float4x4}, projection{matrix_identity_float4x4},
        textureStack[2] = {{matrix_identity_float4x4}, {matrix_identity_float4x4}};
    int activeUnit = 0, clientUnit = 0;
    GLuint bound[2] = {0, 0};
    TexEnv env[2];
    std::map<GLuint, TexObject> textures;
    std::map<GLuint, BufferObject> buffers;
    GLuint arrayBuffer = 0, nextId = 1;
    ClientArray vertexArray, colorArray, texArray[2];
    float rasterPos[2] = {0, 0};
    std::uint32_t draws = 0;
    int unpackRow = 0, packRow = 0;

    simd_float4x4& CurrentMatrix() {
        if (s.matrixMode == 0x1701) return projection.back();
        if (s.matrixMode == 0x1702) return textureStack[activeUnit].back();
        return modelview.back();
    }
    std::vector<simd_float4x4>& CurrentStack() {
        if (s.matrixMode == 0x1701) return projection;
        if (s.matrixMode == 0x1702) return textureStack[activeUnit];
        return modelview;
    }

    void MakeTargets() {
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModePrivate;
        color = [device newTextureWithDescriptor:d];
        MTLTextureDescriptor* z = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:width height:height mipmapped:NO];
        z.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        z.storageMode = MTLStorageModePrivate;
        depth = [device newTextureWithDescriptor:z];
        clearColorPending = clearDepthPending = true;
    }

    void EndEncoder() {
        if (enc) {
            [enc endEncoding];
            enc = nil;
        }
    }
    void Submit(bool wait) {
        EndEncoder();
        if (cb) {
            [cb commit];
            if (wait) [cb waitUntilCompleted];
            cb = nil;
        }
        ringOffset = 0;   // (frames in flight are protected by the 3-deep ring; a waited submit means all done)
        if (wait) ringIndex = 0;
    }
    void BeginEncoder() {
        if (enc) return;
        if (!cb) cb = [queue commandBuffer];
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = color;
        pass.colorAttachments[0].loadAction = clearColorPending ? MTLLoadActionClear : MTLLoadActionLoad;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.depthAttachment.texture = depth;
        pass.depthAttachment.loadAction = clearDepthPending ? MTLLoadActionClear : MTLLoadActionLoad;
        pass.depthAttachment.clearDepth = clearDepth;
        pass.depthAttachment.storeAction = MTLStoreActionStore;
        clearColorPending = clearDepthPending = false;
        enc = [cb renderCommandEncoderWithDescriptor:pass];
    }

    id<MTLRenderPipelineState> Pipeline(bool blend, GLenum src, GLenum dst, const std::uint8_t* mask) {
        const std::uint64_t key = (blend ? 1u : 0u) | static_cast<std::uint64_t>(src & 0x3ff) << 1 | static_cast<std::uint64_t>(dst & 0x3ff) << 11 |
                                  static_cast<std::uint64_t>(mask[0] | mask[1] << 1 | mask[2] << 2 | mask[3] << 3) << 21;
        auto it = pipelines.find(key);
        if (it != pipelines.end()) return it->second;
        MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = [library newFunctionWithName:@"ff_vs"];
        d.fragmentFunction = [library newFunctionWithName:@"ff_fs"];
        d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        d.colorAttachments[0].writeMask = (mask[0] ? MTLColorWriteMaskRed : 0) | (mask[1] ? MTLColorWriteMaskGreen : 0) |
                                          (mask[2] ? MTLColorWriteMaskBlue : 0) | (mask[3] ? MTLColorWriteMaskAlpha : 0);
        if (blend) {
            d.colorAttachments[0].blendingEnabled = YES;
            d.colorAttachments[0].sourceRGBBlendFactor = d.colorAttachments[0].sourceAlphaBlendFactor = BlendFactor(src);
            d.colorAttachments[0].destinationRGBBlendFactor = d.colorAttachments[0].destinationAlphaBlendFactor = BlendFactor(dst);
        }
        NSError* err = nil;
        id<MTLRenderPipelineState> p = [device newRenderPipelineStateWithDescriptor:d error:&err];
        if (!p) NSLog(@"MetalGL pipeline error: %@", err);
        pipelines[key] = p;
        return p;
    }
    id<MTLDepthStencilState> DepthState(bool test, GLenum func, bool write) {
        const std::uint32_t key = (test ? 1u : 0u) | (write ? 2u : 0u) | (func & 0xf) << 2;
        auto it = depthStates.find(key);
        if (it != depthStates.end()) return it->second;
        MTLDepthStencilDescriptor* d = [MTLDepthStencilDescriptor new];
        d.depthCompareFunction = test ? Compare(func) : MTLCompareFunctionAlways;
        d.depthWriteEnabled = write && test;   // (GL writes depth only while the depth test is on)
        id<MTLDepthStencilState> st = [device newDepthStencilStateWithDescriptor:d];
        depthStates[key] = st;
        return st;
    }
    id<MTLSamplerState> Sampler(const TexObject& t) {
        const std::uint64_t key = t.minFilter | static_cast<std::uint64_t>(t.magFilter & 0xffff) << 16 |
                                  static_cast<std::uint64_t>(t.wrapS & 1) << 32 | static_cast<std::uint64_t>(t.wrapT & 1) << 33 |
                                  static_cast<std::uint64_t>(std::clamp(t.maxLevel, 0, 255)) << 34;
        auto it = samplers.find(key);
        if (it != samplers.end()) return it->second;
        MTLSamplerDescriptor* d = [MTLSamplerDescriptor new];
        const bool minLinear = t.minFilter == 0x2601 || t.minFilter == 0x2701 || t.minFilter == 0x2703;
        d.minFilter = minLinear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.magFilter = t.magFilter == 0x2600 ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
        const bool mips = t.minFilter >= 0x2700;
        d.mipFilter = !mips ? MTLSamplerMipFilterNotMipmapped : (t.minFilter == 0x2702 || t.minFilter == 0x2703 ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest);
        d.sAddressMode = (t.wrapS & 1) ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
        d.tAddressMode = (t.wrapT & 1) ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
        d.lodMaxClamp = static_cast<float>(std::clamp(t.maxLevel, 0, 255));
        id<MTLSamplerState> st = [device newSamplerStateWithDescriptor:d];
        samplers[key] = st;
        return st;
    }

    // Builds (or refreshes) the MTLTexture of a GL texture object from its CPU levels.
    id<MTLTexture> Resolve(TexObject& t) {
        if (t.levels.empty() || t.levels[0].width == 0) return nil;
        if (t.texture && !t.dirty) return t.texture;
        std::size_t usable = 0;   // levels forming a valid chain
        int w = t.levels[0].width, h = t.levels[0].height;
        for (std::size_t i = 0; i < t.levels.size(); ++i) {
            if (t.levels[i].width != w || t.levels[i].height != h || t.levels[i].bytes.empty()) break;
            ++usable;
            if (w == 1 && h == 1) break;
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        }
        if (usable == 0) return nil;
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:t.format width:t.levels[0].width height:t.levels[0].height mipmapped:usable > 1];
        d.mipmapLevelCount = usable;
        d.storageMode = MTLStorageModeShared;
        d.usage = MTLTextureUsageShaderRead;
        t.texture = [device newTextureWithDescriptor:d];
        for (std::size_t i = 0; i < usable; ++i) {
            const Level& L = t.levels[i];
            const NSUInteger bytesPerRow = t.compressed ? std::max(1, (L.width + 3) / 4) * (t.format == MTLPixelFormatBC1_RGBA ? 8 : 16) : static_cast<NSUInteger>(L.width) * 4;
            [t.texture replaceRegion:MTLRegionMake2D(0, 0, L.width, L.height) mipmapLevel:i withBytes:L.bytes.data() bytesPerRow:bytesPerRow];
        }
        t.dirty = false;
        return t.texture;
    }

    std::uint8_t* Ring(std::size_t bytes, std::size_t* offset) {
        ringOffset = (ringOffset + 255) & ~std::size_t{255};
        if (!ring[ringIndex]) ring[ringIndex] = [device newBufferWithLength:kRingBytes options:MTLResourceStorageModeShared];
        if (ringOffset + bytes > kRingBytes) {   // out of room this frame: flush and wait, then reuse from the start
            Submit(true);
            if (!ring[ringIndex]) ring[ringIndex] = [device newBufferWithLength:kRingBytes options:MTLResourceStorageModeShared];
        }
        *offset = ringOffset;
        ringOffset += bytes;
        return static_cast<std::uint8_t*>([ring[ringIndex] contents]) + *offset;
    }

    const std::uint8_t* ArrayBase(const ClientArray& a) const {
        if (a.buffer != 0) {
            auto it = buffers.find(a.buffer);
            if (it == buffers.end()) return nullptr;
            return it->second.bytes.data() + reinterpret_cast<std::uintptr_t>(a.pointer);
        }
        return static_cast<const std::uint8_t*>(a.pointer);
    }

    // Gathers vertices [first, first+count) from the enabled client arrays into the fixed layout.
    Vtx* Gather(int first, int count, std::size_t* offset) {
        Vtx* out = reinterpret_cast<Vtx*>(Ring(static_cast<std::size_t>(count) * sizeof(Vtx), offset));
        const std::uint8_t* pos = vertexArray.enabled ? ArrayBase(vertexArray) : nullptr;
        const std::uint8_t* col = colorArray.enabled ? ArrayBase(colorArray) : nullptr;
        const std::uint8_t* tex[2] = {texArray[0].enabled ? ArrayBase(texArray[0]) : nullptr, texArray[1].enabled ? ArrayBase(texArray[1]) : nullptr};
        const std::uint32_t current = PackColor(s.current);
        for (int i = 0; i < count; ++i) {
            Vtx& v = out[i];
            const std::size_t index = static_cast<std::size_t>(first + i);
            if (pos) {
                const float* p = reinterpret_cast<const float*>(pos + index * (vertexArray.stride ? vertexArray.stride : vertexArray.size * 4));
                v.pos[0] = p[0]; v.pos[1] = p[1]; v.pos[2] = vertexArray.size > 2 ? p[2] : 0.0f;
            } else {
                v.pos[0] = v.pos[1] = v.pos[2] = 0.0f;
            }
            if (col) {
                const std::uint8_t* c = col + index * (colorArray.stride ? colorArray.stride : colorArray.size);
                if (colorArray.type == gl::FLOAT) {
                    const float* f = reinterpret_cast<const float*>(c);
                    float rgba[4] = {f[0], f[1], f[2], colorArray.size > 3 ? f[3] : 1.0f};
                    v.color = PackColor(rgba);
                } else {
                    v.color = c[0] | c[1] << 8 | c[2] << 16 | static_cast<std::uint32_t>(colorArray.size > 3 ? c[3] : 255) << 24;
                }
            } else {
                v.color = current;
            }
            for (int u = 0; u < 2; ++u) {
                float* uv = u == 0 ? v.uv0 : v.uv1;
                uv[0] = uv[1] = uv[2] = 0.0f;
                uv[3] = 1.0f;
                if (tex[u]) {
                    const ClientArray& a = texArray[u];
                    const float* t = reinterpret_cast<const float*>(tex[u] + index * (a.stride ? a.stride : a.size * 4));
                    for (int k = 0; k < a.size && k < 4; ++k) uv[k] = t[k];
                }
            }
        }
        return out;
    }

    void FillUniforms(UniformsU& u) {
        std::memset(&u, 0, sizeof u);
        u.mv = modelview.back();
        u.proj = projection.back();
        u.current = simd_make_float4(s.current[0], s.current[1], s.current[2], s.current[3]);
        u.matAmbient = simd_make_float4(s.matAmbient[0], s.matAmbient[1], s.matAmbient[2], s.matAmbient[3]);
        u.matDiffuse = simd_make_float4(s.matDiffuse[0], s.matDiffuse[1], s.matDiffuse[2], s.matDiffuse[3]);
        u.modelAmbient = simd_make_float4(s.modelAmbient[0], s.modelAmbient[1], s.modelAmbient[2], s.modelAmbient[3]);
        u.fogColor = simd_make_float4(s.fogColor[0], s.fogColor[1], s.fogColor[2], s.fogColor[3]);
        const float mode = !s.fog ? 0.0f : (s.fogMode == 0x2601 ? 1.0f : (s.fogMode == 0x800 ? 2.0f : 3.0f));
        u.fogParams = simd_make_float4(s.fogStart, s.fogEnd, s.fogDensity, mode);
        u.hasColor = colorArray.enabled ? 1 : 0;
        u.lighting = s.lighting ? 1 : 0;
        u.colorMaterial = !s.colorMaterial ? 0 : (s.colorMaterialMode == 0x1200 ? 1 : (s.colorMaterialMode == 0x1201 ? 2 : 3));
        u.alphaFunc = static_cast<int>(s.alphaFunc);
        u.alphaRef = s.alphaRef;
        u.alphaTest = s.alphaTest ? 1 : 0;
        for (int i = 0; i < 2; ++i) {
            StageU& st = u.st[i];
            const TexEnv& e = env[i];
            auto it = textures.find(bound[i]);
            const bool usable = s.texture2D[i] && it != textures.end() && !it->second.levels.empty() && it->second.levels[0].width > 0;
            st.enabled = usable ? 1 : 0;
            st.texMat = textureStack[i].back();
            st.envColor = simd_make_float4(e.color[0], e.color[1], e.color[2], e.color[3]);
            st.rgbCombine = static_cast<int>(e.rgbCombine);
            st.alphaCombine = static_cast<int>(e.alphaCombine);
            for (int k = 0; k < 3; ++k) {
                st.rgbSrc[k] = static_cast<int>(e.rgbSrc[k]);
                st.alphaSrc[k] = static_cast<int>(e.alphaSrc[k]);
                st.rgbOp[k] = static_cast<int>(e.rgbOp[k]);
                st.alphaOp[k] = static_cast<int>(e.alphaOp[k]);
            }
            st.rgbScale = e.rgbScale;
            st.alphaScale = e.alphaScale;
        }
    }

    void BindState(UniformsU& u) {
        BeginEncoder();
        [enc setRenderPipelineState:Pipeline(s.blend, s.blendSrc, s.blendDst, s.colorMask)];
        [enc setDepthStencilState:DepthState(s.depthTest, s.depthFunc, s.depthMask)];
        [enc setCullMode:s.cull ? (s.cullFace == 0x404 ? MTLCullModeFront : MTLCullModeBack) : MTLCullModeNone];
        [enc setFrontFacingWinding:s.frontFace == 0x900 ? MTLWindingClockwise : MTLWindingCounterClockwise];
        [enc setTriangleFillMode:s.polygonMode == 0x1b01 ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
        FillUniforms(u);
        [enc setVertexBytes:&u length:sizeof u atIndex:1];
        [enc setFragmentBytes:&u length:sizeof u atIndex:1];
        for (int i = 0; i < 2; ++i) {
            id<MTLTexture> tex = whiteTexture;
            id<MTLSamplerState> sampler = nil;
            auto it = textures.find(bound[i]);
            if (it != textures.end()) {
                if (id<MTLTexture> r = Resolve(it->second)) tex = r;
                sampler = Sampler(it->second);
            }
            if (!sampler) {
                TexObject dummy;
                sampler = Sampler(dummy);
            }
            [enc setFragmentTexture:tex atIndex:i];
            [enc setFragmentSamplerState:sampler atIndex:i];
        }
        [enc setViewport:(MTLViewport){0, 0, double(width), double(height), 0, 1}];
    }

    void Draw(GLenum mode, int first, int count, const std::uint16_t* indices, int indexCount) {
        if (count <= 0) return;
        std::size_t offset = 0;
        Gather(first, count, &offset);
        UniformsU u;
        BindState(u);
        [enc setVertexBuffer:ring[ringIndex] offset:offset atIndex:0];
        MTLPrimitiveType prim = MTLPrimitiveTypeTriangle;
        switch (mode) {
        case 0: prim = MTLPrimitiveTypePoint; break;
        case 1: prim = MTLPrimitiveTypeLine; break;
        case 3: prim = MTLPrimitiveTypeLineStrip; break;
        case 5: prim = MTLPrimitiveTypeTriangleStrip; break;
        default: prim = MTLPrimitiveTypeTriangle; break;
        }
        if (mode == 6) {   // triangle fan -> list
            std::vector<std::uint16_t> list;
            auto at = [&](int i) { return indices ? indices[i] : static_cast<std::uint16_t>(i); };
            const int n = indices ? indexCount : count;
            for (int i = 1; i + 1 < n; ++i) { list.push_back(at(0)); list.push_back(at(i)); list.push_back(at(i + 1)); }
            if (list.empty()) return;
            std::size_t ioff;
            std::memcpy(Ring(list.size() * 2, &ioff), list.data(), list.size() * 2);
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:list.size() indexType:MTLIndexTypeUInt16 indexBuffer:ring[ringIndex] indexBufferOffset:ioff];
        } else if (indices) {
            std::size_t ioff;
            std::memcpy(Ring(static_cast<std::size_t>(indexCount) * 2, &ioff), indices, static_cast<std::size_t>(indexCount) * 2);
            [enc drawIndexedPrimitives:prim indexCount:indexCount indexType:MTLIndexTypeUInt16 indexBuffer:ring[ringIndex] indexBufferOffset:ioff];
        } else {
            [enc drawPrimitives:prim vertexStart:0 vertexCount:count];
        }
        ++draws;
    }
};

// ---------------------------------------------------------------------------------------------------------------

MetalGL::MetalGL() : impl_(new Impl) {}
MetalGL::~MetalGL() = default;

std::unique_ptr<MetalGL> MetalGL::Create(void* caMetalLayer, std::uint32_t width, std::uint32_t height) {
    std::unique_ptr<MetalGL> gl(new MetalGL);
    Impl& m = *gl->impl_;
    m.device = MTLCreateSystemDefaultDevice();
    if (!m.device) return nullptr;
    m.queue = [m.device newCommandQueue];
    NSError* err = nil;
    m.library = [m.device newLibraryWithSource:kShader options:nil error:&err];
    if (!m.library) {
        NSLog(@"MetalGL shader error: %@", err);
        return nullptr;
    }
    m.layer = (__bridge CAMetalLayer*)caMetalLayer;
    m.width = width;
    m.height = height;
    m.MakeTargets();
    MTLTextureDescriptor* w = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:1 height:1 mipmapped:NO];
    m.whiteTexture = [m.device newTextureWithDescriptor:w];
    const std::uint32_t white = 0xffffffff;
    [m.whiteTexture replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&white bytesPerRow:4];
    // Pixel (glDrawPixels) pipeline.
    MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [m.library newFunctionWithName:@"px_vs"];
    d.fragmentFunction = [m.library newFunctionWithName:@"px_fs"];
    d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    m.pixelPipeline = [m.device newRenderPipelineStateWithDescriptor:d error:&err];
    return gl;
}

void MetalGL::Resize(std::uint32_t width, std::uint32_t height) {
    Impl& m = *impl_;
    m.Submit(true);
    m.width = width;
    m.height = height;
    m.MakeTargets();
}

std::uint32_t MetalGL::DrawCallCount() const { return impl_->draws; }
std::uint32_t MetalGL::PipelineCount() const { return static_cast<std::uint32_t>(impl_->pipelines.size()); }

void MetalGL::Finish() { impl_->Submit(true); }

void MetalGL::Present() {
    Impl& m = *impl_;
    m.EndEncoder();
    if (m.layer && m.cb) {
        id<CAMetalDrawable> drawable = [m.layer nextDrawable];
        if (drawable) {
            id<MTLBlitCommandEncoder> blit = [m.cb blitCommandEncoder];
            const NSUInteger w = std::min<NSUInteger>(m.width, drawable.texture.width), h = std::min<NSUInteger>(m.height, drawable.texture.height);
            [blit copyFromTexture:m.color sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                        toTexture:drawable.texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
            [m.cb presentDrawable:drawable];
        }
    }
    m.Submit(false);
    m.ringIndex = (m.ringIndex + 1) % Impl::kRing;
    m.ringOffset = 0;
}

// ---- capabilities ----
void MetalGL::Enable(GLenum cap) {
    State& s = impl_->s;
    switch (cap) {
    case gl::BLEND: s.blend = true; break;
    case gl::ALPHA_TEST: s.alphaTest = true; break;
    case gl::DEPTH_TEST: s.depthTest = true; break;
    case gl::CULL_FACE: s.cull = true; break;
    case gl::LIGHTING: s.lighting = true; break;
    case gl::FOG: s.fog = true; break;
    case gl::COLOR_MATERIAL: s.colorMaterial = true; break;
    case gl::SCISSOR_TEST: s.scissor = true; break;
    case gl::STENCIL_TEST: s.stencilTest = true; break;
    case gl::DITHER: s.dither = true; break;
    case gl::TEXTURE_2D: s.texture2D[impl_->activeUnit] = true; break;
    case gl::VERTEX_PROGRAM_ARB: s.vertexProgram = true; break;
    case 0x8037: s.polygonOffset = true; break;
    default: break;
    }
}
void MetalGL::Disable(GLenum cap) {
    State& s = impl_->s;
    switch (cap) {
    case gl::BLEND: s.blend = false; break;
    case gl::ALPHA_TEST: s.alphaTest = false; break;
    case gl::DEPTH_TEST: s.depthTest = false; break;
    case gl::CULL_FACE: s.cull = false; break;
    case gl::LIGHTING: s.lighting = false; break;
    case gl::FOG: s.fog = false; break;
    case gl::COLOR_MATERIAL: s.colorMaterial = false; break;
    case gl::SCISSOR_TEST: s.scissor = false; break;
    case gl::STENCIL_TEST: s.stencilTest = false; break;
    case gl::DITHER: s.dither = false; break;
    case gl::TEXTURE_2D: s.texture2D[impl_->activeUnit] = false; break;
    case gl::VERTEX_PROGRAM_ARB: s.vertexProgram = false; break;
    case 0x8037: s.polygonOffset = false; break;
    default: break;
    }
}
bool MetalGL::IsEnabled(GLenum cap) {
    const State& s = impl_->s;
    switch (cap) {
    case gl::BLEND: return s.blend;
    case gl::ALPHA_TEST: return s.alphaTest;
    case gl::DEPTH_TEST: return s.depthTest;
    case gl::CULL_FACE: return s.cull;
    case gl::LIGHTING: return s.lighting;
    case gl::FOG: return s.fog;
    case gl::COLOR_MATERIAL: return s.colorMaterial;
    case gl::SCISSOR_TEST: return s.scissor;
    case gl::STENCIL_TEST: return s.stencilTest;
    case gl::DITHER: return s.dither;
    case gl::TEXTURE_2D: return s.texture2D[impl_->activeUnit];
    case gl::TEXTURE_CUBE_MAP: return false;
    default: return false;
    }
}
void MetalGL::GetIntegerv(GLenum pname, GLint* out) {
    const Impl& m = *impl_;
    switch (pname) {
    case gl::VIEWPORT: out[0] = 0; out[1] = 0; out[2] = static_cast<GLint>(m.width); out[3] = static_cast<GLint>(m.height); break;
    case gl::STENCIL_WRITEMASK: out[0] = static_cast<GLint>(m.s.stencilMask); break;
    case gl::DEPTH_WRITEMASK: out[0] = m.s.depthMask ? 1 : 0; break;
    case gl::DEPTH_FUNC: out[0] = static_cast<GLint>(m.s.depthFunc); break;
    case gl::ACTIVE_TEXTURE: out[0] = static_cast<GLint>(gl::TEXTURE0 + m.activeUnit); break;
    default: out[0] = 0; break;
    }
}
void MetalGL::GetBooleanv(GLenum pname, std::uint8_t* out) {
    if (pname == gl::COLOR_WRITEMASK) std::memcpy(out, impl_->s.colorMask, 4);
    else out[0] = 0;
}
// Deliberately does NOT advertise GL_APPLE_vertex_array_object / GL_APPLE_fence / GL_APPLE_element_array: those would turn on
// the shim's vertex-array-range path (MacOpenGL::IsVertexArraySupported), whose per-buffer client-state cache relies on
// BindVertexArrayAPPLE actually switching state, and that call is a stub here. Without them the shim uses its device-level
// array mirrors (UpdateNonVarPtrs). (The "vertex-array" preference also defaults to off.)
const char* MetalGL::GetString(GLenum name) {
    if (name == gl::EXTENSIONS)
        return "GL_SGIS_texture_lod GL_ARB_window_pos GL_ARB_texture_compression GL_EXT_texture_compression_s3tc GL_ARB_multitexture";
    if (name == 0x1f01) return "Apple Metal (MetalGL)";
    return "";
}
void MetalGL::PixelStorei(GLenum pname, GLint value) {
    if (pname == gl::UNPACK_ROW_LENGTH) impl_->unpackRow = value;
    else if (pname == gl::PACK_ROW_LENGTH) impl_->packRow = value;
}
void MetalGL::PushAttrib(GLuint) {
    Impl& m = *impl_;
    if (m.savedCount < 16) m.saved[m.savedCount++] = m.s;
}
void MetalGL::PopAttrib() {
    Impl& m = *impl_;
    if (m.savedCount > 0) m.s = m.saved[--m.savedCount];
}

// ---- matrices ----
void MetalGL::MatrixMode(GLenum mode) { impl_->s.matrixMode = mode; }
void MetalGL::LoadIdentity() { impl_->CurrentMatrix() = matrix_identity_float4x4; }
void MetalGL::LoadMatrixf(const float* m) { impl_->CurrentMatrix() = FromGL(m); }
void MetalGL::MultMatrixf(const float* m) { impl_->CurrentMatrix() = simd_mul(impl_->CurrentMatrix(), FromGL(m)); }
void MetalGL::PushMatrix() {
    auto& stack = impl_->CurrentStack();
    stack.push_back(stack.back());
}
void MetalGL::PopMatrix() {
    auto& stack = impl_->CurrentStack();
    if (stack.size() > 1) stack.pop_back();
}
void MetalGL::Ortho(double l, double r, double b, double t, double n, double f) {
    const float m[16] = {static_cast<float>(2 / (r - l)), 0, 0, 0, 0, static_cast<float>(2 / (t - b)), 0, 0, 0, 0,
                         static_cast<float>(-2 / (f - n)), 0, static_cast<float>(-(r + l) / (r - l)),
                         static_cast<float>(-(t + b) / (t - b)), static_cast<float>(-(f + n) / (f - n)), 1};
    MultMatrixf(m);
}
void MetalGL::Color4f(float r, float g, float b, float a) {
    float* c = impl_->s.current;
    c[0] = r; c[1] = g; c[2] = b; c[3] = a;
}
void MetalGL::RasterPos3f(float x, float y, float z) {
    Impl& m = *impl_;
    const simd_float4 clip = simd_mul(simd_mul(m.projection.back(), m.modelview.back()), simd_make_float4(x, y, z, 1));
    if (clip.w == 0) return;
    m.rasterPos[0] = (clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(m.width);
    m.rasterPos[1] = (clip.y / clip.w * 0.5f + 0.5f) * static_cast<float>(m.height);
}
void MetalGL::WindowPos2i(GLint x, GLint y) {
    impl_->rasterPos[0] = static_cast<float>(x);
    impl_->rasterPos[1] = static_cast<float>(y);
}

// ---- fixed function state ----
void MetalGL::BlendFunc(GLenum src, GLenum dst) { impl_->s.blendSrc = src; impl_->s.blendDst = dst; }
void MetalGL::AlphaFunc(GLenum func, float ref) { impl_->s.alphaFunc = func; impl_->s.alphaRef = ref; }
void MetalGL::Materialfv(GLenum, GLenum pname, const float* v) {
    State& s = impl_->s;
    if (pname == gl::AMBIENT) std::memcpy(s.matAmbient, v, 16);
    else if (pname == gl::DIFFUSE) std::memcpy(s.matDiffuse, v, 16);
}
void MetalGL::ColorMaterial(GLenum, GLenum mode) { impl_->s.colorMaterialMode = mode; }
void MetalGL::ShadeModel(GLenum) {}
void MetalGL::PolygonMode(GLenum, GLenum mode) { impl_->s.polygonMode = mode; }
void MetalGL::FrontFace(GLenum mode) { impl_->s.frontFace = mode; }
void MetalGL::CullFace(GLenum mode) { impl_->s.cullFace = mode; }
void MetalGL::Fogfv(GLenum pname, const float* v) {
    State& s = impl_->s;
    switch (pname) {
    case 0xb66: std::memcpy(s.fogColor, v, 16); break;
    case 0xb63: s.fogStart = v[0]; break;
    case 0xb64: s.fogEnd = v[0]; break;
    case 0xb62: s.fogDensity = v[0]; break;
    default: break;
    }
}
void MetalGL::Fogi(GLenum pname, GLint v) {
    if (pname == 0xb65) impl_->s.fogMode = v;
}
void MetalGL::LightModelfv(GLenum pname, const float* v) {
    if (pname == 0xb53) std::memcpy(impl_->s.modelAmbient, v, 16);
}
void MetalGL::ColorMask(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    std::uint8_t* m = impl_->s.colorMask;
    m[0] = r; m[1] = g; m[2] = b; m[3] = a;
}
void MetalGL::DepthMask(std::uint8_t flag) { impl_->s.depthMask = flag != 0; }
void MetalGL::DepthFunc(GLenum func) { impl_->s.depthFunc = func; }
void MetalGL::StencilMask(GLuint mask) { impl_->s.stencilMask = mask; }
void MetalGL::ClearColor(float r, float g, float b, float a) {
    float* c = impl_->clearColor;
    c[0] = r; c[1] = g; c[2] = b; c[3] = a;
}
void MetalGL::ClearDepth(double d) { impl_->clearDepth = d; }
void MetalGL::ClearStencil(GLint) {}
void MetalGL::Clear(GLuint mask) {
    Impl& m = *impl_;
    if ((mask & 0x4000) == 0 && (mask & 0x100) == 0) return;
    m.EndEncoder();
    if (mask & 0x4000) m.clearColorPending = true;
    if (mask & 0x100) m.clearDepthPending = true;
}

// ---- frame buffer transfers ----
void MetalGL::ReadBuffer(GLenum) {}
void MetalGL::DrawBuffer(GLenum) {}
void MetalGL::CopyPixels(GLint, GLint, GLsizei, GLsizei, GLenum) {}   // buffer-to-buffer copies are not needed on a single target

void MetalGL::ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data) {
    Impl& m = *impl_;
    if (w <= 0 || h <= 0 || !data) return;
    m.Submit(true);
    if (m.clearColorPending || m.clearDepthPending) {   // honour a clear that nothing has drawn into yet
        m.BeginEncoder();
        m.Submit(true);
    }
    const bool depth = format == 0x1902;
    id<MTLTexture> src = depth ? m.depth : m.color;
    const NSUInteger bpp = 4;
    const NSUInteger rowBytes = static_cast<NSUInteger>(m.width) * bpp;
    id<MTLBuffer> staging = [m.device newBufferWithLength:rowBytes * m.height options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [m.queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:src sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(m.width, m.height, 1)
                 toBuffer:staging destinationOffset:0 destinationBytesPerRow:rowBytes destinationBytesPerImage:rowBytes * m.height
                  options:depth ? MTLBlitOptionDepthFromDepthStencil : MTLBlitOptionNone];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    const auto* base = static_cast<const std::uint8_t*>([staging contents]);
    auto* out = static_cast<std::uint8_t*>(data);
    const int rowLength = m.packRow > 0 ? m.packRow : w;
    for (int r = 0; r < h; ++r) {
        const int srcRow = static_cast<int>(m.height) - 1 - (y + r);   // GL rows run bottom-up
        std::uint8_t* dstRow = out + static_cast<std::size_t>(r) * rowLength * 4;
        if (srcRow < 0 || srcRow >= static_cast<int>(m.height)) { std::memset(dstRow, 0, static_cast<std::size_t>(w) * 4); continue; }
        const std::uint8_t* srcPx = base + static_cast<std::size_t>(srcRow) * rowBytes + static_cast<std::size_t>(x) * 4;
        if (depth || (format == 0x80e1 && type == 0x8367)) {
            std::memcpy(dstRow, srcPx, static_cast<std::size_t>(w) * 4);
        } else {   // RGBA bytes
            for (int i = 0; i < w; ++i) { dstRow[i * 4] = srcPx[i * 4 + 2]; dstRow[i * 4 + 1] = srcPx[i * 4 + 1]; dstRow[i * 4 + 2] = srcPx[i * 4]; dstRow[i * 4 + 3] = srcPx[i * 4 + 3]; }
        }
    }
}

void MetalGL::DrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const void* data) {
    Impl& m = *impl_;
    if (!data || w <= 0 || h <= 0 || format == 0x1902) return;   // (depth uploads are not supported)
    std::vector<std::uint8_t> bgra;
    const int rowLength = m.unpackRow > 0 ? m.unpackRow : w;
    std::vector<std::uint8_t> packed(static_cast<std::size_t>(w) * h * 4);
    const int pixelBytes = (type == 0x8365 || type == 0x8366) ? 2 : 4;
    for (int r = 0; r < h; ++r)
        std::memcpy(packed.data() + static_cast<std::size_t>(r) * w * pixelBytes, static_cast<const std::uint8_t*>(data) + static_cast<std::size_t>(r) * rowLength * pixelBytes, static_cast<std::size_t>(w) * pixelBytes);
    if (!ToBGRA8(format, type, w, h, packed.data(), bgra)) return;
    MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
    id<MTLTexture> tex = [m.device newTextureWithDescriptor:d];
    // GL draws bottom-up: flip while uploading.
    for (int r = 0; r < h; ++r)
        [tex replaceRegion:MTLRegionMake2D(0, h - 1 - r, w, 1) mipmapLevel:0 withBytes:bgra.data() + static_cast<std::size_t>(r) * w * 4 bytesPerRow:static_cast<NSUInteger>(w) * 4];
    m.BeginEncoder();
    const float W = static_cast<float>(m.width), H = static_cast<float>(m.height);
    const float x0 = m.rasterPos[0] / W * 2 - 1, x1 = (m.rasterPos[0] + w) / W * 2 - 1;
    const float y0 = m.rasterPos[1] / H * 2 - 1, y1 = (m.rasterPos[1] + h) / H * 2 - 1;
    const simd_float4 quad[6] = {{x0, y0, 0, 1}, {x1, y0, 1, 1}, {x0, y1, 0, 0}, {x1, y0, 1, 1}, {x1, y1, 1, 0}, {x0, y1, 0, 0}};
    [m.enc setRenderPipelineState:m.pixelPipeline];
    [m.enc setDepthStencilState:m.DepthState(false, 0x207, false)];
    [m.enc setCullMode:MTLCullModeNone];
    [m.enc setVertexBytes:quad length:sizeof quad atIndex:0];
    [m.enc setFragmentTexture:tex atIndex:0];
    [m.enc setViewport:(MTLViewport){0, 0, double(m.width), double(m.height), 0, 1}];
    [m.enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    ++m.draws;
}

// ---- textures ----
void MetalGL::ActiveTexture(GLenum unit) { impl_->activeUnit = unit >= gl::TEXTURE0 + 1 ? 1 : 0; }
void MetalGL::ClientActiveTexture(GLenum unit) { impl_->clientUnit = unit >= gl::TEXTURE0 + 1 ? 1 : 0; }

void MetalGL::TexEnvi(GLenum, GLenum pname, GLint v) {
    TexEnv& e = impl_->env[impl_->activeUnit];
    const GLenum value = static_cast<GLenum>(v);
    switch (pname) {
    case 0x8571: e.rgbCombine = value; break;
    case 0x8572: e.alphaCombine = value; break;
    case 0x8580: case 0x8581: case 0x8582: e.rgbSrc[pname - 0x8580] = value; break;
    case 0x8588: case 0x8589: case 0x858a: e.alphaSrc[pname - 0x8588] = value; break;
    case 0x8590: case 0x8591: case 0x8592: e.rgbOp[pname - 0x8590] = value; break;
    case 0x8598: case 0x8599: case 0x859a: e.alphaOp[pname - 0x8598] = value; break;
    default: break;   // TEXTURE_ENV_MODE: the game always selects COMBINE
    }
}
void MetalGL::TexEnvf(GLenum, GLenum pname, float v) {
    TexEnv& e = impl_->env[impl_->activeUnit];
    if (pname == 0x8573) e.rgbScale = v;
    else if (pname == 0xd1c) e.alphaScale = v;
}
void MetalGL::TexEnvfv(GLenum, GLenum pname, const float* v) {
    if (pname == 0x2201) std::memcpy(impl_->env[impl_->activeUnit].color, v, 16);
}
void MetalGL::TexParameteri(GLenum, GLenum pname, GLint v) {
    Impl& m = *impl_;
    auto it = m.textures.find(m.bound[m.activeUnit]);
    if (it == m.textures.end()) return;
    TexObject& t = it->second;
    switch (pname) {
    case 0x2800: t.magFilter = static_cast<GLenum>(v); break;
    case 0x2801: t.minFilter = static_cast<GLenum>(v); break;
    case 0x2802: t.wrapS = static_cast<GLenum>(v); break;
    case 0x2803: t.wrapT = static_cast<GLenum>(v); break;
    case 0x813d: t.maxLevel = v; break;
    default: break;
    }
}
void MetalGL::GenTextures(GLsizei n, GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) {
        ids[i] = impl_->nextId++;
        impl_->textures[ids[i]];
    }
}
void MetalGL::DeleteTextures(GLsizei n, const GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) {
        impl_->textures.erase(ids[i]);
        for (GLuint& b : impl_->bound)
            if (b == ids[i]) b = 0;
    }
}
void MetalGL::BindTexture(GLenum, GLuint id) {
    Impl& m = *impl_;
    m.bound[m.activeUnit] = id;
    if (id) m.textures[id];
}

namespace {
MTLPixelFormat CompressedFormat(GLenum internal) {
    switch (internal) {
    case 0x83f1: return MTLPixelFormatBC1_RGBA;
    case 0x83f2: return MTLPixelFormatBC2_RGBA;
    case 0x83f3: return MTLPixelFormatBC3_RGBA;
    default: return MTLPixelFormatBC1_RGBA;
    }
}
}  // namespace

void MetalGL::TexImage2D(GLenum, GLint level, GLint, GLsizei w, GLsizei h, GLint, GLenum format, GLenum type, const void* data) {
    Impl& m = *impl_;
    TexObject& t = m.textures[m.bound[m.activeUnit]];
    if (level >= static_cast<GLint>(t.levels.size())) t.levels.resize(static_cast<std::size_t>(level) + 1);
    if (level == 0) {
        t.compressed = false;
        t.format = MTLPixelFormatBGRA8Unorm;
    }
    Level& L = t.levels[static_cast<std::size_t>(level)];
    L.width = w;
    L.height = h;
    const int rowLength = m.unpackRow > 0 ? m.unpackRow : w;
    if (data && rowLength != w) {
        std::vector<std::uint8_t> tight(static_cast<std::size_t>(w) * h * 4);
        const int pb = (type == 0x8365 || type == 0x8366) ? 2 : 4;
        for (int r = 0; r < h; ++r) std::memcpy(tight.data() + static_cast<std::size_t>(r) * w * pb, static_cast<const std::uint8_t*>(data) + static_cast<std::size_t>(r) * rowLength * pb, static_cast<std::size_t>(w) * pb);
        ToBGRA8(format, type, w, h, tight.data(), L.bytes);
    } else {
        ToBGRA8(format, type, w, h, data, L.bytes);
    }
    t.dirty = true;
}
void MetalGL::TexSubImage2D(GLenum, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* data) {
    Impl& m = *impl_;
    TexObject& t = m.textures[m.bound[m.activeUnit]];
    if (level >= static_cast<GLint>(t.levels.size()) || t.levels[static_cast<std::size_t>(level)].bytes.empty()) return;
    Level& L = t.levels[static_cast<std::size_t>(level)];
    const void* src = data;
    if (m.arrayBuffer == 0 && !data) return;
    std::vector<std::uint8_t> converted;
    if (data && ToBGRA8(format, type, w, h, data, converted)) {
        for (int r = 0; r < h && y + r < L.height; ++r)
            std::memcpy(L.bytes.data() + (static_cast<std::size_t>(y + r) * L.width + x) * 4, converted.data() + static_cast<std::size_t>(r) * w * 4, static_cast<std::size_t>(std::min(w, L.width - x)) * 4);
    }
    (void)src;
    t.dirty = true;
}
void MetalGL::CompressedTexImage2D(GLenum, GLint level, GLenum internalFormat, GLsizei w, GLsizei h, GLint, GLsizei size, const void* data) {
    Impl& m = *impl_;
    TexObject& t = m.textures[m.bound[m.activeUnit]];
    if (level >= static_cast<GLint>(t.levels.size())) t.levels.resize(static_cast<std::size_t>(level) + 1);
    if (level == 0) {
        t.compressed = true;
        t.format = CompressedFormat(internalFormat);
    }
    Level& L = t.levels[static_cast<std::size_t>(level)];
    L.width = w;
    L.height = h;
    L.bytes.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + size);
    t.dirty = true;
}
void MetalGL::CompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLsizei size, const void* data) {
    if (x == 0 && y == 0) CompressedTexImage2D(target, level, format, w, h, 0, size, data);   // whole-level refresh is all the game does
}

// ---- buffers and client arrays ----
void MetalGL::GenBuffers(GLsizei n, GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) {
        ids[i] = impl_->nextId++;
        impl_->buffers[ids[i]];
    }
}
void MetalGL::DeleteBuffers(GLsizei n, const GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) impl_->buffers.erase(ids[i]);
}
void MetalGL::BindBuffer(GLenum target, GLuint id) {
    if (target == gl::ARRAY_BUFFER) impl_->arrayBuffer = id;
}
void MetalGL::BufferData(GLenum target, std::ptrdiff_t size, const void* data, GLenum) {
    Impl& m = *impl_;
    if (target != gl::ARRAY_BUFFER) return;   // (pixel-unpack buffers are only a transfer path here)
    BufferObject& b = m.buffers[m.arrayBuffer];
    b.bytes.assign(static_cast<std::size_t>(size), 0);
    if (data) std::memcpy(b.bytes.data(), data, static_cast<std::size_t>(size));
}
void* MetalGL::MapBuffer(GLenum, GLenum) {
    Impl& m = *impl_;
    return m.buffers[m.arrayBuffer].bytes.data();
}
void MetalGL::UnmapBuffer(GLenum) {}
void MetalGL::GenVertexArraysAPPLE(GLsizei n, GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) ids[i] = impl_->nextId++;
}
void MetalGL::GenProgramsARB(GLsizei n, GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) ids[i] = impl_->nextId++;
}
void MetalGL::EnableClientState(GLenum array) {
    Impl& m = *impl_;
    if (array == gl::VERTEX_ARRAY) m.vertexArray.enabled = true;
    else if (array == gl::COLOR_ARRAY) m.colorArray.enabled = true;
    else if (array == gl::TEXTURE_COORD_ARRAY) m.texArray[m.clientUnit].enabled = true;
}
void MetalGL::DisableClientState(GLenum array) {
    Impl& m = *impl_;
    if (array == gl::VERTEX_ARRAY) m.vertexArray.enabled = false;
    else if (array == gl::COLOR_ARRAY) m.colorArray.enabled = false;
    else if (array == gl::TEXTURE_COORD_ARRAY) m.texArray[m.clientUnit].enabled = false;
}
void MetalGL::VertexPointer(GLint size, GLenum type, GLsizei stride, const void* p) {
    Impl& m = *impl_;
    auto& a = m.vertexArray;
    a.size = size; a.type = type; a.stride = stride; a.pointer = p; a.buffer = m.arrayBuffer;
}
void MetalGL::ColorPointer(GLint size, GLenum type, GLsizei stride, const void* p) {
    Impl& m = *impl_;
    auto& a = m.colorArray;
    a.size = size; a.type = type; a.stride = stride; a.pointer = p; a.buffer = m.arrayBuffer;
}
void MetalGL::TexCoordPointer(GLint size, GLenum type, GLsizei stride, const void* p) {
    Impl& m = *impl_;
    auto& a = m.texArray[m.clientUnit];
    a.size = size; a.type = type; a.stride = stride; a.pointer = p; a.buffer = m.arrayBuffer;
}

void MetalGL::DrawArrays(GLenum mode, GLint first, GLsizei count) { impl_->Draw(mode, first, count, nullptr, 0); }

void MetalGL::DrawElements(GLenum mode, GLsizei count, GLenum, const void* indices) {
    const auto* idx = static_cast<const std::uint16_t*>(indices);
    int maxIndex = 0;
    for (int i = 0; i < count; ++i) maxIndex = std::max<int>(maxIndex, idx[i]);
    impl_->Draw(mode, 0, maxIndex + 1, idx, count);
}

}  // namespace sgl
