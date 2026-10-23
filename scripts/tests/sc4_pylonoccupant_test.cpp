// cSC4PylonOccupant: the record, the camera-facing billboard RebuildModel builds, the call-number texture quad,
// the two draw passes, the 50-unit pick slab, the dispatch-state bookkeeping (UI state / dispatch state / type /
// call number / blink tick / drag), the texture families (police, fire, MySim), the placement bookkeeping and the
// version-5 record — driven through the class's seams (host, font, texture bindings, occupant manager, renderer)
// and the real cGZBuffer / cSLBuffer for the MySim texture blits.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "gz/Buffer.h"
#include "render/gd/GDriverBase.h"
#include "rz/PortMemoryStreams.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/DrawContext.h"
#include "sc4/PylonOccupant.h"
#include "sc4/RenderProperties.h"
#include "sc4/SLBuffer.h"
#include "sc4/ViewUtilities.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
#define CHECK_NEAR(a, b) CHECK(std::fabs((a) - (b)) <= 1e-5f)

using namespace sc4;

namespace {

// ---- fakes --------------------------------------------------------------------------------------------------
struct FakeBinding final : IPylonTextureBinding {
    std::uint32_t id = 0;
    int releases = 0;
    std::uint32_t TextureId() const override { return id; }
    void Release() override { ++releases; }
};

struct FakeFont final : IPylonFont {
    int addRefs = 0, releases = 0;
    int size = 0;
    std::vector<int> sizes;
    int textWidth = 20, textHeight = 30;
    int drawTexts = 0;
    std::uint32_t lastColor = 0;
    void AddRef() override { ++addRefs; }
    void Release() override { ++releases; }
    int GetFontSize() override { return size; }
    void SetFontSize(int value) override {
        size = value;
        sizes.push_back(value);
    }
    int TextHeight(const char*, std::uint32_t) override { return textHeight; }
    int TextWidth(const char*, std::uint32_t) override { return textWidth; }
    void DrawText(cIGZBuffer*, int, int, const char*, std::uint32_t, std::uint32_t color, std::uint32_t,
                  const void*, int) override {
        ++drawTexts;
        lastColor = color;
    }
};

struct FakeViewSet final : IPylonViewSet {
    int refs = 1;
    bool contains = false;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool ContainsOccupant(cISC4Occupant*) override { return contains; }
};

struct FakeMessages final : cIGZMessageServer2 {
    std::vector<std::pair<cIGZMessageTarget2*, std::uint32_t>> added, removed;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 2; }
    std::uint32_t Release() override { return 1; }
    bool MessageSend(cIGZMessage2*) override { return true; }
    bool MessagePost(cIGZMessage2*, bool) override { return true; }
    bool AddNotification(cIGZMessageTarget2* target, std::uint32_t type) override {
        added.emplace_back(target, type);
        return true;
    }
    bool RemoveNotification(cIGZMessageTarget2* target, std::uint32_t type) override {
        removed.emplace_back(target, type);
        return true;
    }
    bool GeneralMessagePostToTarget(cIGZMessage2*, cIGZMessageTarget2*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return 1; }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }
};

// A tiny occupant manager (the same shape sc4_signpostoccupant_test uses).
class MiniManager final : public cISC4OccupantManager {
public:
    int moves = 0, inserts = 0;
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool SetCitySize(float, float) override { return true; }
    void GetOccupantManagerCellSizes(float&, float&) const override {}
    void GetWorldCellCount(int&, int&) const override {}
    void GetOccupantManagerCellCount(int&, int&) const override {}
    void GetOccupantManagerCellBounds(int&, int&, int&, int&) const override {}
    void WorldCellToOccupantManagerCell(int, int, int&, int&) const override {}
    void OccupantManagerCellToWorldCell(int, int, int&, int&) const override {}
    void WorldCellToStandardCityCell(int, int, int&, int&) const override {}
    void StandardCityCellToWorldCell(int, int, int&, int&) const override {}
    void WorldCellToPosition(int, int, float&, float&) const override {}
    void PositionToWorldCell(float, float, int&, int&) const override {}
    void OccupantManagerCellToPosition(int, int, float&, float&) const override {}
    void PositionToOccupantManagerCell(float, float, int&, int&) const override {}
    void StandardCityCellToPosition(int, int, float&, float&) const override {}
    void PositionToStandardCityCell(float, float, int&, int&) const override {}
    bool InsertOccupant(cISC4Occupant* o, std::uint32_t) override {
        ++inserts;
        o->AddRef();
        items.push_back(o);
        return true;
    }
    bool RemoveOccupant(cISC4Occupant* o, bool, std::uint32_t) override {
        for (auto it = items.begin(); it != items.end(); ++it)
            if (*it == o) {
                items.erase(it);
                o->Release();
                return true;
            }
        return false;
    }
    bool RemoveOccupants(int, bool, std::uint32_t) override { return false; }
    bool RemoveOccupants(cISC4OccupantFilter*, bool, std::uint32_t) override { return false; }
    bool MoveOccupant(cISC4Occupant* o, bool) override {
        ++moves;
        moved.push_back(o);
        return true;
    }
    bool IsCellEmpty(int, int) const override { return true; }
    int GetBoundingCells(int, int, int (*)[2], int&) override { return 0; }
    bool FindOccupant(cISC4Occupant*, bool, int*, int*) override { return false; }
    bool GetFirstOccupantByPosition(cISC4Occupant*&, float, float, std::uint32_t) override { return false; }
    bool GetFirstOccupantByPosition(cISC4Occupant*&, float, float, cISC4OccupantFilter*) override { return false; }
    bool GetFirstOccupant(cISC4Occupant*&, int, int, std::uint32_t) override { return false; }
    bool GetFirstOccupant(cISC4Occupant*&, int, int, cISC4OccupantFilter*) override { return false; }
    bool GetFirstOccupantByStandardCityCell(cISC4Occupant*&, int, int, std::uint32_t) override { return false; }
    bool GetFirstOccupantByStandardCityCell(cISC4Occupant*&, int, int, cISC4OccupantFilter*) override { return false; }
    bool GetFirstOccupantByStandardCityCells(cISC4Occupant*&, const int*, const int*, std::uint32_t) override { return false; }
    bool GetFirstOccupantByStandardCityCells(cISC4Occupant*&, const int*, const int*, cISC4OccupantFilter*) override { return false; }
    bool GetOccupantsByBBox(OccupantList&, const float*, const float*, std::uint32_t, std::uint32_t) override { return false; }
    bool GetOccupantsByBBox(OccupantList&, const float*, const float*, cISC4OccupantFilter*, std::uint32_t) override { return false; }
    bool GetOccupantsByOccupantManagerCells(OccupantList&, const int*, const int*, std::uint32_t, std::uint32_t) override { return false; }
    bool GetOccupantsByOccupantManagerCells(OccupantList&, const int*, const int*, cISC4OccupantFilter*, std::uint32_t) override { return false; }
    bool GetOccupantsByStandardCityCells(OccupantList&, const int*, const int*, std::uint32_t, std::uint32_t) override { return false; }
    bool GetOccupantsByStandardCityCells(OccupantList&, const int*, const int*, cISC4OccupantFilter*, std::uint32_t) override { return false; }
    bool IterateOccupantsByBBox(OccupantCallback, void*, const float*, const float*, std::uint32_t) override { return false; }
    bool IterateOccupantsByBBox(OccupantCallback, void*, const float*, const float*, cISC4OccupantFilter*) override { return false; }
    bool IterateOccupants(OccupantCallback, void*, const int*, const int*, std::uint32_t) override { return false; }
    bool IterateOccupants(OccupantCallback, void*, const int*, const int*, cISC4OccupantFilter*) override { return false; }
    bool IterateOccupantsByStandardCityCell(OccupantCallback, void*, const int*, const int*, std::uint32_t) override { return false; }
    bool IterateOccupantsByStandardCityCell(OccupantCallback, void*, const int*, const int*, cISC4OccupantFilter*) override { return false; }
    void ReleaseOccupantList(OccupantList& list) override {
        for (auto* o : list) o->Release();
        list.clear();
    }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }

    OccupantList items, moved;
};

