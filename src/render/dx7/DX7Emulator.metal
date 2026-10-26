#include <metal_stdlib>
using namespace metal;

// Mirrors dx7::MetalRenderState byte for byte (336 bytes).
struct RenderState {
    float4x4 world;
    float4x4 view;
    float4x4 projection;
    float4x4 texture0;
    float4 ambient;
    float4 textureFactor;
    float4 lightDirection;
    float4 lightDiffuse;
    uint flags;
    uint alphaFunc;
    uint alphaRef;
    uint fvf;
};

constant uint kLighting = 1u << 0, kTexture = 1u << 1, kAlphaTest = 1u << 2, kHasNormal = 1u << 3, kHasDiffuse = 1u << 4, kHasTexMatrix = 1u << 5;

// Vertex descriptor: 0 position float3, 1 normal float3, 2 diffuse uchar4 (D3DCOLOR, B G R A in memory), 3 uv float2.
struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    uchar4 diffuse  [[attribute(2)]];
    float2 uv       [[attribute(3)]];
};
struct VertexOut {
    float4 position [[position]];
    float4 color;
    float2 uv;
};

// D3D row vectors: v * W * V * P. The 16 floats load as columns, so the product is taken on the left.
vertex VertexOut dx7_vertex(VertexIn in [[stage_in]], constant RenderState& s [[buffer(1)]]) {
    VertexOut out;
    float4 world = s.world * float4(in.position, 1.0);
    out.position = s.projection * (s.view * world);
    float4 diffuse = (s.flags & kHasDiffuse) ? float4(in.diffuse.zyxw) / 255.0 : float4(1.0);
    float3 lit = float3(1.0);
    if ((s.flags & kLighting) && (s.flags & kHasNormal)) {
        float3 n = normalize((s.world * float4(in.normal, 0.0)).xyz);
        float ndotl = max(dot(n, -normalize(s.lightDirection.xyz)), 0.0);
        lit = clamp(s.ambient.rgb + s.lightDiffuse.rgb * ndotl, 0.0, 1.0);
    }
    out.color = float4(diffuse.rgb * lit, diffuse.a);
    out.uv = (s.flags & kHasTexMatrix) ? (s.texture0 * float4(in.uv, 0.0, 1.0)).xy : in.uv;
    return out;
}

fragment float4 dx7_fragment(VertexOut in [[stage_in]], constant RenderState& s [[buffer(1)]],
                             texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    float4 color = in.color;
    if (s.flags & kTexture) color *= tex.sample(smp, in.uv);
    if (s.flags & kAlphaTest) {
        uint a = uint(round(color.a * 255.0));
        bool pass;
        switch (s.alphaFunc) {   // D3DCMP 1..8: never, less, equal, lessequal, greater, notequal, greaterequal, always
        case 1: pass = false; break;
        case 2: pass = a <  s.alphaRef; break;
        case 3: pass = a == s.alphaRef; break;
        case 4: pass = a <= s.alphaRef; break;
        case 5: pass = a >  s.alphaRef; break;
        case 6: pass = a != s.alphaRef; break;
        case 7: pass = a >= s.alphaRef; break;
        default: pass = true; break;
        }
        if (!pass) discard_fragment();
    }
    return color;
}
