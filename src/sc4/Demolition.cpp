#include "sc4/Demolition.h"

#include <algorithm>

namespace sc4 {

bool cSC4Demolition::Init() {
    if (initialized_) return true;
    initialized_ = true;
    host_->AddNotification(kMessageTuningReloaded);
    host_->AddNotification(kMessageCityInit);
    host_->AddNotification(kMessageDemolishRequest);
    host_->CityCells(cellsX_, cellsZ_);
    ReadProperties();
    return true;
}

bool cSC4Demolition::Shutdown() {
    if (!initialized_) return true;
    host_->RemoveNotification(kMessageTuningReloaded);
    host_->RemoveNotification(kMessageCityInit);
    host_->RemoveNotification(kMessageDemolishRequest);
    initialized_ = false;
    return true;
}

// Two exemplars: the model / group size thresholds (group 0x690f693f, instance 0x2a8a09ca) and the lot tile cost
// (group 0xe7e2c2db, instance 0xe8da7677). Values stay as they were when a property is missing.
void cSC4Demolition::ReadProperties() {
    if (cISCPropertyHolder* thresholds = host_->TuningExemplar(0x690f693f, 0x2a8a09ca)) {
        cSCPropertyHelp::GetPropertyValue(thresholds, kPropertyModelSmallMedium, smallMediumModel_);
        cSCPropertyHelp::GetPropertyValue(thresholds, kPropertyModelMediumLarge, mediumLargeModel_);
        cSCPropertyHelp::GetPropertyValue(thresholds, kPropertyGroupSmallMedium, smallMediumGroup_);
        cSCPropertyHelp::GetPropertyValue(thresholds, kPropertyGroupMediumLarge, mediumLargeGroup_);
    }
    if (cISCPropertyHolder* costs = host_->TuningExemplar(0xe7e2c2db, 0xe8da7677))
        cSCPropertyHelp::GetPropertyValue(costs, kPropertyDemolishLotTileCost, lotTileCost_);
}

bool cSC4Demolition::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    return !stream.GetError();
}

bool cSC4Demolition::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    return version == 1 && !stream.GetError();
}

bool cSC4Demolition::DoMessage(std::uint32_t type) {
    if (type != kMessageTuningReloaded) return false;
    ReadProperties();
    return true;
}

void cSC4Demolition::PostMessage(std::uint32_t message, IDemolishable* occupant, bool sendNow) {
    host_->PostOccupantMessage(message, occupant, sendNow);
}

// ---- occupant queries -------------------------------------------------------------------------------------

std::vector<IDemolishable*> cSC4Demolition::GetDemolishableLotOccupants(IDemolishableLot* lot) {
    std::vector<IDemolishable*> occupants = lot->Occupants();
    std::vector<IDemolishable*> dropped;
    occupants.erase(std::remove_if(occupants.begin(), occupants.end(),
                                   [&](IDemolishable* o) {
                                       bool drop = !host_->OccupantAllowedByFilter(o);
                                       if (drop) dropped.push_back(o);
                                       return drop;
                                   }),
                    occupants.end());
    host_->ReleaseOccupantList(dropped);
    return occupants;
}

// The rectangle is converted to metre-space bounds (second corner is one cell past the end) and the occupant
// manager walks it with the default filter.
std::vector<IDemolishable*> cSC4Demolition::GetDemolishableRectOccupants(const DemolitionCellRect& rect) {
    return host_->OccupantsInRect(rect, true);
}

// ---- follow-up pass ---------------------------------------------------------------------------------------

// Network pieces (type 0xc772bf98) are collected, told to the neighbour simulator and handed to the network
// manager in one batch; poles (0x2890d4de) only get the demolished message and are removed from the manager.
void cSC4Demolition::UpdateNetworks(const std::set<IDemolishable*>& occupants) {
    std::vector<IDemolishable*> networks;
    for (IDemolishable* occupant : occupants) {
        if (!occupant->HasRemovalFlag(1)) continue;
        std::uint32_t type = occupant->Type();
        if (type == kDemolishTypeNetwork) {
            networks.push_back(occupant);
            host_->NotifyNeighborsNetworkChange(occupant);
        } else if (type == kDemolishTypePole) {
            PostMessage(kMessageOccupantDemolished, occupant, false);
            host_->RemoveOccupant(occupant, true, false);
        }
    }
    if (!networks.empty()) {
        host_->UpdateNetworkManager(networks);
        host_->ReleaseOccupantList(networks);
    }
}

