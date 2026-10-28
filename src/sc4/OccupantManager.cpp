#include "sc4/OccupantManager.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

#include "sc4/SCPropertyHelp.h"

// The coordinate conversions reproduce the original's float arithmetic step by step.
#pragma clang fp contract(off)

namespace sc4 {

namespace {
// round-to-nearest-even through the 1.5 * 2^23 trick the original uses
int RoundMagic(float v) {
    float biased = v + 12582912.0f;
    std::int32_t bits;
    std::memcpy(&bits, &biased, sizeof bits);
    return bits - 0x4b400000;
}
int ClampCell(int v) { return v < 0 ? 0 : (v > kWorldCells - 1 ? kWorldCells - 1 : v); }
}  // namespace

// ---- filters ---------------------------------------------------------------------------------------
bool cSC4OccupantFilter::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSerializable) *out = static_cast<cIGZSerializable*>(this);
    else if (iid == 1 || iid == kGZIID_cISC4OccupantFilter) *out = static_cast<cISC4OccupantFilter*>(this);
    else return false;
    AddRef();
    return true;
}
std::uint32_t cSC4OccupantFilter::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}
bool cSC4OccupantFilter::Write(cIGZOStream& s) const { s.SetUint16(3); return !s.GetError(); }
bool cSC4OccupantFilter::Read(cIGZIStream& s) {
    std::uint16_t v = 0;
    s.GetUint16(v);
    return v == 3 && !s.GetError();
}
bool cSC4OccupantTypeFilter::Write(cIGZOStream& s) const {
    s.SetUint16(3);
    s.SetUint16(3);
    if (!s.GetError()) s.SetUint32(type_);
    return !s.GetError();
}
bool cSC4OccupantTypeFilter::Read(cIGZIStream& s) {
    std::uint16_t v = 0;
    s.GetUint16(v);
    if (v != 3) return false;
    s.GetUint16(v);
    if (v == 3 && !s.GetError()) s.GetUint32(type_);
    return !s.GetError();
}
bool cSC4DefaultOccupantFilter::Write(cIGZOStream& s) const {
    s.SetUint16(3);
    s.SetUint16(3);
    return !s.GetError();
}
bool cSC4DefaultOccupantFilter::Read(cIGZIStream& s) {
    std::uint16_t v = 0;
    s.GetUint16(v);
    if (v != 3) return false;
    s.GetUint16(v);
    return v == 3 && !s.GetError();
}
bool cSC4DefaultOccupantFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    bool included = true;
    void* raw = nullptr;
    if (occupant->QueryInterface(0x4a10afc2, &raw)) { static_cast<cIGZUnknown*>(raw)->Release(); included = false; }
    if (occupant->QueryInterface(0x8a139184, &raw)) { static_cast<cIGZUnknown*>(raw)->Release(); included = false; }
    if (occupant->IsOccupantGroup(0x4217) || occupant->IsOccupantGroup(0x4208) || occupant->IsOccupantGroup(0x421d))
        return false;
    if (!included) return false;
    return IsPropertyHolderIncluded(occupant->AsPropertyHolder());
}
bool cSC4DefaultOccupantFilter::IsPropertyHolderIncluded(cISCPropertyHolder* holder) {
    if (holder->HasProperty(0x49512a09) || holder->HasProperty(0x8a15f1bd)) return false;
    bool flagged = false;
    cSCPropertyHelp::GetPropertyValue(holder, 0xcaa45670, flagged);
    return !flagged;
}

// ---- lifetime ----------------------------------------------------------------------------------------
cSC4OccupantManager::~cSC4OccupantManager() {
    for (auto& column : grid_)
        for (Cell* cell : column) delete cell;
}

std::uint32_t cSC4OccupantManager::AddRef() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return ++refCount_;
}

std::uint32_t cSC4OccupantManager::Release() {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (refCount_ < 2) {
        lock.unlock();
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cSC4OccupantManager::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSerializable) *out = static_cast<cIGZSerializable*>(this);
    else if (iid == kGZIID_cIGZMessageTarget2) *out = static_cast<cIGZMessageTarget2*>(this);
    else if (iid == 1 || iid == kGZIID_cISC4OccupantManager) *out = static_cast<cISC4OccupantManager*>(this);
    else return false;
    AddRef();
    return true;
}

bool cSC4OccupantManager::Init() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!initialized_) {
        initialized_ = true;
        if (!filter_) {
            filter_ = new cSC4OccupantTypeFilter();
            filter_->AddRef();
        }
        if (grid_.empty()) {
            grid_.assign(kWorldCells, std::vector<Cell*>(kWorldCells, nullptr));
        } else {   // grid came from a save: let every occupant (re)initialise
            IterateOccupants(&InitOccupantCallback, this, nullptr, nullptr, std::uint32_t{0});
        }
    }
    return true;
}

bool cSC4OccupantManager::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (initialized_) {
        RemoveOccupants(static_cast<cISC4OccupantFilter*>(nullptr), true, 0);
        if (filter_) {
            filter_->Release();
            filter_ = nullptr;
        }
        initialized_ = false;
    }
    return true;
}

