#include "sc4/IndustrialSimulator.h"

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"

namespace sc4 {

bool cSC4IndustrialSimulator::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 0xe4fda3d4) *out = static_cast<cIGZSerializable*>(this);
    else if (iid == 0x452294aa) *out = static_cast<cIGZMessageTarget2*>(this);
    else if (iid == 1 || iid == kGZIID_cISC4IndustrialSimulator) *out = static_cast<cIGZSerializable*>(this);
    else return false;
    AddRef();
    return true;
}

std::uint32_t cSC4IndustrialSimulator::Release() {
    if (refCount_ < 2) {
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cSC4IndustrialSimulator::Init() {
    initialised_ = true;
    return true;
}

bool cSC4IndustrialSimulator::Shutdown() {
    initialised_ = false;
    return true;
}

bool cSC4IndustrialSimulator::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    return !stream.GetError();
}

bool cSC4IndustrialSimulator::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    return version == 1 && !stream.GetError();
}

bool cSC4IndustrialSimulator::DoMessage(cIGZMessage2* message) {
    (void)message->GetType();
    return true;
}

}  // namespace sc4
