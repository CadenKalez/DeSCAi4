#include "sc4/AutomataControllerGrid.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sc4 {

namespace {
// x / 128 floor, as byte (tracts are 128 m)
std::uint8_t TractOf(float meters) { return static_cast<std::uint8_t>(AutomataFloorIdiom(meters * 0.0078125f)); }
}  // namespace

void NormalizeControllerTime(ControllerTime& time) {
    float t = time.time;
    std::int32_t days = time.days;
    if (std::isnan(t)) {
        time.time = 0.0f;
    } else {
        if (t < 0.0f) {
            do { t += 24.0f; --days; } while (t < 0.0f);
        }
        if (t >= 24.0f) {
            do { t -= 24.0f; ++days; } while (t >= 24.0f);
        }
        time.time = t;
    }
    std::int32_t day = static_cast<std::int16_t>(days);
    if (day < 0) day += 7 + ((-day - 1) / 7) * 7;   // lifts the day into 1..7
    time.days = static_cast<std::int16_t>(day % 7);
}

// ---- cSC4ControllerGrid -------------------------------------------------------------------------------------------------------
bool cSC4ControllerGrid::Init() {
    IAutomataControllerHost* host = AutomataControllerHost();
    const int cellsX = host->CityCellsX();
    const std::uint32_t tracts = static_cast<std::uint32_t>(((cellsX + static_cast<std::int32_t>(static_cast<std::uint32_t>(cellsX >> 31) >> 29)) >> 3));
    width_ = height_ = static_cast<std::uint8_t>(tracts);
    shift_ = 0;
    const std::uint32_t side = tracts & 0xff;
    std::uint8_t probe = 0;
    do {
        if ((1u << (probe & 0x1f)) == side) break;
        ++probe;
        shift_ = probe;
    } while (probe < 0x20);
    cells_.resize(static_cast<std::size_t>(side) * side);
    cityWidth_ = static_cast<float>(cellsX << 4);
    cityDepth_ = static_cast<float>(host->CityCellsZ() << 4);
    return true;
}

void cSC4ControllerGrid::GetGridBounds(const float position[3], float radius, TractRect& out) const {
    const float width = AutomataControllerHost()->CityWidth();
    const float depth = AutomataControllerHost()->CityWidth();   // quirk @ 0x1000f6e3b/0x1000f6e4c: the width is asked twice
    const float fx0 = position[0] - radius, fz0 = position[2] - radius, fx1 = position[0] + radius, fz1 = position[2] + radius;
    const float cx0 = std::min(fx0, width), cz0 = std::min(fz0, depth), cx1 = std::min(fx1, width), cz1 = std::min(fz1, depth);
    out.x0 = fx0 < 0.0f ? 0 : TractOf(cx0);
    out.z0 = fz0 < 0.0f ? 0 : TractOf(cz0);
    out.x1 = fx1 < 0.0f ? 0 : TractOf(cx1);
    out.z1 = fz1 < 0.0f ? 0 : TractOf(cz1);
    out.x1 = std::min<std::uint8_t>(out.x1, static_cast<std::uint8_t>(width_ - 1));
    out.z1 = std::min<std::uint8_t>(out.z1, static_cast<std::uint8_t>(height_ - 1));
}

void cSC4ControllerGrid::GetGridBounds(const cSC4AutomataController* controller, TractRect& out) const {
    float lo[3], hi[3];
    controller->GetBoundingBox(lo, hi);
    out.x0 = TractOf(lo[0]);
    out.z0 = TractOf(lo[2]);
    out.x1 = std::min<std::uint8_t>(TractOf(hi[0]), static_cast<std::uint8_t>(width_ - 1));
    out.z1 = std::min<std::uint8_t>(TractOf(hi[2]), static_cast<std::uint8_t>(height_ - 1));
}

std::list<cSC4AutomataController*>& cSC4ControllerGrid::GetTractByWorldPos(float x, float z) {
    const float cx = x >= 0.0f ? x : 0.0f, cz = z >= 0.0f ? z : 0.0f;
    std::uint32_t tx = static_cast<std::uint32_t>(AutomataFloorIdiom(cx * 0.0078125f));
    std::uint32_t tz = static_cast<std::uint32_t>(AutomataFloorIdiom(cz * 0.0078125f));
    tx = std::min<std::uint32_t>(tx, static_cast<std::uint32_t>(width_) - 1);
    tz = std::min<std::uint32_t>(tz, static_cast<std::uint32_t>(height_) - 1);
    return cells_[(static_cast<std::size_t>(tz) << shift_) + tx];
}

void cSC4ControllerGrid::AddController(cSC4AutomataController* controller) {
    std::uint8_t* stored = controller->TractRect();
    TractRect bounds;
    GetGridBounds(controller, bounds);
    std::memcpy(stored, &bounds, 4);   // the original computes straight into the controller's rect
    for (unsigned z = bounds.z0; z <= bounds.z1; ++z) {
        for (unsigned x = bounds.x0; x <= bounds.x1; ++x) {
            auto& list = cells_[(static_cast<std::size_t>(z) << shift_) + x];
            if (std::find(list.begin(), list.end(), controller) == list.end()) list.push_back(controller);
        }
    }
}