bool cSC4OccupantManager::SetCitySize(float x, float z) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (initialized_) return false;
    sizeX_ = x;
    sizeZ_ = z;
    cellsX_ = static_cast<int>(x * 0.015625f);
    cellsZ_ = static_cast<int>(z * 0.015625f);
    standardX_ = static_cast<int>(x * 0.0625f);
    standardZ_ = static_cast<int>(z * 0.0625f);
    return true;
}

bool cSC4OccupantManager::DoMessage(cIGZMessage2* message) {
    message->GetType();
    return true;
}

// ---- coordinate conversions ---------------------------------------------------------------------------
void cSC4OccupantManager::GetOccupantManagerCellBounds(int& x0, int& z0, int& x1, int& z1) const {
    x0 = z0 = 0x40;
    x1 = cellsX_ + 0x3f;
    z1 = cellsZ_ + 0x3f;
}
void cSC4OccupantManager::WorldCellToStandardCityCell(int x, int z, int& ox, int& oz) const {
    // (sic) the original ignores the first argument for ox and multiplies the output's old value for oz
    ox = z * 4 - 0x100;
    oz = oz << 2;
    (void)x;
}
void cSC4OccupantManager::StandardCityCellToWorldCell(int x, int z, int& ox, int& oz) const {
    ox = ((x + static_cast<int>(static_cast<std::uint32_t>(x >> 31) >> 30)) >> 2) + 0x40;
    oz = ((z + static_cast<int>(static_cast<std::uint32_t>(z >> 31) >> 30)) >> 2) + 0x40;
}
void cSC4OccupantManager::WorldCellToPosition(int x, int z, float& px, float& pz) const {
    px = static_cast<float>(x * 0x40 - 0x1000);
    pz = static_cast<float>(z * 0x40 - 0x1000);
}
void cSC4OccupantManager::PositionToWorldCell(float px, float pz, int& x, int& z) const {
    float vx = (px + 4096.0f) * 0.015625f;
    vx = vx + -0.499f;
    float vz = (pz + 4096.0f) * 0.015625f;
    vz = vz + -0.499f;
    x = RoundMagic(vx);
    z = RoundMagic(vz);
}
void cSC4OccupantManager::OccupantManagerCellToPosition(int x, int z, float& px, float& pz) const {
    px = static_cast<float>(x << 6);
    pz = static_cast<float>(z << 6);
}
void cSC4OccupantManager::PositionToOccupantManagerCell(float px, float pz, int& x, int& z) const {
    float vx = px * 0.015625f;
    vx = vx + -0.499f;
    float vz = pz * 0.015625f;
    vz = vz + -0.499f;
    x = RoundMagic(vx);
    z = RoundMagic(vz);
}
void cSC4OccupantManager::StandardCityCellToPosition(int x, int z, float& px, float& pz) const {
    px = static_cast<float>(x << 4);
    pz = static_cast<float>(z << 4);
}
void cSC4OccupantManager::PositionToStandardCityCell(float px, float pz, int& x, int& z) const {
    auto floorCell = [](float p) {
        const float scaled = p * 0.0625f;
        const float biased = scaled + -0.5f;
        const int rounded = RoundMagic(biased);
        const int next = rounded + 1;
        if (std::isnan(scaled)) return rounded;
        return scaled == static_cast<float>(next) ? next : rounded;
    };
    x = floorCell(px);
    z = floorCell(pz);
}

// ---- grid access ----------------------------------------------------------------------------------------
cSC4OccupantManager::Cell* cSC4OccupantManager::CellAt(int x, int z) const {
    if (x < 0 || z < 0 || x >= static_cast<int>(grid_.size()) || z >= kWorldCells) return nullptr;
    return grid_[x][z];
}

cSC4OccupantManager::Cell* cSC4OccupantManager::CellAtClamped(int x, int z, bool create) {
    if (grid_.empty()) return nullptr;
    x = ClampCell(x);
    z = ClampCell(z);
    Cell*& cell = grid_[x][z];
    if (!cell && create) cell = new Cell();
    return cell;
}

bool cSC4OccupantManager::IsCellEmpty(int x, int z) const {
    if (static_cast<unsigned>(x) >= kWorldCells || static_cast<unsigned>(z) >= kWorldCells) return true;
    const Cell* cell = CellAt(x, z);
    return !cell || cell->empty();
}

void cSC4OccupantManager::EraseFromCell(int x, int z, cISC4Occupant* occupant) {
    Cell* cell = CellAt(x, z);
    if (!cell) return;
    for (auto it = cell->begin(); it != cell->end(); ++it) {
        if ((*it & ~std::uintptr_t{1}) == reinterpret_cast<std::uintptr_t>(occupant)) {
            cell->erase(it);
            if (cell->empty()) {
                delete cell;
                grid_[x][z] = nullptr;
            }
            return;
        }
    }
}

