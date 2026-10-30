#include "sc4/LotConfiguration.h"

#include <algorithm>
#include <cstring>

#include "rz/cRZString.h"
#include "sc4/SCPropertyHelp.h"

namespace sc4 {

namespace {
ILotConfigurationHost* g_host = nullptr;
constexpr float kFixed = 1.0f / 65536.0f;            // 1.5258789e-05
constexpr float kEdgeMargin = 0.040000003f;

void SetBits(std::vector<bool>& bits, const std::vector<std::uint8_t>& values) {
    for (std::uint8_t v : values)
        if (v < bits.size()) bits[v] = true;
}
}  // namespace

ILotConfigurationHost* LotConfigurationHost() { return g_host; }
void SetLotConfigurationHost(ILotConfigurationHost* host) { g_host = host; }

cSC4LotConfiguration::cSC4LotConfiguration() = default;   // 0x100402820: one 8-byte store makes the slopes {90.0f, 0.0f}

std::uint32_t cSC4LotConfiguration::Release() {
    if (refCount_ < 2) {
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cSC4LotConfiguration::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cISC4LotConfiguration) return false;
    *out = this;
    AddRef();
    return true;
}

bool cSC4LotConfiguration::Shutdown() {
    id_ = 0;
    name_.clear();
    sizeX_ = sizeZ_ = 0;
    families_.clear();
    maxSlope_ = 90.0f;                   // one 8-byte store: the minimum slope (+0x5c) is 0
    minSlope_ = 0.0f;
    maxSlopeBeforeFoundation_ = 90.0f;
    growthStage_ = 1;
    minCapacity_ = 0xffff;
    maxCapacity_ = 0;
    water_ = false;
    zoneBits_.assign(16, false);
    wealthBits_.assign(4, false);
    purposeBits_.assign(9, false);
    buildingFlag_ = false;
    doConstruction_ = false;
    requiredRoads_ = 8;
    objects_.clear();
    retainingWalls_.clear();
    fenceIds_.clear();
    fenceSegments_.clear();
    return true;
}

void cSC4LotConfiguration::MakeGeneric(std::uint32_t id, std::uint8_t sizeX, std::uint8_t sizeZ,
                                       const cSC4LotConfigurationObject& object) {
    id_ = id;
    sizeX_ = sizeX;
    sizeZ_ = sizeZ;
    families_.clear();
    maxSlopeBeforeFoundation_ = 5.0f;
    maxSlope_ = 90.0f;
    growthStage_ = 1;
    water_ = false;
    zoneBits_.clear();
    wealthBits_.clear();
    purposeBits_.clear();
    objects_.clear();
    retainingWalls_.clear();
    fenceIds_.clear();
    fenceSegments_.clear();
    objects_.push_back(object);
}

bool cSC4LotConfiguration::BelongsToFamily(std::uint32_t family) const {
    return std::find(families_.begin(), families_.end(), family) != families_.end();
}

bool cSC4LotConfiguration::IsCompatibleWithStyle(std::uint32_t style) const {
    return std::find(styles_.begin(), styles_.end(), style) != styles_.end();
}

void cSC4LotConfiguration::GetRequiredRoads(std::vector<bool>& out) const {
    if (out.size() != 4) out.resize(4, false);
    for (int i = 0; i < 4; ++i) out[i] = ((requiredRoads_ >> i) & 1) != 0;
}

// ------------------------------------------------------------------------------------- object table

void cSC4LotConfiguration::ConvertPropertiesToObjectArray(const cISCPropertyHolder& holder,
                                                          std::vector<cSC4LotConfigurationObject>& objects,
                                                          std::uint8_t version, std::uint8_t growthMode) {
    static std::uint32_t previousGuid = 0;
    objects.clear();
    const std::uint32_t threshold = static_cast<std::uint32_t>(growthMode) << 4;
    std::vector<std::uint32_t> data;
    for (std::uint32_t id = kLotConfigObjectFirst; id < kLotConfigObjectLimit; ++id) {
        if (!cSCPropertyHelp::GetPropertyValue(&holder, id, data)) break;
        if (data.size() <= 9) continue;
        cSC4LotConfigurationObject o;
        o.type = static_cast<std::uint8_t>(data[0]);
        o.flags = static_cast<std::uint8_t>(data[1]);
        o.orientation = static_cast<std::uint8_t>(data[2]);
        o.mirrored = static_cast<std::uint8_t>(data[2] >> 31);
        o.x = static_cast<float>(static_cast<std::int32_t>(data[3])) * kFixed;
        o.y = static_cast<float>(static_cast<std::int32_t>(data[4])) * kFixed;
        o.z = static_cast<float>(static_cast<std::int32_t>(data[5])) * kFixed;
        o.minX = static_cast<float>(static_cast<std::int32_t>(data[6])) * kFixed;
        o.minZ = static_cast<float>(static_cast<std::int32_t>(data[7])) * kFixed;
        o.maxX = static_cast<float>(static_cast<std::int32_t>(data[8])) * kFixed;
        o.maxZ = static_cast<float>(static_cast<std::int32_t>(data[9])) * kFixed;
        if (data.size() > 10) o.value = data[10];
        std::size_t first;
        if (version < 2) {
            if (previousGuid == 0 && g_host) previousGuid = g_host->NextObjectId();
            o.id = ++previousGuid;
            first = 11;
        } else {
            o.id = data.size() > 11 ? data[11] : 0;
            first = 12;
        }
        if (data.size() > first) o.extra.assign(data.begin() + first, data.end());
        if (o.type == 0 || (static_cast<std::uint32_t>(o.flags) & 0x30) < threshold)
            objects.push_back(std::move(o));
    }
}

void cSC4LotConfiguration::ConvertObjectArrayToProperties(cISCPropertyHolder& holder,
                                                          std::vector<cSC4LotConfigurationObject>& objects) {
    std::uint32_t index = 0;
    for (auto& o : objects) {
        if (o.type == 3) continue;
        std::vector<std::uint32_t> data;
        data.push_back(o.type);
        data.push_back(o.flags);
        data.push_back(o.mirrored ? (static_cast<std::uint32_t>(o.orientation) | 0x80000000u) : o.orientation);
        if (o.type != 1 || (o.y <= 0.0f && o.y != 0.0f)) o.y = 0.0f;
        auto fixed = [](float f) { return static_cast<std::uint32_t>(static_cast<std::int64_t>(f * 65536.0f)); };
        data.push_back(fixed(o.x));
        data.push_back(fixed(o.y));
        data.push_back(fixed(o.z));
        data.push_back(fixed(o.minX));
        data.push_back(fixed(o.minZ));
        data.push_back(fixed(o.maxX));
        data.push_back(fixed(o.maxZ));
        data.push_back(o.value);
        data.push_back(o.id);
        data.insert(data.end(), o.extra.begin(), o.extra.end());
        cSCPropertyHelp::SetPropertyValue(&holder, kLotConfigObjectFirst + index, data);
        ++index;
    }
}

bool cSC4LotConfiguration::CorrectObjectPositions(std::vector<cSC4LotConfigurationObject>& objects, float maxX,
                                                  float maxZ, float minX, float minZ) {
    bool changed = false;
    for (auto& o : objects) {
        float target = minX + kEdgeMargin;
        if (o.x < target || (target = maxX - kEdgeMargin, target < o.x)) {
            const float delta = target - o.x;
            o.x += delta;
            o.maxX += delta;
            o.minX += delta;
            changed = true;
        }
        target = minZ + kEdgeMargin;
        if (o.z < target || (target = maxZ - kEdgeMargin, target < o.z)) {
            const float delta = target - o.z;
            o.z += delta;
            o.maxZ += delta;
            o.minZ += delta;
            changed = true;
        }
    }
    return changed;
}

// -------------------------------------------------------------------------------------------- load

bool cSC4LotConfiguration::LoadFromProperties(const cISCPropertyHolder& holder, std::uint32_t id,
                                              std::uint8_t growthMode, std::uint8_t* versionOut) {
    Shutdown();
    {
        cRZString name;
        if (cSCPropertyHelp::GetPropertyValue(&holder, 0x20, name)) name_ = name.ToChar();
    }
    id_ = id;
    std::uint8_t version = 0;
    cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigVersion, version);
    if (versionOut) *versionOut = version;
    if (version == 0) return false;

