// cSC4CommercialSimulator (23 functions, 0x1001fbf24..0x1001fc840, class id 0x8990c080, interfaces
// 0x077abeb2 cISC4CommercialSimulator / 0x452294aa message target / 0xe4fda3d4 serializable). It owns two
// "proximity" grids (distance of every tract to the nearest commercial-demand source), refreshed one step per
// idle tick: state 0/1 -> grid +0x58 (developer type 0x3320, source kind 2), state 2 -> grid +0x60 (type
// 0x3330, source kind 3), state 3 waits for a dirty flag (set when a network change message of kind 3
// arrives) and then restarts. Layout: +0x18 initialised, +0x1c refcount, +0x20 building development
// simulator, +0x28 city, +0x30 message server, +0x38 dirty flag, +0x3c state, +0x40/+0x48 two city services
// (held only), +0x50 tract size byte, +0x58/+0x60 the grids.
// Stream: version 1, a dirty byte (written as true unless the flag is clear and the state is 3), both grids.
// Everything the original reaches through SL:: globals goes through ICommercialHost.
#pragma once
#include <cstdint>

#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "sc4/cSC4SimGrid.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4CommercialSimulator = 0x8990c080;
constexpr std::uint32_t kGZIID_cISC4CommercialSimulator = 0x077abeb2;
constexpr std::uint32_t kDeveloperTypeCommercialLow = 0x3320, kDeveloperTypeCommercialHigh = 0x3330;
constexpr std::uint32_t kCommercialProximityGridLow = 0x89d5b788, kCommercialProximityGridHigh = 0x89d5b6ef;
constexpr std::uint32_t kMessageCommercialChangeA = 0x99ef1142, kMessageCommercialChangeB = 0x99ef1143;
constexpr std::uint32_t kMessageIdleTick = 0x66956814, kMessageIdleTickWithData = 0x6695681a;
constexpr std::uint32_t kCommercialNetworkChangeTag = 0x278128a0, kIIDNetworkChange = 0x87dda3a9;

class cISC4CommercialSimulator : public cIGZUnknown {
public:
    virtual cSC4SimGrid<std::uint8_t>* GetProximityMap(std::uint32_t developerType) const = 0;
};

// Payload of the 0x278128a0 message (QueryInterface 0x87dda3a9): the kind is 3 for a road/network change.
class ICommercialChange : public cIGZUnknown {
public:
    virtual const int* GetKind() = 0;
};

class ICommercialHost {
public:
    virtual ~ICommercialHost() = default;
    // simulator idle client registration (kinds 0, 10, 4, 6, priority 2) / removal
    virtual void AddIdleClient(cIGZMessageTarget2* target, int kind, const char* name, int priority) = 0;
    virtual void RemoveIdleClient(cIGZMessageTarget2* target) = 0;
    virtual void AddNotification(cIGZMessageTarget2* target, std::uint32_t message) = 0;
    virtual void RemoveNotification(cIGZMessageTarget2* target, std::uint32_t message) = 0;
    // takes references on the city, the building development simulator and the two city services;
    // false when any is missing
    virtual bool AcquireServices() = 0;
    virtual void ReleaseServices() = 0;
    // cSC4MapUtilities::FloodFillDistance(grid, developmentMap(), threshold(3, sourceKind))
    virtual void FloodFillDistance(cSC4SimGrid<std::uint8_t>& grid, int sourceKind) = 0;
};

class cSC4CommercialSimulator final : public cISC4CommercialSimulator,
                                      public cIGZMessageTarget2,
                                      public cIGZSerializable {
public:
    explicit cSC4CommercialSimulator(ICommercialHost* host) : host_(host) {}
    ~cSC4CommercialSimulator() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool Init();
    bool Shutdown();
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4CommercialSimulator; }
    bool DoMessage(cIGZMessage2* message) override;
    void DoIdle();
    cSC4SimGrid<std::uint8_t>* GetProximityMap(std::uint32_t developerType) const override;

    bool IsInitialised() const { return initialised_; }
    bool IsDirty() const { return dirty_; }
    int State() const { return state_; }
    void SetTractSize(std::uint8_t size) { tractSize_ = size; }

private:
    cSC4SimGrid<std::uint8_t>* MakeGrid(std::uint32_t instanceId);

    ICommercialHost* host_;
    bool initialised_ = false;
    std::uint32_t refCount_ = 0;
    bool dirty_ = false;
    int state_ = 0;
    std::uint8_t tractSize_ = 2;   // +0x50: written by the (unrecovered) constructor
    cSC4SimGrid<std::uint8_t>* lowGrid_ = nullptr;    // +0x58
    cSC4SimGrid<std::uint8_t>* highGrid_ = nullptr;   // +0x60
};

}  // namespace sc4
