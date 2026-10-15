#include "sc4/NeighborConnection.h"

#include "sc4/DBSegment.h"

namespace sc4 {

static INeighborConnectionHost* g_host = nullptr;
INeighborConnectionHost* NeighborConnectionHost() { return g_host; }
void SetNeighborConnectionHost(INeighborConnectionHost* host) { g_host = host; }

bool cSC4NeighborConnection::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSerializable) {
        *out = static_cast<cIGZSerializable*>(this);
    } else if (iid == 1 || iid == kGZIID_cISC4NeighborConnection) {
        *out = static_cast<cISC4NeighborConnection*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cSC4NeighborConnection::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

void* cSC4NeighborConnection::GetCityOnOtherSideOfConnection() const {
    void* city = nullptr;
    return g_host->CityEntryAtCell(otherX_, otherZ_, city) ? city : nullptr;
}

// The connection cell lies just outside the city on exactly one axis; the inner cell is that cell clamped
// back onto the city's edge. Returns false when it is outside on both axes or on neither.
bool cSC4NeighborConnection::GetConnectionInnerCell(int& x, int& z) const {
    const int cellsX = g_host->CityCellsX();
    const int cellsZ = g_host->CityCellsZ();
    const bool outX = cellX_ < 0 || cellX_ >= cellsX;
    const bool outZ = cellZ_ < 0 || cellZ_ >= cellsZ;
    if (outX == outZ) return false;
    x = cellX_;
    z = cellZ_;
    if (outX) {
        x = cellX_ < 0 ? 0 : cellsX - 1;
    } else {
        z = cellZ_ < 0 ? 0 : cellsZ - 1;
    }
    return true;
}

void cSC4NeighborConnection::Repair(void* regionalCity) {
    g_host->ConvertConnectionCell(cellX_, cellZ_, otherX_, otherZ_, regionalCity);
}

bool cSC4NeighborConnection::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    stream.SetUint32(type_);
    stream.SetSint32(cellX_);
    stream.SetSint32(cellZ_);
    stream.SetSint32(otherX_);
    stream.SetSint32(otherZ_);
    stream.SetUint8(flags_);
    cSCPropertyHolder::Write(stream);
    return !stream.GetError();
}

bool cSC4NeighborConnection::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 1) return false;
    stream.GetUint32(type_);
    stream.GetSint32(cellX_);
    stream.GetSint32(cellZ_);
    stream.GetSint32(otherX_);
    stream.GetSint32(otherZ_);
    stream.GetUint8(flags_);
    cSCPropertyHolder::Read(stream);
    return !stream.GetError();
}

}  // namespace sc4