// Occupant bounding cells: the box is inset by 0.1 when wider than one metre so edge-touching boxes
// do not spill into the neighbouring cell.
void cSC4OccupantManager::ComputeCells(cISC4Occupant* occupant, std::uint8_t cells[4]) const {
    float lo[3], hi[3];
    occupant->GetBoundingBox(lo, hi);
    if (1.0f < hi[0] - lo[0]) { lo[0] = lo[0] + 0.1f; hi[0] = hi[0] + -0.1f; }
    if (1.0f < hi[2] - lo[2]) { lo[2] = lo[2] + 0.1f; hi[2] = hi[2] + -0.1f; }
    int x, z;
    PositionToWorldCell(lo[0], lo[2], x, z);
    cells[0] = static_cast<std::uint8_t>(x);
    cells[1] = static_cast<std::uint8_t>(z);
    PositionToWorldCell(hi[0], hi[2], x, z);
    cells[2] = static_cast<std::uint8_t>(x);
    cells[3] = static_cast<std::uint8_t>(z);
}

static std::uintptr_t Tagged(cISC4Occupant* o, const std::uint8_t c[4]) {
    const bool single = c[0] >= c[2] && c[3] <= c[1];
    return reinterpret_cast<std::uintptr_t>(o) + (single ? 1 : 0);
}

// ---- insert / remove / move --------------------------------------------------------------------------------
bool cSC4OccupantManager::InsertOccupant(cISC4Occupant* occupant, std::uint32_t data) {
    if (!occupant || occupant->IsFlagSet(1)) return false;
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    occupant->AddRef();
    occupant->Init();
    if (!occupant->AsPropertyHolder() || grid_.empty()) {
        lock.unlock();
        occupant->Release();
        return false;
    }
    std::uint8_t cells[4];
    ComputeCells(occupant, cells);
    occupant->SetOccupantManagerBBox(cells);
    const std::uintptr_t tagged = Tagged(occupant, cells);
    bool checkDuplicate = true;
    for (int x = cells[0]; x <= cells[2]; ++x) {
        for (int z = cells[1]; z <= cells[3]; ++z) {
            Cell* cell = CellAtClamped(x, z, true);
            if (!cell) continue;
            if (checkDuplicate) {
                for (std::uintptr_t node : *cell) {
                    if ((node & ~std::uintptr_t{1}) == reinterpret_cast<std::uintptr_t>(occupant)) {
                        lock.unlock();
                        occupant->Release();
                        return false;     // already inserted
                    }
                }
                checkDuplicate = false;
            }
            cell->push_back(tagged);
        }
    }
    lock.unlock();
    occupant->SetFlag(1);
    occupant->PostOccupantMessage(kMessageOccupantInserted, data);
    return true;
}

bool cSC4OccupantManager::RemoveOccupant(cISC4Occupant* occupant, bool shutdown, std::uint32_t data) {
    if (!occupant) return false;
    occupant->AddRef();
    bool removed = false;
    if (occupant->IsFlagSet(1)) {
        std::unique_lock<std::recursive_mutex> lock(mutex_);
        std::uint8_t cells[4];
        if (occupant->AsPropertyHolder() && occupant->GetOccupantManagerBBox(cells)) {
            for (int x = cells[0]; x <= cells[2]; ++x)
                for (int z = cells[1]; z <= cells[3]; ++z) EraseFromCell(x, z, occupant);
            removed = true;
        }
        lock.unlock();
        if (removed) {
            occupant->PostOccupantMessage(kMessageOccupantRemovedFromManager, data);
            occupant->ClearFlag(1);
            occupant->Release();     // the grid's reference
        }
    }
    if (shutdown) occupant->Shutdown();
    occupant->Release();
    return removed;
}

bool cSC4OccupantManager::RemoveOccupants(int type, bool shutdown, std::uint32_t data) {
    TypeFilter(static_cast<std::uint32_t>(type));
    return RemoveOccupants(static_cast<cISC4OccupantFilter*>(filter_), shutdown, data);
}

cSC4OccupantTypeFilter* cSC4OccupantManager::TypeFilter(std::uint32_t type) {
    if (filter_) filter_->SetType(type);
    return filter_;
}

bool cSC4OccupantManager::RemoveOccupants(cISC4OccupantFilter* filter, bool shutdown, std::uint32_t data) {
    std::vector<cISC4Occupant*> removed;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (int x = 0; x < static_cast<int>(grid_.size()); ++x) {
            for (int z = 0; z < kWorldCells; ++z) {
                Cell* cell = grid_[x][z];
                if (!cell) continue;
                std::vector<cISC4Occupant*> matches;
                for (std::uintptr_t node : *cell) {
                    auto* o = reinterpret_cast<cISC4Occupant*>(node & ~std::uintptr_t{1});
                    if (PassesFilter(filter, o)) matches.push_back(o);
                }
                for (cISC4Occupant* o : matches) {
                    std::uint8_t cells[4];
                    if (o->AsPropertyHolder() && o->GetOccupantManagerBBox(cells)) {
                        for (int cx = cells[0]; cx <= cells[2]; ++cx)
                            for (int cz = cells[1]; cz <= cells[3]; ++cz) EraseFromCell(cx, cz, o);
                        removed.push_back(o);
                        o->ClearFlag(1);
                    }
                }
            }
        }
    }
    for (cISC4Occupant* o : removed) {
        o->PostOccupantMessage(kMessageOccupantRemovedFromManager, data);
        if (shutdown) o->Shutdown();
        o->Release();     // the grid's reference
    }
    return true;
}