    std::vector<std::uint8_t> bytes;
    if (cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigSize, bytes) && bytes.size() > 1) {
        sizeX_ = bytes[0];
        sizeZ_ = bytes[1];
    }
    cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigFamilies, families_);
    cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigMaxSlopeBeforeFoundation, maxSlopeBeforeFoundation_);
    cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigMaxSlope, maxSlope_);
    cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigMinSlope, minSlope_);
    cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigGrowthStage, growthStage_);
    std::uint8_t flag = 0;
    water_ = cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigWater, flag) && flag != 0;
    if (cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigZoneTypes, bytes)) SetBits(zoneBits_, bytes);
    if (cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigWealthTypes, bytes)) SetBits(wealthBits_, bytes);
    if (cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigPurposeTypes, bytes)) SetBits(purposeBits_, bytes);
    flag = 0;
    doConstruction_ = cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigDoConstruction, flag) && flag != 0;
    cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigRetainingWalls, retainingWalls_);

    std::vector<std::uint32_t> fences;
    if (cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigFences, fences) && fences.size() > 1) {
        const std::uint32_t count = fences[0];
        std::size_t index = 2;
        if (count != 0) {
            for (std::uint32_t i = 0; i < count && index < fences.size(); ++i) fenceIds_.push_back(fences[index++]);
            index = 2 + count;
        }
        if (index < fences.size() && fences[index] != 0) {
            const std::uint32_t segments = fences[index];
            std::size_t at = index;
            for (std::uint32_t i = 0; i < segments && at + 4 < fences.size(); ++i) {
                for (int b = 1; b <= 4; ++b) fenceSegments_.push_back(static_cast<std::int8_t>(fences[at + b]));
                at += 4;
            }
        }
    }

    cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigRequiredRoads, requiredRoads_);
    if ((requiredRoads_ & 8) == 0) requiredRoads_ |= 8;       // a road is always required on the south edge

    ConvertPropertiesToObjectArray(holder, objects_, version, growthMode);
    CorrectObjectPositions(objects_, static_cast<float>(sizeX_ << 4), static_cast<float>(sizeZ_ << 4), 0.0f, 0.0f);

    std::uint32_t foundation = 0;
    if (cSCPropertyHelp::GetPropertyValue(&holder, kLotConfigFoundation, foundation)) {
        cSC4LotConfigurationObject o;
        o.type = 3;
        o.x = 0.5f; o.y = 0.0f; o.z = 0.5f;
        o.maxX = 0.6f; o.maxZ = 0.6f;
        o.minX = 0.4f; o.minZ = 0.4f;
        o.value = foundation;
        objects_.push_back(std::move(o));
    }
    return true;
}

