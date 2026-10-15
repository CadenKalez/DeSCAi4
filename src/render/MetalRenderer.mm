#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cmath>
#include <cstring>
#include <simd/simd.h>
#include <vector>

#include "render/MetalRenderer.h"

namespace render {

// MSL is compiled at runtime: the offline `metal` tool is not part of the Command Line Tools.
static NSString* const kShaderSource = @R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 viewProj;
    float4 lightDir;      // xyz = direction towards light
    float4 params;        // x = cell meters, y = sea level, z = n, w = heightScale
    float4 heightRange;   // x = min, y = max
};

struct VOut {
    float4 position [[position]];
    float3 normal;
    float height;
    float2 uv;
};

constant float2 kCorner[6] = { float2(0,0), float2(1,0), float2(0,1), float2(1,0), float2(1,1), float2(0,1) };

vertex VOut terrain_vs(uint vid [[vertex_id]],
                       constant Uniforms& u [[buffer(0)]],
                       texture2d<float> heights [[texture(0)]]) {
    uint n = uint(u.params.z);
    uint quad = vid / 6;
    uint cx = quad % (n - 1), cz = quad / (n - 1);
    uint2 g = uint2(cx, cz) + uint2(kCorner[vid % 6]);
    constexpr sampler s(coord::pixel, filter::nearest, address::clamp_to_edge);
    float h  = heights.read(g).r;
    float hl = heights.read(uint2(g.x > 0 ? g.x - 1 : g.x, g.y)).r;
    float hr = heights.read(uint2(min(g.x + 1, n - 1), g.y)).r;
    float hd = heights.read(uint2(g.x, g.y > 0 ? g.y - 1 : g.y)).r;
    float hu = heights.read(uint2(g.x, min(g.y + 1, n - 1))).r;
    float cell = u.params.x;
    float3 nrm = normalize(float3(hl - hr, 2.0 * cell, hd - hu));
    float3 world = float3(float(g.x) * cell, max(h, u.params.y) * u.params.w, float(g.y) * cell);
    VOut o;
    o.position = u.viewProj * float4(world, 1.0);
    o.normal = (h < u.params.y) ? float3(0, 1, 0) : nrm;
    o.height = h;
    o.uv = float2(g);
    return o;
}

struct BIn { float3 position [[attribute(0)]]; float2 uv [[attribute(1)]]; };
struct BOut { float4 position [[position]]; float2 uv; };

vertex BOut building_vs(uint vid [[vertex_id]], const device float* v [[buffer(1)]], constant Uniforms& u [[buffer(0)]]) {
    uint o = vid * 5;
    BOut r;
    r.position = u.viewProj * float4(v[o], v[o + 1], v[o + 2], 1.0);
    r.uv = float2(v[o + 3], v[o + 4]);
    return r;
}

fragment float4 building_fs(BOut in [[stage_in]], texture2d<float> tex [[texture(0)]]) {
    constexpr sampler s(filter::linear, address::repeat);
    float4 c = tex.sample(s, in.uv);
    if (c.a < 0.4) discard_fragment();
    return float4(c.rgb, 1.0);
}

fragment float4 terrain_fs(VOut in [[stage_in]], constant Uniforms& u [[buffer(0)]]) {
    float sea = u.params.y;
    float3 base;
    if (in.height < sea) {
        float depth = clamp((sea - in.height) / 40.0, 0.0, 1.0);
        base = mix(float3(0.20, 0.45, 0.65), float3(0.05, 0.18, 0.40), depth);
    } else {
        float t = clamp((in.height - sea) / max(u.heightRange.y - sea, 1.0), 0.0, 1.0);
        base = mix(float3(0.42, 0.62, 0.28), float3(0.62, 0.55, 0.42), smoothstep(0.15, 0.85, t));
    }
    float ndl = clamp(dot(normalize(in.normal), normalize(u.lightDir.xyz)), 0.0, 1.0);
    float3 col = base * (0.35 + 0.75 * ndl);
    // faint 1-tile grid so tile alignment problems are visible in survey screenshots
    float2 f = fract(in.uv);
    return float4(col, 1.0);
}
)MSL";

