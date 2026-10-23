#include "sc4/AutomataState.h"

#include "gz/StreamOps.h"
#include "sc4/DBSegment.h"

namespace sc4 {

namespace {
constexpr std::uint32_t kAutomataStateControllerIid = 0x6a1857f8;
constexpr std::uint16_t kAutomataStateVersion = 3;
}

// 0x10010e128
void tAutomataState::Shutdown(IAutomataStateAutomaton* automaton) {
    if (controller) {
        controller->Release();
        controller = nullptr;
    }
    if (counterA != 0) automaton->CancelState();
}

// 0x10010e164
bool tAutomataState::Write(cIGZOStream& stream) const {
    stream.SetUint16(kAutomataStateVersion);
    const cIGZUnknown* written = nullptr;
    if (controller && !controller->IsFinished()) written = controller;
    SC4WriteGZSerializable(stream, written);
    stream.SetUint32(stateId);
    stream.SetFloat32(progress);
    stream << flag;
    stream.SetUint32(counterA);
    stream.SetUint32(counterB);
    stream.SetUint32(counterC);
    return !stream.GetError();
}

// 0x10010e214: versions 0..3 (the third counter exists from version 3); anything newer is refused.
bool tAutomataState::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version >= 4) return false;
    void* raw = nullptr;
    SC4ReadGZSerializable(stream, kAutomataStateControllerIid, &raw, false);
    controller = static_cast<IAutomataStateController*>(raw);
    stream.GetUint32(stateId);
    stream.GetFloat32(progress);
    stream >> flag;
    stream.GetUint32(counterA);
    stream.GetUint32(counterB);
    if (version > 2) stream.GetUint32(counterC);
    runtime = nullptr;
    return !stream.GetError();
}

}  // namespace sc4
