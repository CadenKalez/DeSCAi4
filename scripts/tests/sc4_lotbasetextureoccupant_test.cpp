// Tests cSC4LotBaseTextureOccupant: identity/layout, the version 2 record, cell coverage, the texture-specification
// update paths (in-place match, rebuild, empty list), frame randomisation and the per-texture frame-count cache, the
// terrain / locked-lot bounding box, and the model load / unload / renderer paths. The unrecovered
// cSC4CellTextureModel and the engine globals are fakes behind ILotCellTextureModel / ILotBaseTextureHost.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "rz/PortMemoryStreams.h"
#include "sc4/Lot.h"
#include "sc4/LotBaseTextureOccupant.h"
#include "sc4/PlaceableObject.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {

// ---------------------------------------------------------------- cISC4CellTextureModel fake (cSC4CellTextureModel)

struct FakeCellModel;

// The cIS3DModelInstance subobject of the fake model; its reference count is the model's.
struct FakeModelInstance final : cIS3DModelInstance {
    explicit FakeModelInstance(FakeCellModel* model) : model_(model) {}
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override;
    std::uint32_t Release() override;
    void SetOwner(void*) override {}

    FakeCellModel* model_;
};

struct FakeCellModel final : ILotCellTextureModel {
    FakeCellModel() : instance_(this) {}

    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cISC4CellTextureModel || iid == 1) {
            *out = static_cast<ILotCellTextureModel*>(this);
            AddRef();
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override {
        if (refs > 1) return --refs;
        delete this;
        return 0;
    }
    void UpdateTextures(int zoom, const LotBaseTextureSpecification& spec) override {
        ++textureCalls;
        lastZoom = zoom;
        lastSpec = spec;
    }
    void SetupVertices(bool updateVertices, bool updateLighting, void* terrain, void* lighting) override {
        ++setupCalls;
        verticesArg = updateVertices;
        lightingArg = updateLighting;
        terrainArg = terrain;
        lightingPtrArg = lighting;
    }
    cIS3DModelInstance* ModelInstance() override { return &instance_; }

    FakeModelInstance instance_;
    std::uint32_t refs = 0;
    int textureCalls = 0;
    int setupCalls = 0;
    int lastZoom = -1;
    bool verticesArg = false;
    bool lightingArg = false;
    void* terrainArg = nullptr;
    void* lightingPtrArg = nullptr;
    LotBaseTextureSpecification lastSpec;
};

bool FakeModelInstance::QueryInterface(std::uint32_t iid, void** out) { return model_->QueryInterface(iid, out); }
std::uint32_t FakeModelInstance::AddRef() { return model_->AddRef(); }
std::uint32_t FakeModelInstance::Release() { return model_->Release(); }

// A model instance that is not a cell-texture model (its QueryInterface always fails).
struct PlainInstance final : cIS3DModelInstance {
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override {
        if (refs > 1) return --refs;
        delete this;
        return 0;
    }
    void SetOwner(void*) override {}
    std::uint32_t refs = 1;
};

// ---------------------------------------------------------------- the placeable object

struct FakePlaceable final : cISC43DPlaceableObject {
    ~FakePlaceable() { RemoveAll(); }
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cISC43DPlaceableObject || iid == 1) {
            *out = static_cast<cISC43DPlaceableObject*>(this);
            AddRef();
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override {
        if (refs > 1) return --refs;
        delete this;
        return 0;
    }
    int GetModelCount() override { return static_cast<int>(models.size()); }
    std::uint32_t GetModels(cIS3DModelInstance** out, std::uint32_t& count) override {
        count = std::min<std::uint32_t>(count, static_cast<std::uint32_t>(models.size()));
        for (std::uint32_t i = 0; i < count; ++i) out[i] = models[i];
        return count;
    }
    bool AddModel(cIS3DModelInstance* model) override {
        model->AddRef();
        models.push_back(model);
        ++adds;
        return true;
    }
    bool RemoveModel(cIS3DModelInstance* model) override {
        const auto it = std::find(models.begin(), models.end(), model);
        if (it == models.end()) return false;
        (*it)->Release();
        models.erase(it);
        return true;
    }
    bool AddOverlay(std::uint32_t) override { return true; }
    void SetVisibility(bool visible) override {
        visibleCalls.push_back(visible);
        visible_ = visible;
    }
    void SetOverlaysVisible(bool) override {}
    bool IsVisible() const override { return visible_; }
    void RemoveAll() override {
        for (cIS3DModelInstance* model : models) model->Release();
        models.clear();
    }
    std::uint32_t GetZoomLevelsCreatedFor() const override { return zoomLevels; }
    void SetZoomLevelsCreatedFor(std::uint32_t values) override {
        zoomLevels = values;
        ++zoomLevelSets;
    }
    std::uint32_t GetRotationsCreatedFor() const override { return rotations; }
    void SetRotationsCreatedFor(std::uint32_t values) override {
        rotations = values;
        ++rotationSets;
    }

    std::uint32_t refs = 1;
    std::vector<cIS3DModelInstance*> models;
    std::vector<bool> visibleCalls;
    bool visible_ = false;
    std::uint32_t zoomLevels = 0;
    std::uint32_t rotations = 0;
    int adds = 0;
    int zoomLevelSets = 0;
    int rotationSets = 0;
};

