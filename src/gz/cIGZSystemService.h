// cIGZSystemService — a framework service (sound manager, resource manager, ...). Slots 3..12.
// Recovered from the vtable of cRZSystemService @ 0x100a688b0 (slot 12 default @ 0x100040410).
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZSystemService : public cIGZUnknown {
public:
    virtual std::uint32_t GetServiceID() const = 0;          // [3]
    virtual void SetServiceID(std::uint32_t id) = 0;         // [4]
    virtual std::int32_t GetServicePriority() const = 0;     // [5]
    virtual bool IsServiceRunning() const = 0;               // [6]
    virtual void SetServiceRunning(bool running) = 0;        // [7]
    virtual bool Init() = 0;                                 // [8]
    virtual bool Shutdown() = 0;                             // [9]
    virtual bool OnTick(std::uint32_t elapsedMs) = 0;        // [10]
    virtual bool OnIdle(std::uint32_t elapsedMs) = 0;        // [11]
    // [12] non-const overload with a default body that returns 0; cRZSystemService adds a const
    // overload (slot 13) that returns the stored tick priority. Callers through this interface
    // therefore always see 0.
    virtual std::int32_t GetServiceTickPriority() { return 0; }
};

constexpr std::uint32_t kGZIID_cIGZSystemService = 0x287fb697;  // accepted by cRZSystemService::QueryInterface
