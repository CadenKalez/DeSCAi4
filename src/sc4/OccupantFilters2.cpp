#include "sc4/OccupantFilters2.h"

#include <algorithm>

namespace sc4 {

static IOccupantFilterWorld* g_world = nullptr;
IOccupantFilterWorld* OccupantFilterWorld() { return g_world; }
void SetOccupantFilterWorld(IOccupantFilterWorld* world) { g_world = world; }

bool cSC4OccupantTypesFilter::IsOccupantTypeIncluded(std::uint32_t type) {
    return std::find(types_.begin(), types_.end(), type) != types_.end();
}

// Version 3, base version 3, then (unless the stream has already failed) the count and the types.
bool cSC4OccupantTypesFilter::Write(cIGZOStream& s) const {
    s.SetUint16(3);
    s.SetUint16(3);
    if (!s.GetError()) {
        s.SetUint32(static_cast<std::uint32_t>(types_.size()));
        for (const std::uint32_t type : types_) s.SetUint32(type);
    }
    return !s.GetError();
}

bool cSC4OccupantTypesFilter::Read(cIGZIStream& s) {
    std::uint16_t version = 0;
    s.GetUint16(version);
    if (version != 3) return false;
    s.GetUint16(version);
    if (version == 3 && !s.GetError()) {
        std::uint32_t count = 0;
        s.GetUint32(count);
        types_.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) s.GetUint32(types_[i]);
    }
    return !s.GetError();
}

bool cSC4OccupantTypesFilterHashed::Write(cIGZOStream& s) const {
    s.SetUint16(3);
    s.SetUint16(3);
    if (!s.GetError()) {
        s.SetUint32(static_cast<std::uint32_t>(types_.size()));
        for (const std::uint32_t type : types_) s.SetUint32(type);
    }
    return !s.GetError();
}

bool cSC4OccupantTypesFilterHashed::Read(cIGZIStream& s) {
    std::uint16_t version = 0;
    s.GetUint16(version);
    if (version != 3) return false;
    s.GetUint16(version);
    if (version == 3 && !s.GetError()) {
        std::uint32_t count = 0;
        s.GetUint32(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t type = 0;
            s.GetUint32(type);
            types_.insert(type);
        }
    }
    return !s.GetError();
}

bool cSC4ViewLevelOccupantFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    return static_cast<std::uint32_t>(g_world->OccupantViewLevel(occupant)) == level_;
}

bool cSC4ViewLevelOccupantFilter::Write(cIGZOStream& s) const {
    s.SetUint16(1);
    s.SetUint8(static_cast<std::uint8_t>(level_));
    return !s.GetError();
}

bool cSC4ViewLevelOccupantFilter::Read(cIGZIStream& s) {
    std::uint16_t version = 0;
    s.GetUint16(version);
    if (version != 1) return false;
    std::uint8_t level = 0;
    s.GetUint8(level);
    level_ = level;
    return !s.GetError();
}

bool cSC4NetworkOccupantFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    std::uint32_t flags = 0;
    return g_world->NetworkFlags(occupant, flags) && (mask_ & flags) != 0;
}

bool cSC4NetworkOccupantFilter::Write(cIGZOStream& s) const {
    s.SetUint16(1);
    s.SetUint32(mask_);
    return !s.GetError();
}

bool cSC4NetworkOccupantFilter::Read(cIGZIStream& s) {
    std::uint16_t version = 0;
    s.GetUint16(version);
    if (version != 1) return false;
    s.GetUint32(mask_);
    return !s.GetError();
}

bool cSC4SurfaceNetworkOccupantFilter::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != kGZIID_cISC4OccupantFilter && iid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4SurfaceNetworkOccupantFilter::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cSC4SurfaceNetworkOccupantFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    std::uint32_t flags = 0;
    return g_world->NetworkFlags(occupant, flags) && (flags & 0x1f4f) != 0;
}

cSC4PlacedNetworkOccupantFilter::cSC4PlacedNetworkOccupantFilter(cISC4OccupantFilter* inner) : inner_(inner) {
    if (inner_) inner_->AddRef();
}

cSC4PlacedNetworkOccupantFilter::~cSC4PlacedNetworkOccupantFilter() {
    if (inner_) inner_->Release();
}

bool cSC4PlacedNetworkOccupantFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    return inner_->IsOccupantIncluded(occupant) && IsOccPlaced(occupant);
}

bool cSC4PlacedNetworkOccupantFilter::IsOccPlaced(cISC4Occupant* occupant) {
    return occupant != nullptr && g_world->NetworkIsPlaced(occupant);
}

}  // namespace sc4