// The SL::spRM bitmap service cSLBuffer uses: hands out freshly built ARGB8888 buffers by resource group.
struct FakeBufferSource final : ISLBufferHost {
    std::vector<cIGZBuffer*> owned;   // one extra reference each; released by the test
    std::vector<std::uint32_t> requestedInstances;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> requestedGroups;

    static cGZBuffer* Make(std::uint32_t width, std::uint32_t height) {
        auto* buffer = new cGZBuffer();
        buffer->AddRef();
        if (!buffer->Init(width, height, cGZBufferColorType{kBufferColorARGB8888, 0x20})) {
            buffer->Release();
            return nullptr;
        }
        return buffer;
    }
    static void FillBuffer(cIGZBuffer* buffer, std::uint32_t color) {
        if (buffer->Lock(cIGZBuffer::kLockWrite)) {
            buffer->Fill(color, nullptr);
            buffer->Unlock(cIGZBuffer::kLockWrite);
        }
    }
    static void SetPixelAt(cIGZBuffer* buffer, int x, int y, std::uint32_t color) {
        if (buffer->Lock(cIGZBuffer::kLockWrite)) {
            auto* bits = static_cast<std::uint32_t*>(buffer->GetColorSurfaceBits());
            bits[static_cast<std::size_t>(y) * (buffer->GetColorSurfaceStride() / 4) + x] = color;
            buffer->Unlock(cIGZBuffer::kLockWrite);
        }
    }
    static std::uint32_t PixelAt(cIGZBuffer* buffer, int x, int y) {
        const auto* bits = static_cast<const std::uint32_t*>(buffer->GetColorSurfaceBits());
        return bits[static_cast<std::size_t>(y) * (buffer->GetColorSurfaceStride() / 4) + x];
    }
    cIGZBuffer* Base() const { return owned.empty() ? nullptr : owned.front(); }

    bool GetBufferResource(const cGZPersistResourceKey& key, std::int32_t, bool, cIGZBuffer** out) override {
        requestedGroups.emplace_back(key.group, key.instance);
        if (key.group == kPylonBitmapGroup) {
            requestedInstances.push_back(key.instance);
            cGZBuffer* bitmap = Make(10, 8);
            if (!bitmap) return false;
            FillBuffer(bitmap, 0xFFAABBCCu);
            SetPixelAt(bitmap, 2, 1, 0x00FF00FFu);   // magenta: the blit's colour key
            bitmap->AddRef();
            owned.push_back(bitmap);
            *out = bitmap;
            return true;
        }
        if (key.group == kPylonBaseBitmapGroup) {
            requestedInstances.push_back(key.instance);
            cGZBuffer* base = Make(0x34, 0xd0);
            if (!base) return false;
            FillBuffer(base, 0x00112233u);   // alpha 0 -> the blits may overwrite every pixel
            base->AddRef();
            owned.push_back(base);
            *out = base;
            return true;
        }
        return false;
    }
    bool HasResource(const cGZPersistResourceKey&) override { return true; }
    bool LowQualityTextures() override { return false; }
    bool LoadBufferFromFile(const std::string&, std::int32_t, cIGZBuffer** out) override {
        cGZBuffer* bitmap = Make(6, 6);
        if (!bitmap) return false;
        FillBuffer(bitmap, 0xFF445566u);
        bitmap->AddRef();
        owned.push_back(bitmap);
        *out = bitmap;
        return true;
    }
    cIGZBuffer* CreateBuffer() override {
        // an uninitialised buffer: cSLBuffer's colour-type conversion calls Init on it
        auto* buffer = new cGZBuffer();
        buffer->AddRef();
        buffer->AddRef();   // the caller releases its reference; the test keeps one
        owned.push_back(buffer);
        return buffer;
    }
    ~FakeBufferSource() override {
        for (cIGZBuffer* buffer : owned) buffer->Release();
    }
};