bool cSC4OccupantManager::MoveOccupant(cISC4Occupant* occupant, bool post) {
    if (!occupant || !occupant->IsFlagSet(1)) return false;
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    std::uint8_t old[4];
    bool ok = false;
    if (occupant->AsPropertyHolder() && occupant->GetOccupantManagerBBox(old)) {
        std::uint8_t now[4];
        ComputeCells(occupant, now);
        occupant->SetOccupantManagerBBox(now);
        ok = true;
        if (std::memcmp(old, now, 4) != 0) {
            const std::uintptr_t tagged = Tagged(occupant, now);
            auto inside = [](int x, int z, const std::uint8_t b[4]) { return x >= b[0] && x <= b[2] && z >= b[1] && z <= b[3]; };
            for (int x = old[0]; x <= old[2]; ++x)
                for (int z = old[1]; z <= old[3]; ++z)
                    if (!inside(x, z, now)) EraseFromCell(x, z, occupant);
            for (int x = now[0]; x <= now[2]; ++x)
                for (int z = now[1]; z <= now[3]; ++z)
                    if (!inside(x, z, old))
                        if (Cell* cell = CellAtClamped(x, z, true)) cell->push_back(tagged);
            // cells kept by both boxes get the (possibly changed) single-cell tag
            for (int x = std::max(old[0], now[0]); x <= std::min(old[2], now[2]); ++x)
                for (int z = std::max(old[1], now[1]); z <= std::min(old[3], now[3]); ++z)
                    if (Cell* cell = CellAt(x, z))
                        for (std::uintptr_t& node : *cell)
                            if ((node & ~std::uintptr_t{1}) == reinterpret_cast<std::uintptr_t>(occupant)) { node = tagged; break; }
        }
    }
    lock.unlock();
    if (ok && post) occupant->PostOccupantMessage(kMessageOccupantRemoved, 0);
    return ok;
}

int cSC4OccupantManager::GetBoundingCells(int x, int z, int (*cells)[2], int& count) {
    count = 0;
    if (static_cast<unsigned>(x - 1) < 0xbe && static_cast<unsigned>(z - 1) < 0xbe) {
        static const int kOffsets[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};
        for (const auto& o : kOffsets) {
            cells[count][0] = x + o[0];
            cells[count][1] = z + o[1];
            ++count;
        }
        return count;
    }
    for (int cx = x - 1; cx <= x + 1; ++cx) {
        for (int cz = z - 1; cz <= z + 1; ++cz) {
            if (static_cast<unsigned>(cz) < 0xc0 && static_cast<unsigned>(cx) < 0xc0 && !(cx == x && cz == z)) {
                cells[count][0] = cx;
                cells[count][1] = cz;
                ++count;
            }
        }
    }
    return count;
}

bool cSC4OccupantManager::FindOccupant(cISC4Occupant* occupant, bool searchAll, int* outX, int* outZ) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    int x = 0, z = 0;
    if (occupant->AsPropertyHolder()) {
        std::uint8_t cells[4];
        if (occupant->GetOccupantManagerBBox(cells)) {
            x = cells[0];
            z = cells[1];
        } else {
            float lo[3], hi[3];
            occupant->GetBoundingBox(lo, hi);
            if (1.0f < hi[0] - lo[0]) { lo[0] = lo[0] + 0.1f; hi[0] = hi[0] + -0.1f; }
            PositionToWorldCell(lo[0], lo[2], x, z);
        }
    }
    if (Cell* cell = CellAtClamped(x, z, false)) {
        for (std::uintptr_t node : *cell)
            if ((node & ~std::uintptr_t{1}) == reinterpret_cast<std::uintptr_t>(occupant)) return true;
    }
    if (!searchAll) return false;
    for (int cx = 0; cx < static_cast<int>(grid_.size()); ++cx) {
        for (int cz = 0; cz < kWorldCells; ++cz) {
            const Cell* cell = grid_[cx][cz];
            if (!cell) continue;
            for (std::uintptr_t node : *cell) {
                if ((node & ~std::uintptr_t{1}) == reinterpret_cast<std::uintptr_t>(occupant)) {
                    if (outX) *outX = cx;
                    if (outZ) *outZ = cz;
                    return true;
                }
            }
        }
    }
    return false;
}

// ---- single-result queries -----------------------------------------------------------------------------------
bool cSC4OccupantManager::GetFirstOccupantByPosition(cISC4Occupant*& out, float x, float z, std::uint32_t type) {
    if (!initialized_) return false;
    return GetFirstOccupantByPosition(out, x, z, static_cast<cISC4OccupantFilter*>(TypeFilter(type)));
}

bool cSC4OccupantManager::GetFirstOccupantByPosition(cISC4Occupant*& out, float x, float z, cISC4OccupantFilter* f) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!initialized_) return false;
    int cx, cz;
    PositionToWorldCell(x, z, cx, cz);
    const Cell* cell = CellAt(cx, cz);
    if (!cell) return false;
    for (std::uintptr_t node : *cell) {
        auto* o = reinterpret_cast<cISC4Occupant*>(node & ~std::uintptr_t{1});
        if (!PassesFilter(f, o)) continue;
        float lo[3], hi[3];
        o->GetBoundingBox(lo, hi);
        if (z < hi[2] && x < hi[0] && lo[0] <= x && lo[2] <= z) {
            out = o;
            o->AddRef();
            return true;
        }
    }
    return false;
}

