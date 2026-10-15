// cSC4FloraOccupant — a tree / bush / rock placed by the flora simulator (30 functions, 0x1000cb74e..0x1000cbe40).
// Class id 0xa9c05c85, version 3 record: occupant data, u32 flora type, float3 position, u32 birth date,
// u32 last seeding date, s8 state, u8 orientation, u32 lot tag. Layout: +0 cISC4FloraOccupant, +8 cSC4Occupant
// (default occupant type 0x74758926).
#pragma once
#include <cstdint>

#include "sc4/Occupant.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4FloraOccupant = 0xa9c05c85;
constexpr std::uint32_t kGZIID_cISC4FloraOccupant = 0xa9c05c85;
constexpr std::uint32_t kOccupantTypeFlora = 0x74758926;
constexpr std::uint32_t kOccupantFlagFlora = 8;

// The per-species data shared by all trees of a kind (cSC4StaticFloraData, 0x188 bytes in the original; only the
// fields the occupant reads are modelled).
class cSC4StaticFloraData {
public:
    float halfExtent = 1.0f;                 // +0x38 half the footprint (bounding radius)
    float height = 1.0f;                     // +0x44
    cISCResExemplar* exemplar = nullptr;     // +0x178 the species' exemplar (the occupant's default exemplar)
    void AddRef() { ++refs_; }
    void Release() { if (--refs_ <= 0) delete this; }
private:
    int refs_ = 0;                           // +0x184
};

class IFloraHost {
public:
    virtual ~IFloraHost() = default;
    virtual cSC4StaticFloraData* StaticFloraData(std::uint32_t floraType) = 0;   // flora simulator vslot 0x38
};
IFloraHost* FloraHost();
void SetFloraHost(IFloraHost* host);

class cISC4FloraOccupant : public cIGZUnknown {
public:
    virtual cISC4Occupant* AsOccupant() = 0;                                  // [3]
    virtual std::uint32_t GetFloraType() const = 0;                           // [4]
    virtual void SetFloraType(std::uint32_t type) = 0;                        // [5]
    virtual std::uint32_t GetBirthDate() const = 0;                           // [6]
    virtual void SetBirthDate(std::uint32_t date) = 0;                        // [7]
    virtual std::uint32_t GetLastSeedingDate() const = 0;                     // [8]
    virtual void SetLastSeedingDate(std::uint32_t date) = 0;                  // [9]
    virtual std::int32_t GetState() const = 0;                                // [10]
    virtual void SetState(std::int32_t state) = 0;                            // [11]
    virtual std::uint32_t GetOrientation() const = 0;                         // [12]
    virtual void SetOrientation(std::uint32_t orientation) = 0;               // [13]
    virtual void SetStaticFloraData(cSC4StaticFloraData* data) = 0;           // [14]
};

class cSC4FloraOccupant final : public cISC4FloraOccupant, public cSC4Occupant {
public:
    cSC4FloraOccupant();
    ~cSC4FloraOccupant() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cSC4Occupant::AddRef(); }
    std::uint32_t Release() override { return cSC4Occupant::Release(); }

    cISC4Occupant* AsOccupant() override { return static_cast<cSC4Occupant*>(this); }
    std::uint32_t GetFloraType() const override { return floraType_; }
    void SetFloraType(std::uint32_t type) override { floraType_ = type; }
    std::uint32_t GetBirthDate() const override { return birthDate_; }
    void SetBirthDate(std::uint32_t date) override { birthDate_ = date; }
    std::uint32_t GetLastSeedingDate() const override { return lastSeedingDate_; }
    void SetLastSeedingDate(std::uint32_t date) override { lastSeedingDate_ = date; }
    std::int32_t GetState() const override { return state_; }
    void SetState(std::int32_t state) override { state_ = static_cast<std::int8_t>(state); }
    std::uint32_t GetOrientation() const override { return orientation_; }
    void SetOrientation(std::uint32_t orientation) override { orientation_ = static_cast<std::uint8_t>(orientation); }
    void SetStaticFloraData(cSC4StaticFloraData* data) override;

    void GetPosition(float* xyz) override;
    void SetPosition(const float* xyz) override;
    void GetBoundingBox(float* minXyz, float* maxXyz) override;
    bool GetLotTag(std::uint32_t& tag) const override;
    void SetLotTag(std::uint32_t tag) override { lotTag_ = tag; }
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4FloraOccupant; }

private:
    std::uint32_t floraType_ = 0;          // +0x68
    std::uint32_t lotTag_ = 0;             // +0x6c (note: starts at 0, not -1)
    float position_[3] = {};               // +0x70
    std::uint32_t birthDate_ = 0;          // +0x7c
    std::uint32_t lastSeedingDate_ = 0;    // +0x80
    cSC4StaticFloraData* data_ = nullptr;  // +0x88
    std::int8_t state_ = 0;                // +0x90
    std::uint8_t orientation_ = 0;         // +0x91
};

}  // namespace sc4
