// tAutomataState — the saved behaviour state of one automaton (pedestrian / vehicle): the controller driving it, a state id, a progress
// value, a flag and three counters. 3 functions, 0x10010e128..0x10010e214. Original layout (0x38 bytes): +0x10 controller (class
// 0x6a1857f8, AddRef'd), +0x18 state id, +0x1c progress (float), +0x20 flag, +0x24 / +0x28 / +0x2c counters (the third only in stream
// version >= 3), +0x30 runtime pointer (cleared by Read).
// Stream: version u16 = 3, controller (GZ serializable, written as nothing when the controller reports itself finished - slot 0x70),
// state id u32, progress float, flag bool, three u32.
#pragma once
#include <cstdint>

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZUnknown.h"

namespace sc4 {

class IAutomataStateController : public cIGZUnknown {
public:
    virtual bool IsFinished() const = 0;   // slot 0x70
};
class IAutomataStateAutomaton {
public:
    virtual ~IAutomataStateAutomaton() = default;
    virtual void CancelState() = 0;        // cISC4Automaton slot 0x1f0
};

struct tAutomataState {
    IAutomataStateController* controller = nullptr;   // +0x10
    std::uint32_t stateId = 0;                        // +0x18
    float progress = 0.0f;                            // +0x1c
    bool flag = false;                                // +0x20
    std::uint32_t counterA = 0;                       // +0x24: non-zero means the automaton must be told to cancel on shutdown
    std::uint32_t counterB = 0;                       // +0x28
    std::uint32_t counterC = 0;                       // +0x2c
    void* runtime = nullptr;                          // +0x30

    void Shutdown(IAutomataStateAutomaton* automaton);   // 0x10010e128: release the controller; cancel the automaton's state if counterA
    bool Write(cIGZOStream& stream) const;               // 0x10010e164
    bool Read(cIGZIStream& stream);                      // 0x10010e214
};

}  // namespace sc4
