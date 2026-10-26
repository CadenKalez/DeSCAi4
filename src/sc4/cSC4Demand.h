// cSC4Demand — one RCI demand channel: supply, demand, the smoothed "active demand" shown to the
// player and the economy/tax/cap modifiers that shape it. Recovered from Sim City 4 Deluxe
// Editionsub 1.2.1 (vtable 0x100a864a0, 41 functions; class id 0x8990c12d, iid 0x797a4e8).
//
// Active demand (CalculateActiveDemand): target = clamp(demand - supply, min, max)
//   + min(demand, max) * (economy + tax), clamped again; capped at max * (1 - demandCap) and floored
// at min. When `smooth` is set the stored value moves towards the target by at most 7% of the
// (max - min) span per call, snapping to 0 when a step would otherwise cross zero (quirk kept).
// Every update posts message 0x426840a0 {data1 = id, data2 = value in 0.1%: value * 1000 / max
// for positive demand, value * -1000 / min for negative, rounded to nearest-even}.
#pragma once
#include <cstdint>

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"

namespace sc4 {

constexpr std::uint32_t kDemandChangedMessage = 0x426840a0;
constexpr std::uint32_t kDemandClassId = 0x8990c12d;
constexpr std::uint32_t kGZIID_cISC4Demand = 0x0797a4e8;

class IDemandHost {
public:
    virtual ~IDemandHost() = default;
    virtual void PostMessage(std::uint32_t message, std::uint32_t data1, std::int32_t data2) = 0;
};

class cSC4Demand {
public:
    enum RegionUse : std::int32_t { kRegionUseNone = 0 };

    explicit cSC4Demand(IDemandHost* host = nullptr) : host_(host) {}

    bool Init();
    bool Shutdown();
    bool SimulationBegin();      // recomputes unless loaded from a save, then announces the value

    std::uint32_t GetId() const { return id_; }
    bool SetId(std::uint32_t id) { id_ = id; return true; }
    float QuerySupplyValue() const { return supply_; }
    float QueryDemandValue() const { return demand_; }
    float QueryNewSupply() const { return newSupply_; }
    float QueryNewDemand() const { return newDemand_; }
    float QueryActiveDemandValue() const { return active_; }
    float QueryActiveDemandMax() const { return activeMax_; }
    float QueryActiveDemandMin() const { return activeMin_; }
    float QueryEconomyModifier() const { return economyModifier_; }
    bool AddToSupplyValue(float v) { newSupply_ += v; return true; }
    bool AddToDemandValue(float v) { newDemand_ += v; return true; }
    bool SetSupplyValue(float v) { supply_ = v; return true; }
    bool SetDemandValue(float v) { demand_ = v; return true; }
    void SetActiveDemandMax(float v) { activeMax_ = v; if (v < activeMin_) activeMax_ = activeMin_; }
    void SetActiveDemandMin(float v) { activeMin_ = v; if (activeMax_ < v) activeMin_ = activeMax_; }
    float GetEconomyModifier() const { return economyModifier_; }
    void SetEconomyModifier(float v) { economyModifier_ = v; }
    float GetTaxModifier() const { return taxModifier_; }
    void SetTaxModifier(float v) { taxModifier_ = v; }
    float GetDemandCap() const { return demandCap_; }              // SC4Percentage (0..1)
    void SetDemandCap(float fraction) { demandCap_ = fraction; }
    std::int32_t GetRegionUse() const { return regionUse_; }
    void SetRegionUse(std::int32_t use) { regionUse_ = use; }

    // Folds the new supply/demand into the totals; true if either was non-zero.
    bool EndOfCycle();
    void CalculateActiveDemand(bool smooth);

    // Debug locking is compiled out of the release build.
    void DebugLockValue(float) {}
    void DebugUnlockValue() {}
    bool DebugIsValueLocked() const { return false; }

    // The state of another demand (a loaded one) without its host.
    void CopyStateFrom(const cSC4Demand& other) { IDemandHost* host = host_; *this = other; host_ = host; }
    bool Write(cIGZOStream& stream) const;
    bool Read(cIGZIStream& stream);      // accepts versions 1 and 2 (and 0)
    std::uint32_t GetGZCLSID() const { return kDemandClassId; }

private:
    IDemandHost* host_;
    bool wasRead_ = false;
    bool initialized_ = false;
    std::uint32_t id_ = 0;
    float supply_ = 0, demand_ = 0, newSupply_ = 0, newDemand_ = 0, extraA_ = 0, extraB_ = 0;
    float activeMax_ = 3.4e38f * 0.25f;     // 0x7e7fffff
    float activeMin_ = -3.4e38f * 0.25f;    // 0xfe7fffff
    float active_ = 0;
    bool dirty_ = true;
    float economyModifier_ = 0, taxModifier_ = 0, demandCap_ = 0;
    std::int32_t regionUse_ = 0;
};

}  // namespace sc4