struct Uniforms {
    simd_float4x4 viewProj;
    simd_float4 lightDir;
    simd_float4 params;
    simd_float4 heightRange;
};

struct MetalRenderer::Impl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    id<MTLRenderPipelineState> buildingPipeline = nil;
    struct GpuBatch { id<MTLBuffer> vertices; id<MTLBuffer> indices; id<MTLTexture> texture; NSUInteger indexCount; };
    std::vector<GpuBatch> batches;
    id<MTLDepthStencilState> depthState = nil;
    id<MTLTexture> heightTex = nil;
    CAMetalLayer* layer = nil;
    id<MTLTexture> depthTex = nil;
    HeightGrid info;  // header only (heights not retained: they live in heightTex)
    float heightScale = 1.0f;
    MTLPixelFormat colorFormat = MTLPixelFormatBGRA8Unorm;

    simd_float4x4 MakeViewProj(const Camera& c, float aspect) const;
    void Encode(id<MTLCommandBuffer> cb, id<MTLTexture> color, const Camera& cam);
    id<MTLTexture> DepthFor(uint32_t w, uint32_t h);
};

static simd_float4x4 LookAt(simd_float3 eye, simd_float3 at, simd_float3 up) {
    simd_float3 f = simd_normalize(at - eye);
    simd_float3 s = simd_normalize(simd_cross(f, up));
    simd_float3 u = simd_cross(s, f);
    simd_float4x4 m;
    m.columns[0] = {s.x, u.x, -f.x, 0};
    m.columns[1] = {s.y, u.y, -f.y, 0};
    m.columns[2] = {s.z, u.z, -f.z, 0};
    m.columns[3] = {-simd_dot(s, eye), -simd_dot(u, eye), simd_dot(f, eye), 1};
    return m;
}

// Orthographic projection with Metal's [0,1] depth range.
static simd_float4x4 Ortho(float l, float r, float b, float t, float n, float f) {
    simd_float4x4 m = {};
    m.columns[0] = {2 / (r - l), 0, 0, 0};
    m.columns[1] = {0, 2 / (t - b), 0, 0};
    m.columns[2] = {0, 0, -1 / (f - n), 0};
    m.columns[3] = {-(r + l) / (r - l), -(t + b) / (t - b), -n / (f - n), 1};
    return m;
}

simd_float4x4 MetalRenderer::Impl::MakeViewProj(const Camera& c, float aspect) const {
    float extent = (info.n - 1) * kCellMeters;
    float yaw = c.yawDegrees * float(M_PI) / 180.0f, pitch = c.pitchDegrees * float(M_PI) / 180.0f;
    simd_float3 center = {extent * 0.5f + c.centerX, (info.minHeight + info.maxHeight) * 0.5f * heightScale,
                          extent * 0.5f + c.centerZ};
    // fit the rotated square (half-diagonal ~0.72*extent) into both axes of the viewport
    float fit = std::fmax(extent * 0.72f / aspect, extent * 0.42f);
    float radius = fit / (c.distance > 0 ? c.distance : 1.0f);
    simd_float3 dir = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    simd_float3 eye = center + dir * extent * 2.0f;
    simd_float4x4 view = LookAt(eye, center, simd_make_float3(0, 1, 0));
    float halfH = radius, halfW = radius * aspect;
    simd_float4x4 proj = Ortho(-halfW, halfW, -halfH, halfH, 1.0f, extent * 6.0f);
    return simd_mul(proj, view);
}

MetalRenderer::MetalRenderer() : impl_(new Impl) {}
MetalRenderer::~MetalRenderer() { delete impl_; }

