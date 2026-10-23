// Tests cSC4LotRetainingWallOccupant: layout accessors, the version 2 record, the per-edge hash scan, the
// retaining-wall occupant create/remove paths and the model create/remove paths. The model and the engine
// managers are fakes behind ILotRetainingWallModel / ILotRetainingWallHost.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "rz/PortMemoryStreams.h"
#include "sc4/LotRetainingWallOccupant.h"
#include "sc4/PlaceableObject.h"
#include "sc4/SCPropertyHelp.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {

struct FakeWallModel;

// The cIS3DModelInstance subobject at model +8; its reference count is the model's.
struct FakeInstance final : cIS3DModelInstance {
    explicit FakeInstance(FakeWallModel* model) : model_(model) {}
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override;
    std::uint32_t Release() override;
    void SetOwner(void* owner) override { owner_ = owner; }

    FakeWallModel* model_;
    void* owner_ = nullptr;
};

struct FakeWallModel final : ILotRetainingWallModel {
    FakeWallModel() : instance_(this) {}

    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cISC4LotRetainingWallModel || iid == 1) {
            *out = static_cast<ILotRetainingWallModel*>(this);
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
    ILotRetainingWallModel* GetWallModel() override { return this; }
    std::int8_t WallEdge() const override { return edge; }
    void SetTextureData(const std::uint32_t* ids, float value) override {
        for (int i = 0; i < 5; ++i) textureIds[i] = ids[i];
        textureScale = value;
        ++textureCalls;
    }
    bool Init(const cS3DBoundingBox& box, int wallEdge, bool& builtOut) override {
        bounds = box;
        edge = static_cast<std::int8_t>(wallEdge);
        builtOut = initOk;
        ++initCalls;
        return true;
    }
    cIS3DModelInstance* AsModelInstance() override { return &instance_; }

    FakeInstance instance_;
    std::uint32_t refs = 0;
    std::int8_t edge = -1;
    std::uint32_t textureIds[5] = {0, 0, 0, 0, 0};
    float textureScale = 0.0f;
    int textureCalls = 0;
    int initCalls = 0;
    bool initOk = true;
    cS3DBoundingBox bounds;
};

bool FakeInstance::QueryInterface(std::uint32_t iid, void** out) { return model_->QueryInterface(iid, out); }
std::uint32_t FakeInstance::AddRef() { return model_->AddRef(); }
std::uint32_t FakeInstance::Release() { return model_->Release(); }

// cISCResExemplar with a property holder; the host hands out one reference on it (a member, never deleted).
struct FakeExemplar final : cISCResExemplar {
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cISCResExemplar || iid == 1) {
            *out = static_cast<cISCResExemplar*>(this);
            AddRef();
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override {
        if (refs > 0) --refs;
        return static_cast<std::uint32_t>(refs);
    }
    cISCPropertyHolder* AsISCPropertyHolder() override { return &properties; }
    bool IsPropertyLocal(std::uint32_t id) override { return properties.HasProperty(id); }
    cISCResExemplarCohort* GetCohort() override { return nullptr; }
    void SetCohort(cISCResExemplarCohort*, bool) override {}
    bool GetKey(cGZPersistResourceKey&) override { return false; }
    bool SetKey(const cGZPersistResourceKey&) override { return true; }
    void CompactProperties() override {}
    void GenerateCachedMap() override {}
    bool Write(cIGZOStream&) const override { return false; }
    bool Read(cIGZIStream&) override { return false; }
    std::uint32_t GetGZCLSID() const override { return 0; }

    cSCPropertyHolder properties;
    int refs = 0;
};

