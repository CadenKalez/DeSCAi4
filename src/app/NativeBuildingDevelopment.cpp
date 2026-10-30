// The building development simulator on the native application (notes/GROWTH_PLAN.md layer 1). Replaces the generated neutral
// IBuildingDevelopmentSimHost: the building / simulator exemplars come from the resource manager (so the building catalogue and the
// growth developers exist), the simulator becomes a real simulator agent and SL::spMS2 listener through a message-target adapter,
// its construction resources are a real cSC4Demand, and its growth developers are real cSC4GrowthDeveloper objects.
//
// Still neutral (each logs SC4_UNWIRED or is named here): the building / foundation occupants and their placement (layer 4), the
// maintenance cost and localized names, the lot configurations behind the zone capacity map.
#include "app/NativeBuildingDevelopment.h"

#include <cstdio>
#include <cstdlib>
#include <map>

#include "app/NativeCity.h"
#include "app/NativeGrowthDevelopers.h"
#include "gz/cGZPersistResourceManager.h"
#include "gz/cIGZPersistResourceKeyList.h"
#include "platform/Unwired.h"
#include "sc4/BuildingDevelopmentSimulator.h"
#include "sc4/DemandSimulator.h"
#include "sc4/Exemplar.h"
#include "sc4/ServiceLocator.h"
#include "sc4/Simulator.h"
#include "sc4/cSC4City.h"
#include "sc4/cSC4Demand.h"

namespace native {
namespace {
using namespace sc4;

// Every key of one resource type.
class TypeFilter final : public cIGZPersistResourceKeyFilter {
public:
    explicit TypeFilter(std::uint32_t type) : type_(type) {}
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool IsKeyIncluded(const cGZPersistResourceKey& key) override { return key.type == type_; }
private:
    std::uint32_t type_;
};

template <class T> T* CityComponent(sc4::CityComponent c, std::uint32_t clsid) {
    cSC4City* city = HostCity();
    cIGZUnknown* object = city ? city->GetComponent(c) : nullptr;
    void* raw = nullptr;
    if (!object || !object->QueryInterface(0xe4fda3d4, &raw) || !raw) return nullptr;
    auto* serializable = static_cast<cIGZSerializable*>(raw);
    T* out = serializable->GetGZCLSID() == clsid ? static_cast<T*>(serializable) : nullptr;
    serializable->Release();
    return out;
}

// The simulator's cIGZMessageTarget2 view (+0x10 in the original): the message server and the simulator deliver to it.
class SimulatorTarget final : public cIGZMessageTarget2 {
public:
    explicit SimulatorTarget(cSC4BuildingDevelopmentSimulator* simulator) : simulator_(simulator) {}
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != kGZIID_cIGZMessageTarget2 && iid != 1) return false;
        *out = static_cast<cIGZMessageTarget2*>(this);
        AddRef();
        return true;
    }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override { return refs_ > 1 ? --refs_ : 1; }   // owned by the host's table
    bool DoMessage(cIGZMessage2* message) override {
        static const bool trace = std::getenv("SC4_SIMDEBUG") != nullptr;
        if (trace && message && (message->GetType() == 0x66956816 || message->GetType() == 0x66956810))
            std::fprintf(stderr, "building development: month %08x, stage %d, %zu developers\n", message->GetType(), simulator_ ? simulator_->GrowthStage() : -1,
                         simulator_ ? simulator_->GetDevelopers().size() : 0);
        return simulator_ ? simulator_->DoMessage(message) : true;
    }
private:
    cSC4BuildingDevelopmentSimulator* simulator_;
    std::uint32_t refs_ = 1;
};
std::map<cSC4BuildingDevelopmentSimulator*, SimulatorTarget*> g_targets;
constexpr std::uint32_t kMessages[] = {0xe8bda2e7, 0x278e6003, 0x99ef1142, 0x99ef1143, 0xb99ae81};
constexpr int kAgentIntervals[] = {0, 4, 6, 10};

// cSC4Demand as the construction resources (+0xe0): supply = the month's total, demand = what construction holds.
class ConstructionResources final : public IConstructionResources {
public:
    float Total() const override { return demand_.QuerySupplyValue(); }       // vslot 0x40
    float Used() const override { return demand_.QueryDemandValue(); }        // vslot 0x48
    void Add(float amount) override { demand_.AddToDemandValue(amount); }      // vslot 0x88
    void Update() override { demand_.EndOfCycle(); }                           // vslot 0xf0
    void Init() override { demand_.Init(); }
    void SetTotal(float total) override { demand_.SetSupplyValue(total); }     // vslot 0x90
    void SetUsed(float used) override { demand_.SetDemandValue(used); }        // vslot 0x98
private:
    cSC4Demand demand_;
};

// One cSC4GrowthDeveloper (a COM object: the reference taken at creation is the simulator's).
class GrowthDeveloperRef final : public IGrowthDeveloperRef {
public:
    GrowthDeveloperRef() : developer_(NewGrowthDeveloper()) {}
    void Init(cISCPropertyHolder* properties) override { GrowthDeveloperInit(developer_, properties); }
    void StartMapUpdate() override { GrowthDeveloperStartMapUpdate(developer_); }
    bool IsMapUpdateFinished() const override { return GrowthDeveloperMapUpdateFinished(developer_); }
private:
    void* developer_;
};