bool MetalRenderer::Init(std::string& error) {
    @autoreleasepool {
        impl_->device = MTLCreateSystemDefaultDevice();
        if (!impl_->device) { error = "no Metal device"; return false; }
        impl_->queue = [impl_->device newCommandQueue];
        NSError* err = nil;
        id<MTLLibrary> lib = [impl_->device newLibraryWithSource:kShaderSource options:nil error:&err];
        if (!lib) { error = std::string("MSL compile failed: ") + err.localizedDescription.UTF8String; return false; }
        MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = [lib newFunctionWithName:@"terrain_vs"];
        d.fragmentFunction = [lib newFunctionWithName:@"terrain_fs"];
        d.colorAttachments[0].pixelFormat = impl_->colorFormat;
        d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        impl_->pipeline = [impl_->device newRenderPipelineStateWithDescriptor:d error:&err];
        if (!impl_->pipeline) { error = std::string("pipeline failed: ") + err.localizedDescription.UTF8String; return false; }
        MTLRenderPipelineDescriptor* bd = [MTLRenderPipelineDescriptor new];
        bd.vertexFunction = [lib newFunctionWithName:@"building_vs"];
        bd.fragmentFunction = [lib newFunctionWithName:@"building_fs"];
        bd.colorAttachments[0].pixelFormat = impl_->colorFormat;
        bd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        impl_->buildingPipeline = [impl_->device newRenderPipelineStateWithDescriptor:bd error:&err];
        if (!impl_->buildingPipeline) { error = std::string("building pipeline failed: ") + err.localizedDescription.UTF8String; return false; }
        MTLDepthStencilDescriptor* ds = [MTLDepthStencilDescriptor new];
        ds.depthCompareFunction = MTLCompareFunctionLess;
        ds.depthWriteEnabled = YES;
        impl_->depthState = [impl_->device newDepthStencilStateWithDescriptor:ds];
    }
    return true;
}

bool MetalRenderer::SetTerrain(const HeightGrid& grid, std::string& error) {
    if (!impl_->device) { error = "renderer not initialised"; return false; }
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                                                                   width:grid.n height:grid.n mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    impl_->heightTex = [impl_->device newTextureWithDescriptor:td];
    [impl_->heightTex replaceRegion:MTLRegionMake2D(0, 0, grid.n, grid.n) mipmapLevel:0
                          withBytes:grid.heights.data() bytesPerRow:grid.n * 4];
    impl_->info.n = grid.n;
    impl_->info.minHeight = grid.minHeight;
    impl_->info.maxHeight = grid.maxHeight;
    return true;
}

bool MetalRenderer::SetBuildings(const std::vector<BuildingBatch>& batches, std::string& error) {
    if (!impl_->device) { error = "renderer not initialised"; return false; }
    impl_->batches.clear();
    for (const BuildingBatch& b : batches) {
        if (b.indices.empty() || b.rgba.empty()) continue;
        Impl::GpuBatch g;
        g.vertices = [impl_->device newBufferWithBytes:b.vertices.data() length:b.vertices.size() * 4 options:MTLResourceStorageModeShared];
        g.indices = [impl_->device newBufferWithBytes:b.indices.data() length:b.indices.size() * 4 options:MTLResourceStorageModeShared];
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                       width:b.texW height:b.texH mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        g.texture = [impl_->device newTextureWithDescriptor:td];
        [g.texture replaceRegion:MTLRegionMake2D(0, 0, b.texW, b.texH) mipmapLevel:0 withBytes:b.rgba.data() bytesPerRow:b.texW * 4];
        g.indexCount = b.indices.size();
        impl_->batches.push_back(g);
    }
    return true;
}

id<MTLTexture> MetalRenderer::Impl::DepthFor(uint32_t w, uint32_t h) {
    if (depthTex && depthTex.width == w && depthTex.height == h) return depthTex;
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                   width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    depthTex = [device newTextureWithDescriptor:td];
    return depthTex;
}

