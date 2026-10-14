// The 37 ambience cell filters (cAmbienceCellFilterIs*): "does city cell (x, z) belong to category X".
// Each remembers the last cell asked about and its answer (cell cache starts at -1,-1).
// 0x1003e5c8c..0x1003e8dbc. Game services are reached through IAmbienceCellWorld.
#pragma once
#include <cstdint>

#include "sc4/AmbienceOccupantFilters.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cIAmbienceCellFilter = 0xca400757;

// Lot as the ambience cell filters see it.
class ISC4CellLot {
public:
    virtual ~ISC4CellLot() = default;
    virtual std::int32_t GetState() = 0;                         // slot 0x118: 4 abandoned, 2..6 distressed range
    virtual std::int32_t GetWealth() = 0;                        // slot 0x148
    virtual std::int32_t GetConstructionPhase() = 0;             // slot 0x108: 2 = under construction
    virtual bool GetBuildingZone(std::int32_t& zone) = 0;        // slot 0x180 -> building record, first field = zone type
    virtual cISC4Occupant* GetBuildingOccupant() = 0;            // slot 0x180 as an occupant (zoo test)
    virtual bool GetGrowthStage(std::uint8_t& stage) = 0;        // slot 0x48 object, its slot 0x58
    virtual std::int32_t GetZoneCode() = 0;                      // slot 0xf8
    virtual std::int32_t GetBuildingTypeId(int arg) = 0;         // slot 0x170
};

class IAmbienceCellWorld {
public:
    virtual ~IAmbienceCellWorld() = default;
    virtual int ZoneTypeAt(int x, int z) = 0;                    // zone manager vslot 0x60
    virtual ISC4CellLot* LotAt(int x, int z) = 0;                // lot manager vslot 0x28
    virtual bool ZoneDeveloperHas(int x, int z, int mask) = 0;   // zone developer vslot 0x40
    virtual bool ZoneDeveloperIsWater(int x, int z) = 0;         // zone developer vslot 0x48
    virtual bool HasCrimeSim() = 0;
    virtual std::uint8_t CrimeAt(int x, int z) = 0;              // crime simulator grid
    virtual bool HasLandValueSim() = 0;
    virtual std::uint8_t DesirabilityAt(int x, int z) = 0;       // land value simulator grid
    virtual bool CellHasOccupant(int x, int z) = 0;              // occupant manager GetFirstOccupant(x, z, type 0)
    virtual bool BuildingZoneById(std::int32_t id, std::int32_t& zone) = 0;   // building development simulator
};
IAmbienceCellWorld* AmbienceCellWorld();
void SetAmbienceCellWorld(IAmbienceCellWorld* world);

class cIAmbienceCellFilter : public cIGZUnknown {
public:
    virtual bool Init() = 0;                          // [3]
    virtual bool Shutdown() = 0;                      // [4]
    virtual bool IsTrueCell(int x, int z) = 0;        // [5]
};

class cAmbienceCellFilter : public cIAmbienceCellFilter {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool Init() override { return true; }
    bool Shutdown() override { return true; }

protected:
    virtual ~cAmbienceCellFilter() = default;
    bool Remembered(int x, int z, bool& answer) const {
        if (x_ == x && z_ == z) { answer = cached_; return true; }
        return false;
    }
    bool Remember(int x, int z, bool answer) { x_ = x; z_ = z; cached_ = answer; return answer; }
    std::int32_t x_ = -1, z_ = -1;     // +0x08, +0x0c
    bool cached_ = false;              // +0x10
    std::uint32_t refCount_ = 0;       // +0x14
};

#define AMBIENCE_CELL_FACTORY(Name) static Name* Create() { return new Name(); }

// zone type range [Lo, Lo + Count)
template <int Lo, int Count>
class cAmbienceZoneTypeCellFilter : public cAmbienceCellFilter {
public:
    bool IsTrueCell(int x, int z) override;
};
class cAmbienceCellFilterIsResidential final : public cAmbienceZoneTypeCellFilter<1, 3> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsResidential) };
class cAmbienceCellFilterIsCommercial final : public cAmbienceZoneTypeCellFilter<4, 3> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsCommercial) };
class cAmbienceCellFilterIsIndustrial final : public cAmbienceZoneTypeCellFilter<7, 3> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsIndustrial) };
class cAmbienceCellFilterIsAirport final : public cAmbienceZoneTypeCellFilter<0xb, 1> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsAirport) };
class cAmbienceCellFilterIsSeaport final : public cAmbienceZoneTypeCellFilter<0xc, 1> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsSeaport) };
class cAmbienceCellFilterIsSpaceport final : public cAmbienceZoneTypeCellFilter<0xd, 1> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsSpaceport) };
class cAmbienceCellFilterIsLandfill final : public cAmbienceZoneTypeCellFilter<0xe, 1> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsLandfill) };
class cAmbienceCellFilterIsUnzoned final : public cAmbienceZoneTypeCellFilter<0, 1> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsUnzoned) };

// the zone developer reports a network/feature mask bit at the cell
template <int Mask>
class cAmbienceDeveloperCellFilter : public cAmbienceCellFilter {
public:
    bool IsTrueCell(int x, int z) override;
};
class cAmbienceCellFilterIsRoad final : public cAmbienceDeveloperCellFilter<1> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsRoad) };
class cAmbienceCellFilterIsRail final : public cAmbienceDeveloperCellFilter<2> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsRail) };
class cAmbienceCellFilterIsHighway final : public cAmbienceDeveloperCellFilter<4> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsHighway) };
class cAmbienceCellFilterIsPipe final : public cAmbienceDeveloperCellFilter<0x10> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsPipe) };
class cAmbienceCellFilterIsStreet final : public cAmbienceDeveloperCellFilter<0x48> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsStreet) };
class cAmbienceCellFilterIsSubway final : public cAmbienceDeveloperCellFilter<0x80> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsSubway) };
class cAmbienceCellFilterIsWater final : public cAmbienceCellFilter { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsWater) bool IsTrueCell(int x, int z) override; };

// lot state / wealth / construction
template <int Wealth>
class cAmbienceCellWealthFilter : public cAmbienceCellFilter {
public:
    bool IsTrueCell(int x, int z) override;
};
class cAmbienceCellFilterIsLowWealth final : public cAmbienceCellWealthFilter<1> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsLowWealth) };
class cAmbienceCellFilterIsMediumWealth final : public cAmbienceCellWealthFilter<2> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsMediumWealth) };
class cAmbienceCellFilterIsHighWealth final : public cAmbienceCellWealthFilter<3> { public: AMBIENCE_CELL_FACTORY(cAmbienceCellFilterIsHighWealth) };

#define AMBIENCE_CELL_CLASS(Name) \
    class Name final : public cAmbienceCellFilter { public: AMBIENCE_CELL_FACTORY(Name) bool IsTrueCell(int x, int z) override; };
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsAbandoned)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsDistressed)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsContruction)     // (sic) the game's spelling
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsBuilding)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsEmpty)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsZoo)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsAgriculture)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsIndustrialClean)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsIndustrialDirty)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsLowGrowthStage)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsMediumGrowthStage)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsHighGrowthStage)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsLowCrime)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsMediumCrime)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsHighCrime)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsLowDesirability)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsMediumDesirability)
AMBIENCE_CELL_CLASS(cAmbienceCellFilterIsHighDesirability)

}  // namespace sc4
