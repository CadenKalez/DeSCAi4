// A scripted cISC4Occupant for filter tests.
#pragma once
#include <set>

#include "sc4/Occupant.h"

namespace sc4_test {
using namespace sc4;
struct FakeOccupant final : cISC4Occupant {
    std::uint32_t type = 0;
    std::uint32_t group = 0;
    std::uint32_t iface = 0;
    cISC43DPlaceableObject* placeable = nullptr;     // GetPlaceableObject
    cISCPropertyHolder* holder = nullptr;            // AsPropertyHolder
    std::uint32_t group2 = 0xffffffffu;              // a second group answered by IsOccupantGroup
    bool QueryInterface(std::uint32_t iid, void** out) override { if (iid == iface) { *out = this; return true; } return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool IsInitialized() const override { return true; }
    cISCPropertyHolder* AsPropertyHolder() override { return holder; }
    std::uint32_t GetType() const override { return type; }
    void GetPosition(float*) override {}
    void SetPosition(const float*) override {}
    void GetBoundingBox(float*, float*) override {}
    void GetBoundingCityCells(SC4CellRect&) override {}
    void SetRemovalFlags(int) override {}
    void UnsetRemovalFlags(int) override {}
    bool CanRemove(int) override { return true; }
    void PostOccupantMessage(std::uint32_t, std::uint32_t) override {}
    std::uint32_t GetHighlight() const override { return 0; }
    void SetHighlight(int, bool) override {}
    void SetVisibility(bool, bool) override {}
    cISC43DPlaceableObject* GetPlaceableObject() const override { return placeable; }
    cISC43DPlaceableObject* GetOrCreatePlaceableObject() override { return nullptr; }
    void SetPlaceableObject(cISC43DPlaceableObject*) override {}
    bool IsOccupantGroup(std::uint32_t g) const override { return g == group || g == group2; }
    void AddOccupantGroup(std::uint32_t) override {}
    bool GetOccupantGroups(std::set<std::uint32_t>&) const override { return false; }
    bool GetOccupantManagerBBox(std::uint8_t*) const override { return false; }
    void SetOccupantManagerBBox(const std::uint8_t*) override {}
    bool GetLotTag(std::uint32_t&) const override { return false; }
    void SetLotTag(std::uint32_t) override {}
    void SetFlag(std::uint32_t) override {}
    void SetAllFlags(std::uint32_t) override {}
    void ClearFlag(std::uint32_t) override {}
    bool IsFlagSet(std::uint32_t) override { return false; }
    std::uint32_t GetFlags() override { return 0; }
};
}  // namespace sc4_test
using sc4_test::FakeOccupant;
