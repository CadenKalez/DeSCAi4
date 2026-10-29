#include "app/NativeShadowOverlay.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

#include "sc4/DrawContext.h"
#include "sc4/PackedRenderState.h"
#include "sc4/Render3D.h"
#include "sc4/CellTextureModel.h"
#include "sc4/LightingManager.h"
#include "sc4/ModelMaker.h"
#include "sc4/OverlayManager.h"
#include "sc4/ServiceLocator.h"
#include "sc4/ViewUtilities.h"
#include "sc4/cSTETerrain.h"

namespace native {
sc4::cSC4LightingManager* LightingManager();   // NativeLighting.cpp

namespace {
using namespace sc4;

cSTETerrain* g_terrain = nullptr;
cSTEOverlayManager* g_manager = nullptr;
cSC43DRender* g_render = nullptr;

// The model material's texture binding (frame ids) as the overlay manager holds it.
class Texture final : public IOverlayTexture {
public:
    explicit Texture(const CellTextureBinding* binding) : binding_(binding) {}
    void AddRef() override { ++refs_; }
    void Release() override { if (--refs_ == 0) delete this; }
    int GlTextureId() const override { return binding_ && binding_->frameCount ? static_cast<int>(binding_->frames[0]) : 0; }
private:
    const CellTextureBinding* binding_;
    int refs_ = 1;
};

class ShadowProjector final : public IOverlayShadowService {
public:
    bool ProjectShadow(int count, const cS3DVector3* points, const cS3DVector2* uvs, const cS3DTransform& transform, cS3DMatrix4& out) override {
        cSC4LightingManager* lighting = LightingManager();
        return lighting && lighting->CalcShadowProjection(count, points, uvs, transform, &out);
    }
    void GetShadowDirection(cS3DVector3& out) override {
        cSC4LightingManager* lighting = LightingManager();
        out = lighting ? lighting->GetShadowDirection() : cS3DVector3{0.0f, -1.0f, 0.0f};
    }
    bool CalcShadowRect(int count, const cS3DVector3* points, const cS3DBoundingBox& box, cRZRect& out) override {
        cSC4LightingManager* lighting = LightingManager();
        return lighting && lighting->CalcShadowTerrainRect(count, points, box, &out);
    }
    void GetShadowColor(cS3DVector4& colour, float& scale, float& intensity) override {
        cSC4LightingManager* lighting = LightingManager();
        cS3DVector3 rgb{};
        float strength = 0.5f, threshold = 0.0f;
        if (lighting) lighting->GetShadowParams(strength, threshold, rgb);
        colour = {rgb.x, rgb.y, rgb.z, 1.0f};
        scale = strength;
        intensity = threshold;
    }
    float ShadowFalloff(const cS3DVector2& position) override {
        cSC4LightingManager* lighting = LightingManager();
        return lighting ? lighting->GetShadowStrengthFromLocation(position) : 1.0f;
    }
};

class Host final : public IOverlayHost {
public:
    cIGZMessageServer2* Messages() override { return SL::spMS2; }
    IOverlayTextureFactory* TextureFactory() override { return nullptr; }   // the decal textures (0xaa592b77) are not needed for model shadows
    IOverlayShadowService* ShadowService() override { return &service_; }
    int RenderQuality() override { const char* q = std::getenv("SC4_SHADOWQ"); return q ? std::atoi(q) : 5; }
    int RenderSecondTextureStage() override { return 0; }
    bool RenderSpatialIndexValid() override { return false; }
    int TerrainCellCountX() override { return g_terrain ? static_cast<int>(g_terrain->CellCountX()) : 0; }
    int TerrainCellCountZ() override { return g_terrain ? static_cast<int>(g_terrain->CellCountZ()) : 0; }
    float TerrainCellSize() override { return 16.0f; }
    int TerrainCellCountInRect(const cRZRect& rect) override { return std::max(0, rect.right - rect.left) * std::max(0, rect.bottom - rect.top); }
    void RedisplayTerrain(int, const cRZRect&) override {}
    void GetTerrainCullingInfo(int& rowBegin, int& rowEnd, const std::int32_t*& colStart, const std::int32_t*& colEnd) override {
        rowBegin = rowEnd = 0; colStart = colEnd = nullptr;
    }
    void DrawTerrainMeshSubset(bool, const cRZRect&, ITerrainVertBufExtn*) override {}
    void DrawWaterSurfaceSubset(bool, const cRZRect&) override {}
    void DrawMixedMeshSubset(bool, const cRZRect&) override {}
private:
    ShadowProjector service_;
};

Host g_host;

class Adapter final : public IShadowOverlay {
public:
    std::uint32_t AddShadow(std::uint64_t texture, int count, const cS3DVector3* points, const cS3DVector2* uvs,
                            const cS3DBoundingBox& box, const cS3DTransform& transform) override {
        if (!g_manager || !texture) return 0xffffffffu;
        if (std::getenv("SC4_DEBUG")) {
            static int shown = 0;
            if (shown++ < 3) {
                cRZRect r{};
                const bool ok = g_host.ShadowService()->CalcShadowRect(count, points, box, r);
                std::fprintf(stderr, "shadow add: box %.1f,%.1f,%.1f..%.1f,%.1f,%.1f translation %.1f,%.1f,%.1f count %d rect(%d) %d,%d,%d,%d\n", box.min[0], box.min[1], box.min[2], box.max[0],
                             box.max[1], box.max[2], transform.translation[0], transform.translation[1], transform.translation[2], count, ok, r.left, r.top, r.right, r.bottom);
            }
        }
        auto* wrapped = new Texture(reinterpret_cast<const CellTextureBinding*>(texture));
        const std::uint32_t id = g_manager->AddShadow(wrapped, count, points, uvs, box, transform);
        wrapped->Release();   // the manager holds its own reference
        static int adds = 0, failed = 0;
        (id == 0xffffffffu ? failed : adds)++;
        if (std::getenv("SC4_DEBUG") && ((adds + failed) % 500 == 1))
            std::fprintf(stderr, "shadow overlay: %d added, %d refused (lighting %p, quality %d)\n", adds, failed, static_cast<void*>(LightingManager()), g_manager->Quality());
        return id;
    }
};
Adapter g_adapter;

struct Vertex { float x, y, z; std::uint8_t rgba[4]; float u, v; };

class DrawObject final : public cISC4ViewObject3D {
public:
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override { return --refs_; }
    void DrawViewObject(SC4DrawContext& context) override {
        static const bool enabled = std::getenv("SC4_NOSHADOWS") == nullptr;
        if (!enabled || !g_manager || !g_terrain) return;
        IOverlayShadowService* service = g_host.ShadowService();
        cS3DVector4 colour; float strength, threshold;
        service->GetShadowColor(colour, strength, threshold);
        const std::uint8_t alpha = static_cast<std::uint8_t>(std::clamp(strength, 0.0f, 1.0f) * 255.0f);
        static cSC4PackedRenderState state;
        static bool packed = false;
        if (!packed) { state.PackRenderState(true, true, true, true, true, 1, false, 1, 4, 5, 4, 0.5f, -1, -1); packed = true; }   // renderStates_[kOverlayDrawModeShadow] (InitDrawStates)
        const int maxX = static_cast<int>(g_terrain->CellCountX()), maxZ = static_cast<int>(g_terrain->CellCountZ());
        auto& slots = g_manager->Overlays();
        // only the cells the camera sees (a margin for shadows cast in from outside)
        int viewX0 = 0, viewZ0 = 0, viewX1 = maxX, viewZ1 = maxZ;
        if (SC4ViewUtilities* view = g_render ? g_render->GetViewUtilities() : nullptr) {
            float midMin[3], midMax[3], blockMin[3], blockMax[3];
            view->GetGroundPlaneVisibleAreaRectangle(midMin, midMax, blockMin, blockMax);
            viewX0 = std::max(0, static_cast<int>(blockMin[0] / 16.0f) - 4); viewX1 = std::min(maxX, static_cast<int>(blockMax[0] / 16.0f) + 4);
            viewZ0 = std::max(0, static_cast<int>(blockMin[2] / 16.0f) - 4); viewZ1 = std::min(maxZ, static_cast<int>(blockMax[2] / 16.0f) + 4);
        }
        static const bool dbg = std::getenv("SC4_SHADOWDBG") != nullptr;
        std::map<int, std::vector<Vertex>> byTexture;   // one draw per texture: every shadow of a texture is one batch
        for (std::uint32_t i = 0; i < slots.size(); ++i) {
            const cOverlay& overlay = slots[i];
            if (overlay.freeLink != -1 || !(overlay.flags & kOverlayFlagShadow) || !(overlay.flags & kOverlayFlagEnabled) || !overlay.texture) continue;
            const int x0 = std::max(viewX0, overlay.rect.left), z0 = std::max(viewZ0, overlay.rect.top);
            const int x1 = std::min(viewX1, overlay.rect.right), z1 = std::min(viewZ1, overlay.rect.bottom);
            if (x1 <= x0 || z1 <= z0) continue;
            const cS3DMatrix4& m = overlay.texMatrix;   // stage 0 of DrawShadows: world position -> the model texture's coordinates (the projection)
            std::vector<Vertex>& triangles = byTexture[dbg ? 0 : overlay.texture->GlTextureId()];
            auto vertex = [&](int x, int z) {
                const float wx = x * 16.0f, wz = z * 16.0f, wy = g_terrain->GetAltitudeAtVertex(x, z) + 0.6f;
                Vertex out{wx, wy, wz, {dbg ? std::uint8_t(255) : std::uint8_t(0), 0, 0, dbg ? std::uint8_t(255) : alpha}, 0, 0};
                const float w = wx * m.m[0][3] + wy * m.m[1][3] + wz * m.m[2][3] + m.m[3][3];
                const float inverseW = (w > 1e-6f || w < -1e-6f) ? 1.0f / w : 1.0f;
                out.u = (wx * m.m[0][0] + wy * m.m[1][0] + wz * m.m[2][0] + m.m[3][0]) * inverseW;
                out.v = (wx * m.m[0][1] + wy * m.m[1][1] + wz * m.m[2][1] + m.m[3][1]) * inverseW;
                return out;
            };
            for (int z = z0; z < z1; ++z)
                for (int x = x0; x < x1; ++x) {
                    const Vertex a = vertex(x, z), b = vertex(x + 1, z), c = vertex(x, z + 1), d = vertex(x + 1, z + 1);
                    triangles.insert(triangles.end(), {a, b, c, b, d, c});
                }
        }
        if (byTexture.empty()) return;
        context.ResetModelViewTransform();
        // the setup of cSTEOverlayManager::DrawShadows (0x10072aee0): shadow colour, alpha test at intensity * strength, combiner 0 on stage 0
        context.SetRenderState(state);
        context.SetTexColor(colour);
        if (threshold <= 0.0f) {
            context.EnableAlphaTestFlag(false);
        } else {
            context.EnableAlphaTestFlag(true);
            context.SetAlphaFunc(6, threshold * strength);
        }
        context.SetTexFiltering(1, 1, 0);
        static const gd::cGDCombiner combiner{0x01030100u, 0x01010001u, 0u};
        context.SetTexCombiner(combiner, 0);
        context.SetDepthOffset(-2);
        std::size_t total = 0;
        for (const auto& batch : byTexture) {
            context.SetTexture(batch.first, 0);
            context.SetTexWrapModes(2, 2, 0);
            for (std::size_t n = 0; n < batch.second.size(); n += 3000)
                context.DrawPrims(0, 10, static_cast<int>(std::min<std::size_t>(3000, batch.second.size() - n)), batch.second.data() + n);
            total += batch.second.size();
        }
        context.SetDepthOffset(0);
        context.EnableAlphaTestFlag(false);
        static int reported = 0;
        if (std::getenv("SC4_DEBUG") && reported++ < 3) std::fprintf(stderr, "shadow overlay: %zu vertices in %zu texture batches\n", total, byTexture.size());
    }
private:
    std::uint32_t refs_ = 1;
};

}  // namespace

void AttachShadowOverlay(cSC43DRender& render, cSTETerrain& terrain) {
    if (g_manager) return;
    g_terrain = &terrain;
    g_render = &render;
    g_manager = new cSTEOverlayManager(&g_host);
    g_manager->Init(nullptr, true, false);
    render.AddViewObject(new DrawObject(), 5, 150);   // after the dynamic view: the lot base textures and roads are drawn there and would cover shadows drawn earlier
}

IShadowOverlay* TheShadowOverlay() { return g_manager ? &g_adapter : nullptr; }

}  // namespace native