void MetalRenderer::Impl::Encode(id<MTLCommandBuffer> cb, id<MTLTexture> color, const Camera& cam) {
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = color;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.08, 0.09, 0.11, 1);
    rp.depthAttachment.texture = DepthFor(uint32_t(color.width), uint32_t(color.height));
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionDontCare;
    rp.depthAttachment.clearDepth = 1.0;
    Uniforms u;
    u.viewProj = MakeViewProj(cam, float(color.width) / float(color.height));
    u.lightDir = simd_normalize((simd_float4){-0.5f, 0.8f, -0.35f, 0});
    u.params = {kCellMeters, kSeaLevelMeters, float(info.n), heightScale};
    u.heightRange = {info.minHeight, info.maxHeight, 0, 0};
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    [enc setRenderPipelineState:pipeline];
    [enc setDepthStencilState:depthState];
    [enc setVertexBytes:&u length:sizeof u atIndex:0];
    [enc setFragmentBytes:&u length:sizeof u atIndex:0];
    [enc setVertexTexture:heightTex atIndex:0];
    NSUInteger verts = NSUInteger(info.n - 1) * (info.n - 1) * 6;
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:verts];
    if (!batches.empty()) {
        [enc setRenderPipelineState:buildingPipeline];
        [enc setCullMode:MTLCullModeNone];
        for (const GpuBatch& g : batches) {
            [enc setVertexBuffer:g.vertices offset:0 atIndex:1];
            [enc setFragmentTexture:g.texture atIndex:0];
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:g.indexCount indexType:MTLIndexTypeUInt32
                           indexBuffer:g.indices indexBufferOffset:0];
        }
    }
    [enc endEncoding];
}

bool MetalRenderer::RenderToPng(const std::string& path, uint32_t w, uint32_t h, const Camera& cam,
                                std::string& error) {
    @autoreleasepool {
        if (!impl_->heightTex) { error = "no terrain set"; return false; }
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:impl_->colorFormat
                                                                                       width:w height:h mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget;
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> target = [impl_->device newTextureWithDescriptor:td];
        id<MTLCommandBuffer> cb = [impl_->queue commandBuffer];
        impl_->Encode(cb, target, cam);
        [cb commit];
        [cb waitUntilCompleted];
        if (cb.status != MTLCommandBufferStatusCompleted) { error = "GPU command buffer failed"; return false; }
        std::vector<uint8_t> px(size_t(w) * h * 4);
        [target getBytes:px.data() bytesPerRow:w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(px.data(), w, h, 8, w * 4, cs,
                                                 (CGBitmapInfo)kCGImageAlphaNoneSkipFirst | (CGBitmapInfo)kCGBitmapByteOrder32Little);
        CGImageRef img = CGBitmapContextCreateImage(ctx);
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url,
                                                                    (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
        bool ok = false;
        if (dst) {
            CGImageDestinationAddImage(dst, img, nullptr);
            ok = CGImageDestinationFinalize(dst);
            CFRelease(dst);
        }
        CGImageRelease(img);
        CGContextRelease(ctx);
        CGColorSpaceRelease(cs);
        if (!ok) error = "PNG write failed: " + path;
        return ok;
    }
}

bool MetalRenderer::AttachLayer(void* l, std::string& error) {
    impl_->layer = (__bridge CAMetalLayer*)l;
    impl_->layer.device = impl_->device;
    impl_->layer.pixelFormat = impl_->colorFormat;
    impl_->layer.framebufferOnly = YES;
    (void)error;
    return true;
}

void MetalRenderer::DrawToLayer(const Camera& cam, uint32_t w, uint32_t h) {
    @autoreleasepool {
        if (!impl_->layer || !impl_->heightTex) return;
        impl_->layer.drawableSize = CGSizeMake(w, h);
        id<CAMetalDrawable> dr = [impl_->layer nextDrawable];
        if (!dr) return;
        id<MTLCommandBuffer> cb = [impl_->queue commandBuffer];
        impl_->Encode(cb, dr.texture, cam);
        [cb presentDrawable:dr];
        [cb commit];
    }
}

}  // namespace render
