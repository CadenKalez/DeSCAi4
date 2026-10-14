// cIGZInitShutdown — objects with an explicit Init/Shutdown pair. Slots 3..4. Interface id 0x89e51472.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZInitShutdown : public cIGZUnknown {
public:
    virtual bool Init() = 0;      // [3]
    virtual bool Shutdown() = 0;  // [4]
};

constexpr std::uint32_t kGZIID_cIGZInitShutdown = 0x89e51472;
