#include "sc4/cSC4FlammabilitySimulator.h"

#include <algorithm>
#include <cmath>

#include "sc4/DBSegment.h"
#include "sc4/SCPropertyHelp.h"

namespace sc4 {

namespace {
// round-half-even of a float product, as the original's "+ 12582912.0" trick does
int RoundEven(float value) { return static_cast<int>(std::nearbyintf(value)); }
const cGZPersistResourceKey kTunablesKey{0x6534284a, 0xe7e2c2db, 0xa8f41ee3};
}  // namespace

cSC4FlammabilitySimulator::cSC4FlammabilitySimulator(IFlammabilityHost* host) : host_(host) {}

// 0x1000c2caa: the original answers +0 for iid 1 / 0x48f41617, +8 for the message target and +0x10 for
// the serializable. The port keeps a single cIGZSerializable base, so every accepted id resolves to `this`;
// the serializable id is what the city-load serializer (SC4ReadGZSerializable) asks for.
bool cSC4FlammabilitySimulator::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 0xe4fda3d4) *out = static_cast<cIGZSerializable*>(this);
    else if (iid == 1 || iid == kGZIID_cISC4FlammabilitySimulator || iid == kFlammabilitySimulatorClassId)
        *out = static_cast<cIGZUnknown*>(this);
    else return false;
    AddRef();
    return true;
}

