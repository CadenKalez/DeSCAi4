#include "rz/cRZDate.h"
#include <cassert>
#include <cstdio>
int main() {
    // Known anchors (JDN of 2000-01-01 is 2451545; 1970-01-01 is 2440588).
    assert(cRZDate::ComputeJulianDay(1, 1, 2000) == 2451545);
    assert(cRZDate::ComputeJulianDay(1, 1, 1970) == 2440588);
    assert(cRZDate::ComputeJulianDay(2, 30, 2000) == 0 && cRZDate::ComputeJulianDay(0, 1, 2000) == 0 && cRZDate::ComputeJulianDay(13, 1, 2000) == 0);
    assert(cRZDate::ComputeJulianDay(2, 29, 2000) != 0 && cRZDate::ComputeJulianDay(2, 29, 1900) == 0 && cRZDate::ComputeJulianDay(2, 29, 2004) != 0);
    // Round-trip every day across 1900..2100.
    std::uint32_t j = cRZDate::ComputeJulianDay(1, 1, 1900);
    for (std::uint32_t y = 1900; y <= 2100; ++y)
        for (std::uint32_t m = 1; m <= 12; ++m)
            for (std::uint32_t d = 1; d <= 31; ++d) {
                if (!cRZDate::DayWithinMonth(m, d, y)) continue;
                cRZDate dt(m, d, y);
                assert(dt.JulianDay() == j++);
                std::uint32_t mm, dd, yy; dt.GetMonthDayYear(mm, dd, yy);
                assert(mm == m && dd == d && yy == y);
            }
    cRZDate a(3, 1, 2024);                       // 2024 is a leap year
    assert(a.DayOfYear() == 61 && a.FirstDayOfMonth(3) == 61 && a.FirstDayOfMonth(1) == 1 && a.FirstDayOfMonth(13) == 0);
    assert(cRZDate(3, 1, 2023).DayOfYear() == 60);
    assert(cRZDate::FromDayOfYear(61, 2024).JulianDay() == a.JulianDay());
    cRZDate b(1, 1, 1);
    b.SetFromMonthNameDayYear("sEpTeMbEr", 9, 1999);
    assert(b.Month() == 9 && b.DayOfMonth() == 9 && b.Year() == 1999);
    b.SetFromMonthNameDayYear("???", 5, 2001); assert(b.Month() == 1);
    cRZDate c(7, 4, 2005);
    assert(c.DateString(1) == "7/4/2005" && c.DateString(0) == "7/4/2005");
    assert(c.DateString(2) == "4/7/2005" && c.DateString(3) == "4.7.2005" && c.DateString(7) == "4-7-2005");
    assert(c.DateString(9) == "2005.7.4" && c.DateString(15) == "2005/7/4");
    // Report the year-0 base the original hard-codes so the constant can be sanity-checked.
    std::printf("Jday(12,31,1899)=%u  year0 base=%u\n", cRZDate::ComputeJulianDay(12, 31, 1899), 0x24db1au);
    std::puts("cRZDate ok");
}
