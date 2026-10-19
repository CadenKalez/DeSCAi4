#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <simd/simd.h>

#include "render/GDIndexOps.h"
#include "render/SimGL.h"
#include "render/TextureStageData.h"
#include "render/gd/TextureTable.h"
#include "render/gd/MetalGDriver.h"

namespace gd {

// ---------------------------------------------------------------------------------------------------------------------
// Shader. Specialisation is by function constants (RENDER_ARCH.md 8.2), not runtime branches:
//   0 kAlphaTest, 1 kAlphaFunc (eGDTestFunc), 2 kTexture0.
// Matrices arrive exactly as the game builds them (D3D row-vector convention); loading the 16 floats as columns and
// multiplying on the left reproduces v * M. Clip space is D3D's (z in [0, w]), so there is no depth remap.
// ---------------------------------------------------------------------------------------------------------------------
static NSString* const kShader = @R"MSL(
#include <metal_stdlib>
using namespace metal;

constant bool kAlphaTest [[function_constant(0)]];
constant int  kAlphaFunc [[function_constant(1)]];
constant bool useAlphaTest = is_function_constant_defined(kAlphaTest) ? kAlphaTest : false;
constant int  alphaFunc    = is_function_constant_defined(kAlphaFunc) ? kAlphaFunc : 7;
// Texture stages (D3D texture-stage operations, as cTextureStageData emits them): per stage s the function constants are
// 3+7s on, +1 colour op, +2/+3 colour args, +4 alpha op, +5/+6 alpha args.
constant bool s0On [[function_constant(3)]];  constant int s0Cop [[function_constant(4)]];  constant int s0Ca1 [[function_constant(5)]];
constant int s0Ca2 [[function_constant(6)]];  constant int s0Aop [[function_constant(7)]];  constant int s0Aa1 [[function_constant(8)]];
constant int s0Aa2 [[function_constant(9)]];
constant bool s1On [[function_constant(10)]]; constant int s1Cop [[function_constant(11)]]; constant int s1Ca1 [[function_constant(12)]];
constant int s1Ca2 [[function_constant(13)]]; constant int s1Aop [[function_constant(14)]]; constant int s1Aa1 [[function_constant(15)]];
constant int s1Aa2 [[function_constant(16)]];
constant bool stage0 = is_function_constant_defined(s0On) ? s0On : false;
constant bool stage1 = is_function_constant_defined(s1On) ? s1On : false;

struct DrawConstants {
    float4x4 view;
    float4x4 proj;
    float4 textureFactor;
    uint stride;      // bytes per vertex (0x18 or 0x20)
    uint alphaRef;    // 0..255
    uint pad0, pad1;
};
struct VOut { float4 position [[position]]; float4 color; float2 uv0; float2 uv1; };

vertex VOut gd_vs(uint vid [[vertex_id]], const device uchar* v [[buffer(0)]], constant DrawConstants& c [[buffer(1)]]) {
    const device uchar* p = v + vid * c.stride;
    float3 pos = float3(*(const device packed_float3*)p);
    uchar4 col = *(const device uchar4*)(p + 12);
    VOut o;
    o.position = c.proj * (c.view * float4(pos, 1.0));
    o.color = float4(col) * (1.0 / 255.0);
    o.uv0 = c.stride >= 0x18 ? float2(*(const device packed_float2*)(p + 16)) : float2(0.0);
    o.uv1 = c.stride >= 0x20 ? float2(*(const device packed_float2*)(p + 24)) : o.uv0;
    return o;
}

static float4 stageArg(int a, float4 diffuse, float4 current, float4 tex, float4 tf) {
    int base = a & 0xf;
    float4 v = base == 0 ? diffuse : (base == 1 ? current : (base == 2 ? tex : tf));
    if ((a & 0x10) != 0) v = 1.0 - v;          // complement
    if ((a & 0x20) != 0) v = float4(v.a);      // alpha replicate
    return v;
}
static float4 stageOp(int op, float4 a1, float4 a2, float4 current, float4 diffuse, float4 tex, float4 tf, bool colour) {
    float4 r;
    switch (op) {
    case 2: r = a1; break;
    case 3: r = a2; break;
    case 4: r = a1 * a2; break;
    case 5: r = a1 * a2 * 2.0; break;
    case 6: r = a1 * a2 * 4.0; break;
    case 7: r = a1 + a2; break;
    case 8: r = a1 + a2 - 0.5; break;
    case 9: r = (a1 + a2 - 0.5) * 2.0; break;
    case 12: r = a1 * diffuse.a + a2 * (1.0 - diffuse.a); break;
    case 13: r = a1 * tex.a + a2 * (1.0 - tex.a); break;
    case 14: r = a1 * tf.a + a2 * (1.0 - tf.a); break;
    case 16: r = a1 * current.a + a2 * (1.0 - current.a); break;
    case 24: r = float4(4.0 * dot(a1.rgb - 0.5, a2.rgb - 0.5)); break;
    default: r = current; break;
    }
    return clamp(r, 0.0, 1.0);
}
static float4 runStage(int cop, int ca1, int ca2, int aop, int aa1, int aa2, float4 diffuse, float4 current, float4 tex, float4 tf) {
    float4 out = current;
    if (cop > 1) {
        float4 c = stageOp(cop, stageArg(ca1, diffuse, current, tex, tf), stageArg(ca2, diffuse, current, tex, tf), current, diffuse, tex, tf, true);
        out.rgb = c.rgb;
    }
    if (cop > 1 && aop > 1) {
        float4 a = stageOp(aop, stageArg(aa1, diffuse, current, tex, tf), stageArg(aa2, diffuse, current, tex, tf), current, diffuse, tex, tf, false);
        out.a = a.a;
    }
    return out;
}

fragment float4 gd_fs(VOut in [[stage_in]], constant DrawConstants& c [[buffer(1)]],
                      texture2d<float> t0 [[texture(0)]], texture2d<float> t1 [[texture(1)]],
                      sampler sm0 [[sampler(0)]], sampler sm1 [[sampler(1)]]) {
    float4 diffuse = in.color;
    float4 color = diffuse;
    if (stage0) color = runStage(s0Cop, s0Ca1, s0Ca2, s0Aop, s0Aa1, s0Aa2, diffuse, color, t0.sample(sm0, in.uv0), c.textureFactor);
    if (stage1) color = runStage(s1Cop, s1Ca1, s1Ca2, s1Aop, s1Aa1, s1Aa2, diffuse, color, t1.sample(sm1, in.uv1), c.textureFactor);
    if (useAlphaTest) {
        uint a = uint(round(color.a * 255.0));
        bool pass;
        switch (alphaFunc) {
        case 0: pass = false; break;
        case 1: pass = a <  c.alphaRef; break;
        case 2: pass = a == c.alphaRef; break;
        case 3: pass = a <= c.alphaRef; break;
        case 4: pass = a >  c.alphaRef; break;
        case 5: pass = a != c.alphaRef; break;
        case 6: pass = a >= c.alphaRef; break;
        default: pass = true; break;
        }
        if (!pass) discard_fragment();
    }
    return color;
}

// Scissored clear: a full-target quad that writes one colour and depth 'depth'.
struct ClearConstants { float4 color; float depth; float pad[3]; };
struct COut { float4 position [[position]]; };
vertex COut clear_vs(uint vid [[vertex_id]], constant ClearConstants& c [[buffer(0)]]) {
    const float2 q[6] = {float2(-1,-1), float2(1,-1), float2(-1,1), float2(1,-1), float2(1,1), float2(-1,1)};
    COut o;
    o.position = float4(q[vid], c.depth, 1.0);
    return o;
}
fragment float4 clear_fs(COut in [[stage_in]], constant ClearConstants& c [[buffer(0)]]) { return c.color; }
)MSL";

