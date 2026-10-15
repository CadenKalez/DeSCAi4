// cIGZDate / cGZDate — the reference-counted calendar date object of the GZ framework: a Julian day
// number (0 = invalid) with the cIGZDate interface (iid 0x43378255). 54 functions at 0x10003e37a..0x10003f2xx.
// Calendar arithmetic comes from cRZDate.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZDate.h"

constexpr std::uint32_t kGZIID_cIGZDate = 0x43378255;

// seconds since the epoch the date code uses (only the leading dword matters to cGZDate::Set)
struct cRZTime { std::uint32_t seconds = 0; };

class cIGZDate : public cIGZUnknown {
public:
    virtual void Set(std::uint32_t dayOfYear, std::uint32_t year) = 0;                  // [3]
    virtual void Set(std::uint32_t month, std::uint32_t day, std::uint32_t year) = 0;   // [4]
    virtual void Set(const cRZTime& time) = 0;                                          // [5]
    virtual void Set(std::uint32_t julianDay) = 0;                                      // [6]
    virtual void Set(const char* monthName, std::uint32_t day, std::uint32_t year) = 0; // [7]
    virtual void Copy(const cIGZDate& other) = 0;                                       // [8]
    virtual void Clone(cIGZDate** out) const = 0;                                       // [9]
    virtual bool Between(const cIGZDate& a, const cIGZDate& b) const = 0;               // [10]
    virtual std::int32_t CompareTo(const cIGZDate& other) const = 0;                    // [11]
    virtual std::uint32_t WeekDay() const = 0;                                          // [12] 1..7
    virtual std::uint32_t Year() const = 0;                                             // [13]
    virtual std::uint32_t DayOfYear() const = 0;                                        // [14]
    virtual std::uint32_t DayOfMonth() const = 0;                                       // [15]
    virtual std::uint32_t FirstDayOfMonth() const = 0;                                  // [16]
    virtual std::uint32_t FirstDayOfMonth(std::uint32_t month) const = 0;               // [17]
    virtual std::uint32_t Hash() const = 0;                                             // [18]
    virtual bool IsValid() const = 0;                                                   // [19]
    virtual bool Leap() const = 0;                                                      // [20]
    virtual void MaxDate(const cIGZDate& other, cIGZDate& out) const = 0;               // [21]
    virtual void MinDate(const cIGZDate& other, cIGZDate& out) const = 0;               // [22]
    virtual std::uint32_t Month() const = 0;                                            // [23]
    virtual void Previous(std::uint32_t weekday, cIGZDate& out) const = 0;              // [24]
    virtual std::uint32_t YearLastTwoDigits() const = 0;                                // [25]
    virtual std::uint32_t DayNumber() const = 0;                                        // [26]
    virtual bool operator<(const cIGZDate& o) const = 0;                                // [27]
    virtual bool operator<=(const cIGZDate& o) const = 0;                               // [28]
    virtual bool operator>(const cIGZDate& o) const = 0;                                // [29]
    virtual bool operator>=(const cIGZDate& o) const = 0;                               // [30]
    virtual bool operator==(const cIGZDate& o) const = 0;                               // [31]
    virtual bool operator!=(const cIGZDate& o) const = 0;                               // [32]
    virtual std::int32_t operator-(const cIGZDate& o) const = 0;                        // [33]
    virtual std::int32_t operator+(const cIGZDate& o) const = 0;                        // [34]
    virtual std::int32_t operator-(std::int32_t days) const = 0;                        // [35]
    virtual std::int32_t operator+(std::int32_t days) const = 0;                        // [36]
    virtual cIGZDate& operator++() = 0;                                                 // [37]
    virtual cIGZDate& operator--() = 0;                                                 // [38]
    virtual cIGZDate& operator+=(std::int32_t days) = 0;                                // [39]
    virtual cIGZDate& operator-=(std::int32_t days) = 0;                                // [40]
    virtual void DateString(cIGZString& out, std::uint32_t style) const = 0;            // [41]
    virtual bool DayWithinMonth(std::uint32_t month, std::uint32_t day, std::uint32_t year) = 0;   // [42]
    virtual std::uint32_t DaysInYear(std::uint32_t year) = 0;                           // [43]
    virtual std::uint32_t Jday(std::uint32_t month, std::uint32_t day, std::uint32_t year) = 0;    // [44]
    virtual bool LeapYear(std::uint32_t year) = 0;                                      // [45]
};