struct FakeHost final : ILotRetainingWallHost {
    ILotRetainingWallModel* CreateRetainingWallModel() override {
        auto* model = new FakeWallModel();
        model->initOk = initOk;
        created.push_back(model);
        return model;
    }
    cSC4Lot* LotAtCell(int x, int z) override {
        const auto it = lotsAtCell.find({x, z});
        return it == lotsAtCell.end() ? nullptr : it->second;
    }
    bool EdgeCellRect(const SC4CellRect&, int edgeMask, SC4CellRect& out) override {
        for (int edge = 0; edge < 4; ++edge) {
            if (edgeMask == (1 << edge) && hasEdgeCell[edge]) {
                out = edgeCells[edge];
                return true;
            }
        }
        return false;
    }
    void EdgeTerrainRect(const SC4CellRect&, int edgeMask, SC4CellRect& out) override {
        out = SC4CellRect{};
        for (int edge = 0; edge < 4; ++edge) {
            if (edgeMask == (1 << edge)) {
                out = terrainCells[edge];
                return;
            }
        }
    }
    bool LotsAround(cSC4Lot*, std::list<cRZAutoRefCount<cSC4Lot>>& out) override {
        for (cSC4Lot* lot : around) out.emplace_back(lot);
        return !around.empty();
    }
    float TerrainAltitudeAtVertex(int x, int z) override {
        return terrain ? terrain(x, z) : 0.0f;
    }
    bool FirstOccupantAtCell(cISC4Occupant*& out, int x, int z, std::uint32_t type) override {
        out = nullptr;
        for (cISC4Occupant* occupant : occupants) {
            if (occupant->GetType() != type) continue;
            SC4CellRect cells;
            occupant->GetBoundingCityCells(cells);
            if (x < cells.x0 || x > cells.x1 || z < cells.z0 || z > cells.z1) continue;
            out = occupant;
            out->AddRef();
            return true;
        }
        return false;
    }
    void InsertOccupant(cISC4Occupant* occupant) override {
        occupant->AddRef();
        occupants.push_back(occupant);
        ++inserts;
    }
    void RemoveOccupant(cISC4Occupant* occupant) override {
        const auto it = std::find(occupants.begin(), occupants.end(), occupant);
        if (it == occupants.end()) return;
        occupants.erase(it);
        occupant->Release();
        ++removals;
    }
    bool LoadRetainingWallProperties(cISC4LotRetainingWallOccupant* occupant, std::uint32_t type) override {
        loadedWall = occupant;
        loadedType = type;
        return loadOk;
    }
    bool RendererAvailable() override { return rendererAvailable; }
    bool RendererHasModel(cIS3DModelInstance* model) override {
        ++hasModelCalls;
        return std::find(renderer.begin(), renderer.end(), model) != renderer.end();
    }
    void RendererAddModel(cIS3DModelInstance* model) override { renderer.push_back(model); }
    void RendererRemoveModel(cIS3DModelInstance* model) override {
        renderer.erase(std::remove(renderer.begin(), renderer.end(), model), renderer.end());
    }
    bool LoadTextureExemplar(cISCResExemplar*& out) override {
        if (!exemplarAvailable) return false;
        ++exemplar.refs;
        out = &exemplar;
        return true;
    }
    const std::vector<std::uint32_t>& RetainingWallIds(cSC4Lot*) override { return wallIds; }
    std::uint32_t RandomBelow(std::uint32_t range) override { return range ? random % range : 0; }
    bool WriteLotReference(cIGZOStream&, cSC4Lot* lot) override {
        writtenLot = lot;
        return false;
    }
    cSC4Lot* ReadLotReference(cIGZIStream&) override {
        readCalled = true;
        return nullptr;
    }

    std::vector<FakeWallModel*> created;
    std::vector<cIS3DModelInstance*> renderer;
    std::vector<cISC4Occupant*> occupants;
    std::vector<cSC4Lot*> around;
    std::map<std::pair<int, int>, cSC4Lot*> lotsAtCell;
    std::vector<std::uint32_t> wallIds;
    std::function<float(int, int)> terrain;
    SC4CellRect edgeCells[4] = {};
    bool hasEdgeCell[4] = {false, false, false, false};
    SC4CellRect terrainCells[4] = {};
    FakeExemplar exemplar;
    cISC4LotRetainingWallOccupant* loadedWall = nullptr;
    cSC4Lot* writtenLot = nullptr;
    std::uint32_t loadedType = 0;
    std::uint32_t random = 0;
    bool rendererAvailable = false;
    bool initOk = true;
    bool loadOk = true;
    bool exemplarAvailable = false;
    bool readCalled = false;
    int inserts = 0;
    int removals = 0;
    int hasModelCalls = 0;
};

cSC4Lot* MakeLot(int x, int z, int sx, int sz, float altitude, int flags) {
    auto* lot = new cSC4Lot(x, z, sx, sz, 0);
    lot->AddRef();
    lot->SetAltitude(altitude);
    lot->SetFlags(flags);
    return lot;
}

void ReleaseOccupants(FakeHost& host) {
    for (cISC4Occupant* occupant : host.occupants) occupant->Release();
    host.occupants.clear();
}

}  // namespace

