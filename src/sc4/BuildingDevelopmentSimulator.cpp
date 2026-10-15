#include "sc4/BuildingDevelopmentSimulator.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "sc4/SCPropertyHelp.h"

namespace sc4 {

namespace {
IBuildingDevelopmentSimHost* g_host = nullptr;
std::unordered_map<std::uint32_t, cGZPersistResourceKey> g_buildingKeys;       // mBuildingExemplarKeyMap
std::unordered_map<std::uint32_t, cRZString> g_buildingNames;                  // mBuildingNameMap
std::unordered_map<std::uint32_t, cRZString> g_exemplarNames;                  // mExemplarNameMap
std::unordered_map<std::uint32_t, BuildingProfile> g_profiles;                 // mBuildingProfileMap
std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> g_families;      // mBuildingFamilyMap
std::map<std::pair<std::int32_t, std::uint8_t>, float> g_zoneCapacity;         // mZoneCapacityMap
std::unordered_map<std::uint32_t, cGZPersistResourceKey> g_foundationKeys;     // mFoundationKeyMap
std::unordered_map<std::uint32_t, cGZPersistResourceKey> g_wallKeys;           // mLotRetainingWallKeyMap
bool g_staticInitialized = false;

// kPropertyIdForCategoryBit: occupant group ids -> bit in BuildingProfile::categoryFlags
constexpr std::uint32_t kCategoryGroupIds[31] = {
    0x1000, 0x1001, 0x1002, 0x1003, 0x1004, 0x1005, 0x1006, 0x1300, 0x1301, 0x1302, 0x1400,
    0x1401, 0x1402, 0x1500, 0x1501, 0x1502, 0x1503, 0x1504, 0x1505, 0x1506, 0x1507, 0x1508,
    0x1509, 0x150a, 0x150b, 0x2000, 0x2001, 0x2002, 0x3000, 0x3001, 0x3002};

void CopyKeyList(const std::unordered_map<std::uint32_t, cGZPersistResourceKey>& map, std::vector<std::uint32_t>& out) {
    out.clear();
    for (const auto& kv : map) out.push_back(kv.first);
}
}  // namespace

IBuildingDevelopmentSimHost* BuildingDevelopmentSimHost() { return g_host; }
void SetBuildingDevelopmentSimHost(IBuildingDevelopmentSimHost* host) { g_host = host; }

// ------------------------------------------------------------------------------------ type mapping

std::uint32_t cSC4BuildingDevelopmentSimulator::GetDeveloperType_Static(std::uint32_t purpose, std::int32_t wealth) {
    switch (purpose) {
    case 1: return static_cast<std::uint32_t>(wealth - 1) < 3 ? (wealth - 1) * 0x10 + 0x1010 : 0;
    case 2: return static_cast<std::uint32_t>(wealth - 1) < 3 ? (wealth - 1) * 0x10 + 0x3110 : 0;
    case 3: return wealth == 2 ? 0x3320 : (wealth == 3 ? 0x3330 : 0);
    case 5: return 0x4100;
    case 6: return 0x4200;
    case 7: return 0x4300;
    case 8: return 0x4400;
    default: return 0;
    }
}

std::uint32_t cSC4BuildingDevelopmentSimulator::GetWealth_Static(std::uint32_t developerType) {
    switch (developerType) {
    case 0x1010: case 0x3110: case 0x4100: return 1;
    case 0x1020: case 0x3120: case 0x3320: case 0x4200: case 0x4300: return 2;
    case 0x1030: case 0x3130: case 0x3330: case 0x4400: return 3;
    default: return 0;
    }
}

void cSC4BuildingDevelopmentSimulator::SetBuildingCategoryFlags(BuildingProfile& profile,
                                                                const cISCPropertyHolder& exemplar) {
    profile.categoryFlags = 0;
    std::vector<std::uint32_t> groups;
    if (!cSCPropertyHelp::GetPropertyValue(&exemplar, kBuildingPropGroups, groups)) return;
    for (std::uint32_t group : groups)
        for (int bit = 0; bit < 31; ++bit)
            if (kCategoryGroupIds[bit] == group) profile.categoryFlags |= 1u << bit;
}

std::uint8_t cSC4BuildingDevelopmentSimulator::GetBuildingMapKey(std::uint32_t buildingType) const {
    const BuildingProfile& p = GetBuildingProfile(buildingType);
    return GetBuildingMapKey(p.purpose, p.wealth);
}

// --------------------------------------------------------------------------------------- catalogue

void cSC4BuildingDevelopmentSimulator::LoadBuildingProperties(cISCPropertyHolder& exemplar, std::uint32_t type,
                                                              bool firstLoad) {
    BuildingProfile profile;
    cRZString exemplarName;
    if (!exemplar.GetProperty(0x20, exemplarName)) exemplarName.AssignFromCString("Building");
    cRZString buildingName;
    if (!(g_host && g_host->LoadLocalizedName(&exemplar, kBuildingPropName, buildingName))) {
        if (!exemplar.GetProperty(kBuildingPropCustomName, buildingName)) buildingName.AssignFromCString("Building");
    }

    std::uint8_t value = 0;
    if (cSCPropertyHelp::GetPropertyValue(&exemplar, kBuildingPropWealth, value)) {
        profile.wealth = value;
        if (value > 3) profile.wealth = 0;            // ("Building %s has a bad wealth property.")
    }
    value = 0;
    if (cSCPropertyHelp::GetPropertyValue(&exemplar, kBuildingPropPurpose, value)) profile.purpose = value;

    std::vector<std::uint32_t> pairs;
    if (cSCPropertyHelp::GetPropertyValue(&exemplar, kBuildingPropCapacities, pairs)) {
        std::map<std::uint32_t, std::uint16_t> capacities;
        for (std::size_t i = 0; i + 1 < pairs.size(); i += 2)
            capacities[pairs[i]] = static_cast<std::uint16_t>(pairs[i + 1]);
        profile.capacities.push_back(capacities);
        const std::uint32_t ownType = GetDeveloperType_Static(profile.purpose, static_cast<std::int32_t>(profile.wealth));
        profile.capacity = capacities[ownType];           // (operator[]: 0 when the own type is missing)
    }
    bool partial = false;
    if (cSCPropertyHelp::GetPropertyValue(&exemplar, kBuildingPropPartialCapacity, partial)) profile.allowPartialCapacity = partial;
    std::uint8_t stage = 0x1e;
    cSCPropertyHelp::GetPropertyValue(&exemplar, kBuildingPropGrowthStage, stage);
    profile.growthStage = stage;
    SetBuildingCategoryFlags(profile, exemplar);
    std::vector<float> size;
    if (cSCPropertyHelp::GetPropertyValue(&exemplar, kBuildingPropSize, size))
        for (std::size_t i = 0; i < size.size() && i < 3; ++i) profile.size[i] = size[i];
    std::int64_t cost = 0;
    if (!cSCPropertyHelp::GetPropertyValue(&exemplar, kBuildingPropCost, cost)) cost = 0;
    profile.cost = cost;
    profile.maintenance = g_host ? g_host->MaintenanceCost(&exemplar) : 0;

    if (!firstLoad) {
        g_buildingNames.erase(type);
        g_exemplarNames.erase(type);
        g_profiles.erase(type);
        for (auto& kv : g_families) {
            auto& members = kv.second;
            auto it = std::find(members.begin(), members.end(), type);
            if (it != members.end()) members.erase(it);
        }
    }
    g_buildingNames[type] = buildingName;
    g_exemplarNames[type] = exemplarName;
    g_profiles[type] = profile;

    std::vector<std::uint32_t> families;
    if (cSCPropertyHelp::GetPropertyValue(&exemplar, kBuildingPropFamilies, families)) {
        for (std::uint32_t family : families) {
            auto& members = g_families[family];
            if (std::find(members.begin(), members.end(), type) == members.end()) members.push_back(type);
        }
    }
}

bool cSC4BuildingDevelopmentSimulator::AddBuildingData(const cGZPersistResourceKey& key) {
    if (!g_host) return false;
    cISCPropertyHolder* exemplar = g_host->AcquireExemplar(key);
    if (!exemplar) return false;
    g_buildingKeys[key.instance] = key;
    LoadBuildingProperties(*exemplar, key.instance, true);
    g_host->ReleaseExemplar(exemplar);
    return true;
}

bool cSC4BuildingDevelopmentSimulator::AddFoundationData(const cGZPersistResourceKey& key) {
    if (!g_host) return false;
    if (cISCPropertyHolder* exemplar = g_host->AcquireExemplar(key)) {
        g_foundationKeys[key.instance] = key;
        g_host->ReleaseExemplar(exemplar);
    }
    return false;                      // (sic) always 0
}

bool cSC4BuildingDevelopmentSimulator::AddLotRetainingWallData(const cGZPersistResourceKey& key) {
    if (!g_host) return false;
    if (cISCPropertyHolder* exemplar = g_host->AcquireExemplar(key)) {
        g_wallKeys[key.instance] = key;
        g_host->ReleaseExemplar(exemplar);
    }
    return false;
}

bool cSC4BuildingDevelopmentSimulator::StaticInit() {
    if (!g_staticInitialized) {
        g_staticInitialized = true;
        if (g_host) {
            g_host->ForEachExemplarKey([](const cGZPersistResourceKey& key) {
                cISCPropertyHolder* exemplar = g_host->AcquireExemplar(key);
                if (!exemplar) return;
                std::uint32_t exemplarType = 0;
                if (exemplar->GetProperty(0x10, exemplarType)) {
                    if (exemplarType == kExemplarTypeBuilding) AddBuildingData(key);
                    else if (exemplarType == kExemplarTypeFoundation) AddFoundationData(key);
                    else if (exemplarType == kExemplarTypeLotRetainingWall) AddLotRetainingWallData(key);
                }
                g_host->ReleaseExemplar(exemplar);
            });
            g_host->PostBuildingTypesUpdated();
        }
        CalculateMaxCapacity();
    }
    return true;
}

bool cSC4BuildingDevelopmentSimulator::StaticShutdown() {
    if (g_staticInitialized) {
        g_staticInitialized = false;
        g_buildingKeys.clear();
        g_buildingNames.clear();
        g_exemplarNames.clear();
        g_profiles.clear();
        g_families.clear();
        g_zoneCapacity.clear();
        g_foundationKeys.clear();
        g_wallKeys.clear();
    }
    return true;
}

void cSC4BuildingDevelopmentSimulator::CalculateMaxCapacity() {
    if (!g_host) return;
    std::vector<IBuildingDevelopmentSimHost::LotConfigurationInfo> configurations;
    g_host->LotConfigurations(configurations);
    float cellX = 16.0f, cellZ = 16.0f;
    g_host->CityCellSize(cellX, cellZ);
    for (const auto& config : configurations) {
        const float area = static_cast<float>(config.sizeX * config.sizeZ);
        const float lotWidth = cellX * static_cast<float>(config.sizeX);
        const float lotDepth = cellZ * static_cast<float>(config.sizeZ);
        std::vector<std::uint32_t> types;
        for (std::uint32_t id : config.buildingIds) {
            auto family = g_families.find(id);
            if (family == g_families.end()) {
                if (std::find(types.begin(), types.end(), id) == types.end()) types.push_back(id);
            } else {
                for (std::uint32_t member : family->second)
                    if (std::find(types.begin(), types.end(), member) == types.end()) types.push_back(member);
            }
        }
        for (std::uint32_t type : types) {
            bool fits = true;
            auto key = g_buildingKeys.find(type);
            cISCPropertyHolder* exemplar = key != g_buildingKeys.end() ? g_host->AcquireExemplar(key->second) : nullptr;
            if (exemplar) {
                std::vector<float> size;
                if (cSCPropertyHelp::GetPropertyValue(exemplar, kBuildingPropSize, size) && size.size() >= 3) {
                    float width, depth;
                    if ((config.objectOrientation | 2) == 3) { width = size[2]; depth = size[0]; }
                    else { width = size[0]; depth = size[2]; }
                    const float minX = config.objectX - width * 0.5f;
                    const float maxX = width * 0.5f + config.objectX;
                    fits = (0.0f <= minX + 0.001f) && (0.0f <= (config.objectZ - depth * 0.5f) + 0.001f) &&
                           (maxX - 0.001f <= lotWidth) && (depth * 0.5f + config.objectZ - 0.001f <= lotDepth);
                }
                g_host->ReleaseExemplar(exemplar);
            }
            if (!fits) continue;
            auto profileIt = g_profiles.find(type);
            if (profileIt == g_profiles.end()) continue;
            const BuildingProfile& profile = profileIt->second;
            if (profile.purpose == 0) continue;
            float perCell = static_cast<float>(profile.capacity) / area + 0.5f;
            if (profile.capacity != 0 && perCell < 1.0f) perCell = 1.0f;
            const std::uint8_t mapKey = static_cast<std::uint8_t>(profile.wealth + 1 + profile.purpose * 4);
            for (int zone = 0; zone < 16; ++zone) {
                if (zone >= static_cast<int>(config.zoneCompatibility.size()) || !config.zoneCompatibility[zone]) continue;
                auto k = std::make_pair(static_cast<std::int32_t>(zone), mapKey);
                auto it = g_zoneCapacity.find(k);
                if (it == g_zoneCapacity.end() || it->second < perCell) g_zoneCapacity[k] = perCell;
            }
        }
    }
}

std::uint32_t cSC4BuildingDevelopmentSimulator::GetMaxCapacity(int zoneType, std::uint32_t purpose,
                                                               std::uint32_t wealth) const {
    auto it = g_zoneCapacity.find({zoneType, GetBuildingMapKey(purpose, wealth)});
    if (it == g_zoneCapacity.end()) return 0;
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(it->second));
}

