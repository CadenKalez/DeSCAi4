#include "render/BuildingMeshes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

#include "s3d/FSH.h"
#include "s3d/S3DModel.h"

namespace render {

namespace {
constexpr std::uint32_t kPropertyModelKey = 0x27812821;       // "Resource Key Type 1": the S3D model
constexpr std::uint32_t kTextureGroup = 0x1abe787d;

struct CachedModel {
    bool ok = false;
    s3d::Model model;
};
}  // namespace

bool BuildBuildingBatches(game::GameData& data, const std::vector<sc4::BuildingRecord>& buildings, int zoom,
                          std::vector<BuildingBatch>& out, BuildingSceneStats& stats) {
    std::map<std::uint64_t, CachedModel> models;                      // (group, instance) -> parsed model
    std::unordered_map<std::uint64_t, cGZPersistResourceKey> exemplarModel;   // exemplar instance -> model key
    std::unordered_map<std::uint32_t, std::size_t> batchIndex;        // texture instance -> batch
    std::unordered_map<std::uint32_t, bool> textureMissing;
    stats = {};
    stats.buildings = buildings.size();

    auto batchFor = [&](std::uint32_t instance) -> BuildingBatch* {
        auto it = batchIndex.find(instance);
        if (it != batchIndex.end()) return &out[it->second];
        if (textureMissing.count(instance)) return nullptr;
        std::vector<std::uint8_t> bytes;
        cGZPersistResourceKey key{s3d::kResourceTypeTexture, kTextureGroup, instance};
        BuildingBatch batch;
        s3d::FshFile fsh;
        if (!data.ReadRecord(key, bytes) || !fsh.Open(bytes.data(), bytes.size()) ||
            !fsh.DecodeRgba8(0, batch.rgba, batch.texW, batch.texH)) {
            textureMissing[instance] = true;
            return nullptr;
        }
        batch.textureInstance = instance;
        batchIndex[instance] = out.size();
        out.push_back(std::move(batch));
        return &out.back();
    };

    for (const sc4::BuildingRecord& b : buildings) {
        const std::uint64_t exKey = (std::uint64_t(b.exemplar.group) << 32) | b.exemplar.instance;
        auto found = exemplarModel.find(exKey);
        if (found == exemplarModel.end()) {
            cGZPersistResourceKey modelKey{};
            sc4::cISCResExemplar* exemplar = data.GetExemplar(b.exemplar);
            if (exemplar) {
                if (sc4::cISCProperty* p = exemplar->AsISCPropertyHolder()->GetProperty(kPropertyModelKey)) {
                    cIGZVariant* v = p->GetPropertyValue();
                    if (v->GetCount() >= 3) {
                        const std::uint32_t* r = v->RefUint32();
                        modelKey = cGZPersistResourceKey{r[0], r[1], r[2]};
                    }
                }
                exemplar->Release();
            }
            found = exemplarModel.emplace(exKey, modelKey).first;
        }
        const cGZPersistResourceKey base = found->second;
        if (base.type == 0) { ++stats.missingExemplar; continue; }

        // model variants: instance + zoom * 0x100 + rotation * 0x10
        const int rotation = ((b.orientation % 4) + 4) % 4;
        const std::uint32_t instance = (base.instance & ~0xfffu) + std::uint32_t(zoom) * 0x100 + std::uint32_t(rotation) * 0x10;
        const std::uint64_t mkey = (std::uint64_t(base.group) << 32) | instance;
        auto mit = models.find(mkey);
        if (mit == models.end()) {
            CachedModel cm;
            std::vector<std::uint8_t> bytes;
            cGZPersistResourceKey key{s3d::kResourceTypeModel, base.group, instance};
            if (data.ReadRecord(key, bytes)) cm.ok = cm.model.Parse(bytes.data(), bytes.size());
            mit = models.emplace(mkey, std::move(cm)).first;
        }
        if (!mit->second.ok) { ++stats.missingModel; continue; }
        const s3d::Model& m = mit->second.model;

        const float cx = (b.min[0] + b.max[0]) * 0.5f, cz = (b.min[2] + b.max[2]) * 0.5f, baseY = b.min[1];
        bool any = false;
        for (const s3d::AnimMesh& mesh : m.meshes) {
            if (mesh.frames.empty()) continue;
            const s3d::FrameRef& f = mesh.frames[0];
            const s3d::VertexGroup& vg = m.vertexGroups[f.vertexGroup];
            const std::vector<std::uint16_t>& ig = m.indexGroups[f.indexGroup];
            const s3d::Material& mat = m.materials[f.materialGroup];
            if (mat.textures.empty()) continue;
            BuildingBatch* batch = batchFor(mat.textures[0].instance);
            if (!batch) { ++stats.missingTexture; continue; }
            const std::uint32_t firstVertex = static_cast<std::uint32_t>(batch->vertices.size() / 5);
            const std::size_t uvOffset = vg.stride >= 24 ? 16 : 12;
            for (std::uint32_t i = 0; i < vg.count; ++i) {
                const std::uint8_t* p = &vg.data[std::size_t(i) * vg.stride];
                float pos[3], uv[2];
                std::memcpy(pos, p, 12);
                std::memcpy(uv, p + uvOffset, 8);
                batch->vertices.push_back(cx + pos[0]);
                batch->vertices.push_back(baseY + pos[1]);
                batch->vertices.push_back(cz + pos[2]);
                batch->vertices.push_back(uv[0]);
                batch->vertices.push_back(uv[1]);
            }
            for (const s3d::Primitive& prim : m.primitiveGroups[f.primitiveGroup].primitives) {
                if (prim.type != 0) continue;                      // triangle lists only
                for (std::uint32_t k = 0; k + 2 < prim.count; k += 3) {
                    if (prim.first + k + 2 >= ig.size()) break;
                    for (int c = 0; c < 3; ++c) {
                        const std::uint16_t idx = ig[prim.first + k + c];
                        batch->indices.push_back(firstVertex + (idx < vg.count ? idx : 0));
                    }
                    ++stats.triangles;
                }
            }
            any = true;
        }
        if (any) ++stats.placed;
    }
    return true;
}

}  // namespace render
