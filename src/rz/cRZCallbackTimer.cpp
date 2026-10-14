#include "rz/cRZCallbackTimer.h"

#include <SDL.h>

#include <ctime>

cRZCallbackTimer::~cRZCallbackTimer() {
    Stop();
    if (held_ != nullptr) {
        held_->Release();
        held_ = nullptr;
    }
    callback_ = nullptr;
    userData_ = nullptr;
}

// cRZCallbackTimer::SetResolution @ 0x1006372f6 / SetPeriod @ 0x10063737e share this shape.
// The held object is pinned across the stop/start so the restart does not lose it.
bool cRZCallbackTimer::SetResolution(std::uint32_t resolution) {
    cIGZUnknown* pin = held_;
    if (pin != nullptr) pin->AddRef();
    resolution_ = resolution;
    if (running_) {
        const bool wasOneShot = oneShot_;
        Stop();
        if (wasOneShot) StartOneShot(0, nullptr); else StartPeriodic(0, nullptr);
    }
    if (pin != nullptr) {
        if (held_ != nullptr) pin->Release();
        held_ = pin;
    }
    return true;
}

bool cRZCallbackTimer::SetPeriod(std::uint32_t period) {
    cIGZUnknown* pin = held_;
    if (pin != nullptr) pin->AddRef();
    period_ = period;
    if (running_) {
        const bool wasOneShot = oneShot_;
        Stop();
        if (wasOneShot) StartOneShot(0, nullptr); else StartPeriodic(0, nullptr);
    }
    if (pin != nullptr) {
        if (held_ != nullptr) pin->Release();
        held_ = pin;
    }
    return true;
}

// cRZCallbackTimer::StartPeriodic @ 0x100637406, StartOneShot @ 0x10063751a
bool cRZCallbackTimer::Start(std::uint32_t period, cIGZUnknown* holdWhileRunning, bool oneShot) {
    if (running_ || callback_ == nullptr) return false;
    if (period != 0) period_ = period;
    if (holdWhileRunning != nullptr) {
        holdWhileRunning->AddRef();
        held_ = holdWhileRunning;
    }
    running_ = true;
    oneShot_ = oneShot;
    timerId_ = static_cast<std::int32_t>(SDL_AddTimer(period_, &cRZCallbackTimer::InternalCallbackFunction, this));
    if (timerId_ != 0) return true;
    running_ = false;
    oneShot_ = false;
    if (holdWhileRunning != nullptr) holdWhileRunning->Release();
    held_ = nullptr;
    return false;
}
bool cRZCallbackTimer::StartPeriodic(std::uint32_t period, cIGZUnknown* hold) { return Start(period, hold, false); }
bool cRZCallbackTimer::StartOneShot(std::uint32_t period, cIGZUnknown* hold) { return Start(period, hold, true); }

// cRZCallbackTimer::Stop() @ 0x100637596
void cRZCallbackTimer::Stop() {
    if (timerId_ == 0) return;
    if (held_ != nullptr) {
        held_->Release();
        held_ = nullptr;
    }
    running_ = false;
    oneShot_ = false;
    SDL_RemoveTimer(static_cast<SDL_TimerID>(timerId_));
    timerId_ = 0;
}

// cRZCallbackTimer::InternalCallbackFunction(unsigned int, void*) @ 0x10063747e
// Runs on the SDL timer thread. Returns the next interval (periodic) or 0 (one-shot, cancels).
std::uint32_t cRZCallbackTimer::InternalCallbackFunction(std::uint32_t interval, void* self) {
    auto* t = static_cast<cRZCallbackTimer*>(self);
    CallbackFunction fn = t->callback_;
    void* user = t->userData_;
    const bool oneShot = t->oneShot_;
    cIGZUnknown* hold = t->held_;
    if (hold != nullptr) hold->AddRef();      // keep the holder alive across the call
    if (oneShot) {
        if (t->held_ != nullptr) {
            t->held_->Release();
            t->held_ = nullptr;
        }
        t->running_ = false;
        t->oneShot_ = false;
    }
    fn(user);
    if (hold != nullptr) hold->Release();
    return oneShot ? 0 : interval;
}

// cRZCallbackTimer::GetCurrentSystemMillisecondTime() @ 0x1006375f6
std::uint32_t cRZCallbackTimer::GetCurrentSystemMillisecondTime() {
    return static_cast<std::uint32_t>(std::clock());
}
