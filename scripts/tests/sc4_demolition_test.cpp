// cSC4Demolition: tuning, save record, occupant dispatch per type, lot union mask, network follow-up.
#include <cstdio>
#include <map>

#include "rz/PortMemoryStreams.h"
#include "sc4/Demolition.h"
#include "sc4/SCProperty.h"

using namespace sc4;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Occ final : IDemolishable {
    std::uint32_t type = 0, flags = 0;
    bool protectedFlag = false, hasNet = false, removed = false, plain = true;
    std::uint32_t Type() override { return type; }
    bool HasRemovalFlag(std::uint32_t f) override { return f == 0x40 ? protectedFlag : hasNet; }
    std::uint32_t Flags() override { return flags; }
    void Remove() override { removed = true; }
};
struct Lot final : IDemolishableLot {
    DemolitionCellRect r; bool plopped = true;
    DemolitionCellRect Bounds() override { return r; }
    bool IsPlopped() override { return plopped; }
    std::vector<IDemolishable*> Occupants() override { return {}; }
};
struct Host final : IDemolitionHost {
    cSCPropertyHolder thresholds, costs;
    std::vector<std::uint32_t> added, removedNotes;
    std::vector<IDemolishable*> neighborCalls, managerRemoved, networkBatch;
    std::vector<std::uint32_t> posted;
    bool propOk = true, propRemoveOk = true;
    std::vector<std::vector<bool>> mask; DemolitionCellRect sweepRect; std::uint32_t sweepFlags = 0;
    void AddNotification(std::uint32_t m) override { added.push_back(m); }
    void RemoveNotification(std::uint32_t m) override { removedNotes.push_back(m); }
    void PostOccupantMessage(std::uint32_t m, IDemolishable*, bool) override { posted.push_back(m); }
    cISCPropertyHolder* TuningExemplar(std::uint32_t group, std::uint32_t) override { return group == 0x690f693f ? &thresholds : &costs; }
    void CityCells(int& x, int& z) override { x = 64; z = 32; }
    bool OccupantAllowedByFilter(IDemolishable*) override { return true; }
    void ToCellBounds(const DemolitionCellRect& r, DemolitionCellRect& o) override { o = r; }
    std::vector<IDemolishable*> OccupantsInRect(const DemolitionCellRect&, bool) override { return {}; }
    bool RemoveOccupant(IDemolishable* o, bool, bool) override { managerRemoved.push_back(o); return true; }
    void ReleaseOccupantList(std::vector<IDemolishable*>&) override {}
    bool PropManagerAvailable() override { return propOk; }
    bool PropManagerRemove(IDemolishable*) override { return propRemoveOk; }
    bool NetworkCost(IDemolishable*, std::int64_t& c) override { c = 25; return true; }
    bool BuildingCost(IDemolishable*, std::int64_t& c) override { c = 100; return true; }
    bool FloraCost(IDemolishable*, std::int64_t&) override { return false; }
    bool PoleCost(IDemolishable*, std::int64_t& c) override { c = 7; return true; }
    std::vector<IDemolishable*> PoleConnections(IDemolishable*) override { return conns; }
    void PoleDetach(IDemolishable*) override {}
    bool IsPlainOccupant(IDemolishable*) override { return true; }
    void NotifyNeighborsNetworkChange(IDemolishable* n) override { neighborCalls.push_back(n); }
    void UpdateNetworkManager(const std::vector<IDemolishable*>& n) override { networkBatch = n; }
    bool Sweep(bool, const DemolitionCellRect& r, const std::vector<std::vector<bool>>* m, int, std::uint32_t f, bool, std::int64_t*) override {
        sweepRect = r; mask = *m; sweepFlags = f; return true;
    }
    std::vector<IDemolishable*> conns;
};
}  // namespace

