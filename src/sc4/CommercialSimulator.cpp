#include "sc4/CommercialSimulator.h"

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "sc4/DBSegment.h"

namespace sc4 {

bool cSC4CommercialSimulator::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 0xe4fda3d4) *out = static_cast<cIGZSerializable*>(this);
    else if (iid == 0x452294aa) *out = static_cast<cIGZMessageTarget2*>(this);
    else if (iid == 1 || iid == kGZIID_cISC4CommercialSimulator) *out = static_cast<cISC4CommercialSimulator*>(this);
    else return false;
    AddRef();
    return true;
}

std::uint32_t cSC4CommercialSimulator::Release() {
    if (refCount_ < 2) {
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

cSC4SimGrid<std::uint8_t>* cSC4CommercialSimulator::MakeGrid(std::uint32_t instanceId) {
    auto* grid = new cSC4SimGrid<std::uint8_t>();
    grid->AddRef();
    grid->Init();
    grid->SetTractSize(tractSize_);
    grid->SetInstanceID(instanceId);
    return grid;
}

bool cSC4CommercialSimulator::Init() {
    if (initialised_) return true;
    initialised_ = true;
    static const char* const kName = "cSC4CommercialSimulator";
    for (int kind : {0, 10, 4, 6}) host_->AddIdleClient(static_cast<cIGZMessageTarget2*>(this), kind, kName, 2);
    host_->AddNotification(this, kMessageCommercialChangeA);
    host_->AddNotification(this, kMessageCommercialChangeB);
    if (!host_->AcquireServices()) return false;
    if (!highGrid_) highGrid_ = MakeGrid(kCommercialProximityGridHigh);
    if (!lowGrid_) lowGrid_ = MakeGrid(kCommercialProximityGridLow);
    return true;
}

bool cSC4CommercialSimulator::Shutdown() {
    if (!initialised_) return true;
    initialised_ = false;
    for (auto** grid : {&highGrid_, &lowGrid_}) {
        if (*grid) {
            (*grid)->Shutdown();
            (*grid)->Release();
            *grid = nullptr;
        }
    }
    host_->RemoveNotification(this, kMessageCommercialChangeA);
    host_->RemoveNotification(this, kMessageCommercialChangeB);
    host_->ReleaseServices();
    host_->RemoveIdleClient(static_cast<cIGZMessageTarget2*>(this));
    return true;
}

bool cSC4CommercialSimulator::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    bool flag = true;
    if (!dirty_) flag = state_ != 3;
    stream.SetUint8(flag);
    SC4WriteGZSerializable(stream, static_cast<const cISC4SimGridBase*>(lowGrid_));
    SC4WriteGZSerializable(stream, static_cast<const cISC4SimGridBase*>(highGrid_));
    return !stream.GetError();
}

bool cSC4CommercialSimulator::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 1) return false;
    std::uint8_t flag = 0;
    stream.GetUint8(flag);
    dirty_ = flag != 0;
    SC4ReadGZSerializable(stream, kGZIID_cISC4SimGrid, reinterpret_cast<void**>(&lowGrid_), false);
    SC4ReadGZSerializable(stream, kGZIID_cISC4SimGrid, reinterpret_cast<void**>(&highGrid_), false);
    return !stream.GetError();
}

bool cSC4CommercialSimulator::DoMessage(cIGZMessage2* message) {
    const std::uint32_t type = message->GetType();
    auto* payload = static_cast<cIGZMessage2Standard*>(message);
    if (type - kMessageCommercialChangeA < 2) {
        if (payload->GetData3() == kCommercialNetworkChangeTag) {
            auto* object = static_cast<cIGZUnknown*>(payload->GetVoid1());
            void* raw = nullptr;
            if (object->QueryInterface(kIIDNetworkChange, &raw)) {
                auto* change = static_cast<ICommercialChange*>(raw);
                if (*change->GetKind() == 3) dirty_ = true;
                change->Release();
            }
        }
    } else if (type == kMessageIdleTick || type == kMessageIdleTickWithData) {
        if (type == kMessageIdleTickWithData) (void)payload->GetData3();
        DoIdle();
    }
    return true;
}

void cSC4CommercialSimulator::DoIdle() {
    switch (state_) {
    case 0:
        state_ = 1;
        [[fallthrough]];
    case 1:
        host_->FloodFillDistance(*lowGrid_, 2);
        state_ = 2;
        break;
    case 2:
        host_->FloodFillDistance(*highGrid_, 3);
        state_ = 3;
        break;
    case 3:
        if (dirty_) {
            dirty_ = false;
            state_ = 0;
        }
        break;
    }
}

cSC4SimGrid<std::uint8_t>* cSC4CommercialSimulator::GetProximityMap(std::uint32_t developerType) const {
    if (developerType == kDeveloperTypeCommercialHigh) return highGrid_;
    if (developerType == kDeveloperTypeCommercialLow) return lowGrid_;
    return nullptr;
}

}  // namespace sc4
