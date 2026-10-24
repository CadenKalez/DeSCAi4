// cRZTime: the two static dates and the millisecond clock's exact arithmetic.
#include <cstdio>
#include <ctime>

#include "rz/RZTime.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    // the static-initialised dates (__GLOBAL__sub_I_RZTime.cpp)
    CHECK(cRZTime::mRefDate.JulianDay() == 249522);
    CHECK(cRZTime::MaxDate.JulianDay() == cRZDate::FromDayOfYear(0xc22d, 0).JulianDay());

    // the clock helper: monotonic, and bit-identical to the assembly's (clock() >> 3) / 1000
    const std::uint32_t first = cRZTime::GetMillisecondSystemTime();
    const std::uint32_t expected = static_cast<std::uint32_t>((static_cast<std::uint64_t>(std::clock()) >> 3) / 125u);
    CHECK(first == expected || first + 1 == expected || first == expected + 1);   // clock may tick between calls
    const std::uint32_t second = cRZTime::GetMillisecondSystemTime();
    CHECK(second >= first);

    if (failures == 0) std::printf("rz_time: all checks passed\n");
    return failures != 0;
}
