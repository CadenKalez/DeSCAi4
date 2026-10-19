// cSC4HistoryWarehouse — the time series behind the city graphs (24 + thunks, 0x1000eee9c..0x1000f0bae together with
// the helper classes cVecRange and cStatVecRange). Class id 0x89efa536, interface 0x69f4a803, serializable view
// 0xe4fda3d4, message target 0x452294aa. A series is a list of {value, julian day}; recording the same day twice
// overwrites the last value, and a series with a length limit drops its oldest point first. A daily message coalesces the
// old points of series without a limit (a few milliseconds per day): points older than 500 years are disposed, then
// 500..100 years are kept one per five years, 100..50 years one per year, 50..10 years one per half year and up to the
// previous month one per month (the first point of each period survives, dated to the start of the period).
// cStatVecRange fits a line through the points of a date range: trends, point estimates and counts come from it.
// Record version 2 (1 and 2 load): u32 series count, then per series u32 id, u32 length limit, u32 point count and per
// point u32 day (0xcdcdcdcd = "previous + 1"), s32 value.
// Quirks kept: Dispose(range) leaves the last point of the range; Coalesce never closes the last period of its range
// and keeps the first point's value (no averaging); a series is created by any lookup of Store/SetSize but not by the
// queries.
#pragma once
#include <cstdint>
#include <list>
#include <map>

#include "gz/cGZDate.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4HistoryWarehouse = 0x89efa536;
constexpr std::uint32_t kGZIID_cISC4HistoryWarehouse = 0x69f4a803;
constexpr std::uint32_t kMessageHistoryStore = 0x89efa536;          // data 1 = series, data 2 = value
constexpr std::uint32_t kMessageHistorySetSize = 0x89efa537;        // data 1 = series, data 2 = length limit
constexpr std::uint32_t kMessageDailyTick = 0x66956814;
constexpr std::uint32_t kMessageCityStarted = 0x66956817;

struct HistoryNode {
    std::int32_t value = 0;       // +0x10 of the list node
    std::uint32_t julianDay = 0;  // the node's cRZDate (+0x18 vptr, +0x20 day)
};
using HistoryList = std::list<HistoryNode>;
using HistoryIter = HistoryList::iterator;

enum class eCoalesce : std::uint32_t { Monthly = 0, HalfYearly = 1, Yearly = 2, FiveYearly = 3 };

// A range of points [begin, end) between two days.
class cVecRange {
public:
    // `includeNeighbours` widens the range by the point before and after when the days fall between points.
    cVecRange(HistoryList& list, std::uint32_t fromDay, std::uint32_t toDay, bool includeNeighbours);   // @ 0x1000eee9c
    bool Coalesce(eCoalesce mode);                                                                      // @ 0x1000ef0b8
    HistoryIter begin, end;
    std::uint32_t startDay;
    HistoryList* list;
};

class cStatVecRange : public cVecRange {
public:
    cStatVecRange(HistoryList& list, std::uint32_t fromDay, std::uint32_t toDay);                       // @ 0x1000ef2fc
    std::int64_t count = 0;                  // +0x20
    double sumX = 0, sumY = 0;               // +0x28 (days from the start), +0x30
    double meanX = 0, meanY = 0;             // +0x38, +0x40
    double stdX = 0, stdY = 0;               // +0x48, +0x50
    double pearson = 0;                      // +0x58
private:
    void ComputeMeans();                     // @ 0x1000ef346
    void ComputeStandardDeviations();        // @ 0x1000ef3d0
    void ComputePearsonsCorrelation();       // @ 0x1000ef46e
};

class IHistoryEnvironment {
public:
    virtual ~IHistoryEnvironment() = default;
    virtual std::uint32_t SimDayNumber() = 0;                           // simulator vslot 0x40
    virtual void SimYearMonth(int& year, int& month) = 0;               // simulator date (year, month)
    virtual void AddAgent(cIGZMessageTarget2* target, int period, const char* name, std::uint32_t flags) = 0;
    virtual void RemoveAgent(cIGZMessageTarget2* target) = 0;
};
IHistoryEnvironment* HistoryEnvironment();
void SetHistoryEnvironment(IHistoryEnvironment* environment);

class cISC4HistoryWarehouse : public cIGZUnknown {};

class cSC4HistoryWarehouse : public cISC4HistoryWarehouse, public cIGZMessageTarget2, public cIGZSerializable {
public:
    struct Series { HistoryList nodes; std::uint32_t maxLength = 0; };   // list +0x28, count +0x38, limit +0x40

    cSC4HistoryWarehouse() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    bool Init();
    bool Shutdown();
    bool GetCount(std::uint32_t series, const cIGZDate& from, const cIGZDate& to, float& count);
    bool GetTrend(std::uint32_t series, const cIGZDate& from, const cIGZDate& to, float& slope, float& intercept);
    bool GetMinMaxAverage(std::uint32_t series, const cIGZDate& from, const cIGZDate& to, float& minimum, float& maximum, float& average);
    bool GetPoint(std::uint32_t series, const cIGZDate& date, float& value);
    bool Coalesce(std::uint32_t series, const cIGZDate& from, const cIGZDate& to, int mode);
    bool Dispose(std::uint32_t series, const cIGZDate& from, const cIGZDate& to);
    bool Dispose(std::uint32_t series);
    void RecordHistoryData(std::uint32_t series, int value);
    void SetHistoryDataQueueLength(std::uint32_t series, std::uint32_t length) { SetSize(series, static_cast<int>(length)); }

    // day-number forms
    bool GetCountDays(std::uint32_t series, std::uint32_t from, std::uint32_t to, float& count);
    bool GetTrendDays(std::uint32_t series, std::uint32_t from, std::uint32_t to, float& slope, float& intercept);
    bool GetMinMaxAverageDays(std::uint32_t series, std::uint32_t from, std::uint32_t to, float& minimum, float& maximum, float& average);
    bool GetPointDay(std::uint32_t series, std::uint32_t day, float& value);

    bool Store(std::uint32_t series, int value);
    bool SetSize(std::uint32_t series, int length);
    bool DoDailySimulation();
    void VariableCoalesce(std::uint32_t series);
    bool DoMessage(cIGZMessage2* message) override;
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4HistoryWarehouse; }

    const std::map<std::uint32_t, Series>& AllSeries() const { return series_; }
    int CoalesceState() const { return state_; }

private:
    Series& GetKeyVector(std::uint32_t series) { return series_[series]; }
    const Series* Find(std::uint32_t series) const;
    Series* Find(std::uint32_t series);

    bool started_ = false;                                   // +0x30
    std::uint32_t refCount_ = 0;                             // +0x34
    std::map<std::uint32_t, Series> series_;                 // +0x18
    int state_ = 0;                                          // +0x38 (0 idle, 1 requested, 2 running)
    std::map<std::uint32_t, Series>::iterator cursor_;       // +0x40
    int coalescedThisDay_ = 0;                               // +0x48
};

}  // namespace sc4
