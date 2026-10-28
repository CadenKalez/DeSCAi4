// cSC4LotConfigurationManager — the city-wide registry of lot configurations (53 functions,
// 0x1006c30f6..0x1006c6a52). The configurations themselves live in process-wide statics shared by all
// manager instances (id -> configuration map and the set of every family id seen), filled once from the
// resource manager (exemplars in group 0xa8fbd372 enumerated by 0x6534284a). Per-instance state is the
// filter cache and the building / flora / prop simulators it validates object ids against.
// Layout: +0x08 serializable vptr, +0x0c refcount (group 1: +0x14, group 0: +0x1c), +0x10 traffic-source-like
// iid 0xe4fda3d4, +0x18 initialised, +0x20 building sim, +0x28 flora sim, +0x30 prop manager,
// +0x38 filter cache (map<filter*, vector<config*>*>).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZSerializable.h"
#include "sc4/LotConfiguration.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4LotConfigurationManager = 0xc8edbec0;
constexpr std::uint32_t kGZCLSID_cSC4LotConfigurationManager = 0x299062c0;
constexpr std::uint32_t kMessageLotConfigurationsInit = 0x26d31ec1;
constexpr std::uint32_t kLotConfigurationGroup = 0xa8fbd372;           // group of lot configuration exemplars
constexpr std::uint32_t kLotConfigurationEnumerator = 0x6534284a;
constexpr std::uint32_t kGZCLSID_cSC4BuildingDevelopmentSimulator = 0x07813eae;
constexpr std::uint32_t kGZCLSID_cSC4FloraSimulator = 0x06c66177;
constexpr std::uint32_t kGZCLSID_cSC4PropManager = 0x28326f3e;
constexpr std::uint32_t kPropertyBuildingNotRegular = 0x2781283f;

class cISC4LotConfigurationFilter {
public:
    virtual ~cISC4LotConfigurationFilter() = default;
    virtual bool Matches(cSC4LotConfiguration* configuration) = 0;     // vslot 0x28
};

struct SC4Footprint { float x = 0, z = 0; };

// The simulators the manager validates object ids with.
class ILotConfigurationManagerHost {
public:
    virtual ~ILotConfigurationManagerHost() = default;
    virtual std::uint8_t PreferencesDetail() = 0;                       // SL::spPreferences + 0xf16
    // Calls back for each exemplar key of the lot configuration type (resource manager enumeration).
    virtual void ForEachLotConfigurationKey(const std::function<void(const cGZPersistResourceKey&)>& fn) = 0;
    // building development simulator
    virtual bool IsBuildingType(std::uint32_t id) = 0;                  // vslot 0x70
    virtual bool BuildingTypeExists(std::uint32_t id) = 0;              // vslot 0x78
    virtual bool BuildingFootprint(std::uint32_t id, SC4Footprint& out) = 0;     // vslot 0x140 (+0x30, +0x38)
    virtual bool BuildingsInFamily(std::uint32_t family, std::vector<std::uint32_t>& out) = 0;   // vslot 0x90
    virtual bool BuildingMatchesFamily(std::uint32_t type, std::uint32_t family) = 0;           // vslot 0x98
    virtual bool RetainingWallExists(std::uint32_t id) = 0;             // vslot 0xe0
    virtual bool BuildingInformation(std::uint32_t type, std::vector<std::uint32_t>& groups, bool& notRegular,
                                     std::uint16_t& capacity) = 0;      // key (0x68) + groups + prop 0x2781283f + 0x138
    // prop manager
    virtual bool IsPropType(std::uint32_t id) = 0;                      // vslot 0x40
    virtual bool PropTypeExists(std::uint32_t id) = 0;                  // vslot 0x58
    virtual bool PropFootprint(std::uint32_t id, SC4Footprint& out) = 0;         // vslot 0x70 (+4, +0xc)
    virtual bool PropsInFamily(std::uint32_t family, std::vector<std::uint32_t>& out) = 0;       // vslot 0x48
    // flora simulator
    virtual bool IsFloraType(std::uint32_t id) = 0;                     // vslot 0x58
    virtual bool FloraTypeExists(std::uint32_t id) = 0;                 // vslot 0x60
    virtual bool FloraFootprint(std::uint32_t id, SC4Footprint& out) = 0;        // vslot 0x38 (+0x3c * 2)
    virtual bool FloraInFamily(std::uint32_t family, std::unordered_set<std::uint32_t>& out) = 0;   // vslot 0x70
    virtual std::uint32_t NewGuid() = 0;                                // GZCOM()->vslot 0x38
    virtual void WriteSerializable(cIGZOStream& stream, std::uint32_t simulator) = 0;
    virtual bool ReadSerializable(cIGZIStream& stream, std::uint32_t clsid) = 0;
};
ILotConfigurationManagerHost* LotConfigurationManagerHost();
void SetLotConfigurationManagerHost(ILotConfigurationManagerHost* host);

