#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <simd/simd.h>

#include "render/GDIndexOps.h"
#include "render/SimGL.h"
#include "render/TextureStageData.h"
#include "render/gd/TextureTable.h"
#include "render/dx7/dx7_to_metal_bridge.h"
#include "render/gd/MetalGDriver.h"

namespace gd {

// The shader is src/render/dx7/DX7Emulator.metal (embedded by scripts/gen_dx7_shader.py). Specialisation is by function
// constants: 0 kAlphaTest, 1 kAlphaFunc (eGDTestFunc), 3+7s per texture stage (see the shader). State travels in one
// dx7::MetalRenderState constant block recorded by dx7::StateRecorder.
#include "render/dx7/DX7EmulatorSource.inc"
static NSString* const kShader = [NSString stringWithUTF8String:kDX7EmulatorSource];

namespace {

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

    // per-stage texture coordinate generation (TexStageCoord / TexStageMatrix) and the colour multiplier
    float colorMulRgba[4] = {1, 1, 1, 1};
    simd_float4x4 View() const { simd_float4x4 m; const dx7::MetalRenderState st = recorder.Snapshot(); std::memcpy(&m, st.view, sizeof m); return m; }
    simd_float4x4 Proj() const { simd_float4x4 m; const dx7::MetalRenderState st = recorder.Snapshot(); std::memcpy(&m, st.projection, sizeof m); return m; }
    dx7::StateRecorder recorder;   // transforms, texture factor, colour multiplier, texture matrices / coordinate sources

    // pending target clears (applied as load actions when the next pass starts)
    bool clearColorPending = true, clearDepthPending = true;
    float pendingRGBA[4] = {0, 0, 0, 0};
    double pendingDepth = 1.0;

    // viewport / scissor in the game's GL-style coordinates (origin bottom-left)
    std::uint32_t vx = 0, vy = 0, vw = 0, vh = 0;
    bool scissor = false;

    // matrices (modelview = target 0, projection = target 1)
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
    std::unique_ptr<cVideoModeList> modeList;