// ---------------------------------------------------------------- the view set / texture binding

struct FakeViewSet final : ILotBaseTextureViewSet {
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 1) {
            *out = static_cast<ILotBaseTextureViewSet*>(this);
            AddRef();
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override {
        ++releases;
        if (refs > 1) return --refs;
        return 0;   // the fake is a member of the host and is never deleted
    }
    bool ContainsOccupant(cISC4Occupant* occupant) override {
        ++containsCalls;
        lastOccupant = occupant;
        return contains;
    }

    int refs = 1;
    int releases = 0;
    int containsCalls = 0;
    bool contains = true;
    cISC4Occupant* lastOccupant = nullptr;
};

struct FakeBinding final : ILotBaseTextureBinding {
    explicit FakeBinding(std::uint32_t frameCount, int* releaseCounter)
        : frames(frameCount), releases(releaseCounter) {}
    std::uint32_t FrameCount() const override { return frames; }
    void Release() override {
        ++*releases;
        delete this;
    }

    std::uint32_t frames;
    int* releases;
};

// ---------------------------------------------------------------- the host

struct FakeHost final : ILotBaseTextureHost {
    cSC4Lot* LotAtCell(int x, int z) override {
        ++lotLookups;
        const auto it = lots.find({x, z});
        return it == lots.end() ? nullptr : it->second;
    }
    float TerrainAltitudeAtVertex(int x, int z) override {
        ++terrainCalls;
        return terrain ? terrain(x, z) : 0.0f;
    }
    ILotBaseTextureBinding* CreateTextureBinding(std::uint32_t instance) override {
        ++bindingLookups;
        lastBindingInstance = instance;
        const auto it = frameCounts.find(instance);
        if (it == frameCounts.end()) return nullptr;
        return new FakeBinding(it->second, &bindingReleases);
    }
    int ViewZoom() override {
        ++zoomCalls;
        return zoom;
    }
    void RendererRemoveModel(cIS3DModelInstance* model) override { removed.push_back(model); }
    void RendererMoveModel(cIS3DModelInstance* model) override { moved.push_back(model); }
    void RendererChangeModel(cIS3DModelInstance* model, int reason) override {
        changed.push_back({model, reason});
    }
    ILotBaseTextureViewSet* AcquireViewSet() override {
        if (!viewSet) return nullptr;
        ++viewAcquires;
        viewSet->AddRef();
        return viewSet;
    }
    void MoveOccupant(cISC4Occupant* occupant) override { movedOccupants.push_back(occupant); }
    ILotCellTextureModel* CreateCellTextureModel(const LotBaseTextureSpecification& spec) override {
        ++modelCreates;
        auto* model = new FakeCellModel();
        model->AddRef();
        model->lastSpec = spec;
        createdModels.push_back(model);
        return model;   // one reference for the caller
    }

    std::map<std::pair<int, int>, cSC4Lot*> lots;
    std::map<std::uint32_t, std::uint32_t> frameCounts;
    std::function<float(int, int)> terrain;
    FakeViewSet* viewSet = nullptr;
    std::vector<FakeCellModel*> createdModels;
    std::vector<cIS3DModelInstance*> moved;
    std::vector<cIS3DModelInstance*> removed;
    std::vector<std::pair<cIS3DModelInstance*, int>> changed;
    std::vector<cISC4Occupant*> movedOccupants;
    int lotLookups = 0;
    int terrainCalls = 0;
    int bindingLookups = 0;
    int bindingReleases = 0;
    int zoomCalls = 0;
    int viewAcquires = 0;
    int modelCreates = 0;
    int zoom = 0;
    std::uint32_t lastBindingInstance = 0;
};