class cSC4LotConfigurationManager {
public:
    cSC4LotConfigurationManager() = default;
    ~cSC4LotConfigurationManager();

    std::uint32_t AddRef() { return ++refCount_; }
    std::uint32_t Release();
    bool QueryInterface(std::uint32_t iid, void** out);
    bool Init();
    bool Shutdown();
    bool DoMessage(std::uint32_t messageType);
    std::uint32_t GetGZCLSID() const { return kGZCLSID_cSC4LotConfigurationManager; }
    bool Write(cIGZOStream& stream) const;
    bool Read(cIGZIStream& stream);

    // registry
    bool AddLotConfiguration(cSC4LotConfiguration* configuration, bool add);
    bool AddLotConfiguration(const cGZPersistResourceKey& key);
    bool RemoveLotConfiguration(std::uint32_t id);
    cSC4LotConfiguration* GetLotConfiguration(std::uint32_t id) const;
    using ConfigurationSet = std::unordered_set<cSC4LotConfiguration*>;
    using IdSet = std::unordered_set<std::uint32_t>;
    int GetLotConfigurationsByFilter(ConfigurationSet& out, cISC4LotConfigurationFilter* filter);
    int GetLotConfigurationsByFamily(ConfigurationSet& out, std::uint32_t family);
    int GetLotConfigurationsBySize(ConfigurationSet& out, std::uint32_t x, std::uint32_t z);
    void ClearLotConfigurationSet(ConfigurationSet& set);
    int GetLotConfigurationIDsByFilter(IdSet& out, cISC4LotConfigurationFilter* filter);
    int GetLotConfigurationIDsByFamily(IdSet& out, std::uint32_t family);
    int GetLotConfigurationIDsBySize(IdSet& out, std::uint32_t x, std::uint32_t z);
    bool LotConfigurationHasBuildingFamily(cSC4LotConfiguration* configuration, std::uint32_t family,
                                           cSC4LotConfigurationObject** object);
    bool LotConfigurationHasBuildingType(cSC4LotConfiguration* configuration, std::uint32_t type,
                                         cSC4LotConfigurationObject** object);
    void RegisterLotConfigurationFilter(cISC4LotConfigurationFilter* filter);
    void UnregisterLotConfigurationFilter(cISC4LotConfigurationFilter* filter);
    static std::unordered_map<std::uint32_t, cSC4LotConfiguration*>& GetLotConfigurationMap();
    static std::unordered_set<std::uint32_t>& GetLotConfigurationFamilyIDSet();

    // object / footprint helpers
    bool CreateGenericLotConfiguration(cSC4LotConfiguration*& out, std::uint32_t buildingType) const;
    int FixLotConfiguration(cSC4LotConfiguration* configuration) const;
    void GetMaxFootprintForBuildingFamilies(const std::vector<std::uint32_t>& families, float out[4]) const;
    void GetMaxFootprintForFloraFamilies(const std::vector<std::uint32_t>& families, float out[4]) const;
    void GetMaxFootprintForPropFamilies(const std::vector<std::uint32_t>& families, float out[4]) const;
    void GetObjectIDArray(const cSC4LotConfigurationObject* object, std::vector<std::uint32_t>& out) const;
    void InitBuildingInformation(cSC4LotConfiguration* configuration) const;

    void LoadAllStandardLotConfigurations();                  // @ 0x1006c33b6
    static bool StaticInit(cSC4LotConfigurationManager* manager);
    static bool StaticShutdown();

private:
    bool ReadLotConfiguration(const cGZPersistResourceKey& key, cSC4LotConfiguration*& out) const;
    void FlushCachedLotConfigurationSearches();

    std::uint32_t refCount_ = 0;
    bool initialized_ = false;
    bool haveBuildingSim_ = false, haveFloraSim_ = false, havePropManager_ = false;   // +0x20, +0x28, +0x30
    std::map<cISC4LotConfigurationFilter*, std::vector<cSC4LotConfiguration*>*> filterCache_;
};

}  // namespace sc4
