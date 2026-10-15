// cSC4FloraSimulator — catalogue of flora types (trees, bushes, rocks) and their placement rules
// (64 functions, 0x1000bd5e6..0x1000c2454). This port covers the type tables loaded from flora exemplars
// (class cSC4StaticFloraData, 0x188 bytes), the elevation / terrain suitability lookup, the best-type search
// and the family queries. Seeding, daily / monthly simulation and the persisted obliterated-flora list stay
// unrecovered (they need the occupant manager and the terrain model).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_set>
#include <vector>

#include "gz/cGZPersistResourceKey.h"
#include "sc4/SCProperty.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4FloraSimulator = 0x06c66177;
constexpr std::uint32_t kExemplarTypeFlora = 0x0;                      // (selected by group in the original)
constexpr std::uint32_t kMessageFloraTypesUpdated = 0;                 // posted through the host

// property ids of a flora exemplar
constexpr std::uint32_t kFloraPropAquatic = 0xea346888;                // presence: grows below the sea level
constexpr std::uint32_t kFloraPropTraits = 0x83dc35b;                  // 14 floats -> +0x10..+0x44
constexpr std::uint32_t kFloraPropU32Plus4 = 0x2a0348ba;
constexpr std::uint32_t kFloraPropFloat48 = 0x2a0348bb;
constexpr std::uint32_t kFloraPropFloat4c = 0x2a0348bc;
constexpr std::uint32_t kFloraPropSuitability = 0x83dc487;             // 16x16 floats (0..1) -> bytes at +0x50
constexpr std::uint32_t kFloraPropU32Plus150 = 0x83de16f;
constexpr std::uint32_t kFloraPropFlagLow = 0xe9d117c3;
constexpr std::uint32_t kFloraPropFlagBit = 0x6a37ebb6;
constexpr std::uint32_t kFloraPropFamilies = 0xa8f149c5;
constexpr std::uint32_t kFloraPropCost = 0x49cac341;

class cSC4StaticFloraData {
public:
    cSC4StaticFloraData();
    void AddRef() { ++refs_; }
    void Release() { if (--refs_ <= 0) delete this; }

    std::uint32_t value4 = 0;                      // +4  (property 0x2a0348ba)
    bool aquatic = false;                          // +0xc
    float traits[14] = {};                         // +0x10..+0x44 (defaults from MakeDefaultStaticFloraData)
    float halfExtent() const { return traits[7]; } // +0x38 bounding-box half extent
    float footprintRadius() const { return traits[8]; }   // +0x3c
    float spacing() const { return traits[9]; }    // +0x40
    float height() const { return traits[10]; }    // +0x44
    float value48 = 0, value4c = 0;                // +0x48, +0x4c
    std::uint8_t suitability[16][16] = {};         // +0x50: [elevation index][terrain class]
    std::uint32_t value150 = 0;                    // +0x150
    std::vector<std::uint32_t> families;           // +0x158
    std::int64_t cost = 0;                         // +0x170
    cISCPropertyHolder* exemplar = nullptr;        // +0x178 (borrowed in this port)
    std::uint8_t flags = 6;                        // +0x180: bit0 excluded from the best-type search
    int refs_ = 0;                                 // +0x184 (the original starts at 1)
};

class IFloraSimulatorHost {
public:
    virtual ~IFloraSimulatorHost() = default;
    virtual void ForEachExemplarKey(const std::function<void(const cGZPersistResourceKey&)>& fn) = 0;
    virtual cISCPropertyHolder* AcquireExemplar(const cGZPersistResourceKey& key) = 0;
    virtual void ReleaseExemplar(cISCPropertyHolder* holder) = 0;
    virtual void PostFloraTypesUpdated() = 0;
    virtual float SeaLevel() = 0;                                   // terrain vslot 0xe8
    virtual float TerrainHeight(float x, float z) = 0;              // terrain vslot 0x60
    virtual int TerrainClass(float x, float z) = 0;                 // sim-grid vslot 0x98
};
IFloraSimulatorHost* FloraSimulatorHost();
void SetFloraSimulatorHost(IFloraSimulatorHost* host);

class cSC4FloraSimulator {
public:
    static void MakeDefaultStaticFloraData(cSC4StaticFloraData& data);
    static bool StaticInit();
    static bool StaticShutdown();
    static bool AddStaticFloraData(cSC4StaticFloraData* data);      // takes a reference; false when the type exists
    static bool AddStaticFloraData(const cGZPersistResourceKey& key, cISCPropertyHolder* exemplar);

    int GetElevationIndex(float x, float z, bool belowSeaLevel) const;
    float GetPropagationScoreForFlora(std::uint32_t type, float x, float z) const;
    std::uint32_t GetBestFloraTypeForLocation(float x, float z, float& score) const;
    cSC4StaticFloraData* GetStaticFloraData(std::uint32_t type) const;
    void GetAllFloraTypes(std::vector<std::uint32_t>& out) const;
    void GetAllFloraFamilyTypes(std::vector<std::uint32_t>& out) const;
    bool IsFloraTypeValid(std::uint32_t type) const;
    bool IsFloraFamilyTypeValid(std::uint32_t family) const;
    int GetFloraTypesByFamily(std::unordered_set<std::uint32_t>& out, std::uint32_t family) const;
    void SetElevationScale(float scale) { elevationScale_ = scale; }

private:
    float elevationScale_ = 1.0f;                                   // +0xd0 (tunable)
};

}  // namespace sc4