void cSC4BuildingDevelopmentSimulator::GetAllBuildingTypes(std::vector<std::uint32_t>& out) const { CopyKeyList(g_buildingKeys, out); }
void cSC4BuildingDevelopmentSimulator::GetAllBuildingFamilyTypes(std::vector<std::uint32_t>& out) const {
    out.clear();
    for (const auto& kv : g_families) out.push_back(kv.first);
}

bool cSC4BuildingDevelopmentSimulator::GetBuildingKeyFromType(std::uint32_t type, cGZPersistResourceKey& key) const {
    auto it = g_buildingKeys.find(type);
    if (it == g_buildingKeys.end()) return false;
    key = it->second;
    return true;
}

bool cSC4BuildingDevelopmentSimulator::IsBuildingTypeValid(std::uint32_t type) const { return g_buildingKeys.count(type) != 0; }

bool cSC4BuildingDevelopmentSimulator::IsBuildingFamilyTypeValid(std::uint32_t type) const {
    return g_families.count(type) != 0 || g_buildingKeys.count(type) != 0;
}

int cSC4BuildingDevelopmentSimulator::GetBuildingTypesByFamily(std::vector<std::uint32_t>& out, std::uint32_t family) const {
    auto it = g_families.find(family);
    if (it != g_families.end()) {
        if (&it->second != &out) out = it->second;
        return static_cast<int>(it->second.size());
    }
    if (g_buildingKeys.count(family) == 0) return 0;
    out.assign(1, family);
    return 0;                              // (sic) like the prop manager: fills one id but returns 0
}

