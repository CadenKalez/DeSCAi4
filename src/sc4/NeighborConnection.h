// cSC4NeighborConnection — one road/rail/... link between this city and a neighbouring region cell
// (0x10007146e..0x100075f82). Class id 0x0a134f24, interface 0x773fef2.
// Layout: +0 interface, +8 serializable view, +0x10 cSCPropertyHolder, +0x38 traffic source view,
// +0x40 refcount, +0x44 type, +0x48 connection cell (in the city's own cells, may be just outside),
// +0x50 region cell on the other side, +0x58 flags (bit0 visited this cycle, bit1 INACTIVE),
// +0x60 trip data list. UpdateTrafficVolume (0x100074cb2) and GetSourceCells are not recovered.
#pragma once
#include <cstdint>
#include <list>

#include "gz/cIGZSerializable.h"
#include "sc4/SCProperty.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4NeighborConnection = 0x0a134f24, kGZIID_cISC4NeighborConnection = 0x0773fef2;

// What the connection needs from the city / region simulators.
class INeighborConnectionHost {
public:
    virtual ~INeighborConnectionHost() = default;
    virtual int CityCellsX() = 0;                                  // SL::spCity vslot 0x2e8
    virtual int CityCellsZ() = 0;                                  // SL::spCity vslot 0x2f0
    virtual bool CityEntryAtCell(int x, int z, void*& city) = 0;  // regional manager vslot 0x58
    // cSC4NeighborsSimulator::ConvertConnectionCellToNeighboringRegionCell
    virtual void ConvertConnectionCell(int x, int z, int& outX, int& outZ, void* regionalCity) = 0;
};
INeighborConnectionHost* NeighborConnectionHost();
void SetNeighborConnectionHost(INeighborConnectionHost* host);

class cISC4NeighborConnection : public cIGZUnknown {
public:
    virtual std::uint32_t GetType() const = 0;
    virtual void GetConnectionCell(int& x, int& z) const = 0;
    virtual void GetRegionCellOnOtherSideOfConnection(int& x, int& z) const = 0;
    virtual bool GetConnectionInnerCell(int& x, int& z) const = 0;
    virtual void* GetCityOnOtherSideOfConnection() const = 0;
    virtual void Repair(void* regionalCity) = 0;
    virtual bool GetVisitedThisCycle() const = 0;
    virtual void SetVisitedThisCycle(bool visited) = 0;
    virtual bool GetIsActive() const = 0;
    virtual void SetIsActive(bool active) = 0;
};

class cSC4NeighborConnection : public cISC4NeighborConnection, public cIGZSerializable, public cSCPropertyHolder {
public:
    cSC4NeighborConnection() = default;
    void* AsTrafficSource() { return this; }    // @ 0x100075ebc (the cISC4TrafficSource view at +0x38 in the original; see cSC4Lot::AsTrafficSource)
    ~cSC4NeighborConnection() override = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    std::uint32_t GetType() const override { return type_; }
    void GetConnectionCell(int& x, int& z) const override { x = cellX_; z = cellZ_; }
    void GetRegionCellOnOtherSideOfConnection(int& x, int& z) const override { x = otherX_; z = otherZ_; }
    bool GetConnectionInnerCell(int& x, int& z) const override;
    void* GetCityOnOtherSideOfConnection() const override;
    void Repair(void* regionalCity) override;
    bool GetVisitedThisCycle() const override { return flags_ & 1; }
    void SetVisitedThisCycle(bool visited) override { flags_ = visited ? (flags_ | 1) : (flags_ & 0xfe); }
    bool GetIsActive() const override { return !((flags_ >> 1) & 1); }
    void SetIsActive(bool active) override { flags_ = active ? (flags_ & 0xfd) : (flags_ | 2); }
    std::list<int>& GetTripDataList() { return tripData_; }   // std::list<cSC4TripData> in the game
    // The record fields cSC4NeighborsSimulator::CreateConnection writes directly in the original (+0x44..+0x54).
    void Initialize(std::uint32_t type, int cellX, int cellZ, int otherX, int otherZ) {
        type_ = type;
        cellX_ = cellX;
        cellZ_ = cellZ;
        otherX_ = otherX;
        otherZ_ = otherZ;
    }

    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4NeighborConnection; }

private:
    std::uint32_t refCount_ = 0;      // +0x40
    std::uint32_t type_ = 0;          // +0x44
    int cellX_ = 0, cellZ_ = 0;       // +0x48
    int otherX_ = 0, otherZ_ = 0;     // +0x50
    std::uint8_t flags_ = 0;          // +0x58
    std::list<int> tripData_;         // +0x60
};

}  // namespace sc4