bool cSC4OccupantManager::GetFirstOccupant(cISC4Occupant*& out, int x, int z, std::uint32_t type) {
    if (!initialized_) return false;
    return GetFirstOccupant(out, x, z, static_cast<cISC4OccupantFilter*>(TypeFilter(type)));
}

bool cSC4OccupantManager::GetFirstOccupant(cISC4Occupant*& out, int x, int z, cISC4OccupantFilter* f) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!initialized_ || static_cast<unsigned>(x) >= kWorldCells || static_cast<unsigned>(z) >= kWorldCells) return false;
    const Cell* cell = CellAt(x, z);
    if (!cell) return false;
    for (std::uintptr_t node : *cell) {
        auto* o = reinterpret_cast<cISC4Occupant*>(node & ~std::uintptr_t{1});
        if (PassesFilter(f, o)) {
            out = o;
            o->AddRef();
            return true;
        }
    }
    return false;
}

bool cSC4OccupantManager::GetFirstOccupantByStandardCityCell(cISC4Occupant*& out, int x, int z, std::uint32_t type) {
    if (!initialized_) return false;
    return GetFirstOccupantByStandardCityCell(out, x, z, static_cast<cISC4OccupantFilter*>(TypeFilter(type)));
}

bool cSC4OccupantManager::GetFirstOccupantByStandardCityCell(cISC4Occupant*& out, int x, int z, cISC4OccupantFilter* f) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!initialized_) return false;
    const float px = static_cast<float>(x << 4), pz = static_cast<float>(z << 4);
    const float minX = px + 0.1f, minZ = pz + 0.1f;
    int cx, cz;
    PositionToWorldCell(minX, minZ, cx, cz);
    const Cell* cell = CellAt(cx, cz);
    if (!cell) return false;
    const float maxX = px + 16.0f + -0.1f, maxZ = pz + 16.0f + -0.1f;
    for (std::uintptr_t node : *cell) {
        auto* o = reinterpret_cast<cISC4Occupant*>(node & ~std::uintptr_t{1});
        if (!PassesFilter(f, o)) continue;
        float lo[3], hi[3];
        o->GetBoundingBox(lo, hi);
        if (lo[0] < maxX && minX < hi[0] && lo[2] < maxZ && minZ < hi[2]) {
            out = o;
            o->AddRef();
            return true;
        }
    }
    return false;
}

bool cSC4OccupantManager::GetFirstOccupantByStandardCityCells(cISC4Occupant*& out, const int* a, const int* b, std::uint32_t type) {
    if (!initialized_) return false;
    return GetFirstOccupantByStandardCityCells(out, a, b, static_cast<cISC4OccupantFilter*>(TypeFilter(type)));
}

bool cSC4OccupantManager::GetFirstOccupantByStandardCityCells(cISC4Occupant*& out, const int* a, const int* b, cISC4OccupantFilter* f) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!initialized_) return false;
    auto clampTo = [&](int v, int count) { return std::min(std::max(v, 0), count - 1); };
    int ax = 0, az = 0, bx, bz;
    if (a) { ax = clampTo(a[0], standardX_); az = clampTo(a[1], standardZ_); }
    if (b) { bx = clampTo(b[0], standardX_); bz = clampTo(b[1], standardZ_); }
    else { bx = standardX_; bz = standardZ_; }
    const int minPosX = ax << 4, minPosZ = az << 4;
    int cx0, cz0, cx1, cz1;
    PositionToWorldCell(static_cast<float>(ax << 4), static_cast<float>(az << 4), cx0, cz0);
    PositionToWorldCell(static_cast<float>(bx << 4), static_cast<float>(bz << 4), cx1, cz1);
    for (int cx = cx0; cx <= cx1; ++cx) {
        for (int cz = cz0; cz <= cz1; ++cz) {
            const Cell* cell = CellAt(cx, cz);
            if (!cell) continue;
            for (std::uintptr_t node : *cell) {
                auto* o = reinterpret_cast<cISC4Occupant*>(node & ~std::uintptr_t{1});
                if (!PassesFilter(f, o)) continue;
                float lo[3], hi[3];
                o->GetBoundingBox(lo, hi);
                if (lo[0] < static_cast<float>(bx * 0x10 + 0x10) + -0.1f && static_cast<float>(minPosX) + 0.1f < hi[0] &&
                    lo[2] < static_cast<float>(bz * 0x10 + 0x10) + -0.1f && static_cast<float>(minPosZ) + 0.1f < hi[2]) {
                    out = o;
                    o->AddRef();
                    return true;
                }
            }
        }
    }
    return false;
}

// ---- list queries ------------------------------------------------------------------------------------------------
bool cSC4OccupantManager::GetOccupantsByBBox(OccupantList& out, const float* min, const float* max, std::uint32_t type, std::uint32_t maxCount) {
    if (!initialized_) return false;
    cISC4OccupantFilter* f = nullptr;
    if (type != 0) f = TypeFilter(type);
    return GetOccupantsByBBox(out, min, max, f, maxCount);
}

