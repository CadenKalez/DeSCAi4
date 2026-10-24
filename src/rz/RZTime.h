// cRZTime - the time service's static state and the millisecond clock (0x1007b0f84 and the two statics at
// 0x100b73888 / 0x100b73890). The class itself is a thin namespace in the binary: two reference dates plus the
// system clock helper, and several recovered classes read mRefDate directly (cSC4PropDeveloper seeds its random
// generator from "today - mRefDate", and the stream operators write dates relative to it).
//
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1.
//   0x1007b0f84  GetMillisecondSystemTime   clock() >> 3, then the signed divide-by-1000 magic (0x20c49ba5e353f7cf,
//                                           high half >> 4), truncated to 32 bits
//   0x1007b0fa6  __GLOBAL__sub_I_RZTime.cpp  mRefDate = cRZDate(0, 0); MaxDate = cRZDate(0xc22d, 0)
#pragma once
#include <cstdint>

#include "rz/cRZDate.h"

class cRZTime {
public:
    // Milliseconds since the process started, as the original computes them. The >>3 before the divide is the
    // binary's own (CLOCKS_PER_SEC is 1e6, so the result runs at 1/8 of the wall clock - kept as it is).
    static std::uint32_t GetMillisecondSystemTime();

    // The reference date the game's saved dates are relative to (static-initialised to julian day 0's date) and the
    // largest date it accepts (0xc22d).
    static cRZDate mRefDate;
    static cRZDate MaxDate;
};