class cGZDate final : public cIGZDate {
public:
    cGZDate();                                    // today
    explicit cGZDate(const cIGZDate& other) : julianDay_(other.DayNumber()) {}
    explicit cGZDate(const cRZDate& d) : julianDay_(d.JulianDay()) {}
    cGZDate(std::uint32_t month, std::uint32_t day, std::uint32_t year) : julianDay_(cRZDate::ComputeJulianDay(month, day, year)) {}
    explicit cGZDate(std::uint32_t julianDay) : julianDay_(julianDay) {}
    cGZDate(const cGZDate& other) : cIGZDate(), julianDay_(other.julianDay_) {}   // (fresh reference count)
    cGZDate& operator=(const cGZDate& other) { julianDay_ = other.julianDay_; return *this; }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    void Set(std::uint32_t dayOfYear, std::uint32_t year) override;
    void Set(std::uint32_t month, std::uint32_t day, std::uint32_t year) override;
    void Set(const cRZTime& time) override;
    void Set(std::uint32_t julianDay) override { julianDay_ = julianDay; }
    void Set(const char* monthName, std::uint32_t day, std::uint32_t year) override;
    void Copy(const cIGZDate& other) override { julianDay_ = other.DayNumber(); }
    void Clone(cIGZDate** out) const override;
    bool Between(const cIGZDate& a, const cIGZDate& b) const override;
    std::int32_t CompareTo(const cIGZDate& other) const override;
    std::uint32_t WeekDay() const override { return ((julianDay_ + 1) % 7 + 6) % 7 + 1; }
    std::uint32_t Year() const override { return cRZDate::FromJulianDay(julianDay_).Year(); }
    std::uint32_t DayOfYear() const override { return cRZDate::FromJulianDay(julianDay_).DayOfYear(); }
    std::uint32_t DayOfMonth() const override { return cRZDate::FromJulianDay(julianDay_).DayOfMonth(); }
    std::uint32_t FirstDayOfMonth() const override;
    std::uint32_t FirstDayOfMonth(std::uint32_t month) const override { return cRZDate::FromJulianDay(julianDay_).FirstDayOfMonth(month); }
    std::uint32_t Hash() const override { return julianDay_; }
    bool IsValid() const override { return julianDay_ != 0; }
    bool Leap() const override { return LeapYearValue(Year()); }
    void MaxDate(const cIGZDate& other, cIGZDate& out) const override { out.Set(std::max(julianDay_, other.DayNumber())); }
    void MinDate(const cIGZDate& other, cIGZDate& out) const override { out.Set(std::min(julianDay_, other.DayNumber())); }
    std::uint32_t Month() const override { return cRZDate::FromJulianDay(julianDay_).Month(); }
    void Previous(std::uint32_t weekday, cIGZDate& out) const override;
    std::uint32_t YearLastTwoDigits() const override { return Year() % 100; }
    std::uint32_t DayNumber() const override { return julianDay_; }
    bool operator<(const cIGZDate& o) const override { return julianDay_ < o.DayNumber(); }
    bool operator<=(const cIGZDate& o) const override { return julianDay_ <= o.DayNumber(); }
    bool operator>(const cIGZDate& o) const override { return julianDay_ > o.DayNumber(); }
    bool operator>=(const cIGZDate& o) const override { return julianDay_ >= o.DayNumber(); }
    bool operator==(const cIGZDate& o) const override { return julianDay_ == o.DayNumber(); }
    bool operator!=(const cIGZDate& o) const override { return julianDay_ != o.DayNumber(); }
    std::int32_t operator-(const cIGZDate& o) const override { return std::int32_t(julianDay_ - o.DayNumber()); }
    std::int32_t operator+(const cIGZDate& o) const override { return std::int32_t(julianDay_ + o.DayNumber()); }
    std::int32_t operator-(std::int32_t days) const override { return std::int32_t(julianDay_) - days; }
    std::int32_t operator+(std::int32_t days) const override { return std::int32_t(julianDay_) + days; }
    cIGZDate& operator++() override { ++julianDay_; return *this; }
    cIGZDate& operator--() override { --julianDay_; return *this; }
    cIGZDate& operator+=(std::int32_t days) override { julianDay_ += days; return *this; }
    cIGZDate& operator-=(std::int32_t days) override { julianDay_ -= days; return *this; }
    void DateString(cIGZString& out, std::uint32_t style) const override;
    bool DayWithinMonth(std::uint32_t month, std::uint32_t day, std::uint32_t year) override { return cRZDate::DayWithinMonth(month, day, year); }
    std::uint32_t DaysInYear(std::uint32_t year) override { return LeapYearValue(year) ? 366 : 365; }
    std::uint32_t Jday(std::uint32_t month, std::uint32_t day, std::uint32_t year) override { return cRZDate::ComputeJulianDay(month, day, year); }
    bool LeapYear(std::uint32_t year) override { return LeapYearValue(year); }

    static constexpr std::uint32_t kEpochDayOffset = 0x24db1a;   // Julian day of 1 January of year 0 of the engine's epoch

private:
    static bool LeapYearValue(std::uint32_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
    std::uint32_t julianDay_ = 0;     // +0x08
    std::uint32_t refCount_ = 0;      // +0x18
};