struct Host final : IPylonOccupantHost {
    FakeMessages messages;
    MiniManager manager;
    FakeBufferSource buffers;
    FakeFont font;
    std::vector<FakeBinding*> bindings;
    std::vector<std::uint32_t> bindingInstances;
    std::vector<s3d::cIS3DModelInstance*> installed;
    std::vector<std::pair<s3d::cIS3DModelInstance*, int>> changes;
    std::vector<std::pair<cIGZMessageTarget2*, int>> ticks;
    std::vector<cIGZBuffer*> uploaded;
    std::vector<std::uint32_t> releasedTextures;
    int moves = 0, drawContextCalls = 0;
    bool renderer = true;
    bool suppressed = false;
    float worldPerViewportUnit = 0.25f;
    float pitch = 0.0f;
    float viewDirX = 0.0f, viewDirZ = -1.0f;
    std::uint32_t nextTexture = 0x7000;

    ~Host() override {
        for (auto* binding : bindings) delete binding;
    }
    cIGZMessageServer2* Messages() override { return &messages; }
    void AddTickNotification(cIGZMessageTarget2* target, int channel) override { ticks.emplace_back(target, channel); }
    void RemoveTickNotification(cIGZMessageTarget2* target, int channel) override { ticks.emplace_back(target, -channel); }
    cISC4OccupantManager* OccupantManager() override { return &manager; }
    bool RendererPresent() override { return renderer; }
    bool RendererHasModel(s3d::cIS3DModelInstance* model) override {
        for (auto* installedModel : installed)
            if (installedModel == model) return true;
        return false;
    }
    bool RendererInsertModel(s3d::cIS3DModelInstance* model) override {
        installed.push_back(model);
        return true;
    }
    bool RendererRemoveModel(s3d::cIS3DModelInstance* model) override {
        for (auto it = installed.begin(); it != installed.end(); ++it)
            if (*it == model) {
                installed.erase(it);
                return true;
            }
        return false;
    }
    bool RendererMoveModel(s3d::cIS3DModelInstance*) override {
        ++moves;
        return true;
    }
    bool RendererChangeModel(s3d::cIS3DModelInstance* model, int reason) override {
        changes.emplace_back(model, reason);
        return true;
    }
    void RendererGetDrawContext() override { ++drawContextCalls; }
    float ViewportUnitsToWorldUnits(float units) override { return units * worldPerViewportUnit; }
    float ViewPitchAngle() override { return pitch; }
    void ViewDirection(float& x, float& z) override {
        x = viewDirX;
        z = viewDirZ;
    }
    bool OccupantDrawSuppressed() override { return suppressed; }
    bool CreateBuffer(cIGZBuffer** out) override {
        // RZ::GS() vslot 0x20 hands out an *uninitialised* buffer; both callers Init it themselves
        auto* buffer = new cGZBuffer();
        buffer->AddRef();
        *out = buffer;
        return true;
    }
    IPylonFont* CallNumberFont() override { return &font; }
    bool UploadTexture(cIGZBuffer* buffer, std::uint32_t& handle) override {
        uploaded.push_back(buffer);
        handle = ++nextTexture;
        return true;
    }
    void ReleaseTexture(std::uint32_t handle) override { releasedTextures.push_back(handle); }
    bool LoadBitmapFromFile(const cIGZString&, cIGZBuffer** out) override {
        cGZBuffer* bitmap = FakeBufferSource::Make(6, 6);
        if (!bitmap) return false;
        FakeBufferSource::FillBuffer(bitmap, 0xFF445566u);
        bitmap->AddRef();
        buffers.owned.push_back(bitmap);
        *out = bitmap;
        return true;
    }
    IPylonTextureBinding* CreateTextureBinding(std::uint32_t instance) override {
        auto* binding = new FakeBinding();
        binding->id = ++nextTexture;
        bindings.push_back(binding);
        bindingInstances.push_back(instance);
        return binding;
    }
};

// The recording driver the draw context talks to (same shape as sc4_signpostoccupant_test).
struct Driver final : gd::cGDriverBase {
    gd::sGDMode mode{};
    std::vector<std::uint32_t> textures;
    std::vector<std::pair<int, int>> draws;      // primitive, count
    std::vector<std::pair<int, int>> formats;    // format, stride
    int matrices = 0;
    bool GetVideoModeInfo(gd::sGDMode& out) override {
        out = mode;
        return true;
    }
    void DrawArrays(gd::eGDPrimType primitive, int, int count) override {
        draws.push_back({static_cast<int>(primitive), count});
    }
    void InterleavedArrays(gd::eGDVertexFormat format, int stride, const void*) override {
        formats.push_back({static_cast<int>(format), stride});
    }
    void SetTexture(std::uint32_t texture, std::uint32_t) override { textures.push_back(texture); }
    void LoadMatrix(const float*) override { ++matrices; }
};

void ExpectTopQuad(const PylonVertexT2F* quad, float halfWidth, float height, float u0, float u1, float v1) {
    CHECK_NEAR(quad[0].x, -halfWidth);
    CHECK_NEAR(quad[0].y, height);
    CHECK_NEAR(quad[0].z, 0.0f);
    CHECK_NEAR(quad[0].u, u0);
    CHECK_NEAR(quad[0].v, 0.0f);
    CHECK_NEAR(quad[1].x, -halfWidth);
    CHECK_NEAR(quad[1].y, 0.0f);
    CHECK_NEAR(quad[1].u, u0);
    CHECK_NEAR(quad[1].v, v1);
    CHECK_NEAR(quad[2].x, halfWidth);
    CHECK_NEAR(quad[2].y, 0.0f);
    CHECK_NEAR(quad[2].u, u1);
    CHECK_NEAR(quad[2].v, v1);
    CHECK_NEAR(quad[3].x, halfWidth);
    CHECK_NEAR(quad[3].y, height);
    CHECK_NEAR(quad[3].u, u1);
    CHECK_NEAR(quad[3].v, 0.0f);
}

}  // namespace