bool cSC4BuildingDevelopmentSimulator::IsBuildingInFamily(std::uint32_t type, std::uint32_t family) const {
    if (type == family) return true;
    auto it = g_families.find(family);
    return it != g_families.end() && std::find(it->second.begin(), it->second.end(), type) != it->second.end();
}

void cSC4BuildingDevelopmentSimulator::GetAllFoundationTypes(std::vector<std::uint32_t>& out) const { CopyKeyList(g_foundationKeys, out); }
bool cSC4BuildingDevelopmentSimulator::GetFoundationKeyFromType(std::uint32_t type, cGZPersistResourceKey& key) const {
    auto it = g_foundationKeys.find(type);
    if (it == g_foundationKeys.end()) return false;
    key = it->second;
    return true;
}
void cSC4BuildingDevelopmentSimulator::GetAllLotRetainingWallTypes(std::vector<std::uint32_t>& out) const { CopyKeyList(g_wallKeys, out); }
bool cSC4BuildingDevelopmentSimulator::GetLotRetainingWallKeyFromType(std::uint32_t type, cGZPersistResourceKey& key) const {
    auto it = g_wallKeys.find(type);
    if (it == g_wallKeys.end()) return false;
    key = it->second;
    return true;
}

BuildingProfile& cSC4BuildingDevelopmentSimulator::GetBuildingProfile(std::uint32_t type) const { return g_profiles[type]; }
const BuildingProfile* cSC4BuildingDevelopmentSimulator::GetBuildingProfilePtr(std::uint32_t type) const {
    auto it = g_profiles.find(type);
    return it == g_profiles.end() ? nullptr : &it->second;
}
cRZString& cSC4BuildingDevelopmentSimulator::GetBuildingName(std::uint32_t type) const { return g_buildingNames[type]; }
cRZString& cSC4BuildingDevelopmentSimulator::GetExemplarName(std::uint32_t type) const { return g_exemplarNames[type]; }

