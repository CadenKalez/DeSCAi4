// cSC4BuildingDevelopmentSimulator — owns the catalogue of building types (profiles loaded from building
// exemplars), the developers that grow lots, and the construction resources that bound how much can be
// under construction at once. 113 functions at 0x10014bddc..0x10015392a. This port covers the catalogue
// (exemplar scan, profiles, families, names, zone capacity map), the developer / wealth type mapping,
// construction resources, growth marks, cheats and the persisted state; the growth planner
// (PlanGrowth/Grow/ExecuteGrowthPlan/PlanDecline, tract and lot developers) remains unrecovered.
// Object layout: +0x80 tract developer map, +0xb8 developers (map type -> growth developer),
// +0xe0 construction resource counter, +0x178 stage population multiplier, +0x188 day of last successful
// build per developer type, +0x1b0 stage multiplier table, +0x1c9..0x1cd cheat bools.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "gz/cGZPersistResourceKey.h"
#include "rz/cRZString.h"
#include "sc4/BuildingOccupant.h"
#include "sc4/SCProperty.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4BuildingDevelopmentSimulator = 0x8990be7a;
constexpr std::uint32_t kExemplarTypeBuilding = 2;
constexpr std::uint32_t kExemplarTypeFoundation = 0x11;
constexpr std::uint32_t kExemplarTypeLotRetainingWall = 0x15;
constexpr std::uint32_t kMessageBuildingTypesUpdated = 0xa783ebc8;

// building exemplar property ids
constexpr std::uint32_t kBuildingPropWealth = 0x27812832;
constexpr std::uint32_t kBuildingPropPurpose = 0x27812833;
constexpr std::uint32_t kBuildingPropCapacities = 0x27812834;
constexpr std::uint32_t kBuildingPropPartialCapacity = 0x27812836;
constexpr std::uint32_t kBuildingPropGrowthStage = 0x499afa38;
constexpr std::uint32_t kBuildingPropCost = 0x49cac341;
constexpr std::uint32_t kBuildingPropName = 0x8a416a99;
constexpr std::uint32_t kBuildingPropCustomName = 0x899afbad;
constexpr std::uint32_t kBuildingPropFamilies = 0x27812870;
constexpr std::uint32_t kBuildingPropSize = 0x27812810;
constexpr std::uint32_t kBuildingPropGroups = 0xaa1dd396;

// cISC4BuildingDevelopmentSimulator::DeveloperType values (purpose x wealth)
enum DeveloperTypeValue : std::uint32_t {
    kDevResidentialLow = 0x1010, kDevResidentialMedium = 0x1020, kDevResidentialHigh = 0x1030,
    kDevCommercialServiceLow = 0x3110, kDevCommercialServiceMedium = 0x3120, kDevCommercialServiceHigh = 0x3130,
    kDevCommercialOfficeMedium = 0x3320, kDevCommercialOfficeHigh = 0x3330,
    kDevIndustrialAgriculture = 0x4100, kDevIndustrialDirty = 0x4200, kDevIndustrialManufacturing = 0x4300,
    kDevIndustrialHighTech = 0x4400,
};

class IConstructionResources {
public:
    virtual ~IConstructionResources() = default;
    virtual float Total() const = 0;                         // vslot 0x40
    virtual float Used() const = 0;                          // vslot 0x48
    virtual void Add(float amount) = 0;                      // vslot 0x88
    virtual void Update() = 0;                               // vslot 0xf0
};

class IBuildingDevelopmentSimHost {
public:
    virtual ~IBuildingDevelopmentSimHost() = default;
    virtual void ForEachExemplarKey(const std::function<void(const cGZPersistResourceKey&)>& fn) = 0;
    virtual cISCPropertyHolder* AcquireExemplar(const cGZPersistResourceKey& key) = 0;     // borrowed
    virtual void ReleaseExemplar(cISCPropertyHolder* holder) = 0;
    virtual std::int64_t MaintenanceCost(cISCPropertyHolder* exemplar) = 0;                // budget sim vslot 0x190
    virtual bool LoadLocalizedName(cISCPropertyHolder* exemplar, std::uint32_t property, cRZString& out) = 0;
    virtual void CityCellSize(float& x, float& z) = 0;                                      // city vslot 0x2d8/0x2e0
    virtual void PostBuildingTypesUpdated() = 0;
    virtual std::int32_t SimulatorDay() = 0;
    // The lot configurations the zone capacity map is computed from.
    struct LotConfigurationInfo {
        std::uint32_t sizeX = 0, sizeZ = 0;
        std::vector<std::uint32_t> buildingIds;               // primary building object ids (types or families)
        float objectX = 0, objectZ = 0;                       // primary building object position
        std::uint8_t objectOrientation = 0;
        std::vector<bool> zoneCompatibility;                  // 16 zone types
    };
    virtual void LotConfigurations(std::vector<LotConfigurationInfo>& out) = 0;
};
IBuildingDevelopmentSimHost* BuildingDevelopmentSimHost();
void SetBuildingDevelopmentSimHost(IBuildingDevelopmentSimHost* host);

class cSC4BuildingDevelopmentSimulator {
public:
    cSC4BuildingDevelopmentSimulator() = default;

    std::uint32_t AddRef() { return ++refCount_; }
    std::uint32_t Release();
    bool QueryInterface(std::uint32_t iid, void** out);
    bool Init();
    bool Shutdown();
    std::uint32_t GetGZCLSID() const { return kGZCLSID_cSC4BuildingDevelopmentSimulator; }

