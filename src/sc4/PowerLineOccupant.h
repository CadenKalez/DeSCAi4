// cSC4PowerLineOccupant — the wire strung between two power poles (51 functions, 0x100324422..0x100324afe).
// Class id 0xc9c05c5d, occupant type 0x990b2ea, version 2 record: occupant data, float3 position, u32 cell x,
// u32 cell z, bool "connected", then the two pole occupants as nested serializables (class 0xa8c86142).
// Layout: +8 cSC4Occupant, +0x68 position, +0x74/+0x78 cell, +0x80/+0x88 pole references (owned),
// +0x90 connected, +0x91 preview. The bounding box is the position itself (zero size); SetPosition derives the
// cell as position / 16 truncated.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"
#include "sc4/Occupant.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4PowerLineOccupant = 0xc9c05c5d;
constexpr std::uint32_t kGZIID_cISC4PowerLineOccupant = 0xc98f3b24;
constexpr std::uint32_t kOccupantTypePowerLine = 0x0990b2ea;
constexpr std::uint32_t kGZCLSID_cSC4PowerPoleOccupant = 0xa8c86142;

// The pole is only held by reference here (cSC4PowerPoleOccupant lives elsewhere).
class cISC4PowerPoleOccupant : public cIGZUnknown {};

class cSC4PowerLineOccupant : public cSC4Occupant {
public:
    cSC4PowerLineOccupant() { type_ = kOccupantTypePowerLine; }

    // answers 0xa9764c8c, 0xc9c05c5d and 0xc98f3b24 itself, the rest through the base
    bool QueryInterface(std::uint32_t iid, void** out) override;
    bool Init() override { return cSC4Occupant::Init(); }
    bool Shutdown() override;
    void GetPosition(float* xyz) override;
    void SetPosition(const float* xyz) override;
    void GetBoundingBox(float* minXyz, float* maxXyz) override;
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4PowerLineOccupant; }

    // Returns referenced poles (one reference each for the caller).
    void GetConnectedPoles(cISC4PowerPoleOccupant*& first, cISC4PowerPoleOccupant*& second, bool& connected) const;
    void SetConnectedPoles(cISC4PowerPoleOccupant* first, cISC4PowerPoleOccupant* second, bool connected);
    void GetCell(std::uint32_t& x, std::uint32_t& z) const { x = cellX_; z = cellZ_; }
    bool IsPreview() const { return preview_; }
    void SetPreview(bool preview) { preview_ = preview; }
    void SetDefaultExemplar(cISCResExemplar*) override {}   // the wire has no exemplar of its own

private:
    float position_[3] = {};                 // +0x68
    std::uint32_t cellX_ = 0, cellZ_ = 0;    // +0x74, +0x78
    cISC4PowerPoleOccupant* pole0_ = nullptr;   // +0x80
    cISC4PowerPoleOccupant* pole1_ = nullptr;   // +0x88
    bool connected_ = false;                 // +0x90
    bool preview_ = false;                   // +0x91
};

}  // namespace sc4
