#include "sc4/TrafficEffectManager.h"

#include "sc4/DBSegment.h"
#include "sc4/SimStreamOps.h"

namespace sc4 {

namespace {
constexpr std::uint32_t kTrafficEffectOccupantIid = 0x26bb767e;
}

// The two rectangles: the near one is the radius - 1 (at least 0) square around the centre, the far one the radius square.
void cSC4TrafficEffectManager::SetRects(TrafficEffect& e) const {
    const int inner = e.radius - 1 < 0 ? 0 : e.radius - 1;
    e.nearRect = {e.x - inner, e.z - inner, e.x + inner, e.z + inner};
    e.farRect = {e.x - e.radius, e.z - e.radius, e.x + e.radius, e.z + e.radius};
}

void cSC4TrafficEffectManager::SetOccupant(TrafficEffect& e, cISC4Occupant* occupant) {
    if (e.occupant == occupant) return;
    if (occupant) occupant->AddRef();
    cISC4Occupant* old = e.occupant;
    e.occupant = occupant;
    if (old) old->Release();
}

// 0x10011b568: detach the accident controller (when attached) and let go of the occupant; the time left is cleared.
bool cSC4TrafficEffectManager::ShutdownEffect(TrafficEffect& effect) {
    if (effect.controllerAttached) {
        if (!effect.occupant) { effect.timeLeft = 0; return true; }   // flag set without occupant: nothing to detach (original skips the release too)
        host_->DetachController(effect.occupant);
        effect.controllerAttached = false;
    }
    if (effect.occupant) {
        cISC4Occupant* occupant = effect.occupant;
        effect.occupant = nullptr;
        occupant->Release();
    }
    effect.timeLeft = 0;
    return true;
}

// 0x100111eaa
void cSC4TrafficEffectManager::Shutdown() {
    for (TrafficEffect& e : effects_) ShutdownEffect(e);
    effects_.clear();
}

// 0x100112e0e
void cSC4TrafficEffectManager::Update() {
    const std::uint32_t now = host_->ElapsedMilliseconds();
    if (nextUpdate_ > now) return;
    nextUpdate_ = now + 500;
    const std::uint32_t delta = now - lastUpdate_;
    for (auto it = effects_.begin(); it != effects_.end();) {
        TrafficEffect& e = *it;
        if (e.timeLeft < delta) {
            ShutdownEffect(e);
            it = effects_.erase(it);
            continue;
        }
        e.timeLeft -= delta;
        if (e.occupant) {
            float position[3] = {0, 0, 0};
            e.occupant->GetPosition(position);
            int cellX = 0, cellZ = 0;
            host_->PositionToCell(position[0], position[2], cellX, cellZ);
            if (e.x != cellX || e.z != cellZ) {
                e.x = cellX;
                e.z = cellZ;
                SetRects(e);
            }
        }
        ++it;
    }
    lastUpdate_ = now;
}

// 0x100113ab4
std::uint8_t cSC4TrafficEffectManager::GetPopulateFlags(int x, int z) const {
    std::uint8_t flags = 0;
    for (const TrafficEffect& e : effects_) {
        if (e.nearRect.x0 <= x && e.nearRect.z0 <= z && x <= e.nearRect.x1) flags |= (z <= e.nearRect.z1) ? 1 : 0;
        if (e.farRect.x0 <= x && e.farRect.z0 <= z && x <= e.farRect.x1 && z <= e.farRect.z1) flags |= 2;
    }
    return flags;
}

// 0x100113baa: an accident centred on the vehicle's cell (middle of its cell rectangle); attaches the accident controller to it.
TrafficEffect* cSC4TrafficEffectManager::CreateAccident(cISC4Occupant* vehicle, int radius, std::uint32_t durationMs) {
    effects_.emplace_back();
    TrafficEffect& e = effects_.back();
    e.timeLeft = durationMs;
    SC4CellRect cells;
    vehicle->GetBoundingCityCells(cells);
    e.x = (cells.x1 + cells.x0) / 2;
    e.z = (cells.z1 + cells.z0) / 2;
    e.radius = radius;
    SetRects(e);
    float position[3] = {0, 0, 0};
    vehicle->GetPosition(position);
    e.controllerAttached = host_->AttachAccidentController(vehicle, position);
    SetOccupant(e, vehicle);
    return &e;
}

// 0x100113d40: an effect at a fixed cell that lasts until stopped (0xffffffff); the occupant is optional.
TrafficEffect* cSC4TrafficEffectManager::CreateEffect(int cellX, int cellZ, int radius, cISC4Occupant* occupant) {
    effects_.emplace_back();
    TrafficEffect& e = effects_.back();
    e.timeLeft = 0xffffffffu;
    e.x = cellX;
    e.z = cellZ;
    e.radius = radius;
    SetRects(e);
    SetOccupant(e, occupant);
    e.controllerAttached = false;
    return &e;
}

// 0x100113e6a
bool cSC4TrafficEffectManager::StopEffect(TrafficEffect* effect) {
    for (auto it = effects_.begin(); it != effects_.end(); ++it) {
        if (&*it == effect) {
            ShutdownEffect(*it);
            effects_.erase(it);
            break;
        }
    }
    return true;
}

// 0x1001160f2
bool cSC4TrafficEffectManager::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    stream.SetUint32(std::uint32_t(effects_.size()));
    for (const TrafficEffect& e : effects_) {
        stream.SetSint32(e.x);
        stream.SetSint32(e.z);
        stream.SetUint32(std::uint32_t(e.radius));
        stream << SC4RectInt{e.nearRect.x0, e.nearRect.z0, e.nearRect.x1, e.nearRect.z1};
        stream << SC4RectInt{e.farRect.x0, e.farRect.z0, e.farRect.x1, e.farRect.z1};
        SC4WriteGZSerializable(stream, e.occupant);
        stream << e.controllerAttached;
        stream.SetUint32(e.timeLeft);
    }
    return !stream.GetError();
}

// 0x10011627c
bool cSC4TrafficEffectManager::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 1) return false;
    std::uint32_t count = 0;
    stream.GetUint32(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        TrafficEffect e;
        std::int32_t v = 0;
        std::uint32_t u = 0;
        stream.GetSint32(v); e.x = v;
        stream.GetSint32(v); e.z = v;
        stream.GetUint32(u); e.radius = int(u);
        std::int32_t r[8] = {};
        for (auto& value : r) stream.GetSint32(value);
        e.nearRect = {r[0], r[1], r[2], r[3]};
        e.farRect = {r[4], r[5], r[6], r[7]};
        void* raw = nullptr;
        SC4ReadGZSerializable(stream, kTrafficEffectOccupantIid, &raw, false);
        e.occupant = static_cast<cISC4Occupant*>(raw);
        bool attached = false;
        stream >> attached;
        e.controllerAttached = attached;
        stream.GetUint32(e.timeLeft);
        effects_.push_back(e);   // the list owns the reference that came with the read
    }
    return !stream.GetError();
}

// 0x10011b6b6: the newest effect's centre (the original tests the list's count word, +0x10).
bool cSC4TrafficEffectManager::GetMostRecentAccidentLocation(int& cellX, int& cellZ) const {
    if (effects_.empty()) return false;
    cellX = effects_.back().x;
    cellZ = effects_.back().z;
    return true;
}

}  // namespace sc4