int main() {
    Host host;
    cSCPropertyHelp::SetPropertyValue(&host.thresholds, kPropertyModelSmallMedium, 1.5f);
    cSCPropertyHelp::SetPropertyValue(&host.thresholds, kPropertyGroupMediumLarge, 9.0f);
    cSCPropertyHelp::SetPropertyValue(&host.costs, kPropertyDemolishLotTileCost, std::int32_t(12));
    cSC4Demolition d(&host);
    CHECK(d.Init());
    CHECK(host.added.size() == 3);
    CHECK(d.CityCellsX() == 64 && d.CityCellsZ() == 32);
    CHECK(d.SmallMediumModelThreshold() == 1.5f && d.MediumLargeGroupThreshold() == 9.0f && d.CostToDemolishDestroyedLotTile() == 12);
    cSCPropertyHelp::SetPropertyValue(&host.costs, kPropertyDemolishLotTileCost, std::int32_t(20));
    CHECK(d.DoMessage(kMessageTuningReloaded) && d.CostToDemolishDestroyedLotTile() == 20);
    CHECK(!d.DoMessage(0x1234));

    PortMemoryOStream out;
    CHECK(d.Write(out));
    PortMemoryIStream in(out.bytes().data(), out.bytes().size());
    CHECK(d.Read(in));
    PortMemoryOStream bad; bad.SetUint16(2);
    PortMemoryIStream badIn(bad.bytes().data(), bad.bytes().size());
    CHECK(!d.Read(badIn));

    Occ prop; prop.type = kDemolishTypeProp;
    std::int64_t cost = -1;
    CHECK(d.DemolishOccupant(true, &prop, 0, &cost, false, nullptr) && prop.removed && cost == 0);
    Occ prop2; prop2.type = kDemolishTypeProp; host.propRemoveOk = false;
    CHECK(!d.DemolishOccupant(true, &prop2, 0, &cost, false, nullptr) && !prop2.removed);
    Occ prot; prot.type = kDemolishTypeProp; prot.protectedFlag = true;
    CHECK(!d.DemolishOccupant(true, &prot, kDemolishSkipProtected, &cost, false, nullptr));
    Occ bld; bld.type = kDemolishTypeBuilding;
    CHECK(d.DemolishOccupant(true, &bld, 0, &cost, false, nullptr) && cost == 100 && host.managerRemoved.empty());
    Occ net; net.type = kDemolishTypeNetwork; net.flags = 1; net.hasNet = true;
    CHECK(d.DemolishOccupant(true, &net, 0, &cost, false, nullptr) && cost == 25);
    CHECK(host.neighborCalls.size() == 1 && host.networkBatch.size() == 1);
    Occ flora; flora.type = kDemolishTypeFlora;
    CHECK(!d.DemolishOccupant(true, &flora, 0, &cost, false, nullptr) && cost == 0);
    Occ found; found.type = kDemolishTypeFoundation; found.hasNet = false;
    CHECK(d.DemolishOccupant(true, &found, 0, &cost, false, nullptr) && host.managerRemoved.empty());
    found.hasNet = true;
    CHECK(d.DemolishOccupant(true, &found, 0, &cost, false, nullptr) && host.managerRemoved.size() == 1);
    Occ unknown; unknown.type = 5;
    CHECK(!d.DemolishOccupant(true, &unknown, 0, &cost, false, nullptr));

    Occ pole; pole.type = kDemolishTypePole; Occ link; link.type = 1; host.conns = {&link};
    std::vector<IDemolishable*> released;
    host.posted.clear();
    CHECK(d.DemolishOccupant(true, &pole, 0, &cost, false, &released) && cost == 7 && pole.removed);
    CHECK(host.posted.size() == 1 && host.posted[0] == kMessageOccupantDemolished && released.size() == 1);

    Lot a, b; a.r = {2, 3, 3, 4}; b.r = {5, 1, 5, 2}; b.plopped = false;
    CHECK(d.DemolishLots(true, {&a, &b}, 0, 0, false, nullptr));
    CHECK(host.sweepRect.x0 == 2 && host.sweepRect.z0 == 1 && host.sweepRect.x1 == 5 && host.sweepRect.z1 == 4);
    CHECK((host.sweepFlags & kDemolishLotsOnly) && host.mask[0][2] && host.mask[1][3] && !host.mask[3][0] && !host.mask[0][0]);
    CHECK(d.Shutdown() && host.removedNotes.size() == 3);
    std::printf(failures ? "demolition: %d failures\n" : "demolition: all passed\n", failures);
    return failures != 0;
}
