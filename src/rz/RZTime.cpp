// cRZTime: the two static dates and the millisecond clock.
#include "rz/RZTime.h"

#include <ctime>

// 0x100b73888 / 0x100b73890, static-initialised by __GLOBAL__sub_I_RZTime.cpp (0x1007b0fa6):
//   mRefDate.julianDay = 249522    - the reference the game's saved dates are relative to
//   MaxDate  = cRZDate(0xc22d, 0)
// The original's two-argument cRZDate(unsigned, unsigned) is the port's FromDayOfYear.
cRZDate cRZTime::mRefDate = cRZDate::FromJulianDay(249522);   // 0x100b73888
cRZDate cRZTime::MaxDate = cRZDate::FromDayOfYear(0xc22d, 0);

// @ 0x1007b0f84. clock() >> 3, then the assembly's signed divide-by-1000 sequence:
//   movabs $0x20c49ba5e353f7cf, %rcx ; mulq %rcx ; shr $4, %rdx ; movl %edx, %eax
// The multiply's high half shifted right by four is a division by 1000, and the result is truncated to 32 bits.
std::uint32_t cRZTime::GetMillisecondSystemTime() {
    const std::uint64_t ticks = static_cast<std::uint64_t>(std::clock()) >> 3;
    constexpr std::uint64_t kDivideBy1000 = 0x20c49ba5e353f7cfull;
    const unsigned __int128 product = static_cast<unsigned __int128>(ticks) * kDivideBy1000;
    const std::uint64_t high = static_cast<std::uint64_t>(product >> 64);
    return static_cast<std::uint32_t>(high >> 4);
}
