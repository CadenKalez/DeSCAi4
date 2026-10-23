// tAutomataState and its ten subclasses — the per-state records of an automaton (what it is doing right now: random walk,
// riot leading, chase, bumped, collide, plop-jump, plummet, pull over, player drive, attach animation). Each state is saved
// with the automaton: base record (u16 version 3; the controller unless its flag 8 is set; state id; timer; flag; sound handle;
// two values) then the subclass fields. 3 functions for the base (0x10010e128 Shutdown, 0x10010e164 Write, 0x10010e214 Read) and
// Write/Read (plus Shutdown for Collide / PlayerDrive) for each subclass, 0x10010e2e2..0x10010e984.
// Base layout: +0x10 controller* (counted), +0x18 u32 state id, +0x1c float, +0x20 bool, +0x24 u32 sound handle,
// +0x28 u32, +0x2c u32 (since version 3), +0x30 u64 runtime pointer (cleared by Read). Subclass fields start at +0x38.
// Quirks kept: Read accepts versions 0..3 only (a newer record answers false without reading anything); the controller read from a
// stream replaces the pointer without releasing the old one; PlayerDrive::Read refills the 13 floats at +0xa8 with -1.0;
// Plummet::Read clears the u64 at +0x50; Collide::Shutdown detaches and releases its object first.
#pragma once
#include <cstdint>

#include "gz/StreamOps.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "sc4/AutomataController.h"

namespace sc4 {

constexpr std::uint16_t kAutomataStateVersion = 3;
constexpr std::uint32_t kControllerFlagTransient = 0x8;   // controllers with this flag are saved as null
constexpr std::uint32_t kAutomatonIid = 0xa9b40f05;
constexpr std::uint32_t kAttachAnimIid = 0xa818edd8;

// The automaton as the states see it (vslot 0x1f0).
class IStateAutomaton {
public:
    virtual ~IStateAutomaton() = default;
    virtual void StopSound(std::uint32_t handle) = 0;   // vslot 0x1f0
};
// The object a Collide state holds (vslot 0x20 is called with `true` before it is released).
class IAutomataStateAttachment : public cIGZUnknown {
public:
    virtual void Slot3() {}
    virtual void Detach(bool final) = 0;   // vslot 0x20
};

class tAutomataState {
public:
    virtual ~tAutomataState() = default;
    virtual void Shutdown(IStateAutomaton* automaton);      // @ 0x10010e128 (vslot 0x00)
    virtual bool Write(cIGZOStream& stream) const;          // @ 0x10010e164 (vslot 0x08)
    virtual bool Read(cIGZIStream& stream);                 // @ 0x10010e214 (vslot 0x10)

    cSC4AutomataController* controller = nullptr;   // +0x10
    std::uint32_t stateId = 0;                      // +0x18
    float timer = 0.0f;                             // +0x1c
    bool flag = false;                              // +0x20
    std::uint32_t soundHandle = 0;                  // +0x24
    std::uint32_t value28 = 0;                      // +0x28
    std::uint32_t value2c = 0;                      // +0x2c
    std::uint64_t runtime30 = 0;                    // +0x30
};

class tAutomataStateRandomWalk : public tAutomataState {   // @ Write 0x10010e2e2, Read 0x10010e33a
public:
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    cS3DVector3 target;        // +0x38
    float speed = 0.0f;        // +0x44
    float turnRate = 0.0f;     // +0x48
};
class tAutomataStateRiotLeader : public tAutomataState {   // @ Write 0x10010e390, Read 0x10010e3e6
public:
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::int32_t value38 = 0, value3c = 0;   // +0x38, +0x3c
    std::uint8_t value40 = 0;                // +0x40
};
class tAutomataStatePlopJump : public tAutomataState {      // @ Write 0x10010e43e, Read 0x10010e48a
public:
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    float value38 = 0.0f, value3c = 0.0f;
};
class tAutomataStatePlummet : public tAutomataState {       // @ Write 0x10010e4d4, Read 0x10010e54c
public:
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    float value38 = 0, value3c = 0, value40 = 0, value44 = 0, value48 = 0;
    std::uint64_t runtime50 = 0;   // +0x50 (cleared by Read)
};
class tAutomataStatePullOver : public tAutomataState {      // @ Write 0x10010e5c2, Read 0x10010e5fe
public:
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    float value38 = 0.0f;
};
class tAutomataStatePlayerDrive : public tAutomataState {   // @ Shutdown 0x10010e63c, Write 0x10010e678, Read 0x10010e6b4
public:
    void Shutdown(IStateAutomaton* automaton) override;
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    float value38 = 0.0f;
    float history[13];   // +0xa8: refilled with -1.0f by Read
    tAutomataStatePlayerDrive() { for (float& v : history) v = -1.0f; }
};
class tAutomataStateBumped : public tAutomataState {        // @ Write 0x10010e70a, Read 0x10010e76a
public:
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    cS3DVector3 v38, v44, v50, v5c;
};
class tAutomataStateCollide : public tAutomataState {       // @ Write 0x10010e7ca, Read 0x10010e7f6, Shutdown 0x10010e822
public:
    ~tAutomataStateCollide() override = default;
    void Shutdown(IStateAutomaton* automaton) override;
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    IAutomataStateAttachment* attachment = nullptr;   // +0x38
};
class tAutomataStateAttachAnim : public tAutomataState {    // @ Write 0x10010e884, Read 0x10010e8ec
public:
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    cIGZUnknown* animation = nullptr;   // +0x38 (counted; the original keeps the 0xa818edd8 interface pointer)
    bool flag40 = false;                // +0x40
    std::uint32_t value44 = 0;          // +0x44 (saved as 16 bit)
};
class tAutomataStateChase : public tAutomataState {         // @ Write 0x10010e984, Read 0x10010e9be
public:
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    cIGZUnknown* target = nullptr;   // +0x38 (counted automaton)
};

}  // namespace sc4