// ---------------------------------------------------------------------------- construction resources

std::uint16_t cSC4BuildingDevelopmentSimulator::GetFreeConstructionResources() const {
    if (!resources_) return 0;
    const float free = resources_->Total() - resources_->Used();
    const std::int32_t value = static_cast<std::int32_t>(free);
    return static_cast<std::uint16_t>(value < 0 ? 0 : value);
}

std::uint16_t cSC4BuildingDevelopmentSimulator::GetTotalConstructionResources() const {
    return resources_ ? static_cast<std::uint16_t>(static_cast<std::int32_t>(resources_->Total())) : 0;
}

bool cSC4BuildingDevelopmentSimulator::ClaimConstructionResources(std::uint16_t amount) {
    if (!resources_) return false;
    const bool ok = resources_->Used() + static_cast<float>(amount) <= resources_->Total();
    if (ok) {
        resources_->Add(static_cast<float>(amount));
        resources_->Update();
    }
    return ok;
}

void cSC4BuildingDevelopmentSimulator::ReleaseConstructionResources(std::uint16_t amount) {
    if (!resources_) return;
    resources_->Add(-static_cast<float>(amount));
    resources_->Update();
}

std::int32_t cSC4BuildingDevelopmentSimulator::GetDayOfLastSuccessfulBuild(std::int32_t developerType) const {
    auto it = lastBuildDay_.find(developerType);
    return it == lastBuildDay_.end() ? 0 : it->second;
}