// IBuildingPlacementServices: the growth developers are real; the occupant placement is layer 4.
class PlacementServices final : public IBuildingPlacementServices {
public:
    IPlacedBuilding* CreateBuilding() override { SC4_UNWIRED("IBuildingPlacementServices::CreateBuilding (layer 4)"); return nullptr; }
    void ReleaseBuilding(IPlacedBuilding*) override {}
    IPlacedFoundation* CreateFoundation() override { SC4_UNWIRED("IBuildingPlacementServices::CreateFoundation (layer 4)"); return nullptr; }
    void ReleaseFoundation(IPlacedFoundation*) override {}
    float FoundationSurfaceHeight(const int*, int) override { return 0.0f; }
    void LoadBuildingProperties(IPlacedBuilding&, std::uint32_t) override {}
    IGrowthDeveloperRef* CreateGrowthDeveloper() override { return new GrowthDeveloperRef(); }
    float SurfaceHeightForFootprint(const float*) override { return 0.0f; }
    void CellToPosition(int x, int z, float& px, float& pz) override { px = (static_cast<float>(x) + 0.5f) * 16.0f; pz = (static_cast<float>(z) + 0.5f) * 16.0f; }
    void PositionToCell(float x, float z, int& cx, int& cz) override {
        if (cSC4City* city = HostCity()) city->PositionToCell(x, z, cx, cz);
        else { cx = static_cast<int>(x / 16.0f); cz = static_cast<int>(z / 16.0f); }
    }
    float TerrainHeight(float, float) override { return 0.0f; }
    bool OccupantAtCell(int, int, std::uint32_t) override { return false; }
    bool AddOccupant(IPlacedOccupant*, bool) override { SC4_UNWIRED("IBuildingPlacementServices::AddOccupant (layer 4)"); return false; }
    bool RemoveOccupant(IPlacedOccupant*, bool, bool) override { return false; }
} g_placement;

class BuildingDevelopmentHost final : public IBuildingDevelopmentSimHost {
public:
    void ForEachExemplarKey(const std::function<void(const cGZPersistResourceKey&)>& fn) override {
        if (!SL::spRM) return;
        cIGZPersistResourceKeyList* list = nullptr;
        TypeFilter filter(0x6534284a);
        SL::spRM->GetAvailableResourceList(&list, &filter);
        if (!list) return;
        std::vector<cGZPersistResourceKey> keys;
        for (std::uint32_t i = 0; i < list->Size(); ++i) keys.push_back(*list->GetKey(i));
        list->Release();
        for (const cGZPersistResourceKey& key : keys) fn(key);
    }
    cISCPropertyHolder* AcquireExemplar(const cGZPersistResourceKey& key) override {
        if (!SL::spRM) return nullptr;
        void* raw = nullptr;
        if (!SL::spRM->GetResource(key, 0xa52160f5, &raw, 0, nullptr) || !raw) return nullptr;
        auto* exemplar = static_cast<cISCResExemplar*>(raw);
        cISCPropertyHolder* holder = exemplar->AsISCPropertyHolder();
        held_[holder] = exemplar;
        return holder;
    }
    void ReleaseExemplar(cISCPropertyHolder* holder) override {
        auto it = held_.find(holder);
        if (it == held_.end()) return;
        it->second->Release();
        held_.erase(it);
    }
    std::int64_t MaintenanceCost(cISCPropertyHolder*) override { return 0; }   // budget simulator vslot 0x190: not wired here
    bool LoadLocalizedName(cISCPropertyHolder*, std::uint32_t, cRZString&) override { return false; }
    void CityCellSize(float& x, float& z) override { x = 16.0f; z = 16.0f; }
    void PostBuildingTypesUpdated() override {}
    std::int32_t SimulatorDay() override {
        cSC4Simulator* simulator = CityComponent<cSC4Simulator>(sc4::CityComponent::Simulator, kGZCLSID_cSC4Simulator);
        return simulator ? static_cast<std::int32_t>(simulator->GetSimDateNumber()) : 0;
    }
    void LotConfigurations(std::vector<LotConfigurationInfo>&) override {}   // the zone capacity map stays empty (GROWTH-1)

    void RegisterMessageTarget(cSC4BuildingDevelopmentSimulator* simulator) override {
        if (!simulator || g_targets.count(simulator)) return;
        auto* target = new SimulatorTarget(simulator);
        g_targets[simulator] = target;
        if (SL::spMS2) for (std::uint32_t type : kMessages) SL::spMS2->AddNotification(target, type);
        int agents = 0;
        for (int interval : kAgentIntervals) agents += AddCitySimulatorAgent(target, interval, "cSC4BuildingDevelopmentSimulator", 2) ? 1 : 0;
        if (std::getenv("SC4_SIMDEBUG")) std::fprintf(stderr, "building development: %d agents registered\n", agents);
    }
    void UnregisterMessageTarget(cSC4BuildingDevelopmentSimulator* simulator) override {
        auto it = g_targets.find(simulator);
        if (it == g_targets.end()) return;
        if (SL::spMS2) for (std::uint32_t type : kMessages) SL::spMS2->RemoveNotification(it->second, type);
        RemoveCitySimulatorAgent(it->second);
        delete it->second;
        g_targets.erase(it);
    }
    IBuildingPlacementServices* PlacementServices() override { return &g_placement; }
    IConstructionResources* CreateConstructionResources() override { return new ConstructionResources(); }
    float DemandChannelSupply(std::uint32_t type) override {
        cSC4DemandSimulator* demand = CityComponent<cSC4DemandSimulator>(sc4::CityComponent::DemandSimulator, kGZCLSID_cSC4DemandSimulator);
        cSC4Demand* channel = demand ? demand->GetDemand(type, 0) : nullptr;
        return channel ? channel->QuerySupplyValue() : 0.0f;
    }

private:
    std::map<cISCPropertyHolder*, cISCResExemplar*> held_;
};

}  // namespace

void InstallBuildingDevelopmentHost() {
    static BuildingDevelopmentHost host;
    sc4::SetBuildingDevelopmentSimHost(&host);
}

}  // namespace native
