// cSC4BuildingOccupant — a building placed in the city (35 functions, 0x10019ad88..0x10019bba0).
// Serialized as class 0xa9bd882d: u16 version 2, the occupant data, then
//   u32 building type (the building exemplar's instance), u8 flags (bit 0 lit), float3 min, float3 max,
//   s8 orientation (0..3), float completion.
// Layout: +0 cISC4BuildingOccupant, +8 cSC4Occupant (type id default 0x278128a0).
#pragma once
#include <cstdint>

#include "rz/cRZString.h"
#include "sc4/Occupant.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4BuildingOccupant = 0xa9bd882d;
constexpr std::uint32_t kOccupantTypeBuilding = 0x278128a0;
constexpr std::uint32_t kMessageBuildingLitChanged = 0x09d8e0b6;
constexpr std::uint32_t kMessageBuildingInserted = 0x8b8b2ced;     // forwarded 0x99ef1142
constexpr std::uint32_t kMessageBuildingRemoved = 0x8b8b2cee;      // forwarded 0x99ef1143
constexpr std::uint32_t kPropertyBuildingCustomName = 0x899afbad;
constexpr std::uint32_t kPropertyExemplarNameKey = 0x8a416a99;     // localized name key
constexpr std::uint32_t kPropertyExemplarName = 0x20;

// cISC4BuildingOccupant::BuildingProfile as far as recovered: the zone type leads the record
// (1..3 residential, 4..6 commercial, 5 is also agriculture in the ambience tests, 6/7 dirty industry,
// 8 clean industry — see cAmbienceZoneFilter); the capacity tables follow in the original.
struct BuildingProfile {
    std::int32_t zoneType = 0;
};

class cISC4BuildingOccupant : public cIGZUnknown {
public:
    virtual cISC4Occupant* AsOccupant() = 0;                                 // [3]
    virtual std::uint32_t GetBuildingType() const = 0;                       // [4]
    virtual void SetBuildingType(std::uint32_t type) = 0;                    // [5]
    virtual std::int32_t GetBuildingAge() const = 0;                         // [6] always 0
    virtual void SetBuildingAge(std::int32_t age) = 0;                       // [7] ignored
    virtual void SetBoundingBox(const float* min, const float* max) = 0;     // [8]
    virtual const float& GetCompletionPercent() = 0;                         // [9]
    virtual void SetCompletionPercent(float percent) = 0;                    // [10]
    virtual const BuildingProfile& GetBuildingProfile() const = 0;           // [11]
    virtual const cRZString& GetBuildingName() const = 0;                    // [12]
    virtual const cRZString& GetExemplarName() const = 0;                    // [13]
    virtual std::int32_t GetOrientation() const = 0;                         // [14]
    virtual bool SetOrientation(std::int32_t orientation) = 0;               // [15]
    virtual bool IsLit() const = 0;                                          // [16]
    virtual void SetLit(bool lit) = 0;                                       // [17]
    virtual void SetName(const cRZString& name) = 0;                         // [18]
    virtual bool GetName(cRZString& name) = 0;                               // [19]
};

// The building development simulator answers per building type; it is absent in headless use.
class IBuildingDevelopmentHost {
public:
    virtual ~IBuildingDevelopmentHost() = default;
    virtual const cRZString& ExemplarName(std::uint32_t buildingType) = 0;    // vslot 0x128
    virtual const cRZString& BuildingName(std::uint32_t buildingType) = 0;    // vslot 0x130
    virtual const BuildingProfile& Profile(std::uint32_t buildingType) = 0;   // vslot 0x138
};
IBuildingDevelopmentHost* BuildingDevelopment();
void SetBuildingDevelopment(IBuildingDevelopmentHost* host);

// SC4LoadExemplarString: resolves a localized string a property of the exemplar points at.
using ExemplarStringLoader = bool (*)(cRZString& out, cISCPropertyHolder* holder, std::uint32_t property);
void SetExemplarStringLoader(ExemplarStringLoader loader);

class cSC4BuildingOccupant final : public cISC4BuildingOccupant, public cSC4Occupant {
public:
    cSC4BuildingOccupant() { type_ = kOccupantTypeBuilding; }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cSC4Occupant::AddRef(); }
    std::uint32_t Release() override { return cSC4Occupant::Release(); }

    cISC4Occupant* AsOccupant() override { return static_cast<cSC4Occupant*>(this); }
    std::uint32_t GetBuildingType() const override { return buildingType_; }
    void SetBuildingType(std::uint32_t type) override { buildingType_ = type; }
    std::int32_t GetBuildingAge() const override { return 0; }
    void SetBuildingAge(std::int32_t) override {}
    void SetBoundingBox(const float* min, const float* max) override;
    const float& GetCompletionPercent() override { return completion_; }
    void SetCompletionPercent(float percent) override { completion_ = percent; }
    const BuildingProfile& GetBuildingProfile() const override;
    const cRZString& GetBuildingName() const override;
    const cRZString& GetExemplarName() const override;
    std::int32_t GetOrientation() const override { return orientation_; }
    bool SetOrientation(std::int32_t orientation) override;
    bool IsLit() const override { return (litFlags_ & 1) != 0; }
    void SetLit(bool lit) override;
    void SetName(const cRZString& name) override;
    bool GetName(cRZString& name) override;

    // cSC4Occupant overrides
    bool Init() override;
    bool Shutdown() override;
    void GetPosition(float* xyz) override;
    void SetPosition(const float* xyz) override;
    void GetBoundingBox(float* minXyz, float* maxXyz) override;
    void PostOccupantMessage(std::uint32_t message, std::uint32_t data) override;
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4BuildingOccupant; }

    static std::uint32_t ConvertZoneToDensity(int zoneType);   // 1..9 -> 1,2,3,1,2,3,3,3,3

private:
    std::uint32_t buildingType_ = 0;       // +0x68
    float min_[3] = {};                    // +0x6c
    float max_[3] = {};                    // +0x78
    std::int8_t orientation_ = 0;          // +0x84
    std::uint8_t litFlags_ = 0;            // +0x85
    float completion_ = 1.0f;              // +0x88
};

}  // namespace sc4