void cSC4ControllerGrid::RemoveController(cSC4AutomataController* controller) {
    for (auto& list : cells_) list.remove(controller);
    std::memset(controller->TractRect(), 0, 4);
}

void cSC4ControllerGrid::MoveController(cSC4AutomataController* controller) {
    std::uint8_t* stored = controller->TractRect();
    TractRect fresh;
    GetGridBounds(controller, fresh);
    TractRect old;
    std::memcpy(&old, stored, 4);
    if (fresh.x0 == old.x0 && fresh.z0 == old.z0 && fresh.x1 == old.x1 && fresh.z1 == old.z1) return;
    // tracts entered
    for (unsigned z = fresh.z0; z <= fresh.z1; ++z) {
        for (unsigned x = fresh.x0; x <= fresh.x1; ++x) {
            if (x < old.x0 || z < old.z0 || old.x1 < x || old.z1 < z) cells_[(static_cast<std::size_t>(z) << shift_) + x].push_back(controller);
        }
    }
    // tracts left
    for (unsigned z = old.z0; z <= old.z1; ++z) {
        for (unsigned x = old.x0; x <= old.x1; ++x) {
            if (x < fresh.x0 || z < fresh.z0 || fresh.z1 < z || fresh.x1 < x) {
                auto& list = cells_[(static_cast<std::size_t>(z) << shift_) + x];
                auto it = std::find(list.begin(), list.end(), controller);
                if (it != list.end()) list.erase(it);
            }
        }
    }
    std::memcpy(stored, &fresh, 4);
}

// ---- tControllerQInfo ---------------------------------------------------------------------------------------------------------
void tControllerQInfo::Clear() {
    for (IControllerAutomaton* automaton : queue) automaton->Release();
    queue.clear();
    savedCount = 0;
}

void tControllerQInfo::AddToQueue(IControllerAutomaton* automaton) {
    if (std::find(queue.begin(), queue.end(), automaton) != queue.end()) return;
    IControllerAutomaton* ahead = queue.empty() ? nullptr : queue.back();
    queue.push_back(automaton);
    automaton->AddRef();
    automaton->SetLeader(ahead);
}

void tControllerQInfo::RemoveFromQueue(IControllerAutomaton* automaton) {
    auto it = std::find(queue.begin(), queue.end(), automaton);
    if (it == queue.end()) return;
    auto next = std::next(it);
    if (next != queue.end()) {
        // the follower now queues behind whoever was ahead of the removed automaton
        IControllerAutomaton* ahead = (it == queue.begin()) ? nullptr : *std::prev(it);
        (*next)->SetLeader(ahead);
    }
    queue.erase(it);
    automaton->Release();
}

void tControllerQInfo::InitQueuePoint(cISC4Occupant* occupant) {
    if (valid) return;
    IAutomataControllerHost* host = AutomataControllerHost();
    float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
    std::uint32_t side;
    SC4CellRect rect;
    std::uint32_t facing = 0;
    if (!host->GetOccupantLot(occupant, rect, facing)) {
        occupant->GetBoundingBox(lo, hi);
        side = host->RandomBelow(4);
    } else {
        lo[0] = static_cast<float>(rect.x0 << 4);
        lo[2] = static_cast<float>(rect.z0 << 4);
        hi[0] = static_cast<float>(rect.x1 * 16 + 16);
        hi[2] = static_cast<float>(rect.z1 * 16 + 16);
        // facing 1,2,3 -> side 2,3,0; facing 0 and >= 4 -> side 1
        const std::uint32_t index = facing - 1;
        side = index > 2 ? 1u : (0x302u >> ((index & 3) << 3)) & 0xff;
    }
    for (int attempt = -1; attempt < 3 && !valid; ++attempt) {
        float ax = 0, bx = 0, az = 0, bz = 0;   // the two ends of the lot edge the queue stands next to
        float dx = 0, dz = 0;
        switch (side & 0xff) {
            case 0: ax = bx = lo[0] - 1.0f; az = hi[2]; bz = lo[2]; dz = -1.0f; break;
            case 1: ax = hi[0]; bx = lo[0]; az = bz = lo[2] - 1.0f; dx = 1.0f; break;
            case 2: ax = bx = hi[0] + 1.0f; az = lo[2]; bz = hi[2]; dz = 1.0f; break;
            case 3: ax = lo[0]; bx = hi[0]; az = bz = hi[2] + 1.0f; dx = -1.0f; break;
            default: break;
        }
        if ((side & 0xff) < 4) {
            direction[0] = dx;
            direction[1] = 0.0f;
            direction[2] = dz;
        }
        point[0] = (ax - bx) * 0.5f + bx;
        point[1] = 0.0f;
        point[2] = (az - bz) * 0.5f + bz;
        if (host->HasNetworkAt(AutomataFloorIdiom(point[0] * 0.0625f), AutomataFloorIdiom(point[2] * 0.0625f))) {
            valid = true;
            break;
        }
        side = (side + 1) & 3;
    }
}

}  // namespace sc4
