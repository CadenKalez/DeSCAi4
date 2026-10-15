#include <cstdio>
#include <cstdlib>
#include "game/GameData.h"
#include "s3d/FSH.h"
#include "s3d/S3DModel.h"
int main(int argc, char** argv) {
    game::GameData data; std::string err;
    if (!data.Open(argv[1], err)) { std::puts(err.c_str()); return 1; }
    cGZPersistResourceKey key{s3d::kResourceTypeModel, (std::uint32_t)std::strtoul(argv[2], 0, 16), (std::uint32_t)std::strtoul(argv[3], 0, 16)};
    std::vector<std::uint8_t> b;
    if (!data.ReadRecord(key, b)) { std::puts("model not found"); return 1; }
    s3d::Model m;
    if (!m.Parse(b.data(), b.size())) { std::puts("parse failed"); return 1; }
    std::printf("v%u.%u vg=%zu ig=%zu pg=%zu mats=%zu meshes=%zu frames=%u\n", m.major, m.minor, m.vertexGroups.size(), m.indexGroups.size(), m.primitiveGroups.size(), m.materials.size(), m.meshes.size(), m.frameCount);
    for (auto& v : m.vertexGroups) std::printf(" vgroup fmt %u stride %u count %u bounds x[%g,%g] y[%g,%g] z[%g,%g]\n", v.format, v.stride, v.count, v.min[0], v.max[0], v.min[1], v.max[1], v.min[2], v.max[2]);
    for (auto& g : m.primitiveGroups) for (auto& p : g.primitives) std::printf(" prim type %u first %u count %u\n", p.type, p.first, p.count);
    for (auto& mat : m.materials) { std::printf(" material flags %08x alpha %u/%u blend %u/%u thr %u class %08x tex %zu\n", mat.flags, mat.alphaFunc, mat.depthFunc, mat.srcBlend, mat.dstBlend, mat.alphaThreshold, mat.materialClass, mat.textures.size());
        for (auto& t : mat.textures) std::printf("   tex %08x wrap %u/%u filt %u/%u rate %u mode %u '%s'\n", t.instance, t.wrapU, t.wrapV, t.magFilter, t.minFilter, t.animRate, t.animMode, t.name.c_str()); }
    for (auto& me : m.meshes) std::printf(" mesh '%s' flags %u frames %zu\n", me.name.c_str(), me.flags, me.frames.size());
    for (auto& p : m.properties) std::printf(" prop %d %d '%s' = '%s'\n", p.a, p.b, p.name.c_str(), p.value.c_str());
    for (auto& r : m.regPoints) std::printf(" regpoint '%s' x%zu\n", r.first.c_str(), r.second.size());
    if (argc > 4) {  // texture group probe
        cGZPersistResourceKey t{s3d::kResourceTypeTexture, (std::uint32_t)std::strtoul(argv[4], 0, 16), m.materials.empty() || m.materials[0].textures.empty() ? 0 : m.materials[0].textures[0].instance};
        std::vector<std::uint8_t> tb;
        std::printf("texture %08x/%08x/%08x: %s\n", t.type, t.group, t.instance, data.ReadRecord(t, tb) ? "found" : "missing");
    }
}