// 0x1000c2d3a: below 2 destroys (Shutdown, then the deleting destructor).
std::uint32_t cSC4FlammabilitySimulator::Release() {
    if (refCount_ < 2) {
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cSC4FlammabilitySimulator::Init() {
    if (initialized_) return true;
    initialized_ = true;
    ReadSimulatorTunableValues();
    host_->RegisterSimulator("cSC4FlammabilitySimulator");
    cityCellsX_ = host_->CitySizeXInCells();
    cityCellsZ_ = host_->CitySizeZInCells();
    if (!flammability_) {
        flammability_ = new cSC4SimGrid<std::uint8_t>();
        flammability_->Init();
        flammability_->SetTractSize(tractSize_);
        flammability_->SetInstanceID(kFlammabilityGridInstance);
        flammability_->SetTractValues(0);
    }
    if (!rawFlammability_) {
        rawFlammability_ = new cSC4SimGrid<std::uint8_t>();
        rawFlammability_->Init();
        rawFlammability_->SetTractSize(tractSize_);
        rawFlammability_->SetInstanceID(kRawFlammabilityGridInstance);
        rawFlammability_->SetTractValues(0);
    }
    const int tractsX = flammability_->GetTractCountX();
    const int tractsZ = flammability_->GetTractCountZ();
    cellsPerDay_ = (tractsZ * tractsX) / cellsPerDayDivisor_;
    cellWalk_.SetRect(0, 0, tractsX > 0 ? tractsX - 1 : 0, tractsZ > 0 ? tractsZ - 1 : 0);
    cellWalk_.SetModular(true);
    sumFlammability_ = 0;
    zonedCells_ = 0;
    return true;
}

bool cSC4FlammabilitySimulator::Shutdown() {
    if (!initialized_) return true;
    initialized_ = false;
    fireCurve_.reset();
    if (flammability_) {
        flammability_->Shutdown();
        flammability_ = nullptr;
    }
    if (rawFlammability_) {
        rawFlammability_->Shutdown();
        rawFlammability_ = nullptr;
    }
    host_->UnregisterSimulator();
    return true;
}

void cSC4FlammabilitySimulator::ReadSimulatorTunableValues() {
    cellsPerDayDivisor_ = 40;
    ordinanceRainFactor_ = 1.2f;
    civicFactor_ = 1.1f;
    noWaterFactor_ = 1.1f;
    tractSize_ = 1;
    mergeDistance_ = 8;
    hotspotThreshold_ = 196;
    minHotspotWeight_ = 16;
    randomFireChance_ = 0.01f;

    cISCPropertyHolder* tunables = host_->GetSimulatorTunables(kTunablesKey);
    if (!tunables) return;
    cSCPropertyHelp::GetPropertyValue(tunables, 0xc9b40128, ordinanceRainFactor_);
    cSCPropertyHelp::GetPropertyValue(tunables, 0x29b40134, civicFactor_);
    cSCPropertyHelp::GetPropertyValue(tunables, 0x29b4013b, noWaterFactor_);
    std::vector<std::uint32_t> triple;
    cSCPropertyHelp::GetPropertyValue(tunables, 0x29b4013c, triple);
    if (triple.size() == 3) {
        hotspotThreshold_ = static_cast<int>(triple[0]);
        mergeDistance_ = static_cast<int>(triple[1]);
        minHotspotWeight_ = static_cast<int>(triple[2]);
    }
    cSCPropertyHelp::GetPropertyValue(tunables, 0x29b4013e, randomFireChance_);
    fireCurve_.reset();
    if (tunables->HasProperty(0x29b4013d)) {
        fireCurve_ = std::make_unique<cSC4ResponseCurve>();
        cISCProperty* property = tunables->GetProperty(0x29b4013d);
        const cIGZVariant* value = property->GetPropertyValue();
        fireCurve_->SetResponseCurveData(value->RefFloat32(), value->GetCount());
    }
}

void cSC4FlammabilitySimulator::OnMessage(std::uint32_t type, ISimOccupant* occupant) {
    switch (type) {
        case kFlammabilityMsgOrdinanceChanged:
        case kFlammabilityMsgWeatherChanged: {
            float multiplier = host_->GetOrdinanceFlameResistance();
            if (host_->GetCurrentWeather() == 2) multiplier *= ordinanceRainFactor_;
            factor_ = multiplier;
            break;
        }
        case kFlammabilityMsgOccupantMoved:
            if (occupant) DoOccupantUpdate(occupant, false);
            break;
        case kMessageDailyTick:
            DoDailySimulation();
            break;
        case kFlammabilityMsgOccupantAdded:
        case kFlammabilityMsgOccupantChanged:
            if (occupant && !occupant->HasProperty(kPropertyNoFlammabilityFlag)) DoOccupantUpdate(occupant, true);
            break;
        case kFlammabilityMsgOccupantRemoved:
            if (occupant && !occupant->HasProperty(kPropertyNoFlammabilityFlag)) DoOccupantUpdate(occupant, false);
            break;
        default:
            break;
    }
}

void cSC4FlammabilitySimulator::DoDailySimulation() {
    for (int n = cellsPerDay_; n != 0; --n) {
        int x = 0, z = 0;
        // finish the pass (and start the next) whenever the walk has run out
        while (!cellWalk_.Next(x, z)) {
            EndOfCycle();
            cellWalk_.Reset(true);
        }
        const std::uint8_t raw = rawFlammability_->GetTractValue(x, z);
        int value = raw;
        if (raw != 0) {
            value = RoundEven(static_cast<float>(raw) * factor_);
            if (!host_->HasWaterAt(x, z)) value = RoundEven(static_cast<float>(value) * noWaterFactor_);
            if (value > 0xff) value = 0xff;
            flammability_->SetTractValue(x, z, static_cast<std::uint8_t>(value));
            if (hotspotThreshold_ < value) candidates_.push_back({x, z, 1});
        }
        if (host_->GetZoneTypeAt(x, z) != 0) {
            ++zonedCells_;
            sumFlammability_ += value;
        }
    }
}

std::uint8_t cSC4FlammabilitySimulator::GetOccupantRawFlammability(ISimOccupant* occupant) {
    float multiplier = 1.0f;
    const std::uint32_t type = occupant->GetOccupantType();
    if (type == kOccupantTypeCivic || type == kOccupantTypeBuilding) {
        multiplier = 1.0f;
        if (host_->GetLotZoneTypeOfOccupant(occupant) == 4) multiplier = civicFactor_;
    } else if (type != kOccupantTypeProp) {
        return 0;
    }
    std::int32_t flammability = 0;
    cSCPropertyHelp::GetPropertyValue(occupant->GetPropertyHolder(), kPropertyOccupantFlammability, flammability);
    const int rounded = RoundEven(static_cast<float>(flammability) * multiplier);
    return static_cast<std::uint8_t>(std::clamp(rounded, 0, 255));
}

void cSC4FlammabilitySimulator::DoOccupantUpdate(ISimOccupant* occupant, bool increaseOnly) {
    const std::uint32_t type = occupant->GetOccupantType();
    if (type != kOccupantTypeCivic && type != kOccupantTypeProp && type != kOccupantTypeBuilding) return;
    std::uint8_t occupantRaw = GetOccupantRawFlammability(occupant);
    if (occupantRaw == 0 && increaseOnly) return;

    SC4Rect rect = occupant->GetBoundingRect();
    const int shift = rawFlammability_->GetTractShift();
    rect.x0 <<= shift;
    rect.z0 <<= shift;
    rect.x1 <<= shift;
    rect.z1 <<= shift;
    if (rect.x0 < 0 || rect.z0 < 0 || rect.x1 >= cityCellsX_ || rect.z1 >= cityCellsZ_) {
        rect.x0 = std::max(rect.x0, 0);
        rect.z0 = std::max(rect.z0, 0);
        rect.x1 = std::min(rect.x1, cityCellsX_ - 1);
        rect.z1 = std::min(rect.z1, cityCellsZ_ - 1);
    }
    for (int x = rect.x0; x <= rect.x1; ++x) {
        for (int z = rect.z0; z <= rect.z1; ++z) {
            std::uint8_t cellRaw = occupantRaw;
            if (!increaseOnly) {
                cellRaw = ComputeTractRawFlammability(x, z);
                rawFlammability_->SetTractValue(x, z, cellRaw);
            } else if (rawFlammability_->GetTractValue(x, z) < occupantRaw) {
                rawFlammability_->SetTractValue(x, z, occupantRaw);
            }
            const int scaled = RoundEven(static_cast<float>(cellRaw) * factor_);
            flammability_->SetTractValue(x, z, static_cast<std::uint8_t>(std::clamp(scaled, 0, 255)));
        }
    }
}

std::uint8_t cSC4FlammabilitySimulator::ComputeTractRawFlammability(int tractX, int tractZ) {
    const SC4Rect rect{tractX * tractSize_, tractZ * tractSize_, tractSize_ - 1 + tractX * tractSize_,
                       tractSize_ - 1 + tractZ * tractSize_};
    const std::uint32_t types[3] = {kOccupantTypeBuilding, kOccupantTypeProp, kOccupantTypeCivic};
    std::uint8_t building = 0, civic = 0, other = 0;
    for (ISimOccupant* occupant : host_->GatherOccupants(rect, types, 3)) {
        const std::uint32_t type = occupant->GetOccupantType();
        if (type == kOccupantTypeBuilding) {
            building = std::max(building, GetOccupantRawFlammability(occupant));
        } else if (type == kOccupantTypeCivic) {
            if (building == 0) civic = std::max(civic, GetOccupantRawFlammability(occupant));
        } else if (civic == 0 && building == 0) {
            other = std::max(other, GetOccupantRawFlammability(occupant));
        }
    }
    std::uint8_t result = other;
    if (civic != 0) result = civic;
    if (building != 0) result = building;
    return result;
}

void cSC4FlammabilitySimulator::EndOfCycle() {
    average_ = zonedCells_ == 0 ? 0 : static_cast<std::uint8_t>(sumFlammability_ / zonedCells_);
    sumFlammability_ = 0;
    zonedCells_ = 0;
    host_->PostFlammabilityMessage();
    host_->AddHistoryValue(kHistoryFlammability, average_);
    ComputeHotspotList();
    candidates_.clear();
    TryRandomFirestart();
}

void cSC4FlammabilitySimulator::ComputeHotspotList() {
    hotspots_.clear();
    for (const tHotspot& candidate : candidates_) {
        bool merged = false;
        for (tHotspot& existing : hotspots_) {
            // Manhattan distance: |dx| + |dz| computed as (max - min) per axis
            const int dx = std::max(existing.x, candidate.x) - std::min(existing.x, candidate.x);
            const int dz = std::max(existing.z, candidate.z) - std::min(existing.z, candidate.z);
            if (dx + dz < mergeDistance_) {
                // running average of the merged positions weighted by the member count
                existing.x = (existing.x * existing.weight + candidate.x) / (existing.weight + 1);
                existing.z = (existing.z * existing.weight + candidate.z) / (existing.weight + 1);
                existing.weight += 1;
                merged = true;
                break;
            }
        }
        if (!merged) hotspots_.push_back({candidate.x, candidate.z, candidate.weight});
    }
    for (auto it = hotspots_.begin(); it != hotspots_.end();) {
        if (it->weight < minHotspotWeight_) it = hotspots_.erase(it);
        else ++it;
    }
}

void cSC4FlammabilitySimulator::TryRandomFirestart() {
    if (host_->RandomDisastersBlocked()) return;
    const double roll = random_.RandomDoubleRangeUniform(0.0, 100.0);
    int x = 0, z = 0;
    bool onRandomLot;
    int lotX = 0, lotZ = 0;
    if (static_cast<float>(roll) < randomFireChance_ && host_->PickRandomLotCell(lotX, lotZ)) {
        x = lotX;
        z = lotZ;
        onRandomLot = true;
    } else {
        if (hotspots_.empty() || !fireCurve_) return;
        LinearRandomIterator order(static_cast<int>(hotspots_.size()), false);
        order.Reset(true);
        bool found = false;
        onRandomLot = false;
        std::uint32_t index = 0;
        while (!found && order.Next(index)) {
            auto it = hotspots_.begin();
            std::advance(it, index);
            const float chance = fireCurve_->GetOutputValue(static_cast<float>(it->weight));
            if (static_cast<float>(roll) < chance) {
                const int size = flammability_->GetTractSize();
                x = it->x * size + size / 2;
                z = it->z * size + size / 2;
                found = true;
            }
        }
        if (!found) return;
    }
    host_->StartFire(x, z, onRandomLot, 5);
}

bool cSC4FlammabilitySimulator::IsCellFlammable(int x, int z) {
    if ((x | z) < 0 || x >= cityCellsX_ || z >= cityCellsZ_) return false;
    return flammability_->GetCellValue(x, z) != 0;
}

bool cSC4FlammabilitySimulator::GetFlammabilityHotspot(std::uint8_t* value, int* x, int* z) {
    int bestWeight = 0;
    int bestX = 0, bestZ = 0;
    std::uint8_t bestValue = 0;
    for (const tHotspot& hotspot : hotspots_) {
        if (bestWeight < hotspot.weight) {
            bestX = hotspot.x;
            bestZ = hotspot.z;
            bestValue = flammability_->GetTractValue(bestX, bestZ);
            bestWeight = hotspot.weight;
        }
    }
    if (hotspots_.empty() || bestValue == 0) return false;
    if (value) *value = bestValue;
    if (x) *x = flammability_->GetTractSize() * bestX;
    if (z) *z = flammability_->GetTractSize() * bestZ;
    return true;
}

bool cSC4FlammabilitySimulator::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    SC4WriteGZSerializable(stream, static_cast<const cIGZSerializable*>(flammability_.get()));
    SC4WriteGZSerializable(stream, static_cast<const cIGZSerializable*>(rawFlammability_.get()));
    stream.SetFloat32(factor_);
    stream.SetUint8(average_);
    stream.SetUint32(static_cast<std::uint32_t>(hotspots_.size()));
    for (const tHotspot& h : hotspots_) {
        stream.SetSint32(h.x);
        stream.SetSint32(h.z);
        stream.SetSint32(h.weight);
    }
    return !stream.GetError();
}

bool cSC4FlammabilitySimulator::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 2) return false;
    void* grid = nullptr;
    if (SC4ReadGZSerializable(stream, kGZIID_cISC4SimGrid, &grid, false)) {
        flammability_ = static_cast<cSC4SimGrid<std::uint8_t>*>(static_cast<cISC4SimGridBase*>(grid));
        static_cast<cISC4SimGridBase*>(grid)->Release();
    }
    grid = nullptr;
    if (SC4ReadGZSerializable(stream, kGZIID_cISC4SimGrid, &grid, false)) {
        rawFlammability_ = static_cast<cSC4SimGrid<std::uint8_t>*>(static_cast<cISC4SimGridBase*>(grid));
        static_cast<cISC4SimGridBase*>(grid)->Release();
    }
    stream.GetFloat32(factor_);
    stream.GetUint8(average_);
    std::uint32_t count = 0;
    stream.GetUint32(count);
    while (count--) {
        tHotspot h;
        stream.GetSint32(h.x);
        stream.GetSint32(h.z);
        stream.GetSint32(h.weight);
        hotspots_.push_back(h);
    }
    return !stream.GetError();
}

}  // namespace sc4