namespace {

struct DrawConstants {
    simd_float4x4 view, proj;
    simd_float4 textureFactor;
    std::uint32_t stride, alphaRef, pad0, pad1;
};
struct ClearConstants {
    simd_float4 color;
    float depth, pad[3];
};

MTLBlendFactor Factor(eGDBlend b) {
    static const MTLBlendFactor table[11] = {
        MTLBlendFactorZero, MTLBlendFactorOne, MTLBlendFactorSourceColor, MTLBlendFactorOneMinusSourceColor,
        MTLBlendFactorSourceAlpha, MTLBlendFactorOneMinusSourceAlpha, MTLBlendFactorDestinationAlpha,
        MTLBlendFactorOneMinusDestinationAlpha, MTLBlendFactorDestinationColor, MTLBlendFactorOneMinusDestinationColor,
        MTLBlendFactorSourceAlphaSaturated};
    return b < 11 ? table[b] : MTLBlendFactorOne;
}
MTLCompareFunction Compare(eGDTestFunc f) {
    static const MTLCompareFunction table[8] = {MTLCompareFunctionNever, MTLCompareFunctionLess, MTLCompareFunctionEqual,
                                                MTLCompareFunctionLessEqual, MTLCompareFunctionGreater,
                                                MTLCompareFunctionNotEqual, MTLCompareFunctionGreaterEqual,
                                                MTLCompareFunctionAlways};
    return table[f & 7];
}

// One texture stage as the D3D texture-stage states describe it (cTextureStageData::Apply* emit these).
struct StageCfg {
    std::uint32_t texture = 0;
    int colorOp = 1, colorArg1 = 2, colorArg2 = 1, alphaOp = 1, alphaArg1 = 2, alphaArg2 = 1;   // the shim's start-up values
    int magFilter = 1, minFilter = 1, mipFilter = 1, addressU = 1, addressV = 1;
    bool Active() const { return colorOp > 1; }
};

struct PipelineKey {
    bool blend, alphaTest;
    std::uint8_t src, dst, alphaFunc;
    bool on[2];
    std::uint8_t ops[2][6];   // colour op, args, alpha op, args
    bool operator<(const PipelineKey& o) const { return std::memcmp(this, &o, sizeof *this) < 0; }
};

}  // namespace