bool cSC4OccupantManager::CollectByBBox(OccupantList& out, const float* min, const float* max, cISC4OccupantFilter* f, std::uint32_t maxCount) {
    float minX = 0.0f, minY = -FLT_MAX, minZ = 0.0f;
    if (min) { minX = min[0]; minY = min[1]; minZ = min[2]; }
    float maxX = sizeX_, maxY = FLT_MAX, maxZ = sizeZ_;
    if (max) { maxX = max[0]; maxY = max[1]; maxZ = max[2]; }
    int x0, z0, x1, z1;
    PositionToWorldCell(minX, minZ, x0, z0);
    PositionToWorldCell(maxX, maxZ, x1, z1);
    x0 = ClampCell(x0); z0 = ClampCell(z0); x1 = ClampCell(x1); z1 = ClampCell(z1);
    std::unordered_set<cISC4Occupant*> seen;
    std::uint32_t count = 0;
    auto overlaps = [&](cISC4Occupant* o) {
        float lo[3], hi[3];
        o->GetBoundingBox(lo, hi);
        return lo[0] < maxX && minX < hi[0] && lo[1] < maxY && minY < hi[1] && lo[2] < maxZ && minZ < hi[2];
    };
    for (int x = x0; x <= x1; ++x) {
        for (int z = z0; z <= z1; ++z) {
            const Cell* cell = CellAt(x, z);
            if (!cell) continue;
            for (std::uintptr_t node : *cell) {
                auto* o = reinterpret_cast<cISC4Occupant*>(node & ~std::uintptr_t{1});
                const bool first = (x0 == x1 && z0 == z1) || (node & 1) != 0 || seen.insert(o).second;
                if (first && PassesFilter(f, o) && overlaps(o)) {
                    o->AddRef();
                    out.push_back(o);
                    if (++count >= maxCount) return true;
                }
            }
        }
    }
    return true;
}

bool cSC4OccupantManager::GetOccupantsByBBox(OccupantList& out, const float* min, const float* max, cISC4OccupantFilter* f, std::uint32_t maxCount) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!initialized_ || maxCount == 0) return false;
    return CollectByBBox(out, min, max, f, maxCount);
}

bool cSC4OccupantManager::GetOccupantsByOccupantManagerCells(OccupantList& out, const int* a, const int* b, std::uint32_t type, std::uint32_t maxCount) {
    if (!initialized_) return false;
    cISC4OccupantFilter* f = nullptr;
    if (type != 0) f = TypeFilter(type);
    return GetOccupantsByOccupantManagerCells(out, a, b, f, maxCount);
}

bool cSC4OccupantManager::GetOccupantsByOccupantManagerCells(OccupantList& out, const int* a, const int* b, cISC4OccupantFilter* f, std::uint32_t maxCount) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!initialized_ || maxCount == 0) return false;
    int x0 = 0x40, z0 = 0x40, x1 = cellsX_ + 0x3f, z1 = cellsZ_ + 0x3f;
    if (a) { x0 = ClampCell(a[0]); z0 = ClampCell(a[1]); }
    if (b) { x1 = ClampCell(b[0]); z1 = ClampCell(b[1]); }
    std::unordered_set<cISC4Occupant*> seen;
    const bool single = x0 == x1 && z0 == z1;
    std::uint32_t count = 0;
    for (int x = x0; x <= x1; ++x) {
        for (int z = z0; z <= z1; ++z) {
            const Cell* cell = CellAt(x, z);
            if (!cell) continue;
            for (std::uintptr_t node : *cell) {
                auto* o = reinterpret_cast<cISC4Occupant*>(node & ~std::uintptr_t{1});
                if (!(single || (node & 1) != 0 || seen.insert(o).second)) continue;
                if (!PassesFilter(f, o)) continue;
                o->AddRef();
                out.push_back(o);
                if (++count >= maxCount) return true;
            }
        }
    }
    return true;
}

bool cSC4OccupantManager::GetOccupantsByStandardCityCells(OccupantList& out, const int* a, const int* b, std::uint32_t type, std::uint32_t maxCount) {
    if (!initialized_) return false;
    cISC4OccupantFilter* f = nullptr;
    if (type != 0) f = TypeFilter(type);
    return GetOccupantsByStandardCityCells(out, a, b, f, maxCount);
}

namespace {
void StandardCellBox(const int* a, const int* b, int countX, int countZ, float lo[3], float hi[3]) {
    int ax = 0, az = 0, bx = 0, bz = 0;
    if (a) {
        ax = std::min(std::max(a[0], 0), countX - 1);
        az = std::min(std::max(a[1], 0), countZ - 1);
    }
    if (b) {
        bx = std::min(std::max(b[0], 0), countX - 1);
        bz = std::min(std::max(b[1], 0), countZ - 1);
    } else {
        bx = countX;
        bz = countZ;
    }
    lo[0] = static_cast<float>(ax << 4) + 0.1f;
    lo[2] = static_cast<float>(az << 4) + 0.1f;
    hi[0] = static_cast<float>((bx + 1) << 4) + -0.1f;
    hi[2] = static_cast<float>((bz + 1) << 4) + -0.1f;
    lo[1] = -FLT_MAX;
    hi[1] = FLT_MAX;
}
}  // namespace

