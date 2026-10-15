#include "sc4/FloraSimulator.h"

#include <cmath>
#include <cstring>

#include "sc4/SCPropertyHelp.h"

namespace sc4 {

namespace {
IFloraSimulatorHost* g_host = nullptr;
bool g_staticInitialized = false;                                           // mbStaticInitialized
std::map<std::uint32_t, cSC4StaticFloraData*> g_staticFloraDataMap;         // mStaticFloraDataMap
std::vector<std::uint32_t> g_floraTypeArray;                                // mFloraTypeArray (insertion order)
std::map<std::uint32_t, std::vector<std::uint32_t>> g_floraFamilyMap;       // mFloraFamilyMap
}  // namespace

IFloraSimulatorHost* FloraSimulatorHost() { return g_host; }
void SetFloraSimulatorHost(IFloraSimulatorHost* host) { g_host = host; }

void cSC4FloraSimulator::MakeDefaultStaticFloraData(cSC4StaticFloraData& data) {
    static const std::uint32_t kDefaultTraits[14] = {
        0x44368000, 0x44368000, 0x43b68000, 0x40000000, 0x43000000, 0x44b68000, 0x44c30000,
        0x3f000000, 0x3e99999a, 0x3f000000, 0x3f800000, 0x40800000, 0x40400000, 0x40400000};
    data.floraType = 0;
    data.familyKey = 0;
    data.groupType = 0;
    data.aquatic = false;
    std::memcpy(data.traits, kDefaultTraits, sizeof data.traits);
    data.value48 = 0;
    data.value4c = 0;
    // 0x99 inside the 8x8 block of elevation indices 5..12 x terrain classes 5..12, zero elsewhere
    for (unsigned elevation = 0; elevation < 16; ++elevation)
        for (unsigned terrainClass = 0; terrainClass < 16; ++terrainClass)
            data.suitability[elevation][terrainClass] =
                ((terrainClass - 5u) | (elevation - 5u)) < 8u ? 0x99 : 0x00;
    data.value150 = 0;
}

bool cSC4FloraSimulator::StaticInit() {
    if (!g_staticInitialized) {
        g_staticInitialized = true;
        LoadAllStandardFloraTypes();
    }
    return true;
}

bool cSC4FloraSimulator::StaticShutdown() {
    if (g_staticInitialized) {
        g_staticInitialized = false;
        for (auto& entry : g_staticFloraDataMap) entry.second->Release();
        g_staticFloraDataMap.clear();
        g_floraTypeArray.clear();          // the family map is left in place, as in the original
    }
    return true;
}

void cSC4FloraSimulator::LoadAllStandardFloraTypes() {
    if (g_host) {
        g_host->EnumerateFloraResources(
            [](const cGZPersistResourceKey& key, void*) { AddStaticFloraDataFromResource(key); }, nullptr);
        g_host->PostFloraTypesUpdated();
    }
}

void cSC4FloraSimulator::AddStaticFloraDataFromResource(const cGZPersistResourceKey& key) {
    if (!g_host) return;
    cISCResExemplar* exemplar = g_host->LoadExemplar(key);
    if (!exemplar) return;
    std::uint32_t exemplarType = 0;
    if (cSCPropertyHelp::GetPropertyValue(exemplar->AsISCPropertyHolder(), 0x10, exemplarType) &&
        exemplarType == kFloraExemplarTypeValue) {
        AddStaticFloraData(key, exemplar);
    }
    exemplar->Release();
}

bool cSC4FloraSimulator::AddStaticFloraData(cSC4StaticFloraData* data) {
    const std::uint32_t type = data->floraType;
    if (g_staticFloraDataMap.count(type)) return false;
    g_staticFloraDataMap[type] = data;
    g_floraTypeArray.push_back(type);
    data->AddRef();
    for (std::uint32_t family : data->families) g_floraFamilyMap[family].push_back(type);
    // Group resolution: a type whose family key names this type is its own group; every type that grouped under
    // this type's family key is regrouped under this type.
    data->groupType = type;
    for (auto& entry : g_staticFloraDataMap) {
        cSC4StaticFloraData* other = entry.second;
        if (other->familyKey == type) data->groupType = type;
        if (other->groupType == data->familyKey) other->groupType = type;
    }
    return true;
}

bool cSC4FloraSimulator::AddStaticFloraData(const cGZPersistResourceKey& key, cISCResExemplar* exemplar) {
    if (!exemplar) {
        if (!g_host) return false;
        exemplar = g_host->LoadExemplar(key);
        if (!exemplar) return false;
    } else {
        exemplar->AddRef();
    }
    cISCPropertyHolder* holder = exemplar->AsISCPropertyHolder();
    auto* data = new cSC4StaticFloraData();
    data->AddRef();
    MakeDefaultStaticFloraData(*data);
    exemplar->AddRef();
    data->exemplar = exemplar;

    cSCPropertyHelp::GetPropertyValue(holder, kFloraPropTypeId, data->floraType);
    std::vector<float> floats;
    if (holder->HasProperty(kFloraPropTraits) && cSCPropertyHelp::GetPropertyValue(holder, kFloraPropTraits, floats) &&
        floats.size() >= 14) {
        for (int i = 0; i < 14; ++i) data->traits[i] = floats[i];
    }
    if (holder->HasProperty(kFloraPropAquatic)) data->aquatic = true;
    cSCPropertyHelp::GetPropertyValue(holder, kFloraPropFamilyKey, data->familyKey);
    cSCPropertyHelp::GetPropertyValue(holder, kFloraPropValue48, data->value48);
    cSCPropertyHelp::GetPropertyValue(holder, kFloraPropValue4c, data->value4c);
    floats.clear();
    if (holder->HasProperty(kFloraPropSuitability) &&
        cSCPropertyHelp::GetPropertyValue(holder, kFloraPropSuitability, floats) && floats.size() >= 256) {
        for (int e = 0; e < 16; ++e)
            for (int c = 0; c < 16; ++c)
                data->suitability[e][c] = static_cast<std::uint8_t>(static_cast<std::int64_t>(floats[e * 16 + c] * 255.0));
    }
    cSCPropertyHelp::GetPropertyValue(holder, kFloraPropValue150, data->value150);
    data->flags = static_cast<std::uint8_t>((data->flags & 0xf8) | 6);
    bool flag = false;
    if (cSCPropertyHelp::GetPropertyValue(holder, kFloraPropFlagExcluded, flag))
        data->flags = static_cast<std::uint8_t>((data->flags & 0xfe) | (flag ? 1 : 0));
    if (cSCPropertyHelp::GetPropertyValue(holder, kFloraPropFlagBit, flag))
        data->flags &= flag ? 0xfb : 0xfd;
    cSCPropertyHelp::GetPropertyValue(holder, kFloraPropFamilies, data->families);
    data->cost = 0;
    cSCPropertyHelp::GetPropertyValue(holder, kFloraPropCost, data->cost);

    const bool added = AddStaticFloraData(data);
    data->Release();
    exemplar->Release();
    return added;
}

int cSC4FloraSimulator::GetElevationIndex(float x, float z, bool belowSeaLevel) const {
    const float sea = g_host ? g_host->SeaLevel() : 0.0f;
    const float height = g_host ? g_host->TerrainHeight(x, z) : 0.0f;
    const float scaled = (belowSeaLevel ? sea - height : height - sea) * elevationScale_;
    const long index = std::lrintf(scaled);                                 // round half to even
    if (index <= 0) return 0;
    return index < 16 ? static_cast<int>(index) : 15;
}

float cSC4FloraSimulator::GetPropagationScoreForFlora(std::uint32_t type, float x, float z) const {
    const cSC4StaticFloraData* data = GetStaticFloraData(type);
    if (!data || !g_host) return 0.0f;
    const int terrainClass = g_host->TerrainClass(x, z);
    const int elevation = GetElevationIndex(x, z, data->aquatic);
    return float(data->suitability[elevation][terrainClass & 15]) * 0.003921569f;
}

std::uint32_t cSC4FloraSimulator::GetBestFloraTypeForLocation(float x, float z, float& score) const {
    const int terrainClass = g_host ? g_host->TerrainClass(x, z) : 0;
    const int aboveIndex = GetElevationIndex(x, z, false);
    const int belowIndex = GetElevationIndex(x, z, true);
    std::uint32_t best = 0;
    float bestScore = 0.0f;
    for (auto& entry : g_staticFloraDataMap) {
        const cSC4StaticFloraData* data = entry.second;
        if (data->flags & 1) continue;
        const int index = data->aquatic ? belowIndex : aboveIndex;
        const float value = float(data->suitability[index][terrainClass & 15]) * 0.003921569f;
        if (value > bestScore) {
            bestScore = value;
            best = entry.first;
        }
    }
    score = bestScore;
    return best;
}

cSC4StaticFloraData* cSC4FloraSimulator::GetStaticFloraData(std::uint32_t type) const {
    auto it = g_staticFloraDataMap.find(type);
    return it == g_staticFloraDataMap.end() ? nullptr : it->second;
}

std::uint32_t cSC4FloraSimulator::GetAllFloraTypes(std::uint32_t* out, std::uint32_t& count) const {
    const std::uint32_t total = static_cast<std::uint32_t>(g_floraTypeArray.size());
    if (!out) {
        count = total;
        return total;
    }
    if (total < count) count = total;
    for (std::uint32_t i = 0; i < count; ++i) out[i] = g_floraTypeArray[i];
    return count;
}

void cSC4FloraSimulator::GetAllFloraTypes(std::vector<std::uint32_t>& out) const {
    out.assign(g_floraTypeArray.begin(), g_floraTypeArray.end());
}

void cSC4FloraSimulator::GetAllFloraFamilyTypes(std::vector<std::uint32_t>& out) const {
    out.clear();
    for (auto& entry : g_floraFamilyMap) out.push_back(entry.first);
}

bool cSC4FloraSimulator::IsFloraTypeValid(std::uint32_t type) const { return g_staticFloraDataMap.count(type) != 0; }

bool cSC4FloraSimulator::IsFloraFamilyTypeValid(std::uint32_t family) const {
    return g_floraFamilyMap.count(family) != 0 || g_staticFloraDataMap.count(family) != 0;
}

int cSC4FloraSimulator::GetFloraTypesByFilter(std::unordered_set<std::uint32_t>& out, cISC4FloraTypeFilter* filter) const {
    int accepted = 0;
    for (auto& entry : g_staticFloraDataMap) {
        if (!filter || filter->Accepts(entry.first)) {
            out.insert(entry.first);
            ++accepted;
        }
    }
    return accepted;
}

int cSC4FloraSimulator::GetFloraTypesByFamily(std::unordered_set<std::uint32_t>& out, std::uint32_t family) const {
    auto it = g_floraFamilyMap.find(family);
    if (it == g_floraFamilyMap.end()) return 0;
    for (std::uint32_t type : it->second) out.insert(type);
    return static_cast<int>(it->second.size());
}

}  // namespace sc4
