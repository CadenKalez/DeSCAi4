#include <cstdio>
#include "gz/cGZDate.h"
#include "rz/cRZString.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
int main() {
    cGZDate d(1, 15, 2000);
    CHECK(d.IsValid() && d.Year() == 2000 && d.Month() == 1 && d.DayOfMonth() == 15 && d.DayOfYear() == 15);
    CHECK(d.Leap() && d.DaysInYear(1900) == 365 && d.DaysInYear(2000) == 366 && d.YearLastTwoDigits() == 0);
    // 15 January 2000 was a Saturday: WeekDay 1 = Monday ... 6 = Saturday
    CHECK(d.WeekDay() == 6);
    cGZDate e(1, 17, 2000);
    CHECK(e - d == 2 && d < e && e > d && d != e && d.CompareTo(e) == -1 && e.CompareTo(d) == 1 && d.CompareTo(d) == 0);
    CHECK(d.Between(e, cGZDate(1, 1, 2000)));
    ++d; d += 2; --d;
    CHECK(d.DayOfMonth() == 17 && d == e);
    cGZDate out;
    cGZDate(1, 20, 2000).Previous(1, out);       // the Monday before Thursday 20 Jan
    CHECK(out.DayOfMonth() == 17 && out.WeekDay() == 1);
    cGZDate(1, 17, 2000).Previous(1, out);       // a Monday: the original returns the same day
    CHECK(out.DayOfMonth() == 17);
    cIGZDate* c = nullptr;
    e.Clone(&c);
    CHECK(c && c->DayNumber() == e.DayNumber());
    c->Release();
    cGZDate fromDoy; fromDoy.Set(60, 2000);
    CHECK(fromDoy.Month() == 2 && fromDoy.DayOfMonth() == 29);
    cGZDate z; z.Set(std::uint32_t(0)); CHECK(!z.IsValid());
    std::puts(failures ? "gz_date: FAILED" : "gz_date ok");
    return failures != 0;
}
