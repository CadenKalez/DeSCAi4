// The 33 ambience occupant filters (cAmbienceOccupantFilterIs*): each answers "does this occupant belong
// to category X" for the ambience simulator. All of them remember the last occupant asked about (by
// pointer identity) and its answer. 0x1003dxxxx..0x1003e8xxx.
//
// Dependencies on the rest of the game are behind IAmbienceWorld (lot manager / city size) and the
// minimal cISC4BuildingOccupant interface.
#pragma once
#include <cstdint>

#include "sc4/BuildingOccupant.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cIAmbienceOccupantFilter = 0xca400753;
constexpr std::uint32_t kGZIID_cIAmbienceOccupantFilterAlt = 0x4a40074c;
constexpr std::uint32_t kGZIID_cISC4NatureOccupant = 0xc83dd057;

// What a lot looks like to the ambience filters.
class ISC4LotInfo : public cIGZUnknown {
public:
    virtual std::int32_t GetState() = 0;    // slot 0x118: 1 = unpowered, 2..6 = distressed/abandoned states, 4 = abandoned
    virtual std::int32_t GetWealth() = 0;   // slot 0x148: 1 low, 2 medium, 3 high
};

class IAmbienceWorld {
public:
    virtual ~IAmbienceWorld() = default;
    virtual std::int32_t CityCellsX() = 0;      // spCity vslot 0x2e8
    virtual std::int32_t CityCellsZ() = 0;      // spCity vslot 0x2f0
    virtual ISC4LotInfo* LotAtCell(int x, int z) = 0;                  // lot manager vslot 0x28 (not AddRef'd)
    virtual ISC4LotInfo* LotOfOccupant(cISC4Occupant* occupant) = 0;   // lot manager vslot 0x70 (AddRef'd)
};
IAmbienceWorld* AmbienceWorld();
void SetAmbienceWorld(IAmbienceWorld* world);

class cIAmbienceOccupantFilter : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                  // [3]
    virtual bool Shutdown() = 0;                              // [4]
    virtual bool IsTrueOccupant(cISC4Occupant* occupant) = 0; // [5]
};

class cAmbienceOccupantFilter : public cIAmbienceOccupantFilter {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool Init() override { return true; }
    bool Shutdown() override { return true; }

protected:
    virtual ~cAmbienceOccupantFilter() = default;
    bool Remembered(cISC4Occupant* occupant, bool& answer) const {
        if (lastOccupant_ == occupant) { answer = cached_; return true; }
        return false;
    }
    bool Remember(cISC4Occupant* occupant, bool answer) {
        lastOccupant_ = occupant;
        cached_ = answer;
        return answer;
    }
    cISC4Occupant* lastOccupant_ = nullptr;     // +0x08
    bool cached_ = false;                       // +0x10
    std::uint32_t refCount_ = 0;                // +0x14
};

// occupant belongs to any of up to three occupant groups
template <std::uint32_t G0, std::uint32_t G1 = 0, std::uint32_t G2 = 0>
class cAmbienceGroupFilter : public cAmbienceOccupantFilter {
public:
    bool IsTrueOccupant(cISC4Occupant* occupant) override {
        bool answer;
        if (Remembered(occupant, answer)) return answer;
        const bool hit = occupant->IsOccupantGroup(G0) || (G1 && occupant->IsOccupantGroup(G1)) ||
                         (G2 && occupant->IsOccupantGroup(G2));
        return Remember(occupant, hit);
    }
};

#define AMBIENCE_FILTER_FACTORY(Name) \
    static Name* Create() { auto* f = new Name(); return f; }