    // type / wealth mapping
    static std::uint32_t GetDeveloperType_Static(std::uint32_t purpose, std::int32_t wealth);
    static std::uint32_t GetWealth_Static(std::uint32_t developerType);
    std::uint32_t GetDeveloperType(std::uint32_t purpose, std::int32_t wealth) const { return GetDeveloperType_Static(purpose, wealth); }
    std::uint32_t GetWealth(std::uint32_t developerType) const { return GetWealth_Static(developerType); }
    std::uint8_t GetBuildingMapKey(std::uint32_t purpose, std::uint32_t wealth) const {
        return static_cast<std::uint8_t>(wealth + 1 + purpose * 4);
    }
    std::uint8_t GetBuildingMapKey(std::uint32_t buildingType) const;
    static void SetBuildingCategoryFlags(BuildingProfile& profile, const cISCPropertyHolder& exemplar);

    // catalogue
    static bool StaticInit();
    static bool StaticShutdown();
    static bool AddBuildingData(const cGZPersistResourceKey& key);
    static bool AddFoundationData(const cGZPersistResourceKey& key);
    static bool AddLotRetainingWallData(const cGZPersistResourceKey& key);
    static void LoadBuildingProperties(cISCPropertyHolder& exemplar, std::uint32_t type, bool firstLoad);
    static void CalculateMaxCapacity();
    void GetAllBuildingTypes(std::vector<std::uint32_t>& out) const;
    void GetAllBuildingFamilyTypes(std::vector<std::uint32_t>& out) const;
    bool GetBuildingKeyFromType(std::uint32_t type, cGZPersistResourceKey& key) const;
    bool IsBuildingTypeValid(std::uint32_t type) const;
    bool IsBuildingFamilyTypeValid(std::uint32_t type) const;
    int GetBuildingTypesByFamily(std::vector<std::uint32_t>& out, std::uint32_t family) const;
    bool IsBuildingInFamily(std::uint32_t type, std::uint32_t family) const;
    void GetAllFoundationTypes(std::vector<std::uint32_t>& out) const;
    bool GetFoundationKeyFromType(std::uint32_t type, cGZPersistResourceKey& key) const;
    void GetAllLotRetainingWallTypes(std::vector<std::uint32_t>& out) const;
    bool GetLotRetainingWallKeyFromType(std::uint32_t type, cGZPersistResourceKey& key) const;
    BuildingProfile& GetBuildingProfile(std::uint32_t type) const;
    const BuildingProfile* GetBuildingProfilePtr(std::uint32_t type) const;
    cRZString& GetBuildingName(std::uint32_t type) const;
    cRZString& GetExemplarName(std::uint32_t type) const;
    std::uint32_t GetMaxCapacity(int zoneType, std::uint32_t purpose, std::uint32_t wealth) const;

    // construction resources (the counter object is owned by the city)
    void SetConstructionResources(IConstructionResources* resources) { resources_ = resources; }
    std::uint16_t GetFreeConstructionResources() const;
    std::uint16_t GetTotalConstructionResources() const;
    bool ClaimConstructionResources(std::uint16_t amount);
    void ReleaseConstructionResources(std::uint16_t amount);
    float GetStagePopulationMultiplier(int stage) const { return stageMultipliers_[stage]; }
    void SetStagePopulationMultipliers(const std::vector<float>& values) { stageMultipliers_ = values; }
    std::int32_t GetDayOfLastSuccessfulBuild(std::int32_t developerType) const;
    void NoteSuccessfulBuild(std::int32_t developerType, std::int32_t day) { lastBuildDay_[developerType] = day; }

    // growth marks
    void MarkTract(std::uint32_t developerType, int x, int z) { growthMarks_[developerType].insert({x, z}); }
    bool IsTractUnmarked(std::uint32_t developerType, int x, int z) const;
    void ClearGrowthMarks() { growthMarks_.clear(); }

    // cheats
    bool CheatIgnorePlumbing() const { return cheatPlumbing_; }
    bool CheatIgnorePower() const { return cheatPower_; }
    bool CheatIgnoreCaps() const { return cheatCaps_; }
    bool CheatDebugGrowth() const { return cheatDebug_; }
    bool CheatCoffeeBar() const { return cheatCoffee_; }
    void SetCheats(bool plumbing, bool power, bool caps) { cheatPlumbing_ = plumbing; cheatPower_ = power; cheatCaps_ = caps; }

    // persisted state: version 4 -- growth marks, last-build days, cheats (the developer objects are
    // serialized by the growth developer classes through the host)
    bool Write(cIGZOStream& stream) const;
    bool Read(cIGZIStream& stream);
    bool ReadCityRecord(cIGZIStream& stream);   // @ 0x10014e064

private:
    std::uint32_t refCount_ = 0;
    bool initialized_ = false;
    IConstructionResources* resources_ = nullptr;                              // +0xe0
    std::vector<float> stageMultipliers_ = std::vector<float>(8, 1.0f);        // +0x1b0
    std::map<std::int32_t, std::int32_t> lastBuildDay_;                         // +0x180
    std::map<std::uint32_t, std::set<std::pair<int, int>>> growthMarks_;       // mGrowthMarks
    bool cheatPlumbing_ = false, cheatPower_ = false, cheatCaps_ = false;       // +0x1c9..0x1cb
    bool cheatDebug_ = false, cheatCoffee_ = false;                             // +0x1cc, +0x1cd
};

}  // namespace sc4
