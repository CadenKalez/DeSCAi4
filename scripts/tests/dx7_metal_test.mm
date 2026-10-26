// Renders one lit, textured, alpha-tested triangle pair through DX7Emulator.metal into an offscreen target and checks pixels.
#import <Metal/Metal.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include "render/dx7/dx7_to_metal_bridge.h"

struct V { float p[3]; float n[3]; uint8_t c[4]; float uv[2]; };

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d %s\n", __LINE__, #c); ++g_fail; } } while (0)

static void Pixel(const uint8_t* px, int w, int x, int y, int out[4]) { for (int i = 0; i < 4; ++i) out[i] = px[(y * w + x) * 4 + i]; }

int main() {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        std::ifstream f("src/render/dx7/DX7Emulator.metal");
        std::stringstream ss; ss << f.rdbuf();
        NSError* err = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:[NSString stringWithUTF8String:ss.str().c_str()] options:nil error:&err];
        CHECK(lib != nil);
        if (!lib) return 1;
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        vd.attributes[0].format = MTLVertexFormatFloat3; vd.attributes[0].offset = 0; vd.attributes[0].bufferIndex = 0;
        vd.attributes[1].format = MTLVertexFormatFloat3; vd.attributes[1].offset = 12; vd.attributes[1].bufferIndex = 0;
        vd.attributes[2].format = MTLVertexFormatUChar4; vd.attributes[2].offset = 24; vd.attributes[2].bufferIndex = 0;
        vd.attributes[3].format = MTLVertexFormatFloat2; vd.attributes[3].offset = 28; vd.attributes[3].bufferIndex = 0;
        vd.layouts[0].stride = sizeof(V);
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [lib newFunctionWithName:@"dx7_vertex"];
        pd.fragmentFunction = [lib newFunctionWithName:@"dx7_fragment"];
        pd.vertexDescriptor = vd;
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        id<MTLRenderPipelineState> pso = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
        CHECK(pso != nil);
        if (!pso) return 1;

        const int W = 16, H = 16;
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:W height:H mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> target = [dev newTextureWithDescriptor:td];
        MTLTextureDescriptor* wd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
        id<MTLTexture> white = [dev newTextureWithDescriptor:wd];
        uint8_t half[4] = {128, 128, 128, 255};
        [white replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:half bytesPerRow:4];
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        id<MTLSamplerState> sampler = [dev newSamplerStateWithDescriptor:sd];
        id<MTLCommandQueue> queue = [dev newCommandQueue];

        // full-screen quad facing +z (D3D clip z in [0,1]); BGRA diffuse white, normal toward the viewer
        const V verts[6] = {
            {{-1,-1,0.5f},{0,0,1},{255,255,255,255},{0,0}}, {{ 1,-1,0.5f},{0,0,1},{255,255,255,255},{1,0}}, {{ 1, 1,0.5f},{0,0,1},{255,255,255,255},{1,1}},
            {{-1,-1,0.5f},{0,0,1},{255,255,255,255},{0,0}}, {{ 1, 1,0.5f},{0,0,1},{255,255,255,255},{1,1}}, {{-1, 1,0.5f},{0,0,1},{255,255,255,255},{0,1}},
        };

        auto draw = [&](const dx7::MetalRenderState& st, int out[4]) {
            MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = target;
            rp.colorAttachments[0].loadAction = MTLLoadActionClear;
            rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
            [enc setRenderPipelineState:pso];
            [enc setVertexBytes:verts length:sizeof verts atIndex:0];
            [enc setVertexBytes:&st length:sizeof st atIndex:1];
            [enc setFragmentBytes:&st length:sizeof st atIndex:1];
            [enc setFragmentTexture:white atIndex:0];
            [enc setFragmentSamplerState:sampler atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
            [enc endEncoding];
            [cb commit]; [cb waitUntilCompleted];
            uint8_t px[W * H * 4];
            [target getBytes:px bytesPerRow:W * 4 fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
            Pixel(px, W, W / 2, H / 2, out);
        };

        dx7::StateRecorder r;
        int p[4];
        r.SetFVF(dx7::kFvfXyz | dx7::kFvfNormal | dx7::kFvfDiffuse | dx7::kFvfTex1);
        r.SetRenderState(dx7::kRenderLighting, 0);
        draw(r.Snapshot(), p);
        CHECK(p[0] == 255 && p[1] == 255 && p[2] == 255 && p[3] == 255);   // unlit white

        r.SetRenderState(dx7::kRenderLighting, 1);
        r.SetRenderState(dx7::kRenderAmbient, 0xff000000u);
        float dir[3] = {0, 0, -1}, diffuse[4] = {0.5f, 0.5f, 0.5f, 1};
        r.SetLight(dir, diffuse);
        draw(r.Snapshot(), p);
        CHECK(p[0] >= 126 && p[0] <= 129);                                   // n.l = 1 -> 0.5 diffuse

        dir[2] = 1;
        r.SetLight(dir, diffuse);
        draw(r.Snapshot(), p);
        CHECK(p[0] == 0 && p[3] == 255);                                     // light from behind

        r.SetRenderState(dx7::kRenderLighting, 0);
        r.SetTextureBound(true);
        draw(r.Snapshot(), p);
        CHECK(p[0] >= 127 && p[0] <= 129);                                   // white * 0.5 grey texel

        r.SetRenderState(dx7::kRenderAlphaTestEnable, 1);
        r.SetRenderState(dx7::kRenderAlphaFunc, 7);                          // D3DCMP_GREATEREQUAL
        r.SetRenderState(dx7::kRenderAlphaRef, 128);
        draw(r.Snapshot(), p);
        CHECK(p[3] == 255);
        r.SetRenderState(dx7::kRenderAlphaFunc, 1);                          // D3DCMP_NEVER
        draw(r.Snapshot(), p);
        CHECK(p[3] == 0);

        if (g_fail == 0) std::printf("dx7_metal ok (%s)\n", [[dev name] UTF8String]);
    }
    return g_fail ? 1 : 0;
}