bool cSC4OccupantManager::GetOccupantsByStandardCityCells(OccupantList& out, const int* a, const int* b, cISC4OccupantFilter* f, std::uint32_t maxCount) {
    if (!initialized_) return false;
    float lo[3], hi[3];
    StandardCellBox(a, b, standardX_, standardZ_, lo, hi);
    return GetOccupantsByBBox(out, lo, hi, f, maxCount);
}

// ---- iteration ------------------------------------------------------------------------------------------------------
bool cSC4OccupantManager::IterateOccupantsByBBox(OccupantCallback cb, void* ctx, const float* min, const float* max, std::uint32_t type) {
    if (!initialized_) return false;
    cISC4OccupantFilter* f = nullptr;
    if (type != 0) f = TypeFilter(type);
    return IterateOccupantsByBBox(cb, ctx, min, max, f);
}

bool cSC4OccupantManager::IterateOccupantsByBBox(OccupantCallback cb, void* ctx, const float* min, const float* max, cISC4OccupantFilter* f) {
    if (!initialized_) return true;
    // Snapshot under the lock, call back without it (callbacks may modify the city).
    OccupantList snapshot;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        CollectByBBox(snapshot, min, max, f, 0xffffffffu);
    }
    bool go = true;
    for (cISC4Occupant* o : snapshot) {
        if (go && !cb(o, ctx)) go = false;
        o->Release();
    }
    snapshot.clear();
    return true;
}

bool cSC4OccupantManager::IterateCells(int x0, int z0, int x1, int z1, OccupantCallback cb, void* ctx, cISC4OccupantFilter* f) {
    std::unordered_set<cISC4Occupant*> seen;
    const bool single = x0 == x1 && z0 == z1;
    bool go = true;
    for (int x = x0; x <= x1 && go; ++x) {
        for (int z = z0; z <= z1 && go; ++z) {
            OccupantList batch;
            {
                std::lock_guard<std::recursive_mutex> lock(mutex_);
                if (const Cell* cell = CellAt(x, z)) {
                    for (std::uintptr_t node : *cell) {
                        auto* o = reinterpret_cast<cISC4Occupant*>(node & ~std::uintptr_t{1});
                        if (!(single || (node & 1) != 0 || seen.insert(o).second)) continue;
                        if (!PassesFilter(f, o)) continue;
                        o->AddRef();
                        batch.push_back(o);
                    }
                }
            }
            for (cISC4Occupant* o : batch) {
                if (go && !cb(o, ctx)) go = false;
                o->Release();
            }
        }
    }
    return true;
}

bool cSC4OccupantManager::IterateOccupants(OccupantCallback cb, void* ctx, const int* a, const int* b, std::uint32_t type) {
    if (!initialized_) return false;
    cISC4OccupantFilter* f = nullptr;
    if (type != 0) f = TypeFilter(type);
    return IterateOccupants(cb, ctx, a, b, f);
}

bool cSC4OccupantManager::IterateOccupants(OccupantCallback cb, void* ctx, const int* a, const int* b, cISC4OccupantFilter* f) {
    if (!initialized_) return true;
    int x0 = 0x40, z0 = 0x40, x1 = cellsX_ + 0x3f, z1 = cellsZ_ + 0x3f;
    // (sic) the corner arrays are read as x = [0], z = [2], like xyz vectors
    if (a) { x0 = ClampCell(a[0]); z0 = ClampCell(a[2]); }
    if (b) { x1 = ClampCell(b[0]); z1 = ClampCell(b[2]); }
    return IterateCells(x0, z0, x1, z1, cb, ctx, f);
}

bool cSC4OccupantManager::IterateOccupantsByStandardCityCell(OccupantCallback cb, void* ctx, const int* a, const int* b, std::uint32_t type) {
    if (!initialized_) return false;
    cISC4OccupantFilter* f = nullptr;
    if (type != 0) f = TypeFilter(type);
    return IterateOccupantsByStandardCityCell(cb, ctx, a, b, f);
}

bool cSC4OccupantManager::IterateOccupantsByStandardCityCell(OccupantCallback cb, void* ctx, const int* a, const int* b, cISC4OccupantFilter* f) {
    if (!initialized_) return true;
    float lo[3], hi[3];
    StandardCellBox(a, b, standardX_, standardZ_, lo, hi);
    return IterateOccupantsByBBox(cb, ctx, lo, hi, f);
}

void cSC4OccupantManager::ReleaseOccupantList(OccupantList& list) {
    for (cISC4Occupant* o : list) o->Release();
    list.clear();
}