    // ---- backing store: persistent colour + depth copies of the frame ----
    struct Store final : IBackingStore {
        Impl* m;
        id<MTLTexture> color = nil, depth = nil;
        explicit Store(Impl* impl) : m(impl) {}
        bool Create(std::uint32_t w, std::uint32_t h) override {
            if (w == 0 || h == 0) return false;
            Destroy();
            MTLTextureDescriptor* c = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
            c.storageMode = MTLStorageModePrivate;
            c.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
            MTLTextureDescriptor* z = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:w height:h mipmapped:NO];
            z.storageMode = MTLStorageModePrivate;
            z.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
            color = [m->device newTextureWithDescriptor:c];
            depth = [m->device newTextureWithDescriptor:z];
            return color != nil && depth != nil;
        }
        void Destroy() override { color = nil; depth = nil; }
        bool Exists() const override { return color != nil; }
        void Copy(bool toStore, int sx, int sy, int w, int h, int dx, int dy) {
            if (!Exists() || w <= 0 || h <= 0) return;
            // clip both blocks to their textures
            id<MTLTexture> src = toStore ? m->color : color, srcZ = toStore ? m->depth : depth;
            id<MTLTexture> dst = toStore ? color : m->color, dstZ = toStore ? depth : m->depth;
            if (sx < 0 || sy < 0 || dx < 0 || dy < 0) return;
            w = std::min<int>(w, std::min<int>(src.width - sx, dst.width - dx));
            h = std::min<int>(h, std::min<int>(src.height - sy, dst.height - dy));
            if (w <= 0 || h <= 0) return;
            m->EndEncoder();
            if (!m->cb) m->cb = [m->queue commandBuffer];
            if (m->clearColorPending || m->clearDepthPending) {   // materialise a pending clear before copying from the frame
                // (the pending clear is applied by a pass that draws nothing)
                m->BeginEncoder(m->pendingDepth, m->pendingRGBA);
                m->EndEncoder();
            }
            id<MTLBlitCommandEncoder> blit = [m->cb blitCommandEncoder];
            [blit copyFromTexture:src sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(sx, sy, 0) sourceSize:MTLSizeMake(w, h, 1)
                        toTexture:dst destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(dx, dy, 0)];
            [blit copyFromTexture:srcZ sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(sx, sy, 0) sourceSize:MTLSizeMake(w, h, 1)
                        toTexture:dstZ destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(dx, dy, 0)];
            [blit endEncoding];
        }
        void SaveFromFrame(int sx, int sy, int w, int h, int dx, int dy) override { Copy(true, sx, sy, w, h, dx, dy); }
        void RestoreToFrame(int sx, int sy, int w, int h, int dx, int dy) override { Copy(false, sx, sy, w, h, dx, dy); }
    } store{this};
    sGDMode currentMode{};
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
        if (std::getenv("SC4_GDNODEPTH")) { test = false; write = false; }
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
        [enc setCullMode:(m.cull && !std::getenv("SC4_GDNOCULL")) ? MTLCullModeBack : MTLCullModeNone];
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

cMetalGDriver::cMetalGDriver() : impl_(new Impl) { impl_->currentMode.id = -1; }
cMetalGDriver::~cMetalGDriver() {
    if (impl_) impl_->Submit(false);   // never release an open encoder
}

std::unique_ptr<cMetalGDriver> cMetalGDriver::Create(void* caMetalLayer, std::uint32_t width, std::uint32_t height, IModeSource* modes) {
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
    // The mode the drawable already is: a Metal device is always the "hardware driver" (the application reads byte 0 of +0x30 as
    // "accelerated" and would otherwise fall back to the software one); +0x2e, the 2-D blit capability, stays 0.
    m.currentMode = sGDMode{};
    m.currentMode.id = 0;
    m.currentMode.width = width;
    m.currentMode.height = height;
    m.currentMode.bitsPerPixel = 32;
    m.currentMode.flags[0] = 1u << 16;   // byte 2 = 1 for every entry; byte 0 (full screen) = 0: windowed
    m.currentMode.reserved30 = 1;
    if (modes) m.modeList = std::make_unique<cVideoModeList>(modes);
    const std::uint32_t white = 0xffffffff;
    [m.whiteTexture replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&white bytesPerRow:4];
    return d;
}

IBackingStore* cMetalGDriver::BackingStore() { return &impl_->store; }

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

void cMetalGDriver::TexStageCoord(eGDTexCoordSource source) {
    // SGLDX7Driver: the table index is ((source & 8) << 28) | (source >> 4): 0 = the vertex's own coordinate set, 1 = camera-space position
    const std::uint32_t index = ((source & 8) << 28) | (source >> 4);
    if (std::getenv("SC4_GDTRACE")) std::fprintf(stderr, "gd: TexStageCoord stage %u source %#x -> index %u\n", mirror_.currentStage, source, index);
    if (impl_->stageCount > mirror_.currentStage && mirror_.currentStage < 2) impl_->recorder.SetStageCameraSpaceCoords(mirror_.currentStage, index == 1);
}

void cMetalGDriver::TexStageMatrix(const float* m, std::uint32_t, std::uint32_t, eGDTexMatFlags) {
    const std::uint32_t stage = mirror_.currentStage;
    if (std::getenv("SC4_GDTRACE")) { if (m) std::fprintf(stderr, "gd: TexStageMatrix stage %u [%g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g]\n", stage, m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11],m[12],m[13],m[14],m[15]); else std::fprintf(stderr, "gd: TexStageMatrix stage %u off\n", stage); }
    if (stage >= 2) return;
    impl_->recorder.SetStageTransform(stage, m);
}

void cMetalGDriver::ColorMultiplier(float r, float g, float b) { impl_->colorMulRgba[0] = r; impl_->colorMulRgba[1] = g; impl_->colorMulRgba[2] = b; impl_->recorder.SetColorMultiplier(impl_->colorMulRgba); }
void cMetalGDriver::AlphaMultiplier(float a) { impl_->colorMulRgba[3] = a; impl_->recorder.SetColorMultiplier(impl_->colorMulRgba); }

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

// The texture environment colour: the texture factor the stage operations read as their TFACTOR argument
// (cGDriver::TexEnv(float*) sets D3DRS_TEXTUREFACTOR from it).
void cMetalGDriver::TexEnv(eGDTextureEnvTarget, eGDTextureEnvParamType, const float* values) {
    impl_->recorder.SetTextureFactor(values);
}

