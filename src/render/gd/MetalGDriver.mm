#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <simd/simd.h>

#include "render/GDIndexOps.h"
#include "render/SimGL.h"
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
constant bool kTexture0  [[function_constant(2)]];
constant bool useAlphaTest = is_function_constant_defined(kAlphaTest) ? kAlphaTest : false;
constant int  alphaFunc    = is_function_constant_defined(kAlphaFunc) ? kAlphaFunc : 7;
constant bool useTexture0  = is_function_constant_defined(kTexture0) ? kTexture0 : false;

struct DrawConstants {
    float4x4 view;
    float4x4 proj;
    uint stride;      // bytes per vertex (0x18 or 0x20)
    uint alphaRef;    // 0..255
    uint pad0, pad1;
};
struct VOut { float4 position [[position]]; float4 color; float2 uv; };

vertex VOut gd_vs(uint vid [[vertex_id]], const device uchar* v [[buffer(0)]], constant DrawConstants& c [[buffer(1)]]) {
    const device uchar* p = v + vid * c.stride;
    float3 pos = float3(*(const device packed_float3*)p);
    uchar4 col = *(const device uchar4*)(p + 12);
    VOut o;
    o.position = c.proj * (c.view * float4(pos, 1.0));
    o.color = float4(col) * (1.0 / 255.0);
    o.uv = c.stride >= 0x18 ? float2(*(const device packed_float2*)(p + 16)) : float2(0.0);
    return o;
}

fragment float4 gd_fs(VOut in [[stage_in]], constant DrawConstants& c [[buffer(1)]],
                      texture2d<float> t0 [[texture(0)]], sampler s0 [[sampler(0)]]) {
    float4 color = in.color;
    if (useTexture0) color *= t0.sample(s0, in.uv);
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

struct PipelineKey {
    bool blend, alphaTest;
    std::uint8_t src, dst, alphaFunc;
    bool texture0;
    std::uint32_t Pack() const {
        return (blend ? 1u : 0u) | (alphaTest ? 2u : 0u) | (texture0 ? 4u : 0u) | std::uint32_t(src) << 4 | std::uint32_t(dst) << 8 |
               std::uint32_t(alphaFunc) << 12;
    }
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

    std::map<std::uint32_t, id<MTLRenderPipelineState>> pipelines;
    std::map<std::uint32_t, id<MTLDepthStencilState>> depthStates;
    id<MTLRenderPipelineState> clearPipeline = nil;
    id<MTLSamplerState> sampler = nil;
    id<MTLTexture> whiteTexture = nil;
    std::uint32_t draws = 0;

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
        auto it = pipelines.find(k.Pack());
        if (it != pipelines.end()) return it->second;
        MTLFunctionConstantValues* fc = [MTLFunctionConstantValues new];
        bool alphaTest = k.alphaTest, tex0 = k.texture0;
        int alphaFunc = k.alphaFunc;
        [fc setConstantValue:&alphaTest type:MTLDataTypeBool atIndex:0];
        [fc setConstantValue:&alphaFunc type:MTLDataTypeInt atIndex:1];
        [fc setConstantValue:&tex0 type:MTLDataTypeBool atIndex:2];
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
        pipelines[k.Pack()] = p;
        return p;
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
void cMetalGDriver::Enable(eGDDriverState s) { cGDriverBase::Enable(s); }
void cMetalGDriver::Disable(eGDDriverState s) { cGDriverBase::Disable(s); }
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
    PipelineKey key{mirror_.blend, mirror_.alphaTest, static_cast<std::uint8_t>(mirror_.blendSrc),
                    static_cast<std::uint8_t>(mirror_.blendDst), static_cast<std::uint8_t>(mirror_.alphaFunc), false};
    [s.enc setRenderPipelineState:s.Pipeline(key)];
    [s.enc setDepthStencilState:s.DepthState(mirror_.depthTest, mirror_.depthFunc, mirror_.depthWrite)];
    s.SetRasterState(mirror_);
    DrawConstants dc;
    dc.view = s.view;
    dc.proj = s.proj;
    dc.stride = static_cast<std::uint32_t>(s.stride);
    dc.alphaRef = AlphaRefByte(mirror_.alphaRef);
    dc.pad0 = dc.pad1 = 0;
    [s.enc setVertexBytes:&dc length:sizeof dc atIndex:1];
    [s.enc setFragmentBytes:&dc length:sizeof dc atIndex:1];
    [s.enc setVertexBuffer:s.ring[s.ringIndex] offset:vertexOffset atIndex:0];
    [s.enc setFragmentTexture:s.whiteTexture atIndex:0];
    [s.enc setFragmentSamplerState:s.sampler atIndex:0];
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