bool cSC4BuildingDevelopmentSimulator::IsTractUnmarked(std::uint32_t developerType, int x, int z) const {
    auto it = growthMarks_.find(developerType);
    return it == growthMarks_.end() || it->second.find({x, z}) == it->second.end();
}

// ------------------------------------------------------------------------------------- lifecycle

std::uint32_t cSC4BuildingDevelopmentSimulator::Release() {
    if (refCount_ < 2) {
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cSC4BuildingDevelopmentSimulator::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

bool cSC4BuildingDevelopmentSimulator::Init() {
    if (!initialized_) {
        initialized_ = true;
        StaticInit();
    }
    return true;
}

bool cSC4BuildingDevelopmentSimulator::Shutdown() {
    initialized_ = false;
    return true;
}

bool cSC4BuildingDevelopmentSimulator::Write(cIGZOStream& stream) const {
    if (stream.GetError()) return false;
    stream.SetUint16(4);
    stream.SetUint32(static_cast<std::uint32_t>(growthMarks_.size()));
    for (const auto& mark : growthMarks_) {
        stream.SetUint32(mark.first);
        stream.SetUint32(static_cast<std::uint32_t>(mark.second.size()));
        for (const auto& cell : mark.second) {
            stream.SetSint32(cell.first);
            stream.SetSint32(cell.second);
        }
    }
    stream.SetUint32(static_cast<std::uint32_t>(lastBuildDay_.size()));
    for (const auto& kv : lastBuildDay_) {
        stream.SetSint32(kv.first);
        stream.SetSint32(kv.second);
    }
    stream.SetUint8(cheatPlumbing_);
    stream.SetUint8(cheatPower_);
    stream.SetUint8(cheatCaps_);
    return !stream.GetError();
}

bool cSC4BuildingDevelopmentSimulator::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 4) return false;
    growthMarks_.clear();
    std::uint32_t count = 0;
    stream.GetUint32(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t key = 0, size = 0;
        stream.GetUint32(key);
        stream.GetUint32(size);
        auto& cells = growthMarks_[key];
        for (std::uint32_t j = 0; j < size; ++j) {
            std::int32_t x = 0, z = 0;
            stream.GetSint32(x);
            stream.GetSint32(z);
            cells.insert({x, z});
        }
    }
    lastBuildDay_.clear();
    stream.GetUint32(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::int32_t type = 0, day = 0;
        stream.GetSint32(type);
        stream.GetSint32(day);
        lastBuildDay_[type] = day;
    }
    std::uint8_t b = 0;
    stream.GetUint8(b); cheatPlumbing_ = b != 0;
    stream.GetUint8(b); cheatPower_ = b != 0;
    stream.GetUint8(b); cheatCaps_ = b != 0;
    return !stream.GetError();
}

}  // namespace sc4
