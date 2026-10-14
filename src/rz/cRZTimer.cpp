#include "rz/cRZTimer.h"

#include "platform/MacTools.h"

#include <SDL.h>

namespace {

constexpr float kMsPerSecondF = 1000.0f;
constexpr float kMsPerMinuteF = 60000.0f;

}  // namespace

// Shared by all three GetElapsedTime overloads (each repeats this inline in the original).
// The 32-bit tick delta is taken modulo 2^32, so SDL_GetTicks wrap-around no longer
// produces a huge value as the original's 64-bit subtraction did.
std::uint64_t cRZTimer::ElapsedMilliseconds() const noexcept {
    if (startTick_ == 0) {
        return accumulated_;
    }
    const auto now = SDL_GetTicks();
    return accumulated_ + static_cast<std::uint32_t>(now - static_cast<std::uint32_t>(startTick_));
}

// cRZTimer::GetElapsedTime() const @ 0x1007b6d54
std::uint32_t cRZTimer::GetElapsedTimeU32() const noexcept {
    const std::uint64_t ms = ElapsedMilliseconds();
    switch (units_) {
        case kMicroseconds: return static_cast<std::uint32_t>(ms * 1000);
        case kMilliseconds: return static_cast<std::uint32_t>(ms);
        case kSeconds:      return static_cast<std::uint32_t>(ms / 1000);
        case kMinutes:      return static_cast<std::uint32_t>(ms / 60000);
        default:            return 0;
    }
}

// cRZTimer::GetElapsedTime(unsigned long long&) const @ 0x1007b6df0
void cRZTimer::GetElapsedTimeU64(std::uint64_t& out) const noexcept {
    const std::uint64_t ms = ElapsedMilliseconds();
    switch (units_) {
        case kMicroseconds: out = ms * 1000; break;
        case kMilliseconds: out = ms; break;
        case kSeconds:      out = ms / 1000; break;
        case kMinutes:      out = ms / 60000; break;
        default: break;
    }
}

// cRZTimer::GetElapsedTime(float&) const @ 0x1007b6e94
void cRZTimer::GetElapsedTimeFloat(float& out) const noexcept {
    const auto ms = static_cast<float>(ElapsedMilliseconds());
    switch (units_) {
        case kMicroseconds: out = ms * 1000.0f; break;
        case kMilliseconds: out = ms; break;
        case kSeconds:      out = ms / kMsPerSecondF; break;
        case kMinutes:      out = ms / kMsPerMinuteF; break;
        default:            out = 0.0f; break;
    }
}

// cRZTimer::SetElapsedTime(float) @ 0x1007b6fa4
void cRZTimer::SetElapsedTimeFloat(float value) noexcept {
    switch (units_) {
        case kMicroseconds: value /= 1000.0f; break;
        case kMilliseconds: break;
        case kSeconds:      value *= kMsPerSecondF; break;
        case kMinutes:      value *= kMsPerMinuteF; break;
        default: return;  // original leaves the timer untouched
    }
    // The original's float->u64 conversion handles values >= 2^63; negative input is UB there too.
    accumulated_ = static_cast<std::uint64_t>(value);
}

// cRZTimer::SetElapsedTime(unsigned long long) @ 0x1007b7024
void cRZTimer::SetElapsedTimeU64(std::uint64_t value) noexcept {
    switch (units_) {
        case kNanoseconds:  value /= 1'000'000; break;
        case kMicroseconds: value /= 1000; break;
        case kMilliseconds: break;
        case kSeconds:      value *= 1000; break;
        case kMinutes:      value *= 60000; break;
        default: return;
    }
    accumulated_ = value;
}

// cRZTimer::Restart() @ 0x1007b70a4
void cRZTimer::Restart() noexcept {
    startTick_ = SDL_GetTicks();
    accumulated_ = 0;
}

// cRZTimer::Start() @ 0x1007b70c8
void cRZTimer::Start() noexcept {
    startTick_ = SDL_GetTicks();
}

// cRZTimer::Stop() @ 0x1007b70e4
void cRZTimer::Stop() noexcept {
    if (startTick_ != 0) {
        accumulated_ += static_cast<std::uint32_t>(SDL_GetTicks() - static_cast<std::uint32_t>(startTick_));
        startTick_ = 0;
    }
}

// cRZTimer::Delay(unsigned int) @ 0x1007b7112
// Original polls a stack-local timer and sleeps duration/1000 ms between polls.
void cRZTimer::Delay(std::uint32_t duration) const noexcept {
    cRZTimer local(units_);
    local.Start();
    while (local.GetElapsedTimeU32() < duration) {
        MacTools::Sleep(duration / 1000);
    }
}

// cRZTimer::GetTimerCycle(unsigned long long*) @ 0x1007b717a
void cRZTimer::GetTimerCycle(std::uint64_t* cycle) noexcept {
    *cycle = SDL_GetPerformanceCounter();
}

// cRZTimer::GetSecondsToTimerCyclesRatio() @ 0x1007b7192
double cRZTimer::GetSecondsToTimerCyclesRatio() noexcept {
    return 1.0 / static_cast<double>(SDL_GetPerformanceFrequency());
}

// cRZTimer::GetMillisecondsToCPUCyclesRatio() @ 0x1007b71c2
double cRZTimer::GetMillisecondsToCPUCyclesRatio() noexcept {
    return 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
}
