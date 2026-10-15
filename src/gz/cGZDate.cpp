#include "gz/cGZDate.h"

#include <algorithm>

cGZDate::cGZDate() : julianDay_(cRZDate().JulianDay()) {}

bool cGZDate::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 1 || iid == kGZIID_cIGZDate) {
        *out = static_cast<cIGZDate*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZDate::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return --refCount_;
}

void cGZDate::Set(std::uint32_t dayOfYear, std::uint32_t year) {
    if (year == 0) julianDay_ = dayOfYear + kEpochDayOffset;
    else julianDay_ = cRZDate::ComputeJulianDay(12, 31, year - 1) + dayOfYear;
}

void cGZDate::Set(std::uint32_t month, std::uint32_t day, std::uint32_t year) {
    julianDay_ = cRZDate::ComputeJulianDay(month, day, year);
}

void cGZDate::Set(const cRZTime& time) {
    julianDay_ = time.seconds == 0 ? 0 : time.seconds / 86400u + kEpochDayOffset;
}

void cGZDate::Set(const char* monthName, std::uint32_t day, std::uint32_t year) {
    cRZDate d = cRZDate::FromJulianDay(julianDay_);
    d.SetFromMonthNameDayYear(monthName, day, year);
    julianDay_ = d.JulianDay();
}

void cGZDate::Clone(cIGZDate** out) const {
    auto* copy = new cGZDate(*this);
    copy->AddRef();
    *out = copy;
}

bool cGZDate::Between(const cIGZDate& a, const cIGZDate& b) const {
    std::uint32_t lo = a.DayNumber(), hi = b.DayNumber();
    if (hi < lo) std::swap(lo, hi);
    return julianDay_ <= hi && lo <= julianDay_;
}

std::int32_t cGZDate::CompareTo(const cIGZDate& other) const {
    const std::uint32_t o = other.DayNumber();
    if (o > julianDay_) return -1;
    return o < julianDay_ ? 1 : 0;
}

std::uint32_t cGZDate::FirstDayOfMonth() const {
    return cRZDate::FromJulianDay(julianDay_).FirstDayOfMonth(Month());
}

// the date of the last `weekday` (1..7, same numbering as WeekDay) before this one
void cGZDate::Previous(std::uint32_t weekday, cIGZDate& out) const {
    const std::uint32_t current = ((julianDay_ + 1) % 7 + 6) % 7;     // 0-based WeekDay
    const std::int32_t step = current < weekday - 1 ? -8 : -1;
    out.Set(julianDay_ + weekday - current + static_cast<std::uint32_t>(step));
}

void cGZDate::DateString(cIGZString& out, std::uint32_t style) const {
    const std::string s = cRZDate::FromJulianDay(julianDay_).DateString(style);
    out.AssignFromChars(s.data(), static_cast<std::uint32_t>(s.size()));
}
