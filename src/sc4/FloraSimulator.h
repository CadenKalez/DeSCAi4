// cSC4FloraSimulator — catalogue of flora types (trees, bushes, rocks) and their placement rules
// (64 functions, 0x1000bd5e6..0x1000c2454). This port covers the type tables loaded from flora exemplars
// (cSC4StaticFloraData, see FloraOccupant.h), the elevation / terrain suitability lookup, the best-type search
// and the family / filter queries. Seeding, daily / monthly simulation and the persisted obliterated-flora list
// stay unrecovered (they need the occupant manager and the terrain model).
#pragma once
#include <cstdint>
#include <map>
#include <unordered_set>
#include <vector>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "sc4/Exemplar.h"
#include "sc4/FloraOccupant.h"
#include "sc4/SCProperty.h"

namespace sc4 {

constexpr std::uint32_t kFloraSimulatorClassId = 0xc990c034;           // GZCOM_cSC4FloraSimulatorCLSID
constexpr std::uint32_t kFloraSimulatorInterfaceId = 0x06c66177;       // cISC4FloraSimulator
constexpr std::uint32_t kFloraDirectoryTypeId = 0x6534284a;            // resource-directory id scanned by StaticInit
constexpr std::uint32_t kFloraExemplarTypeValue = 0xf;                 // exemplar type (property 0x10) of a flora type

// property ids of a flora exemplar
constexpr std::uint32_t kFloraPropTypeId = 0x21;
constexpr std::uint32_t kFloraPropAquatic = 0xea346888;                // presence: elevation measured below the sea level
constexpr std::uint32_t kFloraPropTraits = 0x83dc35b;                  // 14 floats -> +0x10..+0x44
constexpr std::uint32_t kFloraPropFamilyKey = 0x2a0348ba;              // -> +4
constexpr std::uint32_t kFloraPropValue48 = 0x2a0348bb;
constexpr std::uint32_t kFloraPropValue4c = 0x2a0348bc;
constexpr std::uint32_t kFloraPropSuitability = 0x83dc487;             // 16x16 floats (0..1) -> bytes at +0x50
constexpr std::uint32_t kFloraPropValue150 = 0x83de16f;
constexpr std::uint32_t kFloraPropFlagExcluded = 0xe9d117c3;           // bit0 of +0x180
constexpr std::uint32_t kFloraPropFlagBit = 0x6a37ebb6;                // clears bit1 (false) or bit2 (true)
constexpr std::uint32_t kFloraPropFamilies = 0xa8f149c5;
constexpr std::uint32_t kFloraPropCost = 0x49cac341;

class cISC4FloraTypeFilter {
public:
    virtual ~cISC4FloraTypeFilter() = default;
    virtual bool Accepts(std::uint32_t floraType) = 0;                 // vslot 0x18
};

class IFloraSimulatorHost {
public:
    virtual ~IFloraSimulatorHost() = default;
    // Loads an exemplar by key (returned with a reference), or nullptr.
    virtual cISCResExemplar* LoadExemplar(const cGZPersistResourceKey& key) = 0;
    // Enumerates the resources of the flora resource directory (kFloraDirectoryTypeId).
    virtual void EnumerateFloraResources(void (*callback)(const cGZPersistResourceKey&, void*), void* context) = 0;
    virtual void PostFloraTypesUpdated() = 0;
    virtual float SeaLevel() = 0;                                      // terrain vslot 0xe8
    virtual float TerrainHeight(float x, float z) = 0;                 // terrain vslot 0x60
    virtual int TerrainClass(float x, float z) = 0;                    // sim-grid vslot 0x98
};
IFloraSimulatorHost* FloraSimulatorHost();
void SetFloraSimulatorHost(IFloraSimulatorHost* host);

class cSC4FloraSimulator : public cIGZSerializable {
public:
    cSC4FloraSimulator() = default;
    ~cSC4FloraSimulator() = default;

    // cIGZUnknown / cIGZSerializable identity (one shared count at +0x1c; the original answers +0 for
    // iid 1 / 0x06c66177, +8 for the message target and +0x10 for the serializable).
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kFloraSimulatorClassId; }

    static void MakeDefaultStaticFloraData(cSC4StaticFloraData& data);
    static bool StaticInit();
    static bool StaticShutdown();
    static void LoadAllStandardFloraTypes();
    static void PostFloraTypesUpdatedMessage();          // @ 0x1000c05f8
    static void AddStaticFloraDataFromResource(const cGZPersistResourceKey& key);   // AddStaticFloraDataStatic
    static bool AddStaticFloraData(cSC4StaticFloraData* data);      // takes a reference; false when the type exists
    static bool AddStaticFloraData(const cGZPersistResourceKey& key, cISCResExemplar* exemplar);

    int GetElevationIndex(float x, float z, bool belowSeaLevel) const;
    float GetPropagationScoreForFlora(std::uint32_t type, float x, float z) const;
    std::uint32_t GetBestFloraTypeForLocation(float x, float z, float& score) const;
    cSC4StaticFloraData* GetStaticFloraData(std::uint32_t type) const;
    std::uint32_t GetAllFloraTypes(std::uint32_t* out, std::uint32_t& count) const;
    void GetAllFloraTypes(std::vector<std::uint32_t>& out) const;
    void GetAllFloraFamilyTypes(std::vector<std::uint32_t>& out) const;
    bool IsFloraTypeValid(std::uint32_t type) const;
    bool IsFloraFamilyTypeValid(std::uint32_t family) const;       // a family key, or (quirk) a flora type
    int GetFloraTypesByFilter(std::unordered_set<std::uint32_t>& out, cISC4FloraTypeFilter* filter) const;
    int GetFloraTypesByFamily(std::unordered_set<std::uint32_t>& out, std::uint32_t family) const;
    void SetElevationScale(float scale) { elevationScale_ = scale; }

private:
    std::uint32_t refCount_ = 0;                                    // +0x1c
    float elevationScale_ = 1.0f;                                   // +0xd0 (tunable)
};

}  // namespace sc4