// ---- lots ---------------------------------------------------------------------------------------------------

// Builds the union of the plopped lots' rectangles and a cell mask that marks the cells of each plopped lot, then
// sweeps that region with the lots-only flag.
bool cSC4Demolition::DemolishLots(bool commit, const std::list<IDemolishableLot*>& lots, int privilege, std::uint32_t flags,
                                  bool arg, std::int64_t* cost) {
    if (lots.empty()) return true;
    DemolitionCellRect all = lots.front()->Bounds();
    for (IDemolishableLot* lot : lots) {
        DemolitionCellRect r = lot->Bounds();
        all.x0 = std::min(all.x0, r.x0);
        all.z0 = std::min(all.z0, r.z0);
        all.x1 = std::max(all.x1, r.x1);
        all.z1 = std::max(all.z1, r.z1);
    }
    std::vector<std::vector<bool>> mask(all.x1 - all.x0 + 1, std::vector<bool>(all.z1 - all.z0 + 1, false));
    for (IDemolishableLot* lot : lots) {
        if (!lot->IsPlopped()) continue;
        DemolitionCellRect r = lot->Bounds();
        for (int x = r.x0; x <= r.x1; ++x)
            for (int z = r.z0; z <= r.z1; ++z) mask[x - all.x0][z - all.z0] = true;
    }
    return host_->Sweep(commit, all, &mask, privilege, flags | kDemolishLotsOnly, arg, cost);
}

// ---- single occupant ------------------------------------------------------------------------------------------

// Quirks kept: network pieces and buildings are only costed here (their removal is the caller's), foundations are
// removed only when they carry flag 1, and a missing service for flora / buildings fails the call with cost 0.
bool cSC4Demolition::DemolishOccupant(bool commit, IDemolishable* occupant, std::uint32_t flags, std::int64_t* cost,
                                      bool skipNetworks, std::vector<IDemolishable*>* released) {
    if ((flags & kDemolishSkipProtected) && occupant->HasRemovalFlag(0x40)) return false;
    std::int64_t price = 0;
    bool ok = false;
    switch (occupant->Type()) {
    case kDemolishTypeProp:
        ok = host_->PropManagerAvailable();
        if (ok && commit) {
            if (host_->PropManagerRemove(occupant)) occupant->Remove();
            else ok = false;
        }
        break;
    case kDemolishTypeNetwork:
        if (host_->NetworkCost(occupant, price)) {
            if (commit && !skipNetworks && (occupant->Flags() & 1)) UpdateNetworks({occupant});
            ok = true;
        }
        break;
    case kDemolishTypeSpecial:
        ok = commit ? host_->RemoveOccupant(occupant, true, false) : true;
        break;
    case kDemolishTypeBuilding:
        ok = host_->BuildingCost(occupant, price);
        break;
    case kDemolishTypePole:
        if (host_->PoleCost(occupant, price)) {
            ok = true;
            if (commit) {
                std::vector<IDemolishable*> connected = host_->PoleConnections(occupant);
                PostMessage(kMessageOccupantDemolished, occupant, false);
                ok = host_->RemoveOccupant(occupant, false, false);
                if (ok) {
                    host_->PoleDetach(occupant);
                    occupant->Remove();
                    if (released)
                        for (IDemolishable* c : connected)
                            if (host_->IsPlainOccupant(c)) released->push_back(c);
                }
            }
        }
        break;
    case kDemolishTypeFoundation:
        ok = (!commit || !occupant->HasRemovalFlag(1)) ? true : host_->RemoveOccupant(occupant, true, false);
        break;
    case kDemolishTypeFlora:
        if (host_->FloraCost(occupant, price)) {
            ok = true;
            if (commit) host_->RemoveOccupant(occupant, true, false);
        }
        break;
    default:
        break;
    }
    if (cost) *cost = price;
    return ok;
}

}  // namespace sc4