bool cSC4LotConfiguration::Load(const cGZPersistResourceKey& key, std::uint8_t growthMode) {
    if (!g_host) return false;
    cISCPropertyHolder* holder = g_host->AcquireExemplar(key);
    if (!holder) return false;
    std::uint8_t version = 0;
    const bool ok = LoadFromProperties(*holder, key.instance, growthMode, &version);
    if (ok && version != 2) Save(key, g_host->SegmentFor(key));       // upgrade version-1 exemplars in place
    g_host->ReleaseExemplar(holder);
    return ok;
}

// -------------------------------------------------------------------------------------------- save

bool cSC4LotConfiguration::SaveToPropertyHolder(cISCPropertyHolder* holder) const {
    if (!holder) return false;
    holder->RemoveAllProperties(false);
    cSCPropertyHelp::SetPropertyValue(holder, 0x10, static_cast<std::uint32_t>(0x10));
    cRZString name;
    name.AssignFromCString(name_.c_str());
    cSCPropertyHelp::SetPropertyValue(holder, 0x20, name);
    cSCPropertyHelp::SetPropertyValue(holder, kLotConfigVersion, static_cast<std::uint8_t>(2));
    cSCPropertyHelp::SetPropertyValue(holder, kLotConfigSize, std::vector<std::uint8_t>{sizeX_, sizeZ_});
    if (!families_.empty()) cSCPropertyHelp::SetPropertyValue(holder, kLotConfigFamilies, families_);
    cSCPropertyHelp::SetPropertyValue(holder, kLotConfigMaxSlopeBeforeFoundation, maxSlopeBeforeFoundation_);
    cSCPropertyHelp::SetPropertyValue(holder, kLotConfigMaxSlope, maxSlope_);
    cSCPropertyHelp::SetPropertyValue(holder, kLotConfigMinSlope, minSlope_);
    cSCPropertyHelp::SetPropertyValue(holder, kLotConfigGrowthStage, growthStage_);
    if (water_) cSCPropertyHelp::SetPropertyValue(holder, kLotConfigWater, static_cast<std::uint8_t>(1));
    auto setBits = [&](std::uint32_t id, const std::vector<bool>& bits) {
        if (bits.empty()) return;
        std::vector<std::uint8_t> values;
        for (std::size_t i = 0; i < bits.size(); ++i)
            if (bits[i]) values.push_back(static_cast<std::uint8_t>(i));
        cSCPropertyHelp::SetPropertyValue(holder, id, values);
    };
    setBits(kLotConfigZoneTypes, zoneBits_);
    setBits(kLotConfigWealthTypes, wealthBits_);
    setBits(kLotConfigPurposeTypes, purposeBits_);
    if (doConstruction_) cSCPropertyHelp::SetPropertyValue(holder, kLotConfigDoConstruction, static_cast<std::uint8_t>(1));
    for (const auto& o : objects_) {
        if (o.type != 3) continue;
        if (o.value != 0) {
            cSCPropertyHelp::SetPropertyValue(holder, kLotConfigFoundation, o.value);
            break;
        }
        if (!o.extra.empty()) {
            cSCPropertyHelp::SetPropertyValue(holder, kLotConfigFoundation, o.extra.front());
            break;
        }
    }
    if (!retainingWalls_.empty()) cSCPropertyHelp::SetPropertyValue(holder, kLotConfigRetainingWalls, retainingWalls_);
    if (!fenceIds_.empty()) {
        std::vector<std::uint32_t> data;
        data.push_back(static_cast<std::uint32_t>(fenceIds_.size()));
        data.insert(data.end(), fenceIds_.begin(), fenceIds_.end());
        data.push_back(static_cast<std::uint32_t>(fenceSegments_.size() / 4));
        for (std::int8_t b : fenceSegments_) data.push_back(static_cast<std::uint32_t>(static_cast<std::int32_t>(b)));
        cSCPropertyHelp::SetPropertyValue(holder, kLotConfigFences, data);
    }
    cSCPropertyHelp::SetPropertyValue(holder, kLotConfigRequiredRoads, requiredRoads_);
    ConvertObjectArrayToProperties(*holder, const_cast<std::vector<cSC4LotConfigurationObject>&>(objects_));
    return true;
}

bool cSC4LotConfiguration::Save(const cGZPersistResourceKey& key, void* segment) {
    if (!g_host) return false;
    cISCPropertyHolder* holder = g_host->AcquireExemplar(key);
    const bool loaded = holder != nullptr;
    if (!loaded) holder = g_host->CreateExemplar(key);
    bool ok = holder && SaveToPropertyHolder(holder) && g_host->StoreExemplar(holder, key, segment);
    if (holder) {
        if (loaded) g_host->ReleaseExemplar(holder);
        else holder->Release();
    }
    return ok;
}

}  // namespace sc4