// cGDriver::SetCombiner (the DX7 driver's decoding): the stage switches to the combiner environment (mode 4) and takes
// the record's colour / alpha operations, sources, operands and scales.
void cMetalGDriver::SetCombiner(const cGDCombiner& combiner, std::uint32_t stageIndex) {
    Impl& m = *impl_;
    auto& stage = m.records[stageIndex & 1];
    stage.envMode = 4;
    stage.dirty = 1;
    stage.appliedEnvMode = 0xffffffffu;
    m.anyStageDirty = true;
    const auto* raw = reinterpret_cast<const std::uint8_t*>(&combiner);
    if (raw[0] == 0xff) return;
    // the two operand tables of SetCombiner (0x100a26770 / 0x100a26780, nSGLDX7::lookup in the DX7 driver)
    static constexpr std::uint32_t kColorOperand[4] = {2, 3, 4, 5}, kAlphaOperand[4] = {4, 5, 4, 5};
    stage.rgbCombine = raw[0];
    stage.alphaCombine = raw[4];
    for (int i = 0; i < 3; ++i) {
        stage.rgbSource[i] = raw[1 + i] & 0xf;
        stage.alphaSource[i] = raw[5 + i] & 0xf;
        stage.rgbOperand[i] = kColorOperand[(raw[1 + i] >> 4) & 3];
        stage.alphaOperand[i] = kAlphaOperand[(raw[5 + i] >> 4) & 3];
    }
    stage.rgbScale = raw[8];
    stage.alphaScale = raw[9];
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
    if (s.matrixTarget == 0) s.recorder.SetTransform(dx7::kTransformView, m);
    else if (s.matrixTarget == 1) s.recorder.SetTransform(dx7::kTransformProjection, m);   // other targets are ignored by the shipped driver
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
    if (std::getenv("SC4_GDTRACE")) std::fprintf(stderr, "gd: SetViewport %u %u %u %u\n", x, y, w, h);
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
    if (std::getenv("SC4_GDTRACE")) std::fprintf(stderr, "gd: Clear %#x scissor %d vp %u %u %u %u\n", mask, int(s.scissor), s.vx, s.vy, s.vw, s.vh);
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
        impl_->pendingRGBA[0] = rgba[0]; impl_->pendingRGBA[1] = rgba[1]; impl_->pendingRGBA[2] = rgba[2]; impl_->pendingRGBA[3] = rgba[3];
        impl_->pendingDepth = mirror_.clearDepth;
        return;
    }
    s.BeginEncoder(impl_->pendingDepth, impl_->pendingRGBA);
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
    if (std::getenv("SC4_GDTRACE")) std::fprintf(stderr, "gd: DrawArrays mode %u first %d count %d format %u\n", unsigned(mode), first, count, unsigned(s.format));
    if (count < 1 || mode > 7 || !s.base) return;
    if ((s.format != vf::V3F_C4UB && s.format != vf::V3F_C4UB_T2F && s.format != vf::V3F_C4UB_2T2F)) {
        { char note[64]; std::snprintf(note, sizeof note, "DrawArrays(vertex format %#x)", unsigned(s.format)); Unsupported(note); }
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
    if (std::getenv("SC4_GDTRACE") && s.base && count > 0) {
        static int shown = 0;
        if (shown++ < 3 || (s.format != vf::V3F_C4UB_2T2F && shown < 400)) {
            const float* p = reinterpret_cast<const float*>(s.base);
            simd_float4 clip = simd_mul(s.Proj(), simd_mul(s.View(), simd_make_float4(p[0], p[1], p[2], 1.0f)));
            const std::uint8_t* c = s.base + 12;
            std::fprintf(stderr, "gd: first vertex %.1f %.1f %.1f color %02x%02x%02x%02x uv %.3f %.3f -> clip %.3f %.3f %.3f %.3f\n", p[0], p[1], p[2], c[0], c[1], c[2], c[3], s.stride >= 24 ? p[4] : 0.f, s.stride >= 24 ? p[5] : 0.f, clip.x, clip.y, clip.z, clip.w);
            const simd_float4x4 vm = s.View(), pm = s.Proj(); const float* v = reinterpret_cast<const float*>(&vm); const float* q = reinterpret_cast<const float*>(&pm);
            std::fprintf(stderr, "gd: view %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f\n", v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12],v[13],v[14],v[15]);
            std::fprintf(stderr, "gd: proj %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f\n", q[0],q[1],q[2],q[3],q[4],q[5],q[6],q[7],q[8],q[9],q[10],q[11],q[12],q[13],q[14],q[15]);
        }
    }
    if (std::getenv("SC4_GDTRACE")) {
        static int shown2 = 0;
        if (mirror_.blend && shown2++ < 4000) {
            const StageCfg& c0 = s.cfg[0];
            const std::uint8_t* c = s.base + 12;
            std::fprintf(stderr, "gd: blendstate fmt %u color %02x%02x%02x%02x stride %d cull %d depthTest %d func %u write %d blend %d (%u,%u) alphaTest %d stage0 tex %u op %d/%d/%d has %d vp %u %u %u %u scissor %d\n", unsigned(s.format), c[0], c[1], c[2], c[3], s.stride, int(mirror_.cull), int(mirror_.depthTest), unsigned(mirror_.depthFunc), int(mirror_.depthWrite),
                         int(mirror_.blend), unsigned(mirror_.blendSrc), unsigned(mirror_.blendDst), int(mirror_.alphaTest), c0.texture, c0.colorOp, c0.colorArg1, c0.colorArg2, int(s.table.HasTexture(c0.texture) && s.textures.count(c0.texture)), s.vx, s.vy, s.vw, s.vh, int(s.scissor));
        }
    }
    if (std::getenv("SC4_GDTRACE")) std::fprintf(stderr, "gd: DrawElements mode %u count %d itype %u format %u\n", unsigned(mode), count, unsigned(indexType), unsigned(s.format));
    if (count < 1 || mode > 7 || !s.base) return;
    if ((s.format != vf::V3F_C4UB && s.format != vf::V3F_C4UB_T2F && s.format != vf::V3F_C4UB_2T2F)) {
        { char note[64]; std::snprintf(note, sizeof note, "DrawElements(vertex format %#x)", unsigned(s.format)); Unsupported(note); }
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
    if (std::getenv("SC4_GDTRACE")) {
        static int shown3 = 0;
        if (shown3++ < 60 && shown3 % 6 == 0) {
            float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
            for (std::size_t i = 0; i < idx.size(); ++i) {
                const float* p = reinterpret_cast<const float*>(s.base + std::size_t(idx[i]) * s.stride);
                simd_float4 clip = simd_mul(s.Proj(), simd_mul(s.View(), simd_make_float4(p[0], p[1], p[2], 1.0f)));
                const float v[3] = {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
                for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], v[k]); mx[k] = std::max(mx[k], v[k]); }
            }
            std::fprintf(stderr, "gd: draw ndc x %.2f..%.2f y %.2f..%.2f z %.3f..%.3f (indices %zu, vertices %d, lo %u hi %u)\n", mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], idx.size(), vertexCount, lo, hi);
        }
    }
    Encode(MTLPrimitiveTypeTriangle, vOff, vertexCount, rebased.data(), count);
}