// ---- persistence ------------------------------------------------------------------------------------------------------
bool cSC4OccupantManager::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    stream.SetFloat32(sizeX_);
    stream.SetFloat32(sizeZ_);
    stream.SetSint32(cellsX_);
    stream.SetSint32(cellsZ_);
    stream.SetSint32(standardX_);
    stream.SetSint32(standardZ_);
    stream.SetUint32(static_cast<std::uint32_t>(grid_.size()));
    for (const auto& column : grid_) {
        stream.SetUint32(static_cast<std::uint32_t>(column.size()));
        for (const Cell* cell : column) {
            if (!cell || cell->empty()) {
                stream.SetUint32(0);
                continue;
            }
            stream.SetUint32(static_cast<std::uint32_t>(cell->size()));
            for (std::uintptr_t node : *cell)
                SC4WriteGZSerializable(stream, reinterpret_cast<cIGZUnknown*>(node & ~std::uintptr_t{1}));
        }
    }
    return !stream.GetError();
}

bool cSC4OccupantManager::Read(cIGZIStream& stream) {
    loaded_ = true;
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 1) return false;
    stream.GetFloat32(sizeX_);
    stream.GetFloat32(sizeZ_);
    stream.GetSint32(cellsX_);
    stream.GetSint32(cellsZ_);
    stream.GetSint32(standardX_);
    stream.GetSint32(standardZ_);
    std::uint32_t columns = 0;
    stream.GetUint32(columns);
    for (auto& column : grid_)
        for (Cell* cell : column) delete cell;
    grid_.clear();
    grid_.resize(columns);
    std::unordered_set<cISC4Occupant*> seen;
    for (std::uint32_t x = 0; x < columns; ++x) {
        std::uint32_t rows = 0;
        stream.GetUint32(rows);
        grid_[x].assign(rows, nullptr);
        for (std::uint32_t z = 0; z < rows; ++z) {
            std::uint32_t count = 0;
            stream.GetUint32(count);
            if (count == 0) continue;
            auto* cell = new Cell();
            grid_[x][z] = cell;
            for (std::uint32_t i = 0; i < count; ++i) {
                void* raw = nullptr;
                if (!SC4ReadGZSerializable(stream, kGZIID_cISC4Occupant, &raw, false)) continue;
                auto* o = static_cast<cISC4Occupant*>(raw);
                std::uint8_t c[4];
                const bool single = o->GetOccupantManagerBBox(c) && c[0] >= c[2] && c[3] <= c[1];
                cell->push_back(reinterpret_cast<std::uintptr_t>(o) + (single ? 1 : 0));
                if (!single && !seen.insert(o).second) o->Release();   // already loaded from another cell
            }
        }
    }
    return !stream.GetError();
}

// ------------------------------------------------------------------------------------- cell lists

cSC4OccupantManager::Cell* cSC4OccupantManager::GetOccupantListNoClip(int x, int z, bool create) {
    if (x < 0 || z < 0 || x >= static_cast<int>(grid_.size()) || z >= static_cast<int>(grid_[x].size())) return nullptr;   // (the original does not check)
    Cell*& cell = grid_[x][z];
    if (!cell && create) cell = new Cell();
    return cell;
}

cSC4OccupantManager::Cell* cSC4OccupantManager::GetOccupantList(int x, int z, bool create) {
    return CellAtClamped(x, z, create);
}

cSC4OccupantManager::Cell* cSC4OccupantManager::GetOccupantList(cISC4Occupant* occupant, int& x, int& z) {
    if (occupant->AsPropertyHolder()) {
        std::uint8_t cells[4];
        if (!occupant->GetOccupantManagerBBox(cells)) {
            float lo[3], hi[3];
            occupant->GetBoundingBox(lo, hi);
            if (1.0f < hi[0] - lo[0]) {                 // only the x extent is shrunk here (ComputeCells shrinks z too)
                lo[0] = lo[0] + 0.1f;
                hi[0] = hi[0] + -0.1f;
            }
            PositionToWorldCell(lo[0], lo[2], x, z);
        } else {
            x = cells[0];
            z = cells[1];
        }
    }
    return GetOccupantList(x, z, false);
}

// ------------------------------------------------------------------------------- InternalOccupantHashSet

cSC4OccupantManager::InternalOccupantHashSet::InternalOccupantHashSet() = default;

bool cSC4OccupantManager::InternalOccupantHashSet::RehashVectorToHashTable() {
    std::swap(table, previous);
    std::swap(tableSize, previousSize);
    table.resize(0x11);
    std::fill(table.begin(), table.end(), nullptr);
    for (void* entry : previous) {
        const std::uint64_t key = reinterpret_cast<std::uint64_t>(entry);
        const std::uint64_t bucket = key % bucketCount;
        nodes.push_front(HashNode{static_cast<HashNode*>(table[bucket]), key});
        table[bucket] = &nodes.front();
    }
    return true;
}

bool cSC4OccupantManager::InternalOccupantHashSet::RehashHashTable() {
    bucketCount = bucketCount * 2 + 1;
    std::swap(table, previous);
    std::swap(tableSize, previousSize);
    table.resize(bucketCount);
    std::fill(table.begin(), table.end(), nullptr);
    for (void* head : previous) {
        HashNode* node = static_cast<HashNode*>(head);
        while (node) {
            HashNode* next = node->next;
            const std::uint64_t bucket = node->key % bucketCount;
            node->next = static_cast<HashNode*>(table[bucket]);
            table[bucket] = node;
            node = next;
        }
    }
    entryCount = 0;
    return true;
}

}  // namespace sc4
