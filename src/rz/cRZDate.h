// cRZDate — Gregorian calendar date stored as a Julian day number.
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100a689d0).
//
// Original layout: +0x00 vptr (cIGZDate-derived), +0x08 uint32 julianDay. The vtable only holds
// NameOfDayString/NameOfMonthString (both empty stubs) and DateString, so this port is a plain
// value type with no vptr; the cIGZDate/cGZDate-taking constructors and the cRZString& overload
// of DateString are deferred until those interfaces are recovered (see notes/ledger.csv).
#pragma once
#include <cstdint>
#include <string>

class cRZDate {
public:
    // Today's date from the local clock.
    cRZDate() noexcept;
    // Julian day directly, as stored.
    static cRZDate FromJulianDay(std::uint32_t julianDay) noexcept { return cRZDate(julianDay, Tag{}); }
    // (month 1..12, day 1..31, year). Invalid dates give julian day 0.
    cRZDate(std::uint32_t month, std::uint32_t day, std::uint32_t year) noexcept
        : julianDay_(ComputeJulianDay(month, day, year)) {}
    // (day-of-year, year); year 0 is relative to the original's fixed epoch constant.
    // (A named factory: the original's two-argument constructor.)
    static cRZDate FromDayOfYear(std::uint32_t dayOfYear, std::uint32_t year) noexcept;

    void SetFromMonthDayYear(std::uint32_t month, std::uint32_t day, std::uint32_t year) noexcept { julianDay_ = ComputeJulianDay(month, day, year); }
    // `monthName` is matched on its first three characters, case-insensitively; unmatched -> January.
    void SetFromMonthNameDayYear(const char* monthName, std::uint32_t day, std::uint32_t year) noexcept;
    void SetJulianDay(std::uint32_t julianDay) noexcept { julianDay_ = julianDay; }

    std::uint32_t JulianDay() const noexcept { return julianDay_; }
    std::uint32_t Year() const noexcept;
    std::uint32_t Month() const noexcept;
    std::uint32_t DayOfMonth() const noexcept;
    std::uint32_t DayOfYear() const noexcept;
    void GetMonthDayYear(std::uint32_t& month, std::uint32_t& day, std::uint32_t& year) const noexcept;
    // 1-based julian day of `month`'s first day within this date's year (leap-aware); 0 if month invalid.
    std::uint32_t FirstDayOfMonth(std::uint32_t month) const noexcept;

    // Julian day for the given date, or 0 when the date is invalid.
    static std::uint32_t ComputeJulianDay(std::uint32_t month, std::uint32_t day, std::uint32_t year) noexcept;
    static bool DayWithinMonth(std::uint32_t month, std::uint32_t day, std::uint32_t year) noexcept;

    // Style codes: 0/1 -> M/D/YYYY, 3,4,10,11 -> D.M.YYYY, 7,8,14 -> D-M-YYYY,
    // 9 -> YYYY.M.D, 15 -> YYYY/M/D, anything else -> D/M/YYYY.
    std::string DateString(std::uint32_t style = 1) const;

    friend bool operator==(const cRZDate&, const cRZDate&) = default;

private:
    struct Tag {};
    cRZDate(std::uint32_t julianDay, Tag) noexcept : julianDay_(julianDay) {}
    std::uint32_t julianDay_ = 0;
};
