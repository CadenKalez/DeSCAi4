// Recovered from MacTools::Sleep @ 0x100796c1a (Sim City 4 Deluxe Editionsub 1.2.1)
#pragma once
#include <cstdint>

namespace MacTools {

// Sleeps for `milliseconds`. A value of 0 still yields (1 ns sleep), as in the original.
void Sleep(std::uint32_t milliseconds);

}  // namespace MacTools