void cMetalGDriver::Encode(int primitive, std::size_t vertexOffset, int vertexCount, const std::uint16_t* indices, int indexCount) {
    Impl& s = *impl_;
    s.BeginEncoder(impl_->pendingDepth, impl_->pendingRGBA);
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
    dx7::MetalRenderState dc = s.recorder.Snapshot();
    dc.stride = static_cast<std::uint32_t>(s.stride);
    dc.alphaRef = AlphaRefByte(mirror_.alphaRef);
    if (std::getenv("SC4_GDRED")) dc.texMode |= 0x100u;
    if (const char* mode = std::getenv("SC4_GDMODE")) dc.texMode |= static_cast<std::uint32_t>(std::atoi(mode));
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
    if (s.clearColorPending || s.clearDepthPending) s.BeginEncoder(impl_->pendingDepth, impl_->pendingRGBA);
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

namespace gd {

// ---- video modes (list contents and order: VideoModes.h) ----
int cMetalGDriver::CountVideoModes() { return impl_->modeList ? impl_->modeList->CountVideoModes() : 0; }

bool cMetalGDriver::GetVideoModeInfo(std::uint32_t index, sGDMode& mode) {
    if (!impl_->modeList || !impl_->modeList->GetVideoModeInfo(index, mode)) {
        impl_->error = 2;
        return false;
    }
    return true;
}

bool cMetalGDriver::GetVideoModeInfo(sGDMode& mode) {
    mode = impl_->currentMode;
    return true;
}

// Selects a mode: the drawable and targets take the mode's size. -1 releases the current mode.
bool cMetalGDriver::SetVideoMode(int mode, void*, bool, bool) {
    Impl& m = *impl_;
    m.Submit(true);
    if (mode == -1) {
        m.currentMode.id = -1;
        return true;
    }
    sGDMode info{};
    if (!m.modeList || mode < 0 || !m.modeList->GetVideoModeInfo(static_cast<std::uint32_t>(mode), info)) {
        m.error = 2;
        return false;
    }
    m.currentMode = info;
    if (info.width != m.width || info.height != m.height) {
        m.width = info.width;
        m.height = info.height;
        m.MakeTargets();
        m.vx = m.vy = 0;
        m.vw = m.width;
        m.vh = m.height;
        m.scissor = false;
    }
    return true;
}

}  // namespace gd