int main() {
    // ------------------------------------------------------------------ accessors and identity
    {
        FakeHost host;
        SetLotRetainingWallHost(&host);
        auto* occupant = new cSC4LotRetainingWallOccupant();
        CHECK(occupant->AddRef() == 1);
        CHECK(occupant->GetType() == kOccupantTypeLotRetainingWall);
        CHECK(occupant->GetGZCLSID() == kGZCLSID_cSC4LotRetainingWallOccupant);

        void* iface = nullptr;
        CHECK(occupant->QueryInterface(kGZIID_cISC4LotRetainingWallOccupant, &iface) &&
              iface == static_cast<cISC4LotRetainingWallOccupant*>(occupant));
        static_cast<cIGZUnknown*>(iface)->Release();
        iface = nullptr;
        CHECK(occupant->QueryInterface(kGZIID_cISC4Occupant, &iface) &&
              iface == static_cast<cISC4Occupant*>(occupant));
        static_cast<cIGZUnknown*>(iface)->Release();
        iface = nullptr;
        CHECK(occupant->QueryInterface(kGZIID_cIGZSerializable, &iface) &&
              iface == static_cast<cIGZSerializable*>(occupant));
        static_cast<cIGZUnknown*>(iface)->Release();
        CHECK(!occupant->QueryInterface(0xdeadbeef, &iface));

        const float position[3] = {100.0f, 20.0f, 50.0f};
        const float size[3] = {16.0f, 6.0f, 32.0f};
        occupant->SetPosition(position);
        CHECK(occupant->SetSize(size));
        float got[3] = {};
        occupant->GetPosition(got);
        CHECK(got[0] == 100.0f && got[1] == 20.0f && got[2] == 50.0f);
        float lo[3] = {}, hi[3] = {};
        occupant->GetBoundingBox(lo, hi);
        CHECK(lo[0] == 92.0f && hi[0] == 108.0f);
        CHECK(lo[1] == 14.0f && hi[1] == 20.0f);
        CHECK(lo[2] == 34.0f && hi[2] == 66.0f);

        occupant->SetRetainingWallType(0x1234);
        CHECK(occupant->GetRetainingWallType() == 0x1234);
        occupant->SetEdgeFlags(5);
        CHECK(occupant->GetEdgeFlags() == 5);
        const int hashes[4] = {11, 22, 33, 44};
        int readHashes[4] = {};
        occupant->SetEdgeHashArray(hashes);
        occupant->GetEdgeHashArray(readHashes);
        CHECK(readHashes[0] == 11 && readHashes[1] == 22 && readHashes[2] == 33 && readHashes[3] == 44);

        cSC4Lot* lot = MakeLot(2, 2, 2, 2, 5.0f, 1);
        occupant->SetLot(lot);
        CHECK(occupant->GetLot() == lot);
        occupant->SetLot(lot);   // same pointer: no reference churn
        CHECK(occupant->IsInitialized() == false);
        CHECK(occupant->Shutdown());          // not initialized: still reports success...
        CHECK(occupant->GetLot() == lot);     // ...and keeps the lot reference
        CHECK(occupant->Init());
        CHECK(occupant->IsInitialized());
        CHECK(occupant->Shutdown());
        CHECK(!occupant->IsInitialized());
        CHECK(occupant->GetLot() == nullptr);   // Shutdown drops the lot reference
        lot->Release();
        occupant->Release();
        SetLotRetainingWallHost(nullptr);
    }

    // ------------------------------------------------------------------ version 2 record
    {
        FakeHost host;
        SetLotRetainingWallHost(&host);
        cSC4Lot* lot = MakeLot(1, 1, 1, 1, 3.0f, 1);
        auto* source = new cSC4LotRetainingWallOccupant();
        source->AddRef();
        source->SetRetainingWallType(77);
        const float position[3] = {8.0f, 3.0f, 8.0f};
        const float size[3] = {16.0f, 2.0f, 16.0f};
        source->SetPosition(position);
        source->SetSize(size);
        const int hashes[4] = {5, 0, 7, 0};
        source->SetEdgeHashArray(hashes);
        source->SetEdgeFlags(1 | 4);
        source->SetLot(lot);

        PortMemoryOStream out;
        CHECK(source->Write(out));
        CHECK(out.bytes().size() > 2 && out.bytes()[0] == 2 && out.bytes()[1] == 0);
        CHECK(host.writtenLot == lot);

        PortMemoryIStream in(out.bytes().data(), out.bytes().size());
        auto* copy = new cSC4LotRetainingWallOccupant();
        copy->AddRef();
        CHECK(copy->Read(in));
        CHECK(in.RemainingBytes() == 0);
        CHECK(host.readCalled);
        CHECK(copy->GetRetainingWallType() == 77);
        CHECK(copy->GetEdgeFlags() == (1 | 4));
        CHECK(copy->GetLot() == nullptr);   // the plain memory stream has no nested lot record
        int copyHashes[4] = {};
        copy->GetEdgeHashArray(copyHashes);
        CHECK(copyHashes[0] == 5 && copyHashes[1] == 0 && copyHashes[2] == 7 && copyHashes[3] == 0);
        float got[3] = {};
        copy->GetPosition(got);
        CHECK(got[0] == 8.0f && got[1] == 3.0f && got[2] == 8.0f);
        float copyLo[3] = {}, copyHi[3] = {};
        copy->GetBoundingBox(copyLo, copyHi);
        CHECK(copyLo[1] == 1.0f && copyHi[1] == 3.0f);

        // wrong outer version -> false
        const std::uint8_t badVersion[] = {1, 0};
        PortMemoryIStream badStream(badVersion, sizeof badVersion);
        auto* fresh = new cSC4LotRetainingWallOccupant();
        fresh->AddRef();
        CHECK(!fresh->Read(badStream));

        // quirk: the nested occupant record has version 5, cSC4Occupant::Read fails without setting the stream
        // error, and this Read still reports success while leaving the wall fields untouched.
        const std::uint8_t badNested[] = {2, 0, 5, 0};
        PortMemoryIStream nestedStream(badNested, sizeof badNested);
        CHECK(fresh->Read(nestedStream));
        CHECK(fresh->GetRetainingWallType() == 0);
        CHECK(fresh->GetEdgeFlags() == 0);
        CHECK(nestedStream.RemainingBytes() == 0);

        // a record that ends inside the occupant base record sets the stream error and fails
        const std::uint8_t truncated[] = {2, 0};
        PortMemoryIStream shortStream(truncated, sizeof truncated);
        CHECK(!fresh->Read(shortStream));

        source->Release();
        copy->Release();
        lot->Release();
        fresh->Release();
        SetLotRetainingWallHost(nullptr);
    }

    // ------------------------------------------------------------------ wall type from the lot configuration
    {
        FakeHost host;
        SetLotRetainingWallHost(&host);
        CHECK(cSC4LotRetainingWallOccupant::GetRetainingWallType(nullptr) == 0);
        host.wallIds = {111, 222, 333};
        host.random = 1;
        CHECK(cSC4LotRetainingWallOccupant::GetRetainingWallType(nullptr) == 222);
        host.random = 4;   // 4 % 3 == 1
        CHECK(cSC4LotRetainingWallOccupant::GetRetainingWallType(nullptr) == 222);
        host.random = 2;
        CHECK(cSC4LotRetainingWallOccupant::GetRetainingWallType(nullptr) == 333);
        SetLotRetainingWallHost(nullptr);
    }

    // ------------------------------------------------------------------ per-edge flags and hashes
    {
        FakeHost host;
        SetLotRetainingWallHost(&host);
        host.terrain = [](int x, int z) { return static_cast<float>(x * 10 + z + 1); };
        host.hasEdgeCell[0] = true;
        host.edgeCells[0] = SC4CellRect{0, 0, 0, 0};
        host.terrainCells[0] = SC4CellRect{0, 0, 1, 1};   // four cells: heights 1, 2, 11, 12

        int flags[4] = {9, 9, 9, 9};
        float maxAltitude = 0.0f;
        // no lot on the scanned cell -> the edge is flagged and its terrain hash accumulated
        CHECK(cSC4LotRetainingWallOccupant::GetLotWallEdgeFlags(nullptr, 5.0f, SC4CellRect{2, 2, 3, 3}, flags,
                                                                maxAltitude));
        // hash = round(1)*0+1 + round(2)*0+1 + round(11)*0+1 + round(12)*1+1 = 16
        CHECK(flags[0] == 16);
        CHECK(flags[1] == 0 && flags[2] == 0 && flags[3] == 0);
        CHECK(maxAltitude == 12.0f);

        // a locked lot on every scanned cell: no wall on that edge, nothing sampled
        cSC4Lot* neighbor = MakeLot(0, 0, 1, 1, 0.0f, 1);
        host.lotsAtCell[{0, 0}] = neighbor;
        flags[0] = flags[1] = flags[2] = flags[3] = 9;
        maxAltitude = 0.0f;
        CHECK(!cSC4LotRetainingWallOccupant::GetLotWallEdgeFlags(nullptr, 5.0f, SC4CellRect{2, 2, 3, 3}, flags,
                                                                 maxAltitude));
        CHECK(flags[0] == 0 && flags[1] == 0 && flags[2] == 0 && flags[3] == 0);
        CHECK(maxAltitude == 5.1f);

        // ... but not when the lot is unlocked
        neighbor->SetFlags(0);
        CHECK(cSC4LotRetainingWallOccupant::GetLotWallEdgeFlags(nullptr, 5.0f, SC4CellRect{2, 2, 3, 3}, flags,
                                                                maxAltitude));
        CHECK(flags[0] == static_cast<int>(0x80000000u | 16u));   // the cell bit plus the hash

        // ... and a locked lot in the demolishable state also counts as "no wall"
        neighbor->SetFlags(1);
        neighbor->SetState(kLotStateDemolishable, 0, 0);
        CHECK(neighbor->GetState() == kLotStateDemolishable);
        CHECK(!cSC4LotRetainingWallOccupant::GetLotWallEdgeFlags(nullptr, 5.0f, SC4CellRect{2, 2, 3, 3}, flags,
                                                                 maxAltitude));
        CHECK(flags[0] == 0);

        neighbor->Release();
        SetLotRetainingWallHost(nullptr);
    }

    // ------------------------------------------------------------------ edge mask bit order
    {
        FakeHost host;
        SetLotRetainingWallHost(&host);
        host.terrain = [](int, int) { return 0.0f; };
        host.hasEdgeCell[1] = true;
        host.edgeCells[1] = SC4CellRect{0, 0, 1, 1};      // x outer, z inner: (0,0) (0,1) (1,0) (1,1)
        host.terrainCells[1] = SC4CellRect{0, 0, 0, 0};   // one vertex of height 0
        // (0,0) has no lot: the edge is flagged but that cell gets no bit. The locked lots take the
        // next bits from the top (30 and 28); the demolishable one (1,0) takes none.
        cSC4Lot* lockedA = MakeLot(0, 1, 1, 1, 0.0f, 1);
        cSC4Lot* lockedB = MakeLot(1, 0, 1, 1, 0.0f, 1);
        cSC4Lot* lockedC = MakeLot(1, 1, 1, 1, 0.0f, 1);
        lockedB->SetState(kLotStateDemolishable, 0, 0);
        host.lotsAtCell[{0, 1}] = lockedA;
        host.lotsAtCell[{1, 0}] = lockedB;
        host.lotsAtCell[{1, 1}] = lockedC;
        int flags[4] = {0, 0, 0, 0};
        float maxAltitude = 0.0f;
        CHECK(cSC4LotRetainingWallOccupant::GetLotWallEdgeFlags(nullptr, 0.0f, SC4CellRect{0, 0, 1, 1}, flags,
                                                                maxAltitude));
        // 0x50000000 from the two cell bits, +1 for the single terrain vertex (0 * (0*0) + 1)
        CHECK(flags[1] == static_cast<int>(0x50000001u));
        CHECK(maxAltitude == 0.1f);
        lockedA->Release();
        lockedB->Release();
        lockedC->Release();
        SetLotRetainingWallHost(nullptr);
    }

    // ------------------------------------------------------------------ altitude rounding
    {
        FakeHost host;
        SetLotRetainingWallHost(&host);
        host.terrain = [](int, int) { return 1.5f; };
        host.hasEdgeCell[0] = true;
        host.edgeCells[0] = SC4CellRect{0, 0, 0, 0};
        host.terrainCells[0] = SC4CellRect{1, 1, 1, 1};   // one vertex, x*z = 1
        int flags[4] = {0, 0, 0, 0};
        float maxAltitude = 0.0f;
        CHECK(cSC4LotRetainingWallOccupant::GetLotWallEdgeFlags(nullptr, 0.0f, SC4CellRect{0, 0, 0, 0}, flags,
                                                                maxAltitude));
        // the 2^23 bias bit trick (MOVD at 0x10015da9b) rounds 1.5 up to 2, a plain (int) cast would keep 1
        CHECK(flags[0] == 3);
        CHECK(maxAltitude == 1.5f);
        SetLotRetainingWallHost(nullptr);
    }

    // ------------------------------------------------------------------ create/update/remove the occupant
    {
        FakeHost host;
        SetLotRetainingWallHost(&host);
        host.wallIds = {1234};
        host.terrain = [](int x, int) { return x == 2 ? 9.0f : 7.0f; };
        host.hasEdgeCell[0] = true;
        host.edgeCells[0] = SC4CellRect{0, 0, 0, 0};
        host.terrainCells[0] = SC4CellRect{1, 1, 1, 1};   // height 7, x*z = 1 -> hash 8
        host.hasEdgeCell[2] = true;
        host.edgeCells[2] = SC4CellRect{2, 2, 2, 2};
        host.terrainCells[2] = SC4CellRect{2, 2, 2, 2};   // height 9, x*z = 4 -> hash 37

        cSC4Lot* lot = MakeLot(2, 2, 2, 2, 5.0f, 1);   // cells 2..3, 2..3
        CHECK(!cSC4LotRetainingWallOccupant::AddRetainingWallOccupant(lot, true));   // create reports false
        CHECK(host.inserts == 1);
        CHECK(host.occupants.size() == 1);
        CHECK(host.loadedType == 1234);
        CHECK(host.loadedWall != nullptr);
        CHECK(lot->GetFlags() == (1 | kLotFlagRetainingWall));

        cISC4LotRetainingWallOccupant* wall = host.loadedWall;
        CHECK(wall->GetRetainingWallType() == 1234);
        CHECK(wall->GetEdgeFlags() == (1 | 4));
        CHECK(wall->GetLot() == lot);
        float position[3] = {};
        CHECK(wall->AsOccupant() ==
              static_cast<cISC4Occupant*>(static_cast<cSC4LotRetainingWallOccupant*>(wall)));
        static_cast<cSC4LotRetainingWallOccupant*>(wall)->GetPosition(position);
        CHECK(position[0] == 48.0f && position[1] == 5.0f && position[2] == 48.0f);
        float lo[3] = {}, hi[3] = {};
        static_cast<cSC4LotRetainingWallOccupant*>(wall)->GetBoundingBox(lo, hi);
        CHECK(lo[0] == 32.0f && hi[0] == 64.0f);   // 48 +- 16
        CHECK(lo[1] == 1.0f && hi[1] == 5.0f);     // 5 - 4 .. 5
        int hashes[4] = {};
        wall->GetEdgeHashArray(hashes);
        CHECK(hashes[0] == 8 && hashes[1] == 0 && hashes[2] == 37 && hashes[3] == 0);

        // unchanged edges: UpdateRetainingWalls rebuilds nothing and rewrites nothing
        const int insertsBefore = host.inserts;
        cSC4LotRetainingWallOccupant::UpdateRetainingWalls(lot);
        CHECK(host.inserts == insertsBefore);
        wall->GetEdgeHashArray(hashes);
        CHECK(hashes[0] == 8);

        // terrain moves: the edge hash changes and the occupant's copy is refreshed
        host.terrainCells[0] = SC4CellRect{1, 2, 1, 2};   // height 7 at (1,2): 7 * 2 + 1 = 15
        cSC4LotRetainingWallOccupant::UpdateRetainingWalls(lot);
        wall->GetEdgeHashArray(hashes);
        CHECK(hashes[0] == 15);
        CHECK(hashes[2] == 37);

        // an unlocked lot never gets a new occupant (UpdateRetainingWalls returns early)
        lot->SetFlags(0);
        cSC4LotRetainingWallOccupant::UpdateRetainingWalls(lot);
        CHECK(host.occupants.size() == 1);
        lot->SetFlags(1 | kLotFlagRetainingWall);

        // removal path: host removes the occupant and the lot loses the flag
        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallOccupant(lot, false));
        CHECK(host.removals == 1);
        CHECK(host.occupants.empty());
        CHECK(lot->GetFlags() == 1);
        // quirk: the remove path clears the lot flag even when there is no occupant to remove
        lot->SetFlags(1 | kLotFlagRetainingWall);
        CHECK(!cSC4LotRetainingWallOccupant::AddRetainingWallOccupant(lot, false));   // nothing left to remove
        CHECK(lot->GetFlags() == 1);

        // the building-development simulator can refuse the wall type: the wall is dropped, nothing is inserted
        host.loadOk = false;
        host.loadedWall = nullptr;
        CHECK(!cSC4LotRetainingWallOccupant::AddRetainingWallOccupant(lot, true));
        CHECK(host.inserts == 1);
        CHECK(host.occupants.empty());
        CHECK(lot->GetFlags() == 1);

        // no edge is flagged (no lot manager cell rect): no occupant either
        host.loadOk = true;
        host.hasEdgeCell[0] = false;
        host.hasEdgeCell[2] = false;
        CHECK(!cSC4LotRetainingWallOccupant::AddRetainingWallOccupant(lot, true));
        CHECK(host.inserts == 1);
        CHECK(host.occupants.empty());
        CHECK(lot->GetFlags() == 1);

        lot->Release();
        SetLotRetainingWallHost(nullptr);
    }

    // ------------------------------------------------------------------ retaining wall models
    {
        FakeHost host;
        SetLotRetainingWallHost(&host);
        host.rendererAvailable = true;
        auto* occupant = new cSC4LotRetainingWallOccupant();
        occupant->AddRef();
        const float position[3] = {0.0f, 10.0f, 0.0f};
        const float size[3] = {32.0f, 4.0f, 32.0f};
        occupant->SetPosition(position);
        occupant->SetSize(size);
        occupant->SetEdgeFlags(1 | 4);

        const std::vector<std::uint32_t> textureIds = {1, 2, 3, 4, 5};
        cISCPropertyHolder* occupantProperties = occupant->AsPropertyHolder();
        cSCPropertyHelp::SetPropertyValue(occupantProperties, kPropertyRetainingWallTextures, textureIds);
        float textureScale = 2.5f;
        cSCPropertyHelp::SetPropertyValue(occupantProperties, kPropertyRetainingWallTextureScale, textureScale);

        auto* placeable = new cSC43DPlaceableObject();
        placeable->AddRef();
        occupant->SetPlaceableObject(placeable);
        placeable->Release();   // the occupant keeps the only reference

        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallModels(occupant, true, true));
        CHECK(placeable->GetModelCount() == 2);
        CHECK(host.created.size() == 2);
        CHECK(host.renderer.size() == 2);
        CHECK(host.created[0]->textureCalls == 1 && host.created[0]->initCalls == 1);
        CHECK(host.created[0]->initOk);
        CHECK(host.created[0]->textureScale == 2.5f);
        CHECK(host.created[0]->textureIds[4] == 5);
        CHECK(host.created[0]->bounds.min[0] == -16.0f && host.created[0]->bounds.min[1] == 6.0f);
        CHECK(host.created[0]->bounds.max[0] == 16.0f && host.created[0]->bounds.max[1] == 10.0f);
        CHECK(host.created[0]->instance_.owner_ == static_cast<cISC4Occupant*>(occupant));
        CHECK(host.created[1]->instance_.owner_ == static_cast<cISC4Occupant*>(occupant));
        CHECK(host.created[0]->edge == 0 && host.created[1]->edge == 2);

        ILotRetainingWallModel* found = nullptr;
        CHECK(cSC4LotRetainingWallOccupant::FindRetainingWallModel(placeable, 2, found));
        CHECK(found == host.created[1]);
        found->Release();
        CHECK(!cSC4LotRetainingWallOccupant::FindRetainingWallModel(placeable, 1, found));
        CHECK(found == host.created[1]);   // quirk: the out pointer is written even without a match

        // remove the model of edge 0
        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallModel(occupant, occupant, 0, false, false));
        CHECK(placeable->GetModelCount() == 1);
        CHECK(host.renderer.size() == 1);

        // quirk: the remove path asks the renderer about the model without checking that the renderer exists
        host.rendererAvailable = false;
        const int checksBefore = host.hasModelCalls;
        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallModel(occupant, occupant, 2, false, false));
        CHECK(host.hasModelCalls == checksBefore + 1);
        CHECK(placeable->GetModelCount() == 0);
        host.rendererAvailable = true;

        // create=false on AddRetainingWallModels removes every model of the placeable object
        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallModels(occupant, false, false));
        CHECK(placeable->GetModelCount() == 0);
        occupant->Release();

        // ---- model Init can refuse a segment: the edge flag is cleared and nothing is added
        auto* second = new cSC4LotRetainingWallOccupant();
        second->AddRef();
        second->SetEdgeFlags(1 << 1);
        auto* placeable2 = new cSC43DPlaceableObject();
        placeable2->AddRef();
        second->SetPlaceableObject(placeable2);
        placeable2->Release();
        host.initOk = false;
        host.created.clear();
        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallModel(second, second, 1, true, false));
        CHECK(second->GetEdgeFlags() == 0);
        CHECK(placeable2->GetModelCount() == 0);
        host.initOk = true;
        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallModel(second, second, 1, true, false));
        CHECK(second->GetEdgeFlags() == (1 << 1));
        CHECK(placeable2->GetModelCount() == 1);
        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallModel(second, second, 1, false, false));
        CHECK(placeable2->GetModelCount() == 0);
        second->Release();

        // ---- no texture properties on the occupant: the base texture exemplar is used and released
        auto* third = new cSC4LotRetainingWallOccupant();
        third->AddRef();
        auto* placeable3 = new cSC43DPlaceableObject();
        placeable3->AddRef();
        third->SetPlaceableObject(placeable3);
        placeable3->Release();
        const std::vector<std::uint32_t> baseIds = {9, 8, 7, 6, 5};
        cSCPropertyHelp::SetPropertyValue(&host.exemplar.properties, kPropertyRetainingWallTextures, baseIds);
        float baseScale = 0.5f;
        cSCPropertyHelp::SetPropertyValue(&host.exemplar.properties, kPropertyRetainingWallTextureScale, baseScale);
        host.exemplarAvailable = true;
        host.exemplar.refs = 0;
        host.created.clear();
        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallModel(third, third, 0, true, false));
        CHECK(host.created.size() == 1);
        CHECK(host.created[0]->textureIds[0] == 9 && host.created[0]->textureIds[4] == 5);
        CHECK(host.created[0]->textureScale == 0.5f);
        CHECK(host.exemplar.refs == 0);   // the reference handed out by the host was released
        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallModel(third, third, 0, false, false));

        // ---- neither the holder nor the base exemplar has the properties: zeros, no read at all
        host.exemplarAvailable = false;
        host.created.clear();
        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallModel(third, third, 0, true, false));
        CHECK(host.created.size() == 1);
        CHECK(host.created[0]->textureCalls == 1);
        CHECK(host.created[0]->textureIds[0] == 0 && host.created[0]->textureIds[4] == 0);
        CHECK(host.created[0]->textureScale == 0.0f);
        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallModel(third, third, 0, false, false));

        // ---- no placeable object at all (GetOrCreatePlaceableObject may return null): nothing happens
        auto* noPlaceable = new cSC4LotRetainingWallOccupant();
        noPlaceable->AddRef();
        noPlaceable->SetEdgeFlags(1);
        host.created.clear();
        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallModel(noPlaceable, noPlaceable, 0, true, false));
        CHECK(noPlaceable->GetEdgeFlags() == 1);
        CHECK(host.created.empty());
        CHECK(cSC4LotRetainingWallOccupant::AddRetainingWallModel(noPlaceable, noPlaceable, 0, false, false));
        noPlaceable->Release();
        third->Release();
        SetLotRetainingWallHost(nullptr);
    }

    // ------------------------------------------------------------------ surrounding lots
    {
        FakeHost host;
        SetLotRetainingWallHost(&host);
        host.wallIds = {77};
        host.terrain = [](int, int) { return 4.0f; };
        host.hasEdgeCell[0] = true;
        host.edgeCells[0] = SC4CellRect{0, 0, 0, 0};
        host.terrainCells[0] = SC4CellRect{0, 0, 0, 0};
        cSC4Lot* first = MakeLot(1, 1, 1, 1, 5.0f, 1);
        cSC4Lot* second = MakeLot(4, 4, 1, 1, 5.0f, 1);
        host.around = {first, second};
        cSC4LotRetainingWallOccupant::UpdateSurroundingRetainingWalls(first);
        CHECK(host.inserts == 2);
        CHECK(host.occupants.size() == 2);
        host.around.clear();   // an empty neighbour list changes nothing
        cSC4LotRetainingWallOccupant::UpdateSurroundingRetainingWalls(first);
        CHECK(host.inserts == 2);
        CHECK(host.occupants.size() == 2);
        ReleaseOccupants(host);
        first->Release();
        second->Release();
        SetLotRetainingWallHost(nullptr);
    }

    if (failures == 0) std::printf("sc4_lotretainingwalloccupant: ok\n");
    else std::printf("sc4_lotretainingwalloccupant: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
