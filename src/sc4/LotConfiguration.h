// cSC4LotConfiguration — a lot template (44 functions, 0x100402820..0x1004049f0): footprint size, family
// ids, slope limits, compatible zone/wealth/purpose bit sets, required roads, and the list of objects
// (building / prop / base / foundation) placed on it. Loaded from an exemplar (class 0xa52160f5) of
// version 1 or 2; version 1 exemplars are upgraded in place on load.
// Layout: +0x08 refcount, +0x0c id, +0x10 name, +0x38/+0x39 size, +0x40 families, +0x58 max slope,
// +0x5c min slope, +0x60 max slope before foundation, +0x64 growth stage, +0x66/+0x68 building capacity,
// +0x6a water, +0x70 zone bits(16), +0x88 wealth bits(4), +0xa0 purpose bits(9), +0xb8 styles,
// +0xd1 do-construction, +0xd2 required roads, +0xd8 objects, +0xf0 retaining walls, +0x108 fences,
// +0x120 fence segments.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZString.h"
#include "sc4/SCProperty.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4LotConfiguration = 0x68edbebc;
constexpr std::uint32_t kGZCLSID_cSCResExemplar = 0xa52160f5;

// Property ids of a lot configuration exemplar.
constexpr std::uint32_t kLotConfigVersion = 0x88edc789;
constexpr std::uint32_t kLotConfigSize = 0x88edc790;
constexpr std::uint32_t kLotConfigFamilies = 0x88edc791;
constexpr std::uint32_t kLotConfigMaxSlopeBeforeFoundation = 0x88edc792;
constexpr std::uint32_t kLotConfigZoneTypes = 0x88edc793;
constexpr std::uint32_t kLotConfigWealthTypes = 0x88edc795;
constexpr std::uint32_t kLotConfigPurposeTypes = 0x88edc796;
constexpr std::uint32_t kLotConfigFences = 0x88edc797;
constexpr std::uint32_t kLotConfigRetainingWalls = 0x88edc798;
constexpr std::uint32_t kLotConfigObjectFirst = 0x88edc900;     // one property per object, up to 0x88edcdff
constexpr std::uint32_t kLotConfigObjectLimit = 0x88edce00;
constexpr std::uint32_t kLotConfigMaxSlope = 0xe99b068c;
constexpr std::uint32_t kLotConfigMinSlope = 0x699b08a4;
constexpr std::uint32_t kLotConfigGrowthStage = 0x27812837;
constexpr std::uint32_t kLotConfigWater = 0x89136440;
constexpr std::uint32_t kLotConfigDoConstruction = 0xe99b068d;
constexpr std::uint32_t kLotConfigFoundation = 0x88fcd877;
constexpr std::uint32_t kLotConfigRequiredRoads = 0x4a4a88f0;

// cSC4LotConfigurationObject (0x40 bytes).
struct cSC4LotConfigurationObject {
    std::uint8_t type = 0;                 // +0 0 building, 1 prop, 2 base, 3 foundation
    std::uint8_t flags = 0;                // +1 bits 4..5 = growth stage the object appears at
    std::uint8_t orientation = 0;          // +2
    std::uint8_t mirrored = 0;             // +3 (sign bit of the stored orientation word)
    float x = 0, y = 0, z = 0;             // +4, +8, +0xc
    float maxX = 0, maxZ = 0;              // +0x10, +0x14
    float minX = 0, minZ = 0;              // +0x18, +0x1c
    std::uint32_t value = 0;               // +0x20
    std::uint32_t id = 0;                  // +0x24
    std::vector<std::uint32_t> extra;      // +0x28 type-specific ids
};

class ILotConfigurationHost {
public:
    virtual ~ILotConfigurationHost() = default;
    // Resource manager: the exemplar holder for a key (borrowed until Release), or null.
    virtual cISCPropertyHolder* AcquireExemplar(const cGZPersistResourceKey& key) = 0;
    virtual void ReleaseExemplar(cISCPropertyHolder* holder) = 0;
    // A fresh exemplar holder to save into when none is loaded for the key.
    virtual cISCPropertyHolder* CreateExemplar(const cGZPersistResourceKey& key) = 0;
    // The sequential id source used for objects of version-1 exemplars (starts after the COM GUID).
    virtual std::uint32_t NextObjectId() = 0;
    // Persists a holder under the key into the database segment that owns the key.
    virtual bool StoreExemplar(cISCPropertyHolder* holder, const cGZPersistResourceKey& key, void* segment) = 0;
    virtual void* SegmentFor(const cGZPersistResourceKey& key) = 0;
};
ILotConfigurationHost* LotConfigurationHost();
void SetLotConfigurationHost(ILotConfigurationHost* host);

class cSC4WinLotConfigurationEditor;

class cSC4LotConfiguration {
public:
    cSC4LotConfiguration();

    std::uint32_t AddRef() { return ++refCount_; }
    std::uint32_t Release();
    bool QueryInterface(std::uint32_t iid, void** out);
    bool Init() { return true; }
    bool Shutdown();

