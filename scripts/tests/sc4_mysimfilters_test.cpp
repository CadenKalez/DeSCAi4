#include <cstdio>

#include "sc4/MySimFilters.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Link final : cIGZUnknown {
    bool answers = true;
    int refs = 1;
    bool QueryInterface(std::uint32_t iid, void** out) override { if (answers && iid == kIidPylonLink) { *out = this; ++refs; return true; } return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
struct Facts final : IMySimBuildingFacts {
    float completion = 1.0f; int kind = 1; std::uint32_t lot = 7;
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    std::uint32_t LotKey() override { return lot; }
    const float* Completion() override { return &completion; }
    const int* OccupancyKind() override { return &kind; }
};
// An occupant that hands out the facts for the two interfaces.
struct Occ final : cSC4Occupant {
    Facts* facts; std::uint32_t kindOfOccupant;
    Occ(Facts* f, std::uint32_t t) : facts(f), kindOfOccupant(t) { type_ = t; }
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kIidMySimBuilding) { *out = static_cast<IMySimBuildingFacts*>(facts); return true; }
        return cSC4Occupant::QueryInterface(iid, out);
    }
};
struct Pylon final : IMySimPylonFacts {
    int kind = 3; bool isBlocked = false; Link* link = nullptr; int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    int Kind() override { return kind; }
    bool Blocked() override { return isBlocked; }
    cIGZUnknown* LinkedObject() override { return link; }
};
struct PylonOcc final : cSC4Occupant {
    Pylon* pylon;
    PylonOcc(Pylon* p, std::uint32_t t) : pylon(p) { type_ = t; }
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kIidMySimPylon) { *out = static_cast<IMySimPylonFacts*>(pylon); ++pylon->refs; return true; }
        return cSC4Occupant::QueryInterface(iid, out);
    }
};
struct Host final : IMySimFilterHost {
    bool hasLot = false; std::uint8_t stage = 0;
    bool LotStage(std::uint32_t, std::uint8_t& s) override { s = stage; return hasLot; }
};
}  // namespace

int main() {
    Host host;
    SetMySimFilterHost(&host);
    // building
    Facts facts;
    auto* b = new Occ(&facts, kOccupantTypeBuilding);
    b->AddRef();
    cMySimBuildingFilter any, kindOne(true);
    CHECK(any.IsOccupantIncluded(b) && kindOne.IsOccupantIncluded(b));
    facts.kind = 2;
    CHECK(any.IsOccupantIncluded(b) && !kindOne.IsOccupantIncluded(b));
    facts.completion = 0.99f;
    CHECK(!any.IsOccupantIncluded(b));
    facts.completion = 1.0f;
    host.hasLot = true; host.stage = 3;
    CHECK(any.IsOccupantIncluded(b));
    host.stage = 4;
    CHECK(!any.IsOccupantIncluded(b));
    host.hasLot = false;
    auto* wrongType = new Occ(&facts, 0x1234);
    wrongType->AddRef();
    CHECK(!any.IsOccupantIncluded(wrongType));
    auto* noInterface = new cSC4Occupant();
    noInterface->AddRef();
    CHECK(!any.IsOccupantIncluded(noInterface));
    // pylon
    Pylon p;
    Link link;
    p.link = &link;
    auto* po = new PylonOcc(&p, kOccupantTypePylon);
    po->AddRef();
    cMySimPylonFilter pf;
    CHECK(pf.IsOccupantIncluded(po) && p.refs == 1 && link.refs == 1);   // all references returned
    link.answers = false;
    CHECK(!pf.IsOccupantIncluded(po));
    link.answers = true;
    p.isBlocked = true;
    CHECK(!pf.IsOccupantIncluded(po));
    p.isBlocked = false; p.kind = 0;
    CHECK(!pf.IsOccupantIncluded(po));
    p.kind = 1; p.link = nullptr;
    CHECK(!pf.IsOccupantIncluded(po));
    auto* notPylon = new PylonOcc(&p, kOccupantTypeBuilding);
    notPylon->AddRef();
    CHECK(!pf.IsOccupantIncluded(notPylon));
    b->Release(); wrongType->Release(); noInterface->Release(); po->Release(); notPylon->Release();
    std::printf(failures ? "FAILED\n" : "sc4_mysimfilters OK\n");
    return failures ? 1 : 0;
}