int main() {
    Host host;
    SetPylonOccupantHost(&host);
    SetSLBufferHost(&host.buffers);

    // ---- construction and the interface identity ----------------------------------------------------------
    auto* pylon = new cSC4PylonOccupant();
    pylon->AddRef();
    CHECK(!pylon->IsInitialized());
    CHECK(pylon->GetType() == kPylonOccupantType);
    CHECK(pylon->GetGZCLSID() == kGZCLSID_cSC4PylonOccupant);
    CHECK(pylon->GetProperties() == 3);
    CHECK(pylon->GetUIState() == 1 && pylon->GetDispatchState() == 0 && pylon->GetDispatchType() == 0);
    CHECK(pylon->GetCallNumber() == 0 && !pylon->GetDragState());
    CHECK(pylon->GetSourceOccupant() == nullptr && pylon->GetPylonOwner() == nullptr);
    CHECK(pylon->IsDirty() && !pylon->Blink());
    CHECK(pylon->CurrentTexture() == 0 && pylon->CallNumberTexture() == 0 && pylon->TextureBinding() == nullptr);
    CHECK(!pylon->IsInOccupantManager() && !pylon->IsInRenderer());
    {
        cS3DTransform transform;
        pylon->GetTransform(transform);
        // nS3D::AssignTransform counts on the *destination*: the stored counter is not copied (the original's
        // GetTransform does INC [out] and then copies the fields)
        CHECK(transform.changeCounter == 1 && transform.flags == 0);
        CHECK(transform.rotation[0][0] == 1.0f && transform.rotation[1][1] == 1.0f && transform.rotation[2][2] == 1.0f);
        CHECK(transform.translation[0] == 0.0f && transform.scale == 1.0f);
        cS3DBoundingBox box;
        pylon->GetBounds(box);
        CHECK(box.min[0] == 1.0f && box.min[1] == 1.0f && box.min[2] == 1.0f);   // the ctor's "empty" box
        CHECK(box.max[0] == 0.0f && box.max[1] == 0.0f && box.max[2] == 0.0f);
        pylon->GetWorldBounds(box);
        CHECK(box.min[0] == 1.0f && box.max[0] == 0.0f);
        std::uint32_t sort = 0xffffffffu;
        pylon->GetSortValue(sort);
        CHECK(sort == 0);   // the texture handle, not a distance
    }
    CHECK(pylon->GetOwner() == static_cast<cIGZUnknown*>(pylon->AsOccupant()));
    {
        void* out = nullptr;
        CHECK(pylon->QueryInterface(kGZIID_cISC4PylonOccupant, &out));
        CHECK(out == static_cast<cISC4PylonOccupant*>(pylon));
        pylon->Release();   // the QI reference
        out = nullptr;
        CHECK(pylon->QueryInterface(kGZIID_cIGZMessageTarget2, &out));
        CHECK(out == static_cast<cIGZMessageTarget2*>(pylon));
        pylon->Release();
        out = nullptr;
        CHECK(pylon->QueryInterface(kGZIID_cISC4Occupant, &out));
        CHECK(out == pylon->AsOccupant());
        pylon->Release();
        out = nullptr;
        CHECK(pylon->QueryInterface(s3d::kGZIID_cIS3DModelInstance, &out));
        CHECK(out == static_cast<s3d::cIS3DModelInstance*>(pylon));
        pylon->Release();
        out = nullptr;
        CHECK(!pylon->QueryInterface(0x12345678u, &out));
    }

    // ---- Init: notifications, render states, the renderer's draw context ----------------------------------
    CHECK(pylon->Init());
    CHECK(pylon->IsInitialized());
    CHECK(host.messages.added.size() == 3);
    CHECK(host.messages.added[0].second == kPylonMessageViewChanged);
    CHECK(host.messages.added[1].second == kPylonMessageViewSet);
    CHECK(host.messages.added[2].second == kPylonMessageRebuild);
    CHECK(host.ticks.size() == 1 && host.ticks[0].second == kPylonTickChannel);
    CHECK(host.drawContextCalls == 1);
    {
        cSC4PackedRenderState pass1, pass0;
        pass1.PackRenderState(true, true, true, true, true, 1, true, 3, 4, 5, 4, 0.0f, 1, -1);
        pass0.PackRenderState(true, false, false, true, true, 1, true, -1, 4, 5, 4, 0.0f, 1, -1);
        CHECK(pylon->RenderStatePass1().value == pass1.value && pylon->RenderStatePass1().mask == pass1.mask);
        CHECK(pylon->RenderStatePass0().value == pass0.value && pylon->RenderStatePass0().mask == pass0.mask);
    }

    // ---- RebuildModel: the billboard, the police texture, the placement -----------------------------------
    pylon->RebuildModel();
    CHECK(!pylon->IsDirty());
    CHECK(host.bindingInstances.size() == 1 && host.bindingInstances[0] == kPoliceDispatchPylonResId[0]);
    CHECK(host.bindings.size() == 1 && pylon->CurrentTexture() == host.bindings[0]->id);
    // 44 viewport units wide (0.25 world units each) -> half width 5.5; 150 viewport units high -> 37.5
    ExpectTopQuad(pylon->Quad(), 5.5f, 37.5f, 0.171875f, 0.34375f, 0.5859375f);
    {
        cS3DBoundingBox box;
        pylon->GetBounds(box);
        CHECK_NEAR(box.min[0], -5.5f);
        CHECK_NEAR(box.min[1], 0.0f);
        CHECK_NEAR(box.max[0], 5.5f);
        CHECK_NEAR(box.max[1], 37.5f);
        pylon->GetWorldBounds(box);
        CHECK_NEAR(box.min[0], -5.5f);
        CHECK_NEAR(box.max[1], 37.5f);
        float minXyz[3], maxXyz[3];
        pylon->GetBoundingBox(minXyz, maxXyz);
        CHECK_NEAR(minXyz[0], -5.5f);
        CHECK_NEAR(maxXyz[1], 37.5f);
        cS3DTransform transform;
        pylon->GetTransform(transform);
        CHECK(transform.flags == 4);
        CHECK(transform.changeCounter == 1);
        CHECK_NEAR(transform.rotation[0][0], 1.0f);
        CHECK(pylon->IsInOccupantManager() && pylon->IsInRenderer());
        CHECK(host.manager.items.size() == 1 && host.manager.items.front() == pylon->AsOccupant());
        CHECK(host.installed.size() == 1);
    }

    // a view rotated by -pi/2 (view direction +x) turns the flat billboard to face x; only two corners are boxed
    host.viewDirX = 1.0f;
    host.viewDirZ = 0.0f;
    {
        cRZMessage2Standard message;
        message.SetType(0xa6b79602u);   // "view changed": only marks the model dirty
        CHECK(pylon->DoMessage(&message));
        CHECK(pylon->IsDirty());
        message.SetType(kPylonMessageRebuild);
        CHECK(pylon->DoMessage(&message));
        CHECK(!pylon->IsDirty());
        cS3DBoundingBox box;
        pylon->GetWorldBounds(box);
        CHECK_NEAR(box.min[0], 0.0f);
        CHECK_NEAR(box.max[0], 0.0f);
        CHECK_NEAR(box.min[2], -5.5f);
        CHECK_NEAR(box.max[2], 5.5f);
        CHECK_NEAR(box.max[1], 37.5f);
        CHECK(pylon->Transform().changeCounter == 3);   // reset to the identity + the composed rotation
        cS3DTransform transform;
        pylon->GetTransform(transform);
        CHECK_NEAR(transform.rotation[0][2], -1.0f);
        CHECK_NEAR(transform.rotation[2][0], 1.0f);
        CHECK_NEAR(transform.rotation[1][1], 1.0f);
    }
    host.viewDirX = 0.0f;
    host.viewDirZ = -1.0f;
    pylon->RebuildModel();

    // ---- the UI / dispatch state bookkeeping and the blink tick -------------------------------------------
    pylon->SetUIState(2);
    CHECK(pylon->IsDirty() && pylon->GetUIState() == 2);
    pylon->RebuildModel();
    ExpectTopQuad(pylon->Quad(), 5.5f, 37.5f, 0.34375f, 0.515625f, 0.5859375f);
    pylon->SetUIState(2);   // unchanged: no rebuild request
    CHECK(!pylon->IsDirty());
    pylon->SetUIState(0);   // 0 behaves like 1 in the quad
    pylon->RebuildModel();
    ExpectTopQuad(pylon->Quad(), 5.5f, 37.5f, 0.171875f, 0.34375f, 0.5859375f);
    // a dispatch state of 1 or 4 blinks: 500 elapsed seconds flip the flag and force a rebuild
    pylon->SetDispatchState(1);
    CHECK(pylon->IsDirty());
    {
        cRZMessage2Standard tick;
        tick.SetType(kPylonMessageAnimationTick);
        tick.SetData2(499000);   // 499 s: not yet
        pylon->RebuildModel();   // consumes the dirty flag
        CHECK(pylon->DoMessage(&tick));
        CHECK(!pylon->Blink());
        CHECK(!pylon->IsDirty());
        tick.SetData2(1000);
        CHECK(pylon->DoMessage(&tick));
        CHECK(pylon->Blink());
        CHECK(pylon->IsDirty());
    }
    pylon->RebuildModel();
    ExpectTopQuad(pylon->Quad(), 5.5f, 37.5f, 0.515625f, 0.6875f, 0.5859375f);   // 3 * the u span
    pylon->SetDispatchState(5);
    CHECK(!pylon->Blink());
    {
        cRZMessage2Standard tick;
        tick.SetType(kPylonMessageAnimationTick);
        tick.SetData2(600000);
        pylon->RebuildModel();
        pylon->DoMessage(&tick);
        CHECK(!pylon->Blink());   // state 5 does not tick
    }
    // the MySim dispatch type selects the second quad table
    pylon->SetDispatchType(4);
    pylon->RebuildModel();
    ExpectTopQuad(pylon->Quad(), 5.5f, 37.5f, 0.203125f, 0.40625f, 0.6924715638160706f);
    pylon->SetDispatchType(0);
    pylon->RebuildModel();

    // ---- SetPosition moves the box, re-places the occupant and keeps the flag ------------------------------
    {
        host.manager.moves = 0;
        host.installed.clear();
        const std::uint8_t counterBefore = pylon->Transform().changeCounter;
        const float position[3] = {100.0f, 2.0f, 50.0f};
        pylon->SetPosition(position);
        float readBack[3] = {0.0f, 0.0f, 0.0f};
        pylon->GetPosition(readBack);
        CHECK_NEAR(readBack[0], 100.0f);
        CHECK_NEAR(readBack[1], 2.0f);
        CHECK_NEAR(readBack[2], 50.0f);
        cS3DBoundingBox box;
        pylon->GetWorldBounds(box);
        CHECK_NEAR(box.min[0], 94.5f);
        CHECK_NEAR(box.min[1], 2.0f);
        CHECK_NEAR(box.min[2], 50.0f);
        CHECK_NEAR(box.max[0], 105.5f);
        CHECK_NEAR(box.max[1], 39.5f);
        CHECK((pylon->Transform().flags & 1) != 0);
        CHECK(pylon->Transform().changeCounter > counterBefore);
        CHECK(pylon->IsInOccupantManager() && pylon->IsInRenderer());
        CHECK(host.manager.items.size() == 1 && host.installed.size() == 1);
        CHECK(host.manager.moves == 0);   // SetPosition takes it out first, so RebuildModel inserts it again
    }

    // ---- transforms: AssignTransform copies everything and bumps the destination's counter ----------------
    {
        cS3DTransform value{};
        value.flags = 3;
        value.rotation[0][1] = 0.25f;
        value.translation[2] = -7.0f;
        value.scale = 2.0f;
        value.changeCounter = 42;
        const std::uint8_t storedBefore = pylon->Transform().changeCounter;
        pylon->SetTransform(value);
        CHECK(pylon->Transform().changeCounter == static_cast<std::uint8_t>(storedBefore + 1));
        cS3DTransform out;
        out.changeCounter = 9;
        pylon->GetTransform(out);
        CHECK(out.changeCounter == 10);   // the destination's counter, not the stored one
        CHECK(out.flags == 3);
        CHECK_NEAR(out.rotation[0][1], 0.25f);
        CHECK_NEAR(out.translation[2], -7.0f);
        CHECK_NEAR(out.scale, 2.0f);
        // RebuildModel forces the unit scale and re-composes the rotation
        pylon->RebuildModel();
        CHECK_NEAR(pylon->Transform().scale, 1.0f);
        CHECK((pylon->Transform().flags & 2) == 0);
        CHECK((pylon->Transform().flags & 4) != 0);
    }

    // ---- Pick: the 50-unit slab under the top of the world box -------------------------------------------
    {
        cS3DTransform restore{};
        pylon->SetTransform(restore);
        host.viewDirX = 0.0f;
        host.viewDirZ = -1.0f;
        pylon->SetUIState(1);
        const float position[3] = {0.0f, 0.0f, 0.0f};
        pylon->SetPosition(position);
        // world box = (-5.5,0,0) .. (5.5,37.5,0); the slab is 50 * 0.25 = 12.5 high under 37.5 -> 25..37.5
        cSC4PickContext context;
        context.origin = {0.0f, 30.0f, -100.0f};
        context.direction = {0.0f, 0.0f, 1.0f};
        context.maxDistance = 200.0f;
        CHECK(pylon->Pick(&context));
        CHECK_NEAR(context.maxDistance, 100.0f);
        cSC4PickContext tooFar;
        tooFar.origin = {0.0f, 30.0f, -100.0f};
        tooFar.direction = {0.0f, 0.0f, 1.0f};
        tooFar.maxDistance = 50.0f;
        CHECK(!pylon->Pick(&tooFar));
        CHECK_NEAR(tooFar.maxDistance, 50.0f);
        cSC4PickContext below;
        below.origin = {0.0f, 10.0f, -100.0f};   // under the slab
        below.direction = {0.0f, 0.0f, 1.0f};
        below.maxDistance = 200.0f;
        CHECK(!pylon->Pick(&below));
        pylon->SetUIState(0);   // a hidden pylon is never picked
        cSC4PickContext hidden;
        hidden.origin = {0.0f, 30.0f, -100.0f};
        hidden.direction = {0.0f, 0.0f, 1.0f};
        hidden.maxDistance = 200.0f;
        CHECK(!pylon->Pick(&hidden));
        pylon->SetUIState(1);
    }

    // ---- the call number: font metrics, the "%d" texture and its quad -------------------------------------
    {
        host.font.sizes.clear();
        pylon->SetCallNumber(42);
        CHECK(pylon->IsDirty() && pylon->GetCallNumber() == 42);
        const std::size_t uploadsBefore = host.uploaded.size();
        pylon->RebuildModel();
        CHECK(host.font.addRefs == 1 && host.font.releases == 1);
        CHECK(host.font.sizes.size() == 2 && host.font.sizes[0] == kPylonCallNumberFontSize && host.font.sizes[1] == 0);
        CHECK(host.font.drawTexts == 1);
        CHECK(host.font.lastColor == 0xff000000u);
        CHECK(host.uploaded.size() == uploadsBefore + 1);
        CHECK(pylon->CallNumberTexture() != 0);
        // width 20, height 30 -> a 32x32 texture; top = 37.5 - 14*0.25 - 9*0.25, bottom = top - 3.5
        const PylonVertexT2F* quad = pylon->CallNumberQuad();
        CHECK_NEAR(quad[0].x, -2.5f);
        CHECK_NEAR(quad[0].y, 31.75f);
        CHECK_NEAR(quad[0].z, 0.01f);
        CHECK_NEAR(quad[0].u, 0.0f);
        CHECK_NEAR(quad[0].v, 0.0f);
        CHECK_NEAR(quad[1].y, 28.25f);
        CHECK_NEAR(quad[1].v, 30.0f / 32.0f);
        CHECK_NEAR(quad[2].x, 2.5f);
        CHECK_NEAR(quad[2].u, 20.0f / 32.0f);
        CHECK_NEAR(quad[3].y, 31.75f);
        // the previous call-number texture is released by the next build
        const std::uint32_t firstTexture = pylon->CallNumberTexture();
        host.releasedTextures.clear();
        pylon->SetCallNumber(7);
        pylon->RebuildModel();
        CHECK(host.releasedTextures.size() == 1 && host.releasedTextures[0] == firstTexture);
        // a large number asks for the next power of two >= max(width, height)
        host.font.textWidth = 40;
        host.font.textHeight = 90;
        const std::size_t before = host.uploaded.size();
        pylon->RebuildModel();   // call number 7 unchanged but RebuildModel rebuilds the texture anyway
        CHECK(host.uploaded.size() == before + 1);
        CHECK_NEAR(pylon->CallNumberQuad()[2].u, 40.0f / 128.0f);
        CHECK_NEAR(pylon->CallNumberQuad()[2].v, 90.0f / 128.0f);
        host.font.textWidth = 20;
        host.font.textHeight = 30;
    }

    // ---- Draw: the two passes, the call-number quad and the suppression flag ------------------------------
    {
        Driver driver;
        driver.mode.flags[1] = 3;
        cSC4RenderProperties properties;
        SC4DrawContext context;
        context.SetDriver(&driver);
        context.SetRenderProperties(&properties);
        context.Init();
        CHECK(context.IsInitialized());
        // pass 1 draws the billboard and (call number != 0) the number quad
        context.SetRenderPass(1);
        driver.draws.clear();
        driver.formats.clear();
        driver.textures.clear();
        CHECK(pylon->Draw(&context, false) == 1);
        CHECK(driver.draws.size() == 2);
        CHECK(driver.formats.size() == 2 && driver.formats[0].first == 2 && driver.formats[1].first == 2);
        CHECK(driver.textures.size() == 2);
        CHECK(driver.textures[0] == pylon->CurrentTexture());
        CHECK(driver.textures[1] == pylon->CallNumberTexture());
        CHECK(driver.draws[0].first == 6 && driver.draws[0].second == 4);
        // pass 0 is the transparent pass: one draw and a "2" result; the highlight call does nothing
        context.SetRenderPass(0);
        driver.draws.clear();
        driver.textures.clear();
        CHECK(pylon->Draw(&context, false) == 2);
        CHECK(driver.draws.size() == 1);
        CHECK(driver.textures.size() == 1);
        driver.draws.clear();
        CHECK(pylon->Draw(&context, true) == 1);
        CHECK(driver.draws.empty());
        // pass 0 rebuilds a dirty model first
        pylon->SetUIState(2);
        CHECK(pylon->IsDirty());
        driver.draws.clear();
        CHECK(pylon->Draw(&context, false) == 2);
        CHECK(!pylon->IsDirty());
        CHECK(driver.draws.size() == 1);
        // a dispatch state of 1 hides the call number; so does the MySim dispatch type and a zero call number
        pylon->SetDispatchState(1);
        pylon->RebuildModel();
        context.SetRenderPass(1);
        driver.draws.clear();
        CHECK(pylon->Draw(&context, false) == 1);
        CHECK(driver.draws.size() == 1);
        pylon->SetDispatchState(0);
        pylon->SetDispatchType(4);
        pylon->RebuildModel();
        driver.draws.clear();
        CHECK(pylon->Draw(&context, false) == 1);
        CHECK(driver.draws.size() == 1);
        pylon->SetDispatchType(0);
        pylon->SetCallNumber(0);
        pylon->RebuildModel();
        driver.draws.clear();
        CHECK(pylon->Draw(&context, false) == 1);
        CHECK(driver.draws.size() == 1);
        // the render-properties flag suppresses the whole draw
        host.suppressed = true;
        driver.draws.clear();
        CHECK(pylon->Draw(&context, false) == 1);
        CHECK(driver.draws.empty());
        host.suppressed = false;
        pylon->SetCallNumber(9);
        pylon->RebuildModel();
    }

    // ---- the MySim texture: the four 0x34-row tiles and the colour-key / alpha rules -----------------------
    {
        const std::size_t bindingsBefore = host.bindings.size();
        pylon->SetCallNumber(0);   // no call-number texture in this block: the only upload is the MySim one
        pylon->SetDispatchState(0);
        pylon->SetDispatchType(4);
        pylon->SetBitmapInfo(0x1234u, cRZString("mysim"));
        CHECK(pylon->GetDispatchType() == 4);
        CHECK(pylon->BitmapId() == 0x1234);
        CHECK(std::strcmp(pylon->BitmapName().ToChar(), "mysim") == 0);
        host.buffers.requestedGroups.clear();
        const std::size_t uploadsBefore = host.uploaded.size();
        pylon->RebuildModel();
        // no texture binding for the MySim type, the texture comes from BuildMySimTexture
        CHECK(host.bindings.size() == bindingsBefore);
        CHECK(host.uploaded.size() == uploadsBefore + 1);
        CHECK(pylon->CurrentTexture() != 0);
        CHECK(host.buffers.requestedGroups.size() == 2);
        CHECK(host.buffers.requestedGroups[0].first == kPylonBaseBitmapGroup);
        CHECK(host.buffers.requestedGroups[0].second == kMySimDispatchPylonResId[0]);
        CHECK(host.buffers.requestedGroups[1].first == kPylonBitmapGroup);
        CHECK(host.buffers.requestedGroups[1].second == 0x1234u);
        // the destination is the buffer cSLBuffer converted the base picture into: alpha 0 everywhere, and the
        // three overlay tiles were stamped at rows (tile * 0x34 + 21) .. +8 with the magenta pixel left alone
        CHECK(host.buffers.owned.size() >= 2);
        cIGZBuffer* destination = host.uploaded.back();
        const int startX = 21;    // (0x34 - 10) / 2, the column of the first stamped pixel
        const int startY = 10;    // (0x34 - 10) / 4, the row of the first tile
        for (int tile = 1; tile < 4; ++tile) {
            const int column = tile * 0x34 + startX;
            const int row = startY + (tile - 1) * 0x34;   // the running start-y advances one tile per pass
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 10; ++x) {
                    const std::uint32_t value = FakeBufferSource::PixelAt(destination, column + x, row + y);
                    if (x == 2 && y == 1)
                        CHECK(value == 0x00112233u);   // the magenta source pixel is the colour key
                    else
                        CHECK(value == 0xFFAABBCCu);
                }
            }
        }
        // the text path: bitmap id 0 loads the bitmap from the stored name instead
        pylon->SetBitmapInfo(0, cRZString("other"));
        host.buffers.requestedGroups.clear();
        pylon->RebuildModel();
        CHECK(host.buffers.requestedGroups.size() == 1);
        CHECK(host.buffers.requestedGroups[0].first == kPylonBaseBitmapGroup);
        // ... and the pixels of that 6x6 overlay land in the destination too
        cIGZBuffer* textDestination = host.uploaded.back();
        // 6 wide: start column (0x34 - 6) / 2 = 23, first row (0x34 - 6) / 4 = 11
        CHECK(FakeBufferSource::PixelAt(textDestination, 0x34 + 23, 11) == 0xFF445566u);
    }

    // ---- the version-5 record ----------------------------------------------------------------------------
    {
        pylon->SetDispatchType(0);
        pylon->SetDispatchState(3);
        pylon->SetUIState(2);
        pylon->SetCallNumber(77);
        pylon->SetDragState(true);
        pylon->RebuildModel();
        PortMemoryOStream stream;
        CHECK(pylon->Write(stream));
        const auto& bytes = stream.bytes();
        CHECK(bytes.size() > 2 && bytes[0] == kPylonRecordVersion && bytes[1] == 0);
        auto* copy = new cSC4PylonOccupant();
        copy->AddRef();
        PortMemoryIStream input(bytes.data(), bytes.size());
        CHECK(copy->Read(input));
        CHECK(copy->GetUIState() == 2 && copy->GetDispatchState() == 3 && copy->GetDispatchType() == 0);
        CHECK(copy->GetCallNumber() == 77 && copy->GetDragState());
        CHECK(copy->GetType() == kPylonOccupantType);
        CHECK_NEAR(copy->Transform().rotation[0][0], 1.0f);
        CHECK_NEAR(copy->Transform().scale, 1.0f);
        CHECK(std::strcmp(copy->BitmapName().ToChar(), "other") == 0);
        copy->Release();
        // version 2 records are refused before the base record is read
        auto* old = new cSC4PylonOccupant();
        old->AddRef();
        const std::uint8_t version2[2] = {2, 0};
        PortMemoryIStream oldInput(version2, sizeof version2);
        CHECK(!old->Read(oldInput));
        old->Release();
    }

    // ---- the view-set message adds / removes the model from the renderer ----------------------------------
    {
        FakeViewSet view;
        IPylonViewSet* slot = &view;
        cRZMessage2Standard message;
        message.SetType(kPylonMessageViewSet);
        message.SetVoid2(&slot);
        // the pylon is currently in the renderer: leaving the view removes it
        CHECK(pylon->IsInRenderer());
        CHECK(pylon->DoMessage(&message));
        CHECK(!pylon->IsInRenderer());
        view.contains = true;
        CHECK(pylon->DoMessage(&message));
        CHECK(pylon->IsInRenderer());
        // a null view-set pointer is skipped
        IPylonViewSet* nullSlot = nullptr;
        message.SetVoid2(&nullSlot);
        CHECK(pylon->DoMessage(&message));
        CHECK(pylon->IsInRenderer());
    }

    // ---- SetVisibility / the owner and source-occupant references -----------------------------------------
    {
        pylon->SetFlag(kOccupantFlagVisible);
        pylon->SetVisibility(false, false);
        CHECK(!pylon->IsInRenderer());   // placed + hidden -> out of the renderer
        pylon->SetVisibility(true, false);
        CHECK(pylon->IsInRenderer());
        auto* owner = new cRZMessage2Standard();   // any cIGZUnknown with a real refcount
        owner->AddRef();                           // the test's own reference
        pylon->SetPylonOwner(owner);
        CHECK(pylon->GetPylonOwner() == owner);
        pylon->SetPylonOwner(nullptr);
        CHECK(pylon->GetPylonOwner() == nullptr);
        pylon->SetSourceOccupant(pylon->AsOccupant());
        CHECK(pylon->GetSourceOccupant() == pylon->AsOccupant());
        pylon->SetSourceOccupant(nullptr);
        CHECK(pylon->GetSourceOccupant() == nullptr);
        CHECK(owner->Release() == 0);   // the pylon's reference was dropped
    }

    // ---- without a renderer: Init packs nothing and the renderer view stays empty --------------------------
    {
        host.renderer = false;
        auto* bare = new cSC4PylonOccupant();
        bare->AddRef();
        CHECK(bare->Init());
        CHECK(bare->RenderStatePass1().value == 0 && bare->RenderStatePass1().mask == 0);
        CHECK(bare->RenderStatePass0().value == 0 && bare->RenderStatePass0().mask == 0);
        const float position[3] = {0.0f, 0.0f, 0.0f};
        bare->SetPosition(position);
        CHECK(bare->IsInOccupantManager());
        CHECK(!bare->IsInRenderer());
        CHECK(!bare->PlaceInRenderer(true));
        CHECK(!bare->IsInRenderer());
        CHECK(bare->Shutdown());
        bare->Release();
        host.renderer = true;
    }

    // ---- Shutdown releases the textures, the references and the notifications -----------------------------
    {
        const std::uint32_t currentTexture = pylon->CallNumberTexture();
        (void)currentTexture;
        host.releasedTextures.clear();
        const std::size_t removedBefore = host.messages.removed.size();
        const std::size_t ticksBefore = host.ticks.size();
        const std::uint32_t callNumberTexture = pylon->CallNumberTexture();
        CHECK(pylon->Shutdown());
        CHECK(!pylon->IsInitialized());
        CHECK(pylon->CallNumberTexture() == 0);
        bool released = false;
        for (std::uint32_t handle : host.releasedTextures)
            if (handle == callNumberTexture) released = true;
        CHECK(released);
        CHECK(host.messages.removed.size() == removedBefore + 3);
        CHECK(host.ticks.size() == ticksBefore + 1 && host.ticks.back().second == -kPylonTickChannel);
        CHECK(!pylon->IsInOccupantManager());
        CHECK(!pylon->IsInRenderer());
        CHECK(host.manager.items.empty());
        CHECK(host.installed.empty());
    }
    pylon->Release();

    if (failures == 0) std::printf("cSC4PylonOccupant: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