    std::uint32_t GetID() const { return id_; }
    void GetName(cIGZString& out) const { out.AssignFromCString(name_.c_str()); }
    bool BelongsToFamily(std::uint32_t family) const;
    std::vector<std::uint32_t>& GetFamilyIDArray() { return families_; }
    void GetSize(std::uint32_t& x, std::uint32_t& z) const { x = sizeX_; z = sizeZ_; }
    float GetMaximumSlope() const { return maxSlope_; }
    float GetMinimumSlope() const { return minSlope_; }
    float GetMaximumSlopeBeforeFoundation() const { return maxSlopeBeforeFoundation_; }
    std::uint8_t GetGrowthStage() const { return growthStage_; }
    std::uint16_t GetMinBuildingCapacity() const { return minCapacity_; }
    std::uint16_t GetMaxBuildingCapacity() const { return maxCapacity_; }
    bool IsCompatibleWithWater() const { return water_; }
    bool IsCompatibleWithZoneType(int zone) const { return Test(zoneBits_, zone); }
    bool IsCompatibleWithWealthType(int wealth) const { return Test(wealthBits_, wealth); }
    bool IsCompatibleWithPurposeType(int purpose) const { return Test(purposeBits_, purpose); }
    bool IsCompatibleWithStyle(std::uint32_t style) const;
    bool IsCompatibleWithMonopolyPieceUse(std::uint32_t) const { return true; }
    bool IsDoConstruction() const { return doConstruction_; }
    void GetRequiredRoads(std::vector<bool>& out) const;
    std::uint32_t GetLotConfigurationObjectCount() const { return static_cast<std::uint32_t>(objects_.size()); }
    std::vector<cSC4LotConfigurationObject>& GetLotConfigurationObjectArray() { return objects_; }
    // Used by the manager when it derives building information / makes generic configurations.
    void SetBuildingInformation(std::uint16_t minCapacity, std::uint16_t maxCapacity,
                                std::vector<std::uint32_t> styles, bool flag) {
        minCapacity_ = minCapacity; maxCapacity_ = maxCapacity; styles_ = std::move(styles); buildingFlag_ = flag;
    }
    bool GetBuildingFlag() const { return buildingFlag_; }                  // +0xd0
    void MakeGeneric(std::uint32_t id, std::uint8_t sizeX, std::uint8_t sizeZ, const cSC4LotConfigurationObject& object);
    cSC4LotConfigurationObject* GetPrimaryBuildingConfigurationObject() { return FindObject(0); }
    cSC4LotConfigurationObject* GetPrimaryBaseConfigurationObject() { return FindObject(2); }
    cSC4LotConfigurationObject* GetPrimaryFoundationConfigurationObject() { return FindObject(3); }
    bool HasObjectsOfType(int type) { return FindObject(type) != nullptr; }
    std::vector<std::uint32_t>& GetRetainingWallIDArray() { return retainingWalls_; }
    std::vector<std::uint32_t>& GetFenceIDArray() { return fenceIds_; }
    std::vector<std::int8_t>& GetFenceSegmentPositionArray() { return fenceSegments_; }   // 4 bytes each

    // Loads from the exemplar for `key`. `growthMode` filters objects by growth stage (0..3).
    bool Load(const cGZPersistResourceKey& key, std::uint8_t growthMode);
    bool LoadFromProperties(const cISCPropertyHolder& holder, std::uint32_t id, std::uint8_t growthMode,
                            std::uint8_t* versionOut = nullptr);
    bool Save(const cGZPersistResourceKey& key, void* segment);
    bool SaveToPropertyHolder(cISCPropertyHolder* holder) const;

    static void ConvertPropertiesToObjectArray(const cISCPropertyHolder& holder,
                                               std::vector<cSC4LotConfigurationObject>& objects,
                                               std::uint8_t version, std::uint8_t growthMode);
    static void ConvertObjectArrayToProperties(cISCPropertyHolder& holder,
                                               std::vector<cSC4LotConfigurationObject>& objects);
    static bool CorrectObjectPositions(std::vector<cSC4LotConfigurationObject>& objects, float maxX, float maxZ,
                                       float minX, float minZ);

private:
    // The lot editor (cSC4WinLotConfigurationEditor) edits the size, name and slopes in place, as the
    // original does through these same fields.
    friend class cSC4WinLotConfigurationEditor;

    static bool Test(const std::vector<bool>& bits, int index) {
        return index >= 0 && static_cast<std::size_t>(index) < bits.size() && bits[index];
    }
    cSC4LotConfigurationObject* FindObject(int type) {
        for (auto& o : objects_) if (o.type == type) return &o;
        return nullptr;
    }

    std::uint32_t refCount_ = 0;
    std::uint32_t id_ = 0;                          // +0x0c
    std::string name_;                              // +0x10
    std::uint8_t sizeX_ = 0, sizeZ_ = 0;            // +0x38, +0x39
    std::vector<std::uint32_t> families_;           // +0x40
    float maxSlope_ = 90.0f;                        // +0x58
    float minSlope_ = 0.0f;                         // +0x5c
    float maxSlopeBeforeFoundation_ = 90.0f;        // +0x60
    std::uint8_t growthStage_ = 1;                  // +0x64
    std::uint16_t minCapacity_ = 0xffff;            // +0x66
    std::uint16_t maxCapacity_ = 0;                 // +0x68
    bool water_ = false;                            // +0x6a
    std::vector<bool> zoneBits_ = std::vector<bool>(16);      // +0x70
    std::vector<bool> wealthBits_ = std::vector<bool>(4);     // +0x88
    std::vector<bool> purposeBits_ = std::vector<bool>(9);    // +0xa0
    std::vector<std::uint32_t> styles_;             // +0xb8
    bool buildingFlag_ = false;                     // +0xd0 set by InitBuildingInformation
    bool doConstruction_ = false;                   // +0xd1
    std::uint8_t requiredRoads_ = 8;                // +0xd2 (bit per edge)
    std::vector<cSC4LotConfigurationObject> objects_;   // +0xd8
    std::vector<std::uint32_t> retainingWalls_;     // +0xf0
    std::vector<std::uint32_t> fenceIds_;           // +0x108
    std::vector<std::int8_t> fenceSegments_;        // +0x120
};

}  // namespace sc4