class cAmbienceOccupantFilterIsBus final : public cAmbienceGroupFilter<0x4200, 0x420f> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsBus) };
class cAmbienceOccupantFilterIsCheapVehicle final : public cAmbienceGroupFilter<0x420e> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsCheapVehicle) };
class cAmbienceOccupantFilterIsCivillianVehicle final : public cAmbienceGroupFilter<0x420b> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsCivillianVehicle) };
class cAmbienceOccupantFilterIsConstructionVehicle final : public cAmbienceGroupFilter<0x4224, 0x4225> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsConstructionVehicle) };
class cAmbienceOccupantFilterIsEmergencyVehicle final : public cAmbienceGroupFilter<0x4202, 0x4228, 0x4211> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsEmergencyVehicle) };
class cAmbienceOccupantFilterIsFarmVehicle final : public cAmbienceGroupFilter<0x4219> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsFarmVehicle) };
class cAmbienceOccupantFilterIsGarbageTruck final : public cAmbienceGroupFilter<0x4205> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsGarbageTruck) };
class cAmbienceOccupantFilterIsMovingVan final : public cAmbienceGroupFilter<0x4206> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsMovingVan) };
class cAmbienceOccupantFilterIsMySim final : public cAmbienceGroupFilter<0x4209> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsMySim) };
class cAmbienceOccupantFilterIsRichVehicle final : public cAmbienceGroupFilter<0x420d> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsRichVehicle) };
class cAmbienceOccupantFilterIsSoccerMom final : public cAmbienceGroupFilter<0x420a> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsSoccerMom) };
class cAmbienceOccupantFilterIsTaxi final : public cAmbienceGroupFilter<0x420c> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsTaxi) };
class cAmbienceOccupantFilterIsTrain final : public cAmbienceGroupFilter<0x4005> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsTrain) };
class cAmbienceOccupantFilterIsTvReporter final : public cAmbienceGroupFilter<0x4218> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsTvReporter) };
class cAmbienceOccupantFilterIsVehicle final : public cAmbienceGroupFilter<0x4002> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsVehicle) };

// vehicle that is not a train
class cAmbienceOccupantFilterIsTraffic final : public cAmbienceOccupantFilter {
public:
    AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsTraffic)
    bool IsTrueOccupant(cISC4Occupant* occupant) override;
};
// the landfill occupant type (0x6a11bd2b)
class cAmbienceOccupantFilterIsLandfill final : public cAmbienceOccupantFilter {
public:
    AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsLandfill)
    bool IsTrueOccupant(cISC4Occupant* occupant) override;
};
// answers to the building interface (the original leaks the reference the query returns; this releases it)
class cAmbienceOccupantFilterIsBuilding final : public cAmbienceOccupantFilter {
public:
    AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsBuilding)
    bool IsTrueOccupant(cISC4Occupant* occupant) override;
};
class cAmbienceOccupantFilterIsNature final : public cAmbienceOccupantFilter {
public:
    AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsNature)
    bool IsTrueOccupant(cISC4Occupant* occupant) override;
};
class cAmbienceOccupantFilterIsOnFire final : public cAmbienceOccupantFilter {
public:
    AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsOnFire)
    bool IsTrueOccupant(cISC4Occupant* occupant) override;
};

// building zone type tests
template <int Zone>
class cAmbienceZoneFilter : public cAmbienceOccupantFilter {
public:
    bool IsTrueOccupant(cISC4Occupant* occupant) override;
};
class cAmbienceOccupantFilterIsResidential final : public cAmbienceZoneFilter<1> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsResidential) };
class cAmbienceOccupantFilterIsCommercial final : public cAmbienceZoneFilter<2> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsCommercial) };
class cAmbienceOccupantFilterIsAgriculture final : public cAmbienceZoneFilter<5> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsAgriculture) };
class cAmbienceOccupantFilterIsIndustrialClean final : public cAmbienceZoneFilter<8> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsIndustrialClean) };
class cAmbienceOccupantFilterIsIndustrialDirty final : public cAmbienceOccupantFilter {   // zone 6 or 7
public:
    AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsIndustrialDirty)
    bool IsTrueOccupant(cISC4Occupant* occupant) override;
};

// lot-state / wealth tests
class cAmbienceOccupantFilterIsAbandoned final : public cAmbienceOccupantFilter {   // lot at the occupant's first cell, state 4
public:
    AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsAbandoned)
    bool IsTrueOccupant(cISC4Occupant* occupant) override;
};
class cAmbienceOccupantFilterIsDistressed final : public cAmbienceOccupantFilter {  // (sic) cached value differs from the answer for state 5
public:
    AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsDistressed)
    bool IsTrueOccupant(cISC4Occupant* occupant) override;
};
class cAmbienceOccupantFilterIsPowered final : public cAmbienceOccupantFilter {     // lot state != 1
public:
    AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsPowered)
    bool IsTrueOccupant(cISC4Occupant* occupant) override;
};
template <int Wealth>
class cAmbienceWealthFilter : public cAmbienceOccupantFilter {
public:
    bool IsTrueOccupant(cISC4Occupant* occupant) override;
};
class cAmbienceOccupantFilterIsLowWealth final : public cAmbienceWealthFilter<1> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsLowWealth) };
class cAmbienceOccupantFilterIsMediumWealth final : public cAmbienceWealthFilter<2> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsMediumWealth) };
class cAmbienceOccupantFilterIsHighWealth final : public cAmbienceWealthFilter<3> { public: AMBIENCE_FILTER_FACTORY(cAmbienceOccupantFilterIsHighWealth) };

}  // namespace sc4
