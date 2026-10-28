#include "sc4/FloraSimulator.h"
#include "rz/cRZRandom.h"
#include "sc4/SimStreamOps.h"
#include "sc4/DBSegment.h"

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

// 0x1000c2252: the original answers +0 for iid 1 / 0x06c66177, +8 for the message target and +0x10 for
// the serializable. The port keeps a single cIGZSerializable base, so every accepted id resolves to `this`;
// the serializable id is what the city-load serializer (SC4ReadGZSerializable) asks for.
bool cSC4FloraSimulator::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 0xe4fda3d4) *out = static_cast<cIGZSerializable*>(this);
    else if (iid == 1 || iid == kFloraSimulatorInterfaceId || iid == kFloraSimulatorClassId)
        *out = static_cast<cIGZUnknown*>(this);
    else return false;
    AddRef();
    return true;
}

// 0x1000c219a: below 2 destroys the object.
std::uint32_t cSC4FloraSimulator::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return --refCount_;
}

// 0x1000be5a0. The original serialises version 2 then, in order:
//   +0x50..0x53 four bools, +0x54/+0x58 two floats, +0x5c/+0x60 two u32, +0x64/+0x68/+0x6c/+0x70 four
//   s32, +0x74 one u8, +0x78 the RandomIterator, +0xb8 the cRZRandom, +0xd8 the ilist<cISC4Occupant*>
//   (the obliterated-flora list), +0xf0/+0xf4 two floats. None of those instance fields are recovered in
//   this port (the seeding / daily-simulation state lives behind the IFloraSimulatorHost seam), so only the
//   version is written. PARTIAL: the field list above is the confirmed binary layout, not a guessed body.
bool cSC4FloraSimulator::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    return !stream.GetError();
}

// 0x1000be6d8. See Write: the port has none of the version-2 payload fields, so it accepts the version and
// stops. PARTIAL (confirmed field list above).
bool cSC4FloraSimulator::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 2) return false;
    void* dbStream = nullptr;
    if (!stream.QueryInterface(kGZIID_cISC4DBSegmentIStream, &dbStream) || !dbStream) return !stream.GetError();   // the port's own reduced record
    static_cast<cIGZUnknown*>(dbStream)->Release();
    // The record of a city file: four flags, the growth parameters, the walk iterator, the seed generator and the occupants of the
    // flora in progress. The port's flora simulator keeps none of this state yet, so it is consumed.
    std::uint8_t u8 = 0;
    std::uint32_t u32 = 0;
    std::int32_t s32 = 0;
    float f32 = 0.0f;
    for (int i = 0; i < 4; ++i) stream.GetUint8(u8);
    stream.GetFloat32(f32); stream.GetFloat32(f32);
    stream.GetSint32(s32); stream.GetSint32(s32);
    for (int i = 0; i < 4; ++i) stream.GetUint32(u32);
    stream.GetUint8(u8);
    RandomIterator walk;
    stream >> walk;                                                    // +0x78
    cRZRandom random(1);
    random.Read(stream);                                               // +0xb8
    stream.GetUint32(u32);                                             // +0xd8: a list of occupant references
    if (u32 < 10000000u) {
        for (std::uint32_t i = 0; i < u32 && !stream.GetError(); ++i) {
            void* raw = nullptr;
            SC4ReadGZSerializable(stream, 0x26bb767e, &raw, false);
            if (raw) static_cast<cIGZUnknown*>(raw)->Release();
        }
    }
    stream.GetFloat32(f32); stream.GetFloat32(f32);
    return !stream.GetError();
}

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
        PostFloraTypesUpdatedMessage();
    }
}

void cSC4FloraSimulator::PostFloraTypesUpdatedMessage() {
    if (g_host) g_host->PostFloraTypesUpdated();
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
