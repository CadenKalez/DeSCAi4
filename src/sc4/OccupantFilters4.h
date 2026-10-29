// More occupant filters and the plain dummy occupant (0x1001934xx, 0x1002c119a.., 0x1002dc206.., 0x100575a1a..):
//   cSC4StoplightPropOccupantFilter  prop occupants of type 0xa823821e in group 0x5002 or 0x5003 (traffic lights)
//   cSC4ZoomRotationModelChangeOccupantFilter  occupants whose models were not yet built for the current zoom AND rotation
//   cSC4WaterOccupantFilter          network occupants carrying a drivable flag, water-flagged props and similar
//   cSC4DummyOccupant                a position-only occupant (like cSC4DummyAutoOccupant, without the group)
#pragma once
#include "sc4/OccupantFilters2.h"

namespace sc4 {

constexpr std::uint32_t kStoplightPropOccupantType = 0xa823821e;     // -0x57dc7de2
constexpr std::uint32_t kStoplightGroupA = 0x5002, kStoplightGroupB = 0x5003;

class cSC4StoplightPropOccupantFilter : public cSC4OccupantFilter {
public:
    bool IsOccupantIncluded(cISC4Occupant* occupant) override;                   // @ 0x1002dc206
    bool IsOccupantTypeIncluded(std::uint32_t) override { return false; }        // @ 0x1002dc258
    bool IsPropertyHolderIncluded(cISCPropertyHolder*) override { return false; }   // @ 0x1002dc260
};

class cSC4ZoomRotationModelChangeOccupantFilter : public cSC4OccupantFilter {
public:
    // @ 0x1002c11d6: the bit of the zoom (bits 2..6 of the placeable object's word) and of the rotation (bits 7..10).
    void SetZoomAndRotation(int zoom, int rotation) {
        mask_ = (1u << ((static_cast<unsigned>(rotation) + 7u) & 31u)) + (1u << ((static_cast<unsigned>(zoom) + 2u) & 31u));
    }
    // @ 0x1002c11f6: null -> no; an occupant without models is included; otherwise it is included unless the models were
    // built for both the current rotation and the current zoom... (0x780 and 0x7c both set in the masked word means "excluded").
    bool IsOccupantIncluded(cISC4Occupant* occupant) override;
    std::uint32_t Mask() const { return mask_; }

private:
    std::uint32_t mask_ = 0;      // +0x14
};

class cSC4WaterOccupantFilter : public cSC4OccupantFilter {
public:
    bool IsOccupantIncluded(cISC4Occupant* occupant) override;                   // @ 0x100193514
    bool IsPropertyHolderIncluded(cISCPropertyHolder* holder) override;          // @ 0x1001935a4
};

constexpr std::uint32_t kNetworkOccupantType = 0xc772bf98;     // -0x388d4068

// Network occupants that carry any of the flags in the mask (HasAnyNetworkFlag, network interface vslot 0x48).
class cSC4NetworkORFilter : public cSC4OccupantFilter {
public:
    cSC4NetworkORFilter() = default;                                         // @ 0x1002eee0a
    explicit cSC4NetworkORFilter(std::uint32_t mask) : mask_(mask) {}        // @ 0x1002eee4c
    bool IsOccupantIncluded(cISC4Occupant* occupant) override;               // @ 0x1002eee88

private:
    std::uint32_t mask_ = 0;      // +0x14
};

}  // namespace sc4
