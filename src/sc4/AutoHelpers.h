// Small scope helpers (0x1000391xx..0x100043144): cSC4AutoFlushInputMessageQueue, cSC4AutoDisableAppSwitching,
// cSC4AutoGamePause and the record streams cSC4AutoLoadStream / cSC4AutoSaveStream, plus SC4GamePause. Each follows the
// original's constructor/destructor pairing exactly.
//  * AutoFlushInputMessageQueue(ms): every Flush() (and at scope end) the window manager's pending input is dropped
//    once at least `ms` milliseconds passed since the last flush.
//  * AutoDisableAppSwitching: the Windows task-switch block; empty on the Mac build.
//  * AutoGamePause(...): SC4GamePause(true, ...) on entry and SC4GamePause(false, ...) on exit with the same flags.
//  * AutoLoadStream / AutoSaveStream: open the record {type, group, instance} of a segment as a stream (saving also
//    writes the u16 version, loading reads it); stay closed when the open or the version I/O fails.
#pragma once
#include <cstdint>
#include <functional>

#include "gz/cIGZPersistDBSegment.h"
#include "rz/cRZTimer.h"
#include "sc4/DBSegment.h"

namespace sc4 {

// Things SC4GamePause pauses (graphic system, animation ticks, simulator, view).
struct GamePauseServices {
    std::function<void(bool pause)> graphics;       // service 0xc416025c: pause (vslot 0x90) / resume (0x98)
    std::function<void(bool pause)> animation;      // service 0x86ad10ec: slot 0x60(1/0)
    std::function<void(bool pause)> simulator;      // service 0x46956659: HiddenPause (0x50) / HiddenResume (0x68)
    std::function<void(bool pause)> view;           // the application's 3D view, slot 0xd0
    std::function<void()> flushInput;               // window manager vslot 0x98
};
GamePauseServices& GetGamePauseServices();
// SC4GamePause(pause, graphics, animation, simulator, view)  @ 0x10003903e
void SC4GamePause(bool pause, bool graphics, bool animation, bool simulator, bool view);

class cSC4AutoFlushInputMessageQueue {
public:
    explicit cSC4AutoFlushInputMessageQueue(std::uint32_t intervalMs);
    ~cSC4AutoFlushInputMessageQueue() { Flush(); }
    void Flush();
private:
    std::uint32_t interval_;           // +0
    cRZTimer timer_;                   // +8, milliseconds
};

class cSC4AutoDisableAppSwitching {
public:
    cSC4AutoDisableAppSwitching() = default;
    ~cSC4AutoDisableAppSwitching() = default;
};

class cSC4AutoGamePause {
public:
    cSC4AutoGamePause(bool graphics, bool animation, bool simulator, bool view)
        : flags_{graphics, animation, simulator, view} { SC4GamePause(true, graphics, animation, simulator, view); }
    ~cSC4AutoGamePause() { SC4GamePause(false, flags_[0], flags_[1], flags_[2], flags_[3]); }
private:
    bool flags_[4];
};

class cSC4AutoLoadStream {
public:
    cSC4AutoLoadStream(cIGZPersistDBSegment* segment, std::uint32_t type, std::uint32_t group, std::uint32_t instance);
    virtual ~cSC4AutoLoadStream();
    bool IsOpen() const { return segment_ != nullptr && stream_ != nullptr; }
    cISC4DBSegmentIStream* GetStream() const { return stream_; }
    std::uint16_t GetVersion() const { return version_; }
protected:
    bool openStream(cISC4DBSegment* segment, std::uint32_t type, std::uint32_t group, std::uint32_t instance);
    cISC4DBSegment* segment_ = nullptr;           // +8
    cISC4DBSegmentIStream* stream_ = nullptr;     // +0x10
    std::uint16_t version_ = 0xffff;              // +0x18
};

class cSC4AutoSaveStream {
public:
    cSC4AutoSaveStream(cIGZPersistDBSegment* segment, std::uint32_t type, std::uint32_t group, std::uint32_t instance,
                       std::uint16_t version);
    virtual ~cSC4AutoSaveStream();
    bool IsOpen() const { return segment_ != nullptr && stream_ != nullptr; }
    cISC4DBSegmentOStream* GetStream() const { return stream_; }
protected:
    bool openStream(cISC4DBSegment* segment, std::uint32_t type, std::uint32_t group, std::uint32_t instance, std::uint16_t version);
    cISC4DBSegment* segment_ = nullptr;
    cISC4DBSegmentOStream* stream_ = nullptr;
    std::uint16_t version_ = 0xffff;
};

}  // namespace sc4