struct cMetalGDriver::Impl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLLibrary> library = nil;
    CAMetalLayer* layer = nil;
    id<MTLTexture> color = nil, depth = nil;
    std::uint32_t width = 0, height = 0;
    id<MTLCommandBuffer> cb = nil;
    id<MTLRenderCommandEncoder> enc = nil;

    // pending target clears (applied as load actions when the next pass starts)
    bool clearColorPending = true, clearDepthPending = true;

    // viewport / scissor in the game's GL-style coordinates (origin bottom-left)
    std::uint32_t vx = 0, vy = 0, vw = 0, vh = 0;
    bool scissor = false;

    // matrices (modelview = target 0, projection = target 1)
    simd_float4x4 view = matrix_identity_float4x4, proj = matrix_identity_float4x4;
    eGDMatrixTarget matrixTarget = 0;

    // interleaved arrays
    eGDVertexFormat format = 0;
    int stride = 0;
    const std::uint8_t* base = nullptr;
    int error = 0;

    static constexpr int kRing = 3;
    static constexpr std::size_t kRingBytes = 8u << 20;
    id<MTLBuffer> ring[kRing] = {nil, nil, nil};
    int ringIndex = 0;
    std::size_t ringOffset = 0;

    std::map<PipelineKey, id<MTLRenderPipelineState>> pipelines;
    std::map<std::uint64_t, id<MTLSamplerState>> samplers;
    std::map<std::uint32_t, id<MTLDepthStencilState>> depthStates;
    id<MTLRenderPipelineState> clearPipeline = nil;
    id<MTLSamplerState> sampler = nil;
    id<MTLTexture> whiteTexture = nil;
    std::uint32_t draws = 0;

    // ---- textures and stages ----
    struct Backend final : ITextureBackend {
        Impl* m;
        explicit Backend(Impl* impl) : m(impl) {}
        bool CreateSurface(std::uint32_t texId, std::uint32_t format, std::uint32_t w, std::uint32_t h, std::uint32_t mips) override {
            MTLPixelFormat pf = MTLPixelFormatBGRA8Unorm;
            if (format == kFmtDXT1) pf = MTLPixelFormatBC1_RGBA;
            else if (format == kFmtDXT3) pf = MTLPixelFormatBC2_RGBA;
            else if (format == kFmtDXT5) pf = MTLPixelFormatBC3_RGBA;
            int maxLevels = 1;
            for (std::uint32_t d = std::max(w, h); d > 1; d >>= 1) ++maxLevels;
            const NSUInteger levels = std::max<NSUInteger>(1, std::min<NSUInteger>(mips, maxLevels));
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pf width:w height:h mipmapped:levels > 1];
            d.mipmapLevelCount = levels;
            d.storageMode = MTLStorageModeShared;
            d.usage = MTLTextureUsageShaderRead;
            id<MTLTexture> t = [m->device newTextureWithDescriptor:d];
            if (!t) return false;
            m->textures[texId] = t;
            return true;
        }
        void DestroySurface(std::uint32_t texId) override { m->textures.erase(texId); }
        bool UploadLevel(std::uint32_t texId, std::uint32_t level, std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h, const std::uint8_t* data, bool compressed) override {
            auto it = m->textures.find(texId);
            if (it == m->textures.end() || level >= it->second.mipmapLevelCount) return false;
            NSUInteger bytesPerRow = static_cast<NSUInteger>(w) * 4;
            if (compressed) bytesPerRow = ((w + 3) / 4) * (it->second.pixelFormat == MTLPixelFormatBC1_RGBA ? 8 : 16);
            [it->second replaceRegion:MTLRegionMake2D(x, y, w, h) mipmapLevel:level withBytes:data bytesPerRow:bytesPerRow];
            return true;
        }
    } backend{this};
    cTextureTable table{&backend};
    std::map<std::uint32_t, id<MTLTexture>> textures;
    nSGLDX7::cTextureStageData records[2];
    StageCfg cfg[2];
    bool anyStageDirty = false, bindMode = false;
    std::uint32_t stageCount = 2;

    // Applies a stage record's D3D texture-stage states to the stage configuration.
    struct StageHost final : nSGLDX7::ITextureStageHost {
        Impl* m;
        explicit StageHost(Impl* impl) : m(impl) {}
        bool IsTextureUsable(std::uint32_t id) override { return m->table.HasTexture(id); }
        void BindTexture(int stage, std::uint32_t id) override { m->cfg[stage & 1].texture = id; }
        void SetTextureStageState(int stage, int state, int value) override {
            StageCfg& c = m->cfg[stage & 1];
            switch (state) {
            case nSGLDX7::kTssColorOp: c.colorOp = value; break;
            case nSGLDX7::kTssColorArg1: c.colorArg1 = value; break;
            case nSGLDX7::kTssColorArg2: c.colorArg2 = value; break;
            case nSGLDX7::kTssAlphaOp: c.alphaOp = value; break;
            case nSGLDX7::kTssAlphaArg1: c.alphaArg1 = value; break;
            case nSGLDX7::kTssAlphaArg2: c.alphaArg2 = value; break;
            case nSGLDX7::kTssAddressU: c.addressU = value; break;
            case nSGLDX7::kTssAddressV: c.addressV = value; break;
            case nSGLDX7::kTssMagFilter: c.magFilter = value; break;
            case nSGLDX7::kTssMinFilter: c.minFilter = value; break;
            case nSGLDX7::kTssMipFilter: c.mipFilter = value; break;
            default: break;
            }
        }
    } stageHost{this};

    void ApplyDirtyStages() {
        if (!anyStageDirty) return;
        for (std::uint32_t i = 0; i < stageCount; ++i)
            if (records[i].dirty) records[i].ApplyState(static_cast<int>(i), stageHost);
        anyStageDirty = false;
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
        ringOffset = 0;
        if (wait) ringIndex = 0;
    }
    void BeginEncoder(double clearDepthValue, const float* clearRGBA) {
        if (enc) return;
        if (!cb) cb = [queue commandBuffer];
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = color;
        pass.colorAttachments[0].loadAction = clearColorPending ? MTLLoadActionClear : MTLLoadActionLoad;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(clearRGBA[0], clearRGBA[1], clearRGBA[2], clearRGBA[3]);
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.depthAttachment.texture = depth;
        pass.depthAttachment.loadAction = clearDepthPending ? MTLLoadActionClear : MTLLoadActionLoad;
        pass.depthAttachment.clearDepth = clearDepthValue;
        pass.depthAttachment.storeAction = MTLStoreActionStore;
        clearColorPending = clearDepthPending = false;
        enc = [cb renderCommandEncoderWithDescriptor:pass];
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

    id<MTLRenderPipelineState> Pipeline(const PipelineKey& k) {
        auto it = pipelines.find(k);
        if (it != pipelines.end()) return it->second;
        MTLFunctionConstantValues* fc = [MTLFunctionConstantValues new];
        bool alphaTest = k.alphaTest;
        int alphaFunc = k.alphaFunc;
        [fc setConstantValue:&alphaTest type:MTLDataTypeBool atIndex:0];
        [fc setConstantValue:&alphaFunc type:MTLDataTypeInt atIndex:1];
        for (int st = 0; st < 2; ++st) {
            bool on = k.on[st];
            const int base = 3 + st * 7;
            [fc setConstantValue:&on type:MTLDataTypeBool atIndex:base];
            int v[6];
            for (int i = 0; i < 6; ++i) v[i] = k.ops[st][i];
            for (int i = 0; i < 6; ++i) [fc setConstantValue:&v[i] type:MTLDataTypeInt atIndex:base + 1 + i];
        }
        NSError* err = nil;
        MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = [library newFunctionWithName:@"gd_vs" constantValues:fc error:&err];
        d.fragmentFunction = [library newFunctionWithName:@"gd_fs" constantValues:fc error:&err];
        d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        if (k.blend) {
            d.colorAttachments[0].blendingEnabled = YES;
            d.colorAttachments[0].sourceRGBBlendFactor = d.colorAttachments[0].sourceAlphaBlendFactor = Factor(k.src);
            d.colorAttachments[0].destinationRGBBlendFactor = d.colorAttachments[0].destinationAlphaBlendFactor = Factor(k.dst);
        }
        id<MTLRenderPipelineState> p = [device newRenderPipelineStateWithDescriptor:d error:&err];
        if (!p) NSLog(@"cMetalGDriver pipeline error: %@", err);
        pipelines[k] = p;
        return p;
    }
    id<MTLSamplerState> Sampler(const StageCfg& c) {
        const std::uint64_t key = std::uint64_t(c.magFilter) | std::uint64_t(c.minFilter) << 4 | std::uint64_t(c.mipFilter) << 8 |
                                  std::uint64_t(c.addressU) << 12 | std::uint64_t(c.addressV) << 16;
        auto it = samplers.find(key);
        if (it != samplers.end()) return it->second;
        MTLSamplerDescriptor* d = [MTLSamplerDescriptor new];
        d.magFilter = c.magFilter == 2 ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.minFilter = c.minFilter == 2 ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.mipFilter = c.mipFilter == 3 ? MTLSamplerMipFilterLinear : (c.mipFilter == 2 ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterNotMipmapped);
        d.sAddressMode = c.addressU == 3 ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
        d.tAddressMode = c.addressV == 3 ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
        id<MTLSamplerState> st = [device newSamplerStateWithDescriptor:d];
        samplers[key] = st;
        return st;
    }
    id<MTLDepthStencilState> DepthState(bool test, eGDTestFunc func, bool write) {
        const std::uint32_t key = (test ? 1u : 0u) | (write ? 2u : 0u) | (func & 7) << 2;
        auto it = depthStates.find(key);
        if (it != depthStates.end()) return it->second;
        MTLDepthStencilDescriptor* d = [MTLDepthStencilDescriptor new];
        d.depthCompareFunction = test ? Compare(func) : MTLCompareFunctionAlways;
        d.depthWriteEnabled = test && write;   // depth is only written while the depth test is on
        id<MTLDepthStencilState> s = [device newDepthStencilStateWithDescriptor:d];
        depthStates[key] = s;
        return s;
    }
    std::uint8_t* Ring(std::size_t bytes, std::size_t* offset) {
        ringOffset = (ringOffset + 255) & ~std::size_t{255};
        if (!ring[ringIndex]) ring[ringIndex] = [device newBufferWithLength:kRingBytes options:MTLResourceStorageModeShared];
        if (ringOffset + bytes > kRingBytes) {
            Submit(true);
            if (!ring[ringIndex]) ring[ringIndex] = [device newBufferWithLength:kRingBytes options:MTLResourceStorageModeShared];
        }
        *offset = ringOffset;
        ringOffset += bytes;
        return static_cast<std::uint8_t*>([ring[ringIndex] contents]) + *offset;
    }

    void SetRasterState(const StateMirror& m) {
        [enc setCullMode:m.cull ? MTLCullModeBack : MTLCullModeNone];
        [enc setFrontFacingWinding:MTLWindingCounterClockwise];   // Enable(CullFace) = D3DCULL_CW -> GL FrontFace(CCW) + cull back
        MTLViewport vp = {double(vx), double(int(height) - int(vy + vh)), double(vw), double(vh), 0.0, 1.0};
        [enc setViewport:vp];
        if (scissor) {
            MTLScissorRect sr = {vx, height - (vy + vh), vw, vh};
            [enc setScissorRect:sr];
        } else {
            MTLScissorRect sr = {0, 0, width, height};
            [enc setScissorRect:sr];
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------------

cMetalGDriver::cMetalGDriver() : impl_(new Impl) {}
cMetalGDriver::~cMetalGDriver() {
    if (impl_) impl_->Submit(false);   // never release an open encoder
}

std::unique_ptr<cMetalGDriver> cMetalGDriver::Create(void* caMetalLayer, std::uint32_t width, std::uint32_t height) {
    std::unique_ptr<cMetalGDriver> d(new cMetalGDriver);
    Impl& m = *d->impl_;
    m.device = MTLCreateSystemDefaultDevice();
    if (!m.device) return nullptr;
    m.queue = [m.device newCommandQueue];
    NSError* err = nil;
    m.library = [m.device newLibraryWithSource:kShader options:nil error:&err];
    if (!m.library) {
        NSLog(@"cMetalGDriver shader error: %@", err);
        return nullptr;
    }
    m.layer = (__bridge CAMetalLayer*)caMetalLayer;
    m.width = width;
    m.height = height;
    m.vw = width;
    m.vh = height;
    m.MakeTargets();
    MTLRenderPipelineDescriptor* cp = [MTLRenderPipelineDescriptor new];
    cp.vertexFunction = [m.library newFunctionWithName:@"clear_vs"];
    cp.fragmentFunction = [m.library newFunctionWithName:@"clear_fs"];
    cp.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    cp.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    m.clearPipeline = [m.device newRenderPipelineStateWithDescriptor:cp error:&err];
    MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
    m.sampler = [m.device newSamplerStateWithDescriptor:sd];
    MTLTextureDescriptor* wd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:1 height:1 mipmapped:NO];
    m.whiteTexture = [m.device newTextureWithDescriptor:wd];
    m.table.Init();
    const std::uint32_t white = 0xffffffff;
    [m.whiteTexture replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&white bytesPerRow:4];
    return d;
}

std::uint32_t cMetalGDriver::DrawCalls() const { return impl_->draws; }
std::uint32_t cMetalGDriver::PipelineCount() const { return static_cast<std::uint32_t>(impl_->pipelines.size()); }

// ---- state ----
void cMetalGDriver::ColorMask(bool enable) { cGDriverBase::ColorMask(enable); }   // the shipped driver only flushes here
void cMetalGDriver::DepthFunc(eGDTestFunc f) { cGDriverBase::DepthFunc(f); }
void cMetalGDriver::DepthMask(bool e) { cGDriverBase::DepthMask(e); }
void cMetalGDriver::BlendFunc(eGDBlend s, eGDBlend d) { cGDriverBase::BlendFunc(s, d); }
void cMetalGDriver::AlphaFunc(eGDTestFunc f, float ref) { cGDriverBase::AlphaFunc(f, ref); }
void cMetalGDriver::Enable(eGDDriverState s) {
    cGDriverBase::Enable(s);
    if (s == state::Texturing) MarkStage(true);
}
void cMetalGDriver::Disable(eGDDriverState s) {
    cGDriverBase::Disable(s);
    if (s == state::Texturing) MarkStage(false);
}

// ---- textures (nSGLDX7::cGDriver slots; the stage records are the shipped cTextureStageData) ----
void cMetalGDriver::MarkStage(bool enabled) {
    Impl& m = *impl_;
    auto& r = m.records[mirror_.currentStage & 1];
    r.textureEnabled = enabled ? 1 : 0;
    r.dirty = 1;
    m.anyStageDirty = true;
}

void cMetalGDriver::TexStage(std::uint32_t stage) {
    if (stage < impl_->stageCount) cGDriverBase::TexStage(stage);
    else impl_->error = 2;
}

void cMetalGDriver::GenTextures(int count, std::uint32_t* ids) {
    for (int i = 0; i < count; ++i) ids[i] = impl_->table.NewTexture();
}
void cMetalGDriver::DeleteTextures(int count, const std::uint32_t* ids) {
    for (int i = 0; i < count; ++i)
        if (ids[i] != 0) impl_->table.FreeTexture(ids[i]);
}
bool cMetalGDriver::IsTexture(std::uint32_t id) { return impl_->table.IsValid(id); }

void cMetalGDriver::BindTexture(eGDTextureTarget target, std::uint32_t id) {
    Impl& m = *impl_;
    if (target != 0) {
        m.error = 1;
        return;
    }
    m.bindMode = true;
    TextureParams p;
    if (!m.table.GetTextureParams(id, p)) m.table.GetTextureParams(0, p);
    auto& r = m.records[mirror_.currentStage & 1];
    m.anyStageDirty = true;
    r.dirty = 1;
    r.appliedEnvMode = 0xffffffffu;
    r.magFilter = p.magFilter;
    r.minFilter = p.minFilter;
    r.wrapS = p.wrapS;
    r.wrapT = p.wrapT;
    r.textureId = id;
    r.textureHasAlpha = m.table.HasAlpha(id) ? 1 : 0;
}

void cMetalGDriver::SetTexture(std::uint32_t id, std::uint32_t stage) {
    Impl& m = *impl_;
    auto& r = m.records[stage & 1];
    if (r.textureId == id) return;
    m.bindMode = false;
    m.anyStageDirty = true;
    r.dirty = 1;
    r.appliedEnvMode = 0xffffffffu;
    r.textureId = id;
    r.textureHasAlpha = m.table.HasAlpha(id) ? 1 : 0;
}
std::uint32_t cMetalGDriver::GetTexture(std::uint32_t stage) const { return impl_->records[stage & 1].textureId; }

void cMetalGDriver::TexParameter(eGDTextureTarget, eGDTextureParamType param, eGDTextureParam value) {
    Impl& m = *impl_;
    auto& r = m.records[mirror_.currentStage & 1];
    if (m.bindMode && !m.table.SetTextureParam(r.textureId, param, value)) {
        m.error = 1;
        return;
    }
    std::uint32_t* field = nullptr;
    switch (param) {
    case 0: field = &r.magFilter; break;
    case 1: field = &r.minFilter; break;
    case 2: field = &r.wrapS; break;
    case 3: field = &r.wrapT; break;
    default: break;
    }
    if (field) {
        if (*field == value) return;
        *field = value;
    }
    m.anyStageDirty = true;
    r.dirty = 1;
}

void cMetalGDriver::TexEnv(eGDTextureEnvTarget target, eGDTextureEnvParamType param, eGDTextureEnvModeParam mode) {
    cGDriverBase::TexEnv(target, param, mode);   // keeps the mirror the render-state assertion checks
    Impl& m = *impl_;
    auto& r = m.records[mirror_.currentStage & 1];
    m.anyStageDirty = true;
    r.dirty = 1;
    r.envMode = mode;
}

std::uint32_t cMetalGDriver::CreateTexture(eGDInternalTextureFormat format, std::uint32_t width, std::uint32_t height, std::uint32_t mips, eGDTextureHintFlags hint) {
    Impl& m = *impl_;
    const std::uint32_t id = m.table.NewTexture();
    if (!m.table.CreateTexture(id, hint, format, width, height, mips)) m.error = 3;
    return id;
}

void cMetalGDriver::LoadTextureLevel(std::uint32_t id, std::uint32_t level, std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
                                     eGDTextureFormat format, eGDType type, std::uint32_t rowLength, const void* pixels) {
    Impl& m = *impl_;
    if (!m.table.LoadTextureLevel(id, level, x, y, w, h, format, type, rowLength, pixels)) m.error = 3;
}
void cMetalGDriver::ClearColor(float r, float g, float b, float a) { cGDriverBase::ClearColor(r, g, b, a); }
void cMetalGDriver::Flush() {}   // draws are encoded immediately; nothing is queued
int cMetalGDriver::GetError() { int e = impl_->error; impl_->error = 0; return e; }

int cMetalGDriver::VertexFormatStride(eGDVertexFormat f) const { return nRZSimGL::StandardVFStride(f); }

void cMetalGDriver::MatrixMode(eGDMatrixTarget t) { impl_->matrixTarget = t; }
void cMetalGDriver::LoadMatrix(const float* m) {
    Impl& s = *impl_;
    simd_float4x4 mat;
    std::memcpy(&mat, m, sizeof mat);
    if (s.matrixTarget == 0) s.view = mat;
    else if (s.matrixTarget == 1) s.proj = mat;   // other targets are ignored by the shipped driver
}
void cMetalGDriver::LoadIdentity() {
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    LoadMatrix(identity);
}

void cMetalGDriver::SetViewport() {
    Impl& s = *impl_;
    s.vx = s.vy = 0;
    s.vw = s.width;
    s.vh = s.height;
    s.scissor = false;
}
void cMetalGDriver::SetViewport(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
    Impl& s = *impl_;
    s.vx = x; s.vy = y; s.vw = w; s.vh = h;
    s.scissor = true;
}
void cMetalGDriver::GetViewport(std::uint32_t out[4]) {
    const Impl& s = *impl_;
    out[0] = s.vx; out[1] = s.vy; out[2] = s.vx + s.vw; out[3] = s.vy + s.vh;   // (x, y, x+w, y+h)
}

// ---- clear ----
// The shipped driver clears with D3D Clear(): colour from ClearColor (quantised to 8 bits), depth from ClearDepth. The
// shim implements it with glClear, so an active scissor (SetViewport(x,y,w,h)) limits it; colour/depth write masks and the
// depth test do not matter.
void cMetalGDriver::Clear(std::uint32_t mask) {
    Impl& s = *impl_;
    const bool doColor = (mask & kClearColor) != 0, doDepth = (mask & kClearDepth) != 0;
    if (!doColor && !doDepth) return;
    const std::uint32_t argb = mirror_.clearColor;
    const float rgba[4] = {float(argb >> 16 & 0xff) / 255.0f, float(argb >> 8 & 0xff) / 255.0f, float(argb & 0xff) / 255.0f,
                           float(argb >> 24) / 255.0f};
    const bool full = !s.scissor || (s.vx == 0 && s.vy == 0 && s.vw >= s.width && s.vh >= s.height);
    if (full) {
        s.EndEncoder();
        if (doColor) s.clearColorPending = true;
        if (doDepth) s.clearDepthPending = true;
        // The pass that carries the load action starts lazily; remember the values it will need.
        pendingClearRGBA_[0] = rgba[0]; pendingClearRGBA_[1] = rgba[1]; pendingClearRGBA_[2] = rgba[2]; pendingClearRGBA_[3] = rgba[3];
        pendingClearDepth_ = mirror_.clearDepth;
        return;
    }
    s.BeginEncoder(pendingClearDepth_, pendingClearRGBA_);
    ClearConstants cc;
    cc.color = simd_make_float4(rgba[0], rgba[1], rgba[2], rgba[3]);
    cc.depth = mirror_.clearDepth;
    [s.enc setRenderPipelineState:s.clearPipeline];
    MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = MTLCompareFunctionAlways;
    dd.depthWriteEnabled = doDepth;
    [s.enc setDepthStencilState:[s.device newDepthStencilStateWithDescriptor:dd]];
    [s.enc setCullMode:MTLCullModeNone];
    s.SetRasterState(mirror_);
    [s.enc setVertexBytes:&cc length:sizeof cc atIndex:0];
    [s.enc setFragmentBytes:&cc length:sizeof cc atIndex:0];
    // (colour writes are masked off when only depth is cleared by using a zero-alpha-write pipeline is not needed here:
    //  the scissored depth-only clear is not used by the game)
    [s.enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    ++s.draws;
}

// ---- drawing ----
void cMetalGDriver::InterleavedArrays(eGDVertexFormat format, int stride, const void* data) {
    Impl& s = *impl_;
    s.format = format;
    const int natural = nRZSimGL::StandardVFStride(format);
    s.stride = stride == 0 ? natural : stride;
    s.base = static_cast<const std::uint8_t*>(data);
}

void cMetalGDriver::DrawArrays(eGDPrimType mode, int first, int count) {
    Impl& s = *impl_;
    if (count < 1 || mode > 7 || !s.base) return;
    if (s.format != vf::V3F_C4UB_T2F && s.format != vf::V3F_C4UB_2T2F) {
        Unsupported("DrawArrays(vertex format)");
        return;
    }
    if (mode == prim::Points || mode == prim::Lines || mode == prim::LineStrip) {
        Unsupported("DrawArrays(points/lines)");
        return;
    }
    std::size_t vOff;
    std::uint8_t* v = s.Ring(static_cast<std::size_t>(count) * s.stride, &vOff);
    std::memcpy(v, s.base + static_cast<std::size_t>(first) * s.stride, static_cast<std::size_t>(count) * s.stride);
    std::vector<std::uint16_t> indices;
    MTLPrimitiveType type = MTLPrimitiveTypeTriangle;
    switch (mode) {
    case prim::Triangles: break;
    case prim::TriangleStrip: type = MTLPrimitiveTypeTriangleStrip; break;
    case prim::TriangleFan:
        indices.resize(static_cast<std::size_t>(count - 2) * 3);
        nSGLDX7::GenerateIndexedTriListForTriangleFan(static_cast<int>(indices.size()), indices.data());
        break;
    case prim::Quads:
        indices.resize(static_cast<std::size_t>(count >> 1) * 3);
        nSGLDX7::GenerateIndexedTriListForQuads(count, indices.data());
        break;
    case prim::QuadStrip:
        indices.resize(static_cast<std::size_t>(count - 2) * 3);
        nSGLDX7::GenerateIndexedTriListForQuadStrip(count, indices.data());
        break;
    }
    Encode(type, vOff, count, indices.empty() ? nullptr : indices.data(), static_cast<int>(indices.size()));
}

void cMetalGDriver::DrawElements(eGDPrimType mode, int count, eGDType indexType, const void* indices) {
    Impl& s = *impl_;
    if (count < 1 || mode > 7 || !s.base) return;
    if (s.format != vf::V3F_C4UB_T2F && s.format != vf::V3F_C4UB_2T2F) {
        Unsupported("DrawElements(vertex format)");
        return;
    }
    if (mode != prim::Triangles || (indexType != 1 && indexType != 3 && indexType != 5)) {
        Unsupported("DrawElements(mode/index type)");
        return;
    }
    std::vector<std::uint32_t> idx(static_cast<std::size_t>(count));
    std::uint32_t lo = 0xffffffff, hi = 0;
    for (int i = 0; i < count; ++i) {
        std::uint32_t v = indexType == 1 ? static_cast<const std::uint8_t*>(indices)[i]
                          : indexType == 3 ? static_cast<const std::uint16_t*>(indices)[i]
                                           : static_cast<const std::uint32_t*>(indices)[i];
        idx[static_cast<std::size_t>(i)] = v;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    const int vertexCount = static_cast<int>(hi - lo + 1);
    std::size_t vOff;
    std::uint8_t* v = s.Ring(static_cast<std::size_t>(vertexCount) * s.stride, &vOff);
    std::memcpy(v, s.base + static_cast<std::size_t>(lo) * s.stride, static_cast<std::size_t>(vertexCount) * s.stride);
    std::vector<std::uint16_t> rebased(idx.size());
    for (std::size_t i = 0; i < idx.size(); ++i) rebased[i] = static_cast<std::uint16_t>(idx[i] - lo);
    Encode(MTLPrimitiveTypeTriangle, vOff, vertexCount, rebased.data(), count);
}

void cMetalGDriver::Encode(int primitive, std::size_t vertexOffset, int vertexCount, const std::uint16_t* indices, int indexCount) {
    Impl& s = *impl_;
    s.BeginEncoder(pendingClearDepth_, pendingClearRGBA_);
    s.ApplyDirtyStages();
    PipelineKey key{};
    key.blend = mirror_.blend;
    key.alphaTest = mirror_.alphaTest;
    key.src = static_cast<std::uint8_t>(mirror_.blendSrc);
    key.dst = static_cast<std::uint8_t>(mirror_.blendDst);
    key.alphaFunc = static_cast<std::uint8_t>(mirror_.alphaFunc);
    for (int st = 0; st < 2; ++st) {
        const StageCfg& c = s.cfg[st];
        const bool on = c.Active() && s.table.HasTexture(c.texture) && s.textures.count(c.texture);
        key.on[st] = on;
        if (on) {
            const std::uint8_t ops[6] = {std::uint8_t(c.colorOp), std::uint8_t(c.colorArg1), std::uint8_t(c.colorArg2),
                                         std::uint8_t(c.alphaOp), std::uint8_t(c.alphaArg1), std::uint8_t(c.alphaArg2)};
            std::memcpy(key.ops[st], ops, 6);
        }
    }
    [s.enc setRenderPipelineState:s.Pipeline(key)];
    [s.enc setDepthStencilState:s.DepthState(mirror_.depthTest, mirror_.depthFunc, mirror_.depthWrite)];
    s.SetRasterState(mirror_);
    DrawConstants dc;
    dc.view = s.view;
    dc.proj = s.proj;
    dc.textureFactor = simd_make_float4(1, 1, 1, 1);
    dc.stride = static_cast<std::uint32_t>(s.stride);
    dc.alphaRef = AlphaRefByte(mirror_.alphaRef);
    dc.pad0 = dc.pad1 = 0;
    [s.enc setVertexBytes:&dc length:sizeof dc atIndex:1];
    [s.enc setFragmentBytes:&dc length:sizeof dc atIndex:1];
    [s.enc setVertexBuffer:s.ring[s.ringIndex] offset:vertexOffset atIndex:0];
    for (int st = 0; st < 2; ++st) {
        id<MTLTexture> tex = s.whiteTexture;
        if (key.on[st]) tex = s.textures[s.cfg[st].texture];
        [s.enc setFragmentTexture:tex atIndex:st];
        [s.enc setFragmentSamplerState:(key.on[st] ? s.Sampler(s.cfg[st]) : s.sampler) atIndex:st];
    }
    const MTLPrimitiveType type = static_cast<MTLPrimitiveType>(primitive);
    if (indices) {
        std::size_t iOff;
        std::memcpy(s.Ring(static_cast<std::size_t>(indexCount) * 2, &iOff), indices, static_cast<std::size_t>(indexCount) * 2);
        [s.enc drawIndexedPrimitives:type indexCount:indexCount indexType:MTLIndexTypeUInt16 indexBuffer:s.ring[s.ringIndex] indexBufferOffset:iOff];
    } else {
        [s.enc drawPrimitives:type vertexStart:0 vertexCount:vertexCount];
    }
    ++s.draws;
}

// ---- presentation ----
void cMetalGDriver::Present() {
    Impl& s = *impl_;
    s.EndEncoder();
    if (s.layer && s.cb) {
        id<CAMetalDrawable> drawable = [s.layer nextDrawable];
        if (drawable) {
            id<MTLBlitCommandEncoder> blit = [s.cb blitCommandEncoder];
            const NSUInteger w = std::min<NSUInteger>(s.width, drawable.texture.width), h = std::min<NSUInteger>(s.height, drawable.texture.height);
            [blit copyFromTexture:s.color sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                        toTexture:drawable.texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
            [s.cb presentDrawable:drawable];
        }
    }
    s.Submit(false);
    s.ringIndex = (s.ringIndex + 1) % Impl::kRing;
    s.ringOffset = 0;
}

bool cMetalGDriver::ReadBack(std::vector<std::uint8_t>& bgra, std::uint32_t& width, std::uint32_t& height) {
    Impl& s = *impl_;
    if (s.clearColorPending || s.clearDepthPending) s.BeginEncoder(pendingClearDepth_, pendingClearRGBA_);
    s.Submit(true);
    const NSUInteger rowBytes = static_cast<NSUInteger>(s.width) * 4;
    id<MTLBuffer> staging = [s.device newBufferWithLength:rowBytes * s.height options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [s.queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:s.color sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(s.width, s.height, 1)
                 toBuffer:staging destinationOffset:0 destinationBytesPerRow:rowBytes destinationBytesPerImage:rowBytes * s.height];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    width = s.width;
    height = s.height;
    bgra.assign(static_cast<const std::uint8_t*>([staging contents]), static_cast<const std::uint8_t*>([staging contents]) + rowBytes * s.height);
    return true;
}

}  // namespace gd