cSC4Lot* MakeLot(int x, int z, int sx, int sz, float altitude, int flags) {
    auto* lot = new cSC4Lot(x, z, sx, sz, 0);
    lot->AddRef();
    lot->SetAltitude(altitude);
    lot->SetFlags(flags);
    return lot;
}

LotBaseTextureSpecification MakeSpec(std::uint32_t textureId, std::uint8_t cellX, std::uint8_t cellZ,
                                     std::uint8_t alpha = 0xff) {
    LotBaseTextureSpecification spec;
    spec.textureId = textureId;
    spec.cellX = cellX;
    spec.cellZ = cellZ;
    spec.alpha = alpha;
    spec.frame = 0;   // a concrete frame: no generator draw
    return spec;
}

float LastSpecFrame(const cSC4LotBaseTextureOccupant& occupant) {
    PortMemoryOStream out;
    occupant.Write(out);
    return out.bytes().empty() ? -1.0f : static_cast<float>(out.bytes().back());
}

}  // namespace

int main() {
    // ------------------------------------------------------------------ identity, layout and simple accessors
    {
        FakeHost host;
        SetLotBaseTextureHost(&host);
        auto* occupant = new cSC4LotBaseTextureOccupant();
        CHECK(occupant->AddRef() == 1);
        CHECK(occupant->GetType() == kOccupantTypeLotBaseTexture);
        CHECK(occupant->GetGZCLSID() == kGZCLSID_cSC4LotBaseTextureOccupant);

        void* iface = nullptr;
        CHECK(occupant->QueryInterface(kGZIID_cISC4LotBaseTextureOccupant, &iface));
        CHECK(iface == static_cast<cISC4LotBaseTextureOccupant*>(occupant));
        static_cast<cIGZUnknown*>(iface)->Release();
        iface = nullptr;
        CHECK(occupant->QueryInterface(kGZIID_cISC4Occupant, &iface));
        CHECK(iface == static_cast<cISC4Occupant*>(static_cast<cSC4Occupant*>(occupant)));
        static_cast<cIGZUnknown*>(iface)->Release();
        iface = nullptr;
        CHECK(occupant->QueryInterface(kGZIID_cIGZSerializable, &iface));
        static_cast<cIGZUnknown*>(iface)->Release();
        iface = nullptr;
        CHECK(!occupant->QueryInterface(0xdeadbeef, &iface));
        CHECK(occupant->AsOccupant() == static_cast<cISC4Occupant*>(static_cast<cSC4Occupant*>(occupant)));

        // usage is a sign-extended byte
        CHECK(occupant->GetUsage() == 0);
        occupant->SetUsage(7);
        CHECK(occupant->GetUsage() == 7);
        occupant->SetUsage(0x1ff);
        CHECK(occupant->GetUsage() == -1);

        // an uninitialised occupant has a zero bounding box; SetPosition is a no-op
        float lo[3] = {1.0f, 2.0f, 3.0f};
        float hi[3] = {4.0f, 5.0f, 6.0f};
        occupant->GetBoundingBox(lo, hi);
        CHECK(lo[0] == 0.0f && lo[1] == 0.0f && lo[2] == 0.0f);
        CHECK(hi[0] == 0.0f && hi[1] == 0.0f && hi[2] == 0.0f);
        const float ignored[3] = {100.0f, 200.0f, 300.0f};
        occupant->SetPosition(ignored);
        float position[3] = {-1.0f, -1.0f, -1.0f};
        occupant->GetPosition(position);
        CHECK(position[0] == 0.0f && position[1] == 0.0f && position[2] == 0.0f);

        CHECK(!occupant->IsInitialized());
        CHECK(occupant->Init());
        CHECK(occupant->IsInitialized());
        CHECK(occupant->Shutdown());   // no placeable: UnloadModels is a no-op, still reports success
        CHECK(!occupant->IsInitialized());

        // empty list coverage
        std::vector<SC4PointInt> cells(3);
        std::vector<bool> covered(3, true);
        CHECK(occupant->GetCellCoverage(cells, &covered) == 0);
        CHECK(cells.empty());
        CHECK(covered.empty());
        bool cellCovered = true;
        CHECK(!occupant->IsCellCovered(0, 0, &cellCovered));
        CHECK(!cellCovered);

        occupant->Release();
        SetLotBaseTextureHost(nullptr);
    }

    // ------------------------------------------------------------------ version 2 record round trip
    {
        auto* source = new cSC4LotBaseTextureOccupant();
        source->AddRef();
        LotBaseTextureSpecification specs[2] = {MakeSpec(0x1005, 1, 2, 0xff), MakeSpec(0x1006, 3, 4, 0x40)};
        specs[0].rotation = 2;
        specs[0].flip = 1;
        specs[0].red = 0x10;
        specs[0].green = 0x20;
        specs[0].blue = 0x30;
        specs[0].combineMode = 1;
        specs[0].frame = 5;
        specs[1].rotation = 3;
        specs[1].combineMode = 0xff;
        specs[1].frame = 0;
        FakeHost host;
        SetLotBaseTextureHost(&host);
        host.terrain = [](int, int) { return 0.0f; };
        bool changed = true;
        source->SetTextureSpecification(specs, 2, &changed);
        CHECK(changed);

        PortMemoryOStream out;
        CHECK(source->Write(out));
        CHECK(out.bytes().size() > 2 && out.bytes()[0] == 2 && out.bytes()[1] == 0);

        PortMemoryIStream in(out.bytes().data(), out.bytes().size());
        auto* copy = new cSC4LotBaseTextureOccupant();
        copy->AddRef();
        CHECK(copy->Read(in));
        CHECK(in.RemainingBytes() == 0);

        std::vector<SC4PointInt> cells;
        std::vector<bool> covered;
        CHECK(copy->GetCellCoverage(cells, &covered) == 2);
        CHECK(cells.size() == 2 && cells[0].x == 1 && cells[0].z == 2 && cells[1].x == 3 && cells[1].z == 4);
        CHECK(covered.size() == 2 && covered[0] && !covered[1]);

        // the computed bounds and the usage byte survive the record
        float lo[3] = {};
        float hi[3] = {};
        copy->GetBoundingBox(lo, hi);
        CHECK(lo[0] == 16.0f + 0.1f && lo[1] == 0.0f && lo[2] == 32.0f + 0.1f);
        CHECK(hi[0] == 64.0f - 0.1f && hi[1] == 0.0f + 0.1f && hi[2] == 80.0f - 0.1f);

        // a second write reproduces the first byte for byte (all serialised spec fields survived)
        PortMemoryOStream out2;
        CHECK(copy->Write(out2));
        CHECK(out2.bytes() == out.bytes());

        // wrong outer version
        const std::uint8_t badVersion[] = {1, 0};
        PortMemoryIStream badStream(badVersion, sizeof badVersion);
        auto* fresh = new cSC4LotBaseTextureOccupant();
        fresh->AddRef();
        CHECK(!fresh->Read(badStream));

        // quirk: a nested occupant record with version 5 fails cSC4Occupant::Read without setting the stream error,
        // so the spec fields stay untouched and Read still reports success
        const std::uint8_t badNested[] = {2, 0, 5, 0};
        PortMemoryIStream nestedStream(badNested, sizeof badNested);
        CHECK(fresh->Read(nestedStream));
        CHECK(nestedStream.RemainingBytes() == 0);
        CHECK(fresh->GetCellCoverage(cells, nullptr) == 0);

        // a record that ends inside the occupant base record sets the stream error and fails
        const std::uint8_t truncated[] = {2, 0};
        PortMemoryIStream shortStream(truncated, sizeof truncated);
        CHECK(!fresh->Read(shortStream));

        source->Release();
        copy->Release();
        fresh->Release();
        SetLotBaseTextureHost(nullptr);
    }

    // ------------------------------------------------------------------ cell coverage
    {
        auto* occupant = new cSC4LotBaseTextureOccupant();
        occupant->AddRef();
        LotBaseTextureSpecification specs[3] = {MakeSpec(1, 1, 1, 0x80), MakeSpec(2, 1, 1, 0xff),
                                                MakeSpec(3, 2, 3, 0xff)};
        occupant->SetTextureSpecification(specs, 3, nullptr);

        std::vector<SC4PointInt> cells;
        std::vector<bool> covered;
        CHECK(occupant->GetCellCoverage(cells, &covered) == 3);
        CHECK(cells.size() == 3 && cells[0].x == 1 && cells[0].z == 1 && cells[2].x == 2 && cells[2].z == 3);
        CHECK(covered.size() == 3 && !covered[0] && covered[1] && covered[2]);

        bool cellCovered = false;
        // the first spec on (1,1) is translucent, the second is opaque: the scan skips ahead to the opaque one
        CHECK(occupant->IsCellCovered(1, 1, &cellCovered));
        CHECK(cellCovered);
        CHECK(occupant->IsCellCovered(1, 1, nullptr));
        CHECK(occupant->IsCellCovered(2, 3, &cellCovered));
        CHECK(cellCovered);
        cellCovered = true;
        CHECK(!occupant->IsCellCovered(4, 4, &cellCovered));
        CHECK(!cellCovered);
        CHECK(!occupant->IsCellCovered(-1, 1, nullptr));   // the comparison is unsigned
        CHECK(!occupant->IsCellCovered(1, -1, nullptr));
        occupant->Release();
    }

    // ------------------------------------------------------------------ bounding box: terrain path and update paths
    {
        FakeHost host;
        SetLotBaseTextureHost(&host);
        host.terrain = [](int x, int z) { return static_cast<float>(x * 10 + z); };
        auto* occupant = new cSC4LotBaseTextureOccupant();
        occupant->AddRef();
        LotBaseTextureSpecification specs[2] = {MakeSpec(1, 1, 2), MakeSpec(2, 3, 5)};
        bool changed = false;
        occupant->SetTextureSpecification(specs, 2, &changed);
        CHECK(changed);
        CHECK(host.terrainCalls == 8);   // four terrain vertices per cell
        CHECK(host.lotLookups == 1);

        float lo[3] = {};
        float hi[3] = {};
        occupant->GetBoundingBox(lo, hi);
        CHECK(lo[0] == 16.0f + 0.1f);
        CHECK(lo[1] == 12.0f);           // min over (1,2) (1,3) (2,2) (2,3) and (3,5)..(4,6)
        CHECK(lo[2] == 32.0f + 0.1f);
        CHECK(hi[0] == 64.0f - 0.1f);    // (3 + 1) * 16 - 0.1
        CHECK(hi[1] == 46.0f);
        CHECK(hi[2] == 96.0f - 0.1f);    // (5 + 1) * 16 - 0.1
        float position[3] = {};
        occupant->GetPosition(position);
        CHECK(position[0] == (lo[0] + hi[0]) * 0.5f);
        CHECK(position[1] == (12.0f + 46.0f) * 0.5f);
        CHECK(position[2] == (lo[2] + hi[2]) * 0.5f);

        // the same specs again: all cells match, so the models are updated in place and the box does not change
        changed = true;
        occupant->SetTextureSpecification(specs, 2, &changed);
        CHECK(!changed);
        CHECK(host.terrainCalls == 16);

        // a different cell makes the list rebuild (no 3D view set here: LoadModels is not reached)
        LotBaseTextureSpecification moved[1] = {MakeSpec(1, 7, 7)};
        changed = false;
        occupant->SetTextureSpecification(moved, 1, &changed);
        CHECK(changed);
        occupant->GetBoundingBox(lo, hi);
        CHECK(lo[0] == 7.0f * 16.0f + 0.1f && hi[0] == 8.0f * 16.0f - 0.1f);

        // count 0 returns before the bounding box is touched
        changed = false;
        occupant->SetTextureSpecification(nullptr, 0, &changed);
        CHECK(!changed);
        float emptyLo[3] = {};
        float emptyHi[3] = {};
        occupant->GetBoundingBox(emptyLo, emptyHi);
        CHECK(emptyLo[0] == lo[0] && emptyHi[0] == hi[0]);

        occupant->Release();
        SetLotBaseTextureHost(nullptr);
    }

    // ------------------------------------------------------------------ bounding box: locked lot and flat / empty cases
    {
        FakeHost host;
        SetLotBaseTextureHost(&host);
        host.terrain = [](int, int) { return 7.0f; };
        cSC4Lot* lot = MakeLot(1, 2, 1, 1, 20.0f, 1);
        host.lots[{1, 2}] = lot;

        auto* occupant = new cSC4LotBaseTextureOccupant();
        occupant->AddRef();
        LotBaseTextureSpecification specs[2] = {MakeSpec(1, 1, 2), MakeSpec(2, 3, 5)};
        occupant->SetTextureSpecification(specs, 2, nullptr);
        float lo[3] = {};
        float hi[3] = {};
        occupant->GetBoundingBox(lo, hi);
        CHECK(lo[1] == 20.0f);           // a locked lot: the lot altitude, no terrain sampling
        CHECK(hi[1] == 20.0f + 0.1f);
        CHECK(host.terrainCalls == 0);

        // an unlocked lot falls back to the terrain vertices
        lot->SetFlags(0);
        occupant->SetTextureSpecification(specs, 2, nullptr);
        occupant->GetBoundingBox(lo, hi);
        CHECK(lo[1] == 7.0f);
        CHECK(hi[1] == 7.0f + 0.1f);     // a flat cell gets a 0.1-thick box
        CHECK(host.terrainCalls == 8);

        // no specs at all: the inverted placeholder box
        auto* empty = new cSC4LotBaseTextureOccupant();
        empty->AddRef();
        bool changed = false;
        empty->UpdateVertexAltitudes(false, true, &changed);
        CHECK(changed);
        empty->GetBoundingBox(lo, hi);
        CHECK(lo[0] == -15.9f && lo[1] == 3.4028235e+38f && lo[2] == -15.9f);
        CHECK(hi[0] == 15.9f && hi[1] == -2147483648.0f && hi[2] == 15.9f);
        CHECK(host.movedOccupants.empty());   // flag bit 0 is clear: no MoveOccupant
        CHECK(host.lotLookups == 2);          // the empty list never asks for a lot

        // flag bit 0 (in the occupant manager) makes the update move the occupant
        empty->SetAllFlags(kLotBaseTextureFlagManaged);
        changed = false;
        // force also refreshes when the box did not move (the placeholder box is already stored)
        empty->UpdateVertexAltitudes(false, true, &changed);
        CHECK(!changed);
        CHECK(host.movedOccupants.size() == 1 && host.movedOccupants[0] == empty);

        // allowUpdate == false and an unchanged box: neither the move nor the model update happens
        host.movedOccupants.clear();
        empty->UpdateVertexAltitudes(false, false, &changed);
        CHECK(!changed);
        CHECK(host.movedOccupants.empty());

        empty->Release();
        occupant->Release();
        lot->Release();
        SetLotBaseTextureHost(nullptr);
    }

    // ------------------------------------------------------------------ frame randomisation and the frame-count cache
    {
        FakeHost host;
        SetLotBaseTextureHost(&host);
        host.frameCounts[0x1004] = 4;   // the binding is looked up with the texture id + 4 first
        host.frameCounts[0x2004] = 1;   // a single-frame texture: frame 0, no generator draw
        cRZRandom& random = cSC4LotBaseTextureOccupant::Random();
        random.Seed(1234);
        cRZRandom expected(1234);

        auto* occupant = new cSC4LotBaseTextureOccupant();
        occupant->AddRef();
        LotBaseTextureSpecification spec = MakeSpec(0x1000, 1, 1);
        spec.frame = 0xff;   // not chosen yet
        occupant->SetTextureSpecification(&spec, 1, nullptr);
        CHECK(host.bindingLookups == 1 && host.lastBindingInstance == 0x1004);
        CHECK(host.bindingReleases == 1);
        const std::uint32_t firstFrame = expected.RandomUint32Uniform(4);
        CHECK(LastSpecFrame(*occupant) == static_cast<float>(firstFrame));

        // the frame count is cached: a changed binding table is ignored and the generator draws from the cached 4
        host.frameCounts[0x1004] = 8;
        occupant->SetTextureSpecification(&spec, 1, nullptr);
        CHECK(host.bindingLookups == 1);
        const std::uint32_t secondFrame = expected.RandomUint32Uniform(4);
        CHECK(LastSpecFrame(*occupant) == static_cast<float>(secondFrame));

        // a single-frame texture caches 1 and never advances the generator
        LotBaseTextureSpecification single = MakeSpec(0x2000, 2, 2);
        single.frame = 0xff;
        occupant->SetTextureSpecification(&single, 1, nullptr);
        CHECK(LastSpecFrame(*occupant) == 0.0f);
        CHECK(host.bindingLookups == 2);   // 0x2004 found, one release
        CHECK(host.bindingReleases == 2);

        // a texture whose binding is missing at both instances caches one frame
        LotBaseTextureSpecification missing = MakeSpec(0x3000, 3, 3);
        missing.frame = 0xff;
        occupant->SetTextureSpecification(&missing, 1, nullptr);
        CHECK(LastSpecFrame(*occupant) == 0.0f);
        CHECK(host.bindingLookups == 4);   // id + 4, then id
        CHECK(host.bindingReleases == 2);

        // a spec with a concrete frame is kept and does not consult the cache
        LotBaseTextureSpecification concrete = MakeSpec(0x1000, 1, 1);
        concrete.frame = 9;
        occupant->SetTextureSpecification(&concrete, 1, nullptr);
        CHECK(LastSpecFrame(*occupant) == 9.0f);
        CHECK(host.bindingLookups == 4);

        occupant->Release();
        SetLotBaseTextureHost(nullptr);
    }

    // ------------------------------------------------------------------ model loading, SetAllFlags and rotations
    {
        FakeHost host;
        SetLotBaseTextureHost(&host);
        auto* occupant = new cSC4LotBaseTextureOccupant();
        occupant->AddRef();
        auto* placeable = new FakePlaceable();   // the test's reference
        occupant->SetPlaceableObject(placeable);   // one more, owned by the occupant
        placeable->Release();                      // the occupant's reference is the one that keeps it alive

        LotBaseTextureSpecification specs[2] = {MakeSpec(0x1005, 1, 1), MakeSpec(0x1006, 2, 2)};
        occupant->SetTextureSpecification(specs, 2, nullptr);

        // the visible bit (2) turns on: models are created and the placeable's zoom / rotation masks are set
        occupant->SetAllFlags(0);
        CHECK(placeable->models.empty());
        occupant->SetAllFlags(kLotBaseTextureFlagShown);
        CHECK(host.modelCreates == 2);
        CHECK(placeable->models.size() == 2);
        CHECK(placeable->rotations == 0xf);
        CHECK(placeable->zoomLevels == 0x1f);
        CHECK(placeable->rotationSets == 1 && placeable->zoomLevelSets == 1);
        CHECK(host.createdModels.size() == 2);
        CHECK(host.createdModels[0]->lastSpec.cellX == 1 && host.createdModels[1]->lastSpec.cellX == 2);
        CHECK(host.createdModels[0]->refs == 1);   // the placeable's reference

        // the flag is already set: nothing is reloaded
        occupant->SetAllFlags(kLotBaseTextureFlagShown);
        CHECK(host.modelCreates == 2);
        CHECK(placeable->rotationSets == 1);

        // clearing and re-setting it loads again, but the models already exist and the masks are already set
        occupant->SetAllFlags(0);
        occupant->SetAllFlags(kLotBaseTextureFlagShown);
        CHECK(host.modelCreates == 2);
        CHECK(placeable->models.size() == 2);
        CHECK(placeable->rotationSets == 1);

        // LoadModels(true) forwards the shown flag to the placeable's visibility
        occupant->SetAllFlags(0);
        occupant->LoadModels(true);
        CHECK(!placeable->visibleCalls.empty() && placeable->visibleCalls.back() == false);
        occupant->SetAllFlags(kLotBaseTextureFlagShown);
        occupant->LoadModels(true);
        CHECK(placeable->visibleCalls.back() == true);

        // UnloadModels removes every model
        CHECK(host.createdModels[0]->refs == 1);
        occupant->UnloadModels();
        CHECK(placeable->models.empty());
        occupant->UnloadModels();   // second time: still no models

        // an empty occupant does not touch the placeable at all
        auto* blank = new cSC4LotBaseTextureOccupant();
        blank->AddRef();
        blank->SetPlaceableObject(placeable);
        const int addsBefore = placeable->adds;
        blank->LoadModels(true);
        CHECK(placeable->adds == addsBefore);
        CHECK(placeable->models.empty());
        blank->UnloadModels();
        blank->Release();

        occupant->Release();   // releases the placeable (its destructor removes the models)
        SetLotBaseTextureHost(nullptr);
    }

    // ------------------------------------------------------------------ UpdateVertexAltitudes / UpdateExistingModels
    {
        FakeHost host;
        SetLotBaseTextureHost(&host);
        host.terrain = [](int x, int z) { return static_cast<float>(x + z); };
        host.zoom = 3;
        FakeViewSet view;
        host.viewSet = &view;

        auto* occupant = new cSC4LotBaseTextureOccupant();
        occupant->AddRef();
        auto* placeable = new FakePlaceable();   // the test's reference
        occupant->SetPlaceableObject(placeable);   // one more, owned by the occupant
        placeable->Release();                      // the occupant keeps it alive from here on

        LotBaseTextureSpecification specs[2] = {MakeSpec(0x1005, 1, 1), MakeSpec(0x1006, 2, 2)};
        occupant->SetTextureSpecification(specs, 2, nullptr);

        auto* modelA = new FakeCellModel();
        modelA->AddRef();
        auto* modelB = new FakeCellModel();
        modelB->AddRef();
        placeable->AddModel(modelA->ModelInstance());
        placeable->AddModel(modelB->ModelInstance());
        modelA->Release();
        modelB->Release();
        CHECK(placeable->models.size() == 2);
        CHECK(modelA->refs == 1 && modelB->refs == 1);

        // flags: bit 0 = managed, bit 2 = shown
        occupant->SetAllFlags(0);
        occupant->SetAllFlags(kLotBaseTextureFlagManaged | kLotBaseTextureFlagShown);
        CHECK(host.movedOccupants.empty());   // SetAllFlags itself never moves the occupant

        // a forced update moves the occupant and refreshes the textures of both models (the box was already
        // computed by SetTextureSpecification, so `changed` stays false)
        bool changed = true;
        occupant->UpdateVertexAltitudes(false, true, &changed);
        CHECK(!changed);
        CHECK(host.movedOccupants.size() == 1);
        CHECK(modelA->textureCalls == 1 && modelB->textureCalls == 1);
        CHECK(modelA->setupCalls == 1 && modelB->setupCalls == 1);
        CHECK(modelA->lastZoom == 3);
        CHECK(modelA->verticesArg && modelA->lightingArg && modelA->terrainArg == nullptr &&
              modelA->lightingPtrArg == nullptr);
        CHECK(modelA->lastSpec.cellX == 1 && modelB->lastSpec.cellX == 2);
        CHECK(host.moved.size() == 2);     // shown + in view, not a spec change: MoveModel
        CHECK(host.changed.empty() && host.removed.empty());
        CHECK(view.containsCalls == 2);
        CHECK(view.lastOccupant == occupant->AsOccupant());

        // the box did not change and allowUpdate is false: no move, no model refresh
        occupant->UpdateVertexAltitudes(false, false, &changed);
        CHECK(!changed);
        CHECK(host.movedOccupants.size() == 1);
        CHECK(modelA->textureCalls == 1);

        // the view no longer contains the occupant: the models are removed instead
        view.contains = false;
        occupant->UpdateVertexAltitudes(false, true, &changed);
        CHECK(host.removed.size() == 2);
        CHECK(host.moved.size() == 2);

        // the same specs through SetTextureSpecification take the in-place path: ChangeModel(instance, 2)
        view.contains = true;
        host.changed.clear();
        occupant->SetTextureSpecification(specs, 2, nullptr);
        CHECK(host.changed.size() == 2);
        CHECK(host.changed[0].second == 2 && host.changed[1].second == 2);

        // a non-cell-texture model is skipped by the interface query
        auto* plain = new PlainInstance();
        placeable->AddModel(plain);   // refcount now 2
        plain->Release();             // 1, held by the placeable
        host.changed.clear();
        occupant->SetTextureSpecification(specs, 2, nullptr);
        CHECK(host.changed.size() == 2);   // only the two cell-texture models
        CHECK(placeable->models.size() == 3);

        occupant->Release();   // releases the placeable (its destructor releases the models)
        SetLotBaseTextureHost(nullptr);
    }

    // ------------------------------------------------------------------ no host: the port bails out instead of faulting
    {
        SetLotBaseTextureHost(nullptr);
        auto* occupant = new cSC4LotBaseTextureOccupant();
        occupant->AddRef();
        LotBaseTextureSpecification spec = MakeSpec(1, 1, 1);
        spec.frame = 0xff;
        bool changed = true;
        occupant->SetTextureSpecification(&spec, 1, &changed);
        CHECK(!changed);   // CalculateBoundingBox bails out
        float lo[3] = {1.0f, 1.0f, 1.0f};
        float hi[3] = {1.0f, 1.0f, 1.0f};
        occupant->GetBoundingBox(lo, hi);
        CHECK(lo[0] == 0.0f && hi[0] == 0.0f);
        occupant->LoadModels(false);   // no placeable: nothing to do
        occupant->Release();
    }

    if (failures != 0) {
        std::printf("%d cSC4LotBaseTextureOccupant check(s) failed\n", failures);
        return 1;
    }
    std::printf("sc4_lotbasetextureoccupant: all checks passed\n");
    return 0;
}
