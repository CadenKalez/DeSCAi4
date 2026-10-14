#include "rz/cRZDate.h"

#include <strings.h>
#include <time.h>

#include <array>
#include <cstdio>

namespace {

constexpr std::array<std::uint8_t, 12> kDaysInMonth = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
// Day-of-year of the first day of each month in a non-leap year (table @ 0x100a15660).
constexpr std::array<std::uint32_t, 12> kFirstDayOfEachMonth = {1, 32, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};
constexpr std::array<const char*, 12> kMonthAbbrev = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

constexpr std::uint32_t kGregorianCycleDays = 146097;  // days per 400 years
constexpr std::uint32_t kJulianDayBias = 1721119;
// Base used by cRZDate(dayOfYear, 0). Equals ComputeJulianDay(1, 1, 1901): odd for a "year 0", kept verbatim.
constexpr std::uint32_t kYear0DayBase = 0x24db1a;

constexpr bool IsLeapYear(std::uint32_t y) noexcept { return (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0); }

}  // namespace

// cRZDate::cRZDate() @ 0x10003dbe0
cRZDate::cRZDate() noexcept {
    const time_t now = time(nullptr);
    const tm* local = localtime(&now);
    julianDay_ = ComputeJulianDay(static_cast<std::uint32_t>(local->tm_mon + 1), static_cast<std::uint32_t>(local->tm_mday),
                      static_cast<std::uint32_t>(local->tm_year + 1900));
}

// cRZDate::cRZDate(unsigned int, unsigned int) @ 0x10003dd32
cRZDate cRZDate::FromDayOfYear(std::uint32_t dayOfYear, std::uint32_t year) noexcept {
    return cRZDate((year == 0) ? dayOfYear + kYear0DayBase : ComputeJulianDay(12, 31, year - 1) + dayOfYear, Tag{});
}

// cRZDate::DayWithinMonth(unsigned int, unsigned int, unsigned int) @ 0x10003de3c
bool cRZDate::DayWithinMonth(std::uint32_t month, std::uint32_t day, std::uint32_t year) noexcept {
    if (day == 0 || month - 1 >= 12) {
        return false;
    }
    const std::uint32_t leapDay = (month == 2 && IsLeapYear(year)) ? 1u : 0u;
    return day <= kDaysInMonth[month - 1] + leapDay;
}

// cRZDate::ComputeJulianDay(unsigned int, unsigned int, unsigned int) @ 0x10003dc26
std::uint32_t cRZDate::ComputeJulianDay(std::uint32_t month, std::uint32_t day, std::uint32_t year) noexcept {
    if (!DayWithinMonth(month, day, year)) {
        return 0;
    }
    const std::uint32_t y = year - (month < 3 ? 1u : 0u);
    const std::uint32_t monthShift = (month > 2) ? static_cast<std::uint32_t>(-3) : 9u;
    const std::uint32_t century = y / 100;
    const std::uint32_t yearOfCentury = y % 100;
    return ((yearOfCentury * 1461) >> 2) + kJulianDayBias + day + ((century * kGregorianCycleDays) >> 2) +
           ((monthShift + month) * 153 + 2) / 5;
}

// cRZDate::Set(char const*, unsigned int, unsigned int) @ 0x10003dd9a
void cRZDate::SetFromMonthNameDayYear(const char* monthName, std::uint32_t day, std::uint32_t year) noexcept {
    std::uint32_t month = 1;
    for (std::uint32_t i = 0; i < 12; ++i) {
        if (strncasecmp(kMonthAbbrev[i], monthName, 3) == 0) {
            month = i + 1;
        }
    }
    julianDay_ = ComputeJulianDay(month, day, year);
}

// cRZDate::GetMonthDayYear(unsigned int&, unsigned int&, unsigned int&) const @ 0x10003df38
// Inverse of Jday. Arithmetic is kept in the original's 32-bit widths so julian day 0
// (the "invalid" marker) produces the same results as the shipped game.
void cRZDate::GetMonthDayYear(std::uint32_t& month, std::uint32_t& day, std::uint32_t& year) const noexcept {
    const std::uint32_t t = julianDay_ * 4u - 6884477u;
    const std::uint32_t century = t / kGregorianCycleDays;
    const std::uint32_t r = (t - century * kGregorianCycleDays) | 3u;
    const std::uint32_t yearOfCentury = r / 1461;
    const std::uint32_t dayIn4Years = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(r) + 4 - static_cast<std::uint64_t>(yearOfCentury) * 1461) >> 2);
    const std::uint32_t m = (dayIn4Years * 5u - 3u) / 153;
    day = static_cast<std::uint32_t>((static_cast<std::uint64_t>(dayIn4Years) * 5 + 2 - static_cast<std::uint64_t>(m) * 153) / 5);
    year = century * 100 + yearOfCentury;
    if (m < 10) {
        month = m + 3;
    } else {
        month = m - 9;
        year += 1;
    }
}

// cRZDate::Year/Month/DayOfMonth @ 0x10003deec / 0x10003e062 / 0x10003df12
std::uint32_t cRZDate::Year() const noexcept { std::uint32_t m, d, y; GetMonthDayYear(m, d, y); return y; }
std::uint32_t cRZDate::Month() const noexcept { std::uint32_t m, d, y; GetMonthDayYear(m, d, y); return m; }
std::uint32_t cRZDate::DayOfMonth() const noexcept { std::uint32_t m, d, y; GetMonthDayYear(m, d, y); return d; }

// cRZDate::DayOfYear() const @ 0x10003dea8
std::uint32_t cRZDate::DayOfYear() const noexcept {
    std::uint32_t m, d, y;
    GetMonthDayYear(m, d, y);
    return julianDay_ - ComputeJulianDay(12, 31, y - 1);
}

// cRZDate::FirstDayOfMonth(unsigned int) const @ 0x10003dfe0
std::uint32_t cRZDate::FirstDayOfMonth(std::uint32_t month) const noexcept {
    if (month - 1 >= 12) {
        return 0;
    }
    std::uint32_t first = kFirstDayOfEachMonth[month - 1];
    if (month > 2 && IsLeapYear(Year())) {
        first += 1;
    }
    return first;
}

// cRZDate::DateString(cRZString&, unsigned int) const @ 0x10003e094
// The original calls Mdy three times per format; once is enough.
std::string cRZDate::DateString(std::uint32_t style) const {
    std::uint32_t m, d, y;
    GetMonthDayYear(m, d, y);
    const char* fmt = "%u/%u/%4u";
    std::uint32_t a = d, b = m, c = y;  // default: D/M/Y
    switch (style) {
        case 0xffffffffu:
        case 0:
        case 1:
            a = m; b = d; c = y;  // M/D/Y
            break;
        case 3: case 4: case 10: case 11:
            fmt = "%u.%u.%4u";
            break;
        case 7: case 8: case 14:
            fmt = "%u-%u-%4u";
            break;
        case 9:
            fmt = "%4u.%u.%u"; a = y; b = m; c = d;
            break;
        case 15:
            fmt = "%4u/%u/%u"; a = y; b = m; c = d;
            break;
        default:
            break;
    }
    char buf[32];
    const int n = std::snprintf(buf, sizeof buf, fmt, a, b, c);
    return std::string(buf, n > 0 ? static_cast<std::size_t>(n) : 0);
}
