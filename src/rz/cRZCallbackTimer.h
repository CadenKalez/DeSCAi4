// cRZCallbackTimer — periodic / one-shot timer that invokes a plain function pointer.
// Original: thin wrapper over SDL_AddTimer/SDL_RemoveTimer (0x100637244..0x100637600).
//
// Layout (0x38): +0x10 callback, +0x18 user pointer, +0x20 resolution (100), +0x24 period (1000),
// +0x28 running, +0x29 one-shot, +0x2c SDL timer id, +0x30 optional cIGZUnknown held while armed.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cRZCallbackTimer {
public:
    using CallbackFunction = void (*)(void*);

    cRZCallbackTimer() = default;
    virtual ~cRZCallbackTimer();

    // Resolution/period changes re-arm a running timer (stop, then start again, same mode).
    virtual bool SetResolution(std::uint32_t resolution);
    virtual bool GetResolution(std::uint32_t& resolution) const { resolution = resolution_; return true; }
    virtual bool SetPeriod(std::uint32_t period);
    virtual bool GetPeriod(std::uint32_t& period) const { period = period_; return true; }
    // `period == 0` keeps the current period. `holdWhileRunning` (may be null) is AddRef'd while
    // the timer is armed and released when it stops/fires one-shot. False if already running, no
    // callback is set, or the OS timer could not be created.
    virtual bool StartPeriodic(std::uint32_t period, cIGZUnknown* holdWhileRunning);
    virtual bool StartOneShot(std::uint32_t period, cIGZUnknown* holdWhileRunning);
    virtual void Stop();
    virtual bool IsRunning() const { return running_; }
    virtual bool SetCallbackFunction(CallbackFunction function, void* userData) {
        callback_ = function;
        userData_ = userData;
        return true;
    }
    virtual void EnableSharedTimerUse(bool) {}  // no-op in this build

    static std::uint32_t GetCurrentSystemMillisecondTime();  // clock() as the original does

private:
    bool Start(std::uint32_t period, cIGZUnknown* holdWhileRunning, bool oneShot);
    static std::uint32_t InternalCallbackFunction(std::uint32_t interval, void* self);

    [[maybe_unused]] bool sharedTimerUse_ = true;  // stored by the original, never read
    CallbackFunction callback_ = nullptr;
    void* userData_ = nullptr;
    std::uint32_t resolution_ = 100;
    std::uint32_t period_ = 1000;
    bool running_ = false;
    bool oneShot_ = false;
    std::int32_t timerId_ = 0;
    cIGZUnknown* held_ = nullptr;
};
