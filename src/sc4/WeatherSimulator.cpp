// cSC4WeatherSimulator — see sc4/WeatherSimulator.h for the layout, the messages and the quirks.
//
// The original is one translation unit: the class (0x1000e320e..0x1000e7138) plus the file-local water-area
// scanner MakeWaterAreaList @ 0x1000e629a, which the port reaches through IWeatherHost instead.
//
// The float conversions use the 1.5 * 2^23 idiom the original uses: adding 12582912.0f and reading the
// integer part (or the low bits of the float) rounds to nearest-even exactly like the binary's code.
#include "sc4/WeatherSimulator.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "rz/cRZDate.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/GeometryUtilities.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4/SimStreamOps.h"

#pragma STDC FP_CONTRACT OFF

namespace sc4 {
namespace {

IWeatherHost* g_host = nullptr;

constexpr float kCellWidth = 16.0f;                // +0x58/+0x5c default (one weather cell = 16 m)
constexpr float kCellWidthInverse = 0.0625f;       // +0x60/+0x64
constexpr float kTwoPiOverTwelve = 6.2831855f;     // 0x40c90fdb (2*pi/12 is applied as *c /12)
constexpr float kTwelve = 12.0f;
constexpr float kRoundingMagic = 12582912.0f;      // 1.5 * 2^23, round-to-nearest-even
constexpr float kDegreesToRadiansHalf = 0.008726646f;   // pi / 360
constexpr float kMonthDayCount = 30.0f;

// The original's universal float -> integer conversion: adding 1.5*2^23 puts the rounded integer into the
// low mantissa bits, so the *bit pattern* minus 0x4b400000 is round-to-nearest-even of the input (the
// binary does `movd %xmm, %eax; leal -0x4b400000(%rax), ...`, 0x1000e59ea and everywhere else).
inline int RoundToInt(float value) {
    const float shifted = value + kRoundingMagic;
    std::int32_t bits = 0;
    std::memcpy(&bits, &shifted, sizeof(bits));
    return bits - 0x4b400000;
}

// floor(value) for value >= 0: the same idiom on value - 0.5 plus the exact-integer correction the
// compiler emits at 0x1000e497a (`if (value == (float)(rounded + 1)) rounded + 1`).
inline int FloorToInt(float value) {
    const float shifted = value - 0.5f + kRoundingMagic;
    std::int32_t bits = 0;
    std::memcpy(&bits, &shifted, sizeof(bits));
    const int rounded = bits - 0x4b400000;
    if (value == static_cast<float>(rounded + 1)) return rounded + 1;
    return rounded;
}

inline float Clamp01Unit(float value) {     // the [0, 1) clamp of 0x1000e3b30 (NaN passes through)
    if (value < 0.0f) return 0.0f;
    if (!(value < 1.0f)) return 1.0f;
    return value;
}

inline std::uint8_t ClampByte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<std::uint8_t>(value);
}

// The message's cIGZMessage2Standard payload interface (the original reads the payload slots of the object
// it already holds; the port has to ask for the interface and release it again).
class StandardMessageRef {
public:
    explicit StandardMessageRef(cIGZMessage2* message) {
        void* raw = nullptr;
        if (message && message->QueryInterface(kGZIID_cIGZMessage2Standard, &raw))
            standard_ = static_cast<cIGZMessage2Standard*>(raw);
    }
    ~StandardMessageRef() {
        if (standard_) standard_->Release();
    }
    StandardMessageRef(const StandardMessageRef&) = delete;
    StandardMessageRef& operator=(const StandardMessageRef&) = delete;
    cIGZMessage2Standard* operator->() const { return standard_; }
    explicit operator bool() const { return standard_ != nullptr; }

private:
    cIGZMessage2Standard* standard_ = nullptr;
};

}  // namespace

IWeatherHost* WeatherHost() { return g_host; }
void SetWeatherHost(IWeatherHost* host) { g_host = host; }

// ---------------------------------------------------------------------------------------------------
// ctor / dtor / COM plumbing
// ---------------------------------------------------------------------------------------------------

// @ 0x1000e320e. The four COM vtables (+0, +8, +0x10, +0x18) are the multiple bases of the port; every
// other field is a member initializer (see the header for the original offsets). The original seeds the
// generator with 0xffffffff (clock seeding) and only writes the low half of the current wind direction at
// +0x19c, leaving +0x1a0 uninitialized until the first monthly update; the port zeroes that half.
cSC4WeatherSimulator::cSC4WeatherSimulator(IWeatherHost* host) : host_(host) {
    messageTarget_ = static_cast<cIGZMessageTarget2*>(this);
}

// @ 0x1000e34cc (complete) / 0x1000e35f6 (deleting). Releases the two nested byte grids (+0x150/+0x138),
// the water-area list (+0xc8), the sea-moderation grid (+0xb0), the temperature grid (+0x98) and the
// RandomIterator's list (+0x18 of +0xe0) — all owned by the members.
cSC4WeatherSimulator::~cSC4WeatherSimulator() = default;

// @ 0x1000e7068. iid 1 and 0xc6c66245 answer the primary view, 0xe98b2f57 the legacy message target at +8,
// 0x452294aa the message target at +0x10 and 0xe4fda3d4 the serializable view at +0x18; everything else
// fails without an AddRef. Success AddRefs through the primary view.
bool cSC4WeatherSimulator::QueryInterface(std::uint32_t iid, void** out) {
    if (!out) return false;
    void* view = nullptr;
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cISC4WeatherSimulator)
        view = static_cast<cIGZUnknown*>(static_cast<cISC4WeatherSimulator*>(this));
    else if (iid == kGZIID_cIGZMessageTargetLegacy)
        view = static_cast<cIGZMessageTarget*>(this);
    else if (iid == kGZIID_cIGZMessageTarget2)
        view = static_cast<cIGZMessageTarget2*>(this);
    else if (iid == kGZIID_cIGZSerializable)
        view = static_cast<cIGZSerializable*>(this);
    else
        return false;
    *out = view;
    AddRef();
    return true;
}

// @ 0x1000e6f76. Below two references it runs Shutdown and then the deleting destructor.
std::uint32_t cSC4WeatherSimulator::Release() {
    if (refCount_ > 1) return --refCount_;
    Shutdown();
    delete this;
    return 0;
}

// ---------------------------------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------------------------------

// @ 0x1000e362a. Reads the tunables, subscribes to the "city started" message, builds the grids from the
// city size (unless a stream already provided them), registers the three simulator intervals, and resets
// the random walk. In the original the city / simulator / terrain pointers are AddRef'd here; the port
// reads them through the host seam instead, so only the observable side effects remain.
bool cSC4WeatherSimulator::Init() {
    if (initialized_) return true;
    ReadSimulatorTunableValues();
    if (host_) host_->AddNotification(messageTarget_, kWeatherMessageCityStarted);
    if (!loadedFromStream_) {
        worldWidth_ = host_ ? host_->CitySizeX() : 0.0f;
        worldDepth_ = host_ ? host_->CitySizeZ() : 0.0f;
        cellCountX_ = static_cast<int>(worldWidth_ * kCellWidthInverse);
        cellCountZ_ = static_cast<int>(worldDepth_ * kCellWidthInverse);
        cellWidthX_ = kCellWidth;
        cellWidthZ_ = kCellWidth;
        inverseCellWidthX_ = kCellWidthInverse;
        inverseCellWidthZ_ = kCellWidthInverse;
        const float standardCellSizeX = host_ ? host_->CityCellSizeX() : kCellWidth;
        const float standardCellSizeZ = host_ ? host_->CityCellSizeZ() : kCellWidth;
        standardCellsPerCellX_ = standardCellSizeX > 0.0f ? static_cast<int>(kCellWidth / standardCellSizeX) : 1;
        standardCellsPerCellZ_ = standardCellSizeZ > 0.0f ? static_cast<int>(kCellWidth / standardCellSizeZ) : 1;
        if (standardCellsPerCellX_ < 1) standardCellsPerCellX_ = 1;
        if (standardCellsPerCellZ_ < 1) standardCellsPerCellZ_ = 1;
        cellsPerUpdate_ = (cellCountX_ * cellCountZ_) / 8;
        SetGridSize(cellCountX_, cellCountZ_);
        GenerateMoistureVariation();
    }
    terrain_ = host_ ? host_->Terrain() : nullptr;
    if (host_) {
        host_->RegisterAgent(messageTarget_, 6, "cSC4WeatherSimulator");   // month
        host_->RegisterAgent(messageTarget_, 5, "cSC4WeatherSimulator");   // week
        host_->RegisterAgent(messageTarget_, 4, "cSC4WeatherSimulator");   // day
    }
    cellWalk_.SetRect(0, 0, cellCountX_ > 0 ? cellCountX_ - 1 : 0, cellCountZ_ > 0 ? cellCountZ_ - 1 : 0);
    cellWalk_.SetModular(true);
    cellWalk_.Reset(true);
    cellsPerUpdate_ = updateDivisor_ > 0 ? (cellCountX_ * cellCountZ_) / updateDivisor_ : 0;
    humidity_ = baseHumidity_;
    temperature_ = maxTemperature_;
    initialized_ = true;
    return true;
}

// @ 0x1000e3f3c.
bool cSC4WeatherSimulator::Shutdown() {
    if (!initialized_) return true;
    terrain_ = nullptr;
    if (host_) {
        host_->UnregisterAgent(messageTarget_);
        host_->RemoveNotification(messageTarget_, kWeatherMessageCityStarted);
    }
    initialized_ = false;
    return true;
}

// Allocates the four grids at the given weather-cell size: temperature 0x7f, moisture variation 0x40,
// sea moderation false, manual moisture 0.
void cSC4WeatherSimulator::SetGridSize(int cellsX, int cellsZ) {
    temperatureMap_ = CreateByteGrid(cellsX, cellsZ, 0x7f);
    moistureMap_ = CreateByteGrid(cellsX, cellsZ, 0x40);
    manualMoisture_ = CreateByteGrid(cellsX, cellsZ, 0x00);
    seaModeration_.assign(cellsX > 0 ? static_cast<std::size_t>(cellsX) : 0,
                          std::vector<bool>(cellsZ > 0 ? static_cast<std::size_t>(cellsZ) : 0, false));
}

std::vector<std::vector<std::uint8_t>> cSC4WeatherSimulator::CreateByteGrid(int cellsX, int cellsZ,
                                                                            std::uint8_t value) const {
    if (cellsX <= 0 || cellsZ <= 0) return {};
    return std::vector<std::vector<std::uint8_t>>(static_cast<std::size_t>(cellsX),
                                                  std::vector<std::uint8_t>(static_cast<std::size_t>(cellsZ), value));
}

// @ 0x1000e3b30. Defaults come from the constructor; each property only overwrites when the exemplar has it.
void cSC4WeatherSimulator::ReadSimulatorTunableValues() {
    cISCPropertyHolder* holder =
        host_ ? host_->SimulatorTunables(kWeatherTunablesKey, kWeatherTunablesFilter) : nullptr;
    if (!holder) return;
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropUpdateDivisor, updateDivisor_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropSecondDivisor, secondDivisor_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropTemperatureRadius, temperatureWriteRadius_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropMaxTemperature, maxTemperature_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropSeasonalAmplitude, seasonalAmplitude_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropTemperatureScale, temperatureScale_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropSeaRadius, seaModerationRadius_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropCoastWarming, coastWarming_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropAltitudeLapse, altitudeLapse_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropWindCooling, windCooling_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropBaseWindSpeed, baseWindSpeed_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropBaseWindDirX, baseWindDirection_.x);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropBaseWindDirZ, baseWindDirection_.y);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropBaseHumidity, baseHumidity_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropHumidityAmplitude, humidityAmplitude_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropHumidityScale, humidityScale_);
    // The scalar is stored as a fraction of 255 in the object (property value * 255).
    if (cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropMoistureScalar, moistureScalar_))
        moistureScalar_ = moistureScalar_ * 255.0f;
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropPeakExtraMoisture, peakExtraMoisture_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropPeakThreshold, peakThreshold_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropUnused1, unused1_);
    cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropUnused2, unused2_);
    // A half-angle in degrees is stored as tan(angle / 2) / 2.
    if (cSCPropertyHelp::GetPropertyValue(holder, kWeatherPropWindAngleDegrees, windAngleSpread_)) {
        windAngleSpread_ = std::tan(windAngleSpread_ * kDegreesToRadiansHalf) * 0.5f;
    }
    if (updateDivisor_ < 1) updateDivisor_ = 1;
    if (secondDivisor_ < 1) secondDivisor_ = 1;
    seasonalAmplitude_ = Clamp01Unit(seasonalAmplitude_);
    humidityAmplitude_ = Clamp01Unit(humidityAmplitude_);
    if (coastWarming_ < 0.0f) coastWarming_ = 0.0f;
}

// @ 0x1000e3e1c. Fills the moisture-variation grid (+0x138) with a smoothed random walk between the
// land simulator's moisture range (defaults 60..100): each row is blended with the row above, then a
// gaussian -10..10 offset is added and clamped.
void cSC4WeatherSimulator::GenerateMoistureVariation() {
    std::uint8_t low = 0x3c, high = 100;
    if (host_) host_->MoistureValueRange(low, high);
    std::uint32_t value = (static_cast<std::uint32_t>(low) + high) >> 1;
    for (int x = 0; x < cellCountX_; ++x) {
        if (x >= static_cast<int>(moistureMap_.size())) break;
        for (int z = 0; z < cellCountZ_; ++z) {
            if (z >= static_cast<int>(moistureMap_[static_cast<std::size_t>(x)].size())) break;
            if (x != 0) {
                const std::uint32_t above = moistureMap_[static_cast<std::size_t>(x - 1)][static_cast<std::size_t>(z)];
                value = (above + (value & 0xff)) >> 1;
            }
            const int offset = random_.RandomSint32RangeGaussianFast(-10, 10);
            const int candidate = static_cast<int>(value & 0xff) + offset;
            std::uint8_t result = low;
            if (candidate >= static_cast<int>(low)) {
                result = high;
                if (candidate <= static_cast<int>(high)) result = static_cast<std::uint8_t>(candidate);
            }
            moistureMap_[static_cast<std::size_t>(x)][static_cast<std::size_t>(z)] = result;
            value = result;
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// messages
// ---------------------------------------------------------------------------------------------------

// @ 0x1000e4bd8: the legacy target ignores everything.
bool cSC4WeatherSimulator::DoMessage(cGZMessage& /*message*/) { return true; }

// @ 0x1000e4be8. 0x26d31ec1 runs the post-city-init catch-up; the day message interpolates the current
// wind across the month the ambient values were computed for; the week and month messages run the steps
// with the interval counter in data 3.
bool cSC4WeatherSimulator::DoMessage(cIGZMessage2* message) {
    if (!message) return true;
    const std::uint32_t type = message->GetType();
    if (type < kWeatherMessageWeek) {
        if (type == kWeatherMessageCityStarted) {
            DoPostCityInit();
        } else if (type == kWeatherMessageDay) {
            StandardMessageRef standard(message);
            if (!standard) return true;
            cRZDate date = cRZDate::FromJulianDay(static_cast<std::uint32_t>(standard->GetData1()));
            if (static_cast<int>(date.Month()) == ambientMonth_) {
                const int day = static_cast<int>(date.DayOfMonth());
                const float fraction = static_cast<float>(day - 1) / kMonthDayCount;
                const float fromX = previousWindSpeed_ * previousWindDirection_.x;
                const float fromZ = previousWindSpeed_ * previousWindDirection_.y;
                const float toX = targetWindSpeed_ * targetWindDirection_.x;
                const float toZ = targetWindSpeed_ * targetWindDirection_.y;
                const float x = fraction * (toX - fromX) + fromX;
                const float z = fraction * (toZ - fromZ) + fromZ;
                windDirection_.x = x;
                windDirection_.y = z;
                const float length = std::sqrt(z * z + x * x);
                windSpeed_ = length;
                if (length > 0.0f) {
                    windDirection_.x = x * (1.0f / length);
                    windDirection_.y = (1.0f / length) * z;
                }
                windSpeed_ = (targetWindSpeed_ - previousWindSpeed_) * fraction + previousWindSpeed_;
            }
        }
        return true;
    }
    if (type == kWeatherMessageWeek) {
        StandardMessageRef standard(message);
        if (!standard) return true;
        const int week = static_cast<int>(standard->GetData3());
        cRZDate date = cRZDate::FromJulianDay(static_cast<std::uint32_t>(standard->GetData1()));
        (void)date.Year();                     // the original computes and discards the year
        DoWeeklySimulation(week);
    } else if (type == kWeatherMessageMonth) {
        StandardMessageRef standard(message);
        if (!standard) return true;
        const int month = static_cast<int>(standard->GetData3());
        cRZDate date = cRZDate::FromJulianDay(static_cast<std::uint32_t>(standard->GetData1()));
        (void)date.Year();                     // ditto
        DoMonthlySimulation(month);
    }
    return true;
}

// @ 0x1000e5122. Allocates a standard message, AddRefs it, sets the type, void1, the sender as the
// unknown payload, data2 and data3 (vslots 0x28 / 0x60 / 0x108 / 0x80 / 0xa8), posts it to the message
// server and releases it again.
void cSC4WeatherSimulator::PostMessageServerMessage(std::uint32_t type, void* void1, std::uint32_t data2,
                                                    std::uint32_t data3) {
    auto* message = new cRZMessage2Standard();
    message->AddRef();
    message->SetType(type);
    message->SetVoid1(void1);
    message->SetIGZUnknown(static_cast<cIGZUnknown*>(static_cast<cISC4WeatherSimulator*>(this)));
    message->SetData2(data2);
    message->SetData3(data3);
    if (host_) host_->PostMessage(message);
    message->Release();
}

// @ 0x1000e3fcc: once per city, run the catch-up simulation.
void cSC4WeatherSimulator::DoPostCityInit() {
    if (!postCityInitDone_) {
        DoCompleteSimulation();
        postCityInitDone_ = true;
    }
}

// @ 0x1000e3ff0: one month, then whole weeks until the weekly walk wraps.
void cSC4WeatherSimulator::DoCompleteSimulation() {
    DoMonthlySimulation(1);
    while (!DoWeeklySimulation(1)) {
    }
}

// ---------------------------------------------------------------------------------------------------
// geometry and seasons
// ---------------------------------------------------------------------------------------------------

// @ 0x1000e487a.
void cSC4WeatherSimulator::PositionToCell(float x, float z, int& cellX, int& cellZ) const {
    cellX = static_cast<int>(x / cellWidthX_);
    cellZ = static_cast<int>(z / cellWidthZ_);
}

// @ 0x1000e4896.
void cSC4WeatherSimulator::CellToPosition(int cellX, int cellZ, float& x, float& z) const {
    x = static_cast<float>(cellX) * cellWidthX_;
    z = static_cast<float>(cellZ) * cellWidthZ_;
}

// @ 0x1000e48ba. Quirk kept: the second output is computed from cellX again, not from cellZ.
void cSC4WeatherSimulator::CellToStandardCityCell(int cellX, int /*cellZ*/, int& stdX, int& stdZ) const {
    stdX = standardCellsPerCellX_ * cellX;
    stdZ = cellX * standardCellsPerCellZ_;
}

// @ 0x1000e48d0.
void cSC4WeatherSimulator::StandardCityCellToCell(int stdX, int stdZ, int& cellX, int& cellZ) const {
    cellX = stdX / standardCellsPerCellX_;
    cellZ = stdZ / standardCellsPerCellZ_;
}

// @ 0x1000e48f8: season 0..3 from the season month.
int cSC4WeatherSimulator::GetCurrentSeason() { return (GetCurrentSeasonMonth() - 1) / 3; }

// @ 0x1000e491c: the simulator's month, shifted six months in the southern hemisphere.
int cSC4WeatherSimulator::GetCurrentSeasonMonth() {
    const int month = host_ ? host_->SimulatorMonth() : 1;
    const int hemisphere = host_ ? host_->CityHemisphere() : 1;
    if (hemisphere != 1) return (month + 5) % 12 + 1;
    return month;
}

// @ 0x1000e4b5a. Quirk kept: the position is ignored and the *base* humidity is used, so the result is
// (1 + baseHumidity) / 2 regardless of the current humidity.
float cSC4WeatherSimulator::GetHumidity(float /*x*/, float /*z*/) {
    return (1.0f - baseHumidity_) * 0.5f + baseHumidity_;
}

// @ 0x1000e4b80: the ambient wind speed, and optionally the direction scaled to that speed.
float cSC4WeatherSimulator::GetAmbientWind(float* direction) {
    const float speed = windSpeed_;
    if (direction) {
        direction[0] = windDirection_.x;
        direction[1] = windDirection_.y;
        SC4GeometryUtilities::ScaleVector2D(direction, speed);
    }
    return speed;
}

// @ 0x1000e4bc4. Quirk kept: the cell is ignored; this tail-calls GetAmbientWind.
float cSC4WeatherSimulator::GetWindAtCell(int /*cellX*/, int /*cellZ*/, float* direction) {
    return GetAmbientWind(direction);
}

// ---------------------------------------------------------------------------------------------------
// moisture queries
// ---------------------------------------------------------------------------------------------------

// @ 0x1000e497a. Clamps the position into the city, converts to a cell (floor) and returns the moisture
// byte; 0 before Init.
std::uint8_t cSC4WeatherSimulator::GetMoistureValue(float x, float z) {
    if (!initialized_) return 0;
    float px = 0.0f;
    if (x >= 0.0f) {
        px = x;
        if (x >= worldWidth_) px = worldWidth_ - 1.0f;
    }
    float pz = 0.0f;
    if (z >= 0.0f) {
        pz = z;
        if (z >= worldDepth_) pz = worldDepth_ - 1.0f;
    }
    const int cellX = FloorToInt(px * inverseCellWidthX_);
    const int cellZ = FloorToInt(pz * inverseCellWidthZ_);
    if (cellX < 0 || cellX >= static_cast<int>(moistureMap_.size())) return 0;
    if (cellZ < 0 || cellZ >= static_cast<int>(moistureMap_[static_cast<std::size_t>(cellX)].size())) return 0;
    return moistureMap_[static_cast<std::size_t>(cellX)][static_cast<std::size_t>(cellZ)];
}

// @ 0x1000e4a52.
void cSC4WeatherSimulator::GetMoistureValueMapPtr(std::vector<std::vector<std::uint8_t>>*& out) {
    out = &moistureMap_;
}

// @ 0x1000e4a62: 16 moisture types, the byte value shifted right by four.
std::uint32_t cSC4WeatherSimulator::GetMoistureType(float x, float z) {
    return static_cast<std::uint32_t>(GetMoistureValue(x, z)) >> 4;
}

// @ 0x1000e4a74. Sets the manual moisture-addition bytes of the cells between the two positions.
// Quirk kept: the byte written is the low byte of round(value * 1.275), so values above 200 wrap.
void cSC4WeatherSimulator::SetManualMoistureAddition(float x, float z, float x2, float z2, float value) {
    float amount = 0.0f;
    if (value >= 0.0f) {
        amount = value;
        if (value > 255.0f) amount = 255.0f;
    }
    int x0 = static_cast<int>(x * inverseCellWidthX_);
    int x1 = static_cast<int>(x2 * inverseCellWidthX_);
    int z0 = static_cast<int>(z * inverseCellWidthZ_);
    int z1 = static_cast<int>(z2 * inverseCellWidthZ_);
    if (x0 < 0) {
        x0 = 0;
    } else if (x1 >= cellCountX_) {
        x1 = cellCountX_ - 1;
    }
    if (z0 < 0) {
        z0 = 0;
    } else if (z1 >= cellCountZ_) {
        z1 = cellCountZ_ - 1;
    }
    const auto byte = static_cast<std::uint8_t>(RoundToInt(amount * 1.275f));
    for (int i = x0; i < x1; ++i) {
        if (i < 0 || i >= static_cast<int>(manualMoisture_.size())) continue;
        for (int j = z0; j < z1; ++j) {
            if (j < 0 || j >= static_cast<int>(manualMoisture_[static_cast<std::size_t>(i)].size())) continue;
            manualMoisture_[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = byte;
        }
    }
}

// @ 0x1000e623e. Scales the city-cell rectangle to weather cells (truncating the near edges, rounding the
// far ones up by 15) and rebuilds that part of the moisture grid.
void cSC4WeatherSimulator::UpdateMoisture(const WeatherRect& rect) {
    if (!terrain_) return;
    WeatherRect cells;
    cells.left = rect.left / 16;
    cells.top = rect.top / 16;
    cells.right = (rect.right + 15) / 16;
    cells.bottom = (rect.bottom + 15) / 16;
    SimulateMoisture(cells);
}

// ---------------------------------------------------------------------------------------------------
// weekly / monthly steps
// ---------------------------------------------------------------------------------------------------

// @ 0x1000e4dd2. Walks cellsPerUpdate cells of the shuffled rectangle (when the walk runs out it is
// rewound and 0x6716a4e6 is posted), recomputing each temperature, then rebuilds the moisture grid over
// the whole city. Returns true when a rewind happened (DoCompleteSimulation loops until it does).
// The original inlines the iterator advance at 0x1000e4e23..0x1000e4ed5 (list mode packs x in the low
// byte, z in the high half of each ushort); the port drives the recovered RandomIterator::Next, which
// follows the same modular arithmetic (index, stride and the (current % width, current / width) cell).
bool cSC4WeatherSimulator::DoWeeklySimulation(int /*week*/) {
    bool wrapped = false;
    int remaining = cellsPerUpdate_;
    while (remaining != 0) {
        int cellX = 0;
        int cellZ = 0;
        if (!cellWalk_.Next(cellX, cellZ)) {
            cellWalk_.Reset(true);
            PostMessageServerMessage(kWeatherMessageWeeklyPassDone,
                                     static_cast<cIGZUnknown*>(static_cast<cISC4WeatherSimulator*>(this)), 0, 0);
            wrapped = true;
            if (!cellWalk_.Next(cellX, cellZ)) break;
        }
        SimulateCellTemperature(cellX, cellZ);
        --remaining;
    }
    WeatherRect whole{0, 0, cellCountX_, cellCountZ_};
    SimulateMoisture(whole);
    return wrapped;
}

// @ 0x1000e4f16 (monthNumber is the month interval counter, only 1 matters).
void cSC4WeatherSimulator::DoMonthlySimulation(int monthNumber) {
    IWeatherTerrain* terrain = terrain_;      // acquired by Init (+0x48 in the original)
    if (monthNumber == 1 && terrain && host_) {
        waterAreas_.clear();
        host_->MakeWaterAreaList(*terrain, waterAreas_, 3, 5, cellCountX_, cellCountZ_,
                                 static_cast<int>(cellWidthX_), static_cast<int>(cellWidthZ_), nullptr);
        RemoveWaterThatIsNotAtCityEdge(waterAreas_);
        CalculateSeaTemperatureModeration(waterAreas_, seaModeration_);
    }
    const int seasonMonth = GetCurrentSeasonMonth();
    GetNewCurrentAmbientValues(seasonMonth);
    const int season = GetCurrentSeason();
    if (lastPublishedSeason_ == season) return;
    lastPublishedSeason_ = season;
    // The new season resets the wind to the base values.
    windSpeed_ = baseWindSpeed_;
    windDirection_ = baseWindDirection_;
    PostMessageServerMessage(kWeatherMessageSeasonChanged,
                             static_cast<cIGZUnknown*>(static_cast<cISC4WeatherSimulator*>(this)),
                             static_cast<std::uint32_t>(season), 0);
}

// @ 0x1000e4ffa. Re-derives one cell's temperature: the ambient temperature (moderated towards the
// maximum near water), minus the altitude lapse and the wind chill, clamped to -15..45 C and quantised
// to a byte with 4.25 steps per degree.
void cSC4WeatherSimulator::SimulateCellTemperature(int cellX, int cellZ) {
    IWeatherTerrain* terrain = terrain_;
    if (!terrain) return;
    const bool moderated = cellX >= 0 && cellX < static_cast<int>(seaModeration_.size()) &&
                           cellZ >= 0 && cellZ < static_cast<int>(seaModeration_[static_cast<std::size_t>(cellX)].size()) &&
                           seaModeration_[static_cast<std::size_t>(cellX)][static_cast<std::size_t>(cellZ)];
    const float altitude = terrain->AltitudeAtVertex(cellX, cellZ);
    const float wind = GetWindAtCell(cellX, cellZ, nullptr);
    float kelvin = temperature_ + 273.0f;
    if (moderated) kelvin = kelvin + (maxTemperature_ - temperature_) * coastWarming_;
    float value = ((kelvin - altitude * altitudeLapse_) - wind * windCooling_) + -273.0f;
    float clamped = -15.0f;
    if (value >= -15.0f) clamped = value;
    if (clamped > 45.0f) clamped = 45.0f;
    WriteTemperatureValue(cellX, cellZ, RoundToInt((clamped + 15.0f) * 4.25f));
}

// @ 0x1000e6e82. Without a write radius the value replaces the cell; otherwise every cell of the
// (2r x 2r) square is averaged with the value in place, in sweep order (overlapping writes therefore
// depend on the order — a quirk of the original).
void cSC4WeatherSimulator::WriteTemperatureValue(int cellX, int cellZ, int value) {
    const int radius = temperatureWriteRadius_;
    if (radius < 1) {
        if (cellX >= 0 && cellX < static_cast<int>(temperatureMap_.size()) && cellZ >= 0 &&
            cellZ < static_cast<int>(temperatureMap_[static_cast<std::size_t>(cellX)].size()))
            temperatureMap_[static_cast<std::size_t>(cellX)][static_cast<std::size_t>(cellZ)] =
                static_cast<std::uint8_t>(value);
        return;
    }
    const int xBegin = cellX - radius;
    const int xEnd = cellX + radius;
    if (xBegin >= xEnd) return;
    for (int i = xBegin; i < xEnd; ++i) {
        if (i < 0 || i >= cellCountX_ || i >= static_cast<int>(temperatureMap_.size())) continue;
        const int zBegin = cellZ - radius;
        const int zEnd = cellZ + radius;
        if (zBegin >= zEnd) continue;
        for (int j = zBegin; j < zEnd; ++j) {
            if (j < 0 || j >= cellCountZ_ || j >= static_cast<int>(temperatureMap_[static_cast<std::size_t>(i)].size()))
                continue;
            const int sum = static_cast<int>(temperatureMap_[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]) + value;
            temperatureMap_[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                static_cast<std::uint8_t>(sum / 2);
        }
    }
}

// @ 0x1000e6b66. Computes the month's ambient temperature and humidity from two cosine curves, moves last
// month's target wind to the "previous" slots, picks a new random target speed (2..4 m/s) and a new target
// direction: the city edge with the fewest water cells (the four edges are sorted by that count), rotated
// by a gaussian angle whose magnitude is |angle| <= 1.
void cSC4WeatherSimulator::GetNewCurrentAmbientValues(int month) {
    const float seasonCos = std::cos(static_cast<float>(month - 8) * kTwoPiOverTwelve / kTwelve);
    temperature_ = ((seasonalAmplitude_ * temperatureScale_ * seasonCos) + 1.0f) * maxTemperature_;
    const float humidityCos = std::cos(static_cast<float>(month - 1) * kTwoPiOverTwelve / kTwelve);
    humidity_ = ((humidityAmplitude_ * humidityScale_ * humidityCos) + 1.0f) * baseHumidity_;

    previousWindSpeed_ = targetWindSpeed_;
    previousWindDirection_ = targetWindDirection_;
    targetWindSpeed_ = static_cast<float>(random_.RandomDoubleRangeUniform(2.0, 4.0));

    IWeatherTerrain* terrain = terrain_;
    // {number of water cells on the edge, unit direction pointing away from that edge}
    struct Edge {
        int water = 0;
        float x = 0.0f, z = 0.0f;
    };
    Edge edges[4];
    if (terrain) {
        const int cellsX = terrain->CellCountX();
        const int cellsZ = terrain->CellCountZ();
        for (int z = 0; z < cellsZ; ++z) {
            edges[0].water += terrain->CellHasWater(0, z) ? 1 : 0;              // west
            edges[2].water += terrain->CellHasWater(cellsX - 1, z) ? 1 : 0;     // east
        }
        for (int x = 0; x < cellsX; ++x) {
            edges[1].water += terrain->CellHasWater(x, 0) ? 1 : 0;              // north
            edges[3].water += terrain->CellHasWater(x, cellsZ - 1) ? 1 : 0;     // south
        }
    }
    edges[0].x = 1.0f;
    edges[1].z = 1.0f;
    edges[2].x = -1.0f;
    edges[3].z = -1.0f;
    std::sort(edges, edges + 4, [](const Edge& a, const Edge& b) {
        if (a.water != b.water) return a.water < b.water;
        if (a.x != b.x) return a.x < b.x;
        return a.z < b.z;
    });
    float angle = 0.0f;
    do {
        angle = static_cast<float>(random_.RandomDoubleGaussian() * static_cast<double>(windAngleSpread_));
    } while (std::fabs(angle) > 1.0f);
    const float sinAngle = angle;
    const float cosAngle = static_cast<float>(std::sqrt(1.0 - static_cast<double>(sinAngle * sinAngle)));
    const float dx = edges[0].x;
    const float dz = edges[0].z;
    targetWindDirection_.x = dx * cosAngle - dz * sinAngle;
    targetWindDirection_.y = dz * cosAngle + dx * sinAngle;
    ambientMonth_ = month;
}

// ---------------------------------------------------------------------------------------------------
// water areas and sea moderation
// ---------------------------------------------------------------------------------------------------

// @ 0x1000e6866. Keeps only the areas that touch (or pass) the city border: an area is dropped when all
// of its edges are strictly inside the city.
void cSC4WeatherSimulator::RemoveWaterThatIsNotAtCityEdge(std::vector<cRZRect>& areas) {
    std::vector<cRZRect> kept;
    for (const cRZRect& r : areas) {
        if (r.left < 1 || r.top < 1 || r.right >= cellCountX_ - 1 || r.bottom >= cellCountZ_ - 1)
            kept.push_back(r);
    }
    if (kept.size() != areas.size()) areas.assign(kept.begin(), kept.end());
}

// @ 0x1000e6994. Clears the moderation grid and marks every cell of each water area grown by
// seaModerationRadius cells, clamped to the grid.
void cSC4WeatherSimulator::CalculateSeaTemperatureModeration(const std::vector<cRZRect>& areas,
                                                             std::vector<std::vector<bool>>& moderation) {
    const int cellsX = cellCountX_;
    const int cellsZ = cellCountZ_;
    for (auto& column : moderation)
        std::fill(column.begin(), column.end(), false);
    if (cellsX <= 0 || cellsZ <= 0) return;
    const int radius = seaModerationRadius_;
    for (const cRZRect& area : areas) {
        int x0 = area.left - radius;
        int z0 = area.top - radius;
        int x1 = area.right + radius;
        int z1 = area.bottom + radius;
        if (!(x0 < cellsX && z0 < cellsZ && (x1 | z1) >= 0)) continue;
        const int cx0 = x0 > 0 ? x0 : 0;
        const int cz0 = z0 > 0 ? z0 : 0;
        const int cx1 = x1 < cellsX - 1 ? x1 : cellsX - 1;
        const int cz1 = z1 < cellsZ - 1 ? z1 : cellsZ - 1;
        if (cx0 > cx1) continue;
        for (int x = cx0; x <= cx1; ++x) {
            if (x < 0 || x >= static_cast<int>(moderation.size())) continue;
            for (int z = cz0; z <= cz1; ++z) {
                if (z < 0 || z >= static_cast<int>(moderation[static_cast<std::size_t>(x)].size())) continue;
                moderation[static_cast<std::size_t>(x)][static_cast<std::size_t>(z)] = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// moisture simulation
// ---------------------------------------------------------------------------------------------------

// @ 0x1000e51ee (4,048 bytes).
//
// PARTIAL: the control flow is recovered, but the original's two separable blurs work on shared stack
// buffers whose edges are reflected/shifted with hand-rolled memmove loops (0x1000e5712..0x1000e5997 and
// 0x1000e5e7e..0x1000e60df). The decompiler collapsed those into register shuffles, so the port uses the
// equivalent clamped/mirrored passes described in the comments; values two cells from a partial update's
// border can therefore differ. Everything else follows the binary:
//   * sample the terrain over the rectangle grown by four cells (mean altitude per weather cell, or
//     GetAltitudes when one standard cell is one weather cell) and mark water cells 0xff;
//   * smooth the altitudes with a separable 1-2-1 kernel (clamped at the sampling border);
//   * per non-water cell, add either the humidity value (plus the extra moisture when the cell is a sharp
//     local peak) or, on slopes exposed to the wind, the humidity minus the wind direction dotted with the
//     vertex normal times the wind/rain factor, averaged over the standard cells of the weather cell;
//   * blur the result with a separable 1-4-6-4-1 kernel and store it into the moisture grid.
void cSC4WeatherSimulator::SimulateMoisture(const WeatherRect& rect) {
    IWeatherTerrain* terrain = terrain_;
    if (!terrain) return;
    const int rows = static_cast<int>(moistureMap_.size());
    const int cols = rows > 0 ? static_cast<int>(moistureMap_[0].size()) : 0;
    if (rows <= 0 || cols <= 0) return;
    const int standardCells = standardCellsPerCellX_ * standardCellsPerCellZ_;
    const int standardX = standardCellsPerCellX_ > 0 ? standardCellsPerCellX_ : 1;
    const int standardZ = standardCellsPerCellZ_ > 0 ? standardCellsPerCellZ_ : 1;

    const int x0 = std::max(rect.left, 0);
    const int z0 = std::max(rect.top, 0);
    const int x1 = std::min(rect.right, rows);
    const int z1 = std::min(rect.bottom, cols);
    if (x0 >= x1 || z0 >= z1) return;
    // Blurring area (±2) and sampling area (±4).
    const int xa = std::max(x0 - 2, 0), za = std::max(z0 - 2, 0);
    const int xb = std::min(x1 + 2, rows), zb = std::min(z1 + 2, cols);
    const int xs = std::max(x0 - 4, 0), zs = std::max(z0 - 4, 0);
    const int xe = std::min(x1 + 4, rows), ze = std::min(z1 + 4, cols);

    const auto index = [cols](int x, int z) { return static_cast<std::size_t>(x) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(z); };
    std::vector<float> heights(static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols), 0.0f);
    std::vector<float> smoothed(heights.size(), 0.0f);
    std::vector<std::uint8_t> work(heights.size(), 0);

    // ---- sample the terrain (the heights are the *sums* over the standard cells) --------------------
    if (standardCells == 1) {
        // One standard cell per weather cell: the original asks GetAltitudes for each column and then
        // fills the work grid over the blurring area (±2) only.
        for (int x = xs; x < xe; ++x)
            terrain->GetAltitudes(x, zs, x, ze - 1, &heights[index(x, zs)], 1, 1);
        for (int x = xa; x < xb; ++x) {
            for (int z = za; z < zb; ++z) {
                work[index(x, z)] = terrain->CellHasWater(x, z)
                                        ? 0xff
                                        : manualMoisture_[static_cast<std::size_t>(x)][static_cast<std::size_t>(z)];
            }
        }
    } else {
        for (int x = xs; x < xe; ++x) {
            for (int z = zs; z < ze; ++z) {
                std::uint8_t base = manualMoisture_[static_cast<std::size_t>(x)][static_cast<std::size_t>(z)];
                float sum = 0.0f;
                for (int sx = 0; sx < standardX; ++sx) {
                    for (int sz = 0; sz < standardZ; ++sz) {
                        const int cx = x * standardX + sx;
                        const int cz = z * standardZ + sz;
                        sum += terrain->AltitudeAtVertex(cx, cz);
                        if (terrain->CellHasWater(cx, cz)) base = 0xff;
                    }
                }
                heights[index(x, z)] = sum;
                work[index(x, z)] = base;
            }
        }
    }

    // ---- smooth the altitudes: separable 1-2-1, normalised by 1/standardCells and 1/16 -----------------
    {
        std::vector<float> row(static_cast<std::size_t>(std::max(rows, cols) + 2), 0.0f);
        const int width = std::max(1, zb - za);
        for (int x = xs; x < xe; ++x) {
            for (int z = za; z < zb; ++z) row[static_cast<std::size_t>(z - za + 1)] = heights[index(x, z)];
            row[0] = row[1];
            row[static_cast<std::size_t>(width + 1)] = row[static_cast<std::size_t>(width)];
            for (int z = za; z < zb; ++z) {
                const auto i = static_cast<std::size_t>(z - za);
                smoothed[index(x, z)] = (row[i] + 2.0f * row[i + 1] + row[i + 2]);
            }
        }
        const int height = std::max(1, xb - xa);
        const float factor = (1.0f / static_cast<float>(standardCells)) * 0.0625f;
        for (int z = za; z < zb; ++z) {
            for (int x = xa; x < xb; ++x) row[static_cast<std::size_t>(x - xa + 1)] = smoothed[index(x, z)];
            row[0] = row[1];
            row[static_cast<std::size_t>(height + 1)] = row[static_cast<std::size_t>(height)];
            for (int x = xa; x < xb; ++x) {
                const auto i = static_cast<std::size_t>(x - xa);
                smoothed[index(x, z)] = ((row[i] + 2.0f * row[i + 1] + row[i + 2]) * factor);
            }
        }
    }

    // ---- per-cell moisture additions --------------------------------------------------------------
    const int baseHumidityByte = RoundToInt(humidity_ * 255.0f);
    const int plainValue = ClampByte(baseHumidityByte);
    const int extraValue = ClampByte(baseHumidityByte + RoundToInt(peakExtraMoisture_ * 255.0f));
    float windDirectionX = windDirection_.x;
    float windDirectionZ = windDirection_.y;
    const float baseFloat = static_cast<float>(baseHumidityByte);
    const int half = standardCells >> 1;
    for (int x = xs; x < xe; ++x) {
        for (int z = zs; z < ze; ++z) {
            std::uint8_t flag = work[index(x, z)];
            if (flag == 0xff) continue;
            const float centre = smoothed[index(x, z)];
            const int xm = x == 0 ? 0 : x - 1;
            const int xp = x >= rows - 1 ? x : x + 1;
            const int zm = z == 0 ? 0 : z - 1;
            const int zp = z >= cols - 1 ? z : z + 1;
            const float lapX = (smoothed[index(xm, z)] + smoothed[index(xp, z)]) - (centre + centre);
            const float lapZ = (smoothed[index(x, zm)] + smoothed[index(x, zp)]) - (centre + centre);
            const float raw = heights[index(x, z)];
            int sum = 0;
            for (int sx = 0; sx < standardX; ++sx) {
                for (int sz = 0; sz < standardZ; ++sz) {
                    int value;
                    if (raw <= centre || (lapZ >= 0.0f && lapX >= 0.0f)) {
                        value = plainValue;
                        if ((lapZ + lapX) > peakThreshold_) value = extraValue;
                        if (lapX <= 0.0f) value = plainValue;
                        if (lapZ <= 0.0f) value = plainValue;
                    } else {
                        float normal[3] = {0.0f, 0.0f, 0.0f};
                        terrain->VertexNormal(x * standardX + sx, z * standardZ + sz, normal);
                        const float dot = windDirectionX * normal[0] + windDirectionZ * normal[2];
                        value = ClampByte(RoundToInt(baseFloat - dot * moistureScalar_));
                    }
                    sum += value;
                }
            }
            const int updated = static_cast<int>(flag) + (sum + half) / standardCells;
            work[index(x, z)] = ClampByte(updated);
        }
    }

    // ---- 1-4-6-4-1 blur into the moisture grid -----------------------------------------------------
    {
        std::vector<std::uint8_t> line(static_cast<std::size_t>(std::max(rows, cols) + 5), 0);
        const auto kernel = [&line](std::size_t i) {
            const std::uint32_t v = static_cast<std::uint32_t>(line[i]) + 4u * line[i + 1] + 6u * line[i + 2] +
                                    4u * line[i + 3] + line[i + 4];
            return static_cast<std::uint8_t>((v + 8u) >> 4);
        };
        std::vector<std::uint8_t> mid(work.size(), 0);
        const int width = std::max(1, zb - za);
        for (int x = xa; x < xb; ++x) {
            for (int z = za; z < zb; ++z) line[static_cast<std::size_t>(z - za + 2)] = work[index(x, z)];
            line[0] = line[2];
            line[1] = line[3];
            line[static_cast<std::size_t>(width + 2)] = line[static_cast<std::size_t>(width + 1)];
            line[static_cast<std::size_t>(width + 3)] = line[static_cast<std::size_t>(width + 1)];
            for (int z = za; z < zb; ++z) mid[index(x, z)] = kernel(static_cast<std::size_t>(z - za));
        }
        const int height = std::max(1, xb - xa);
        for (int z = za; z < zb; ++z) {
            for (int x = xa; x < xb; ++x) line[static_cast<std::size_t>(x - xa + 2)] = mid[index(x, z)];
            line[0] = line[2];
            line[1] = line[3];
            line[static_cast<std::size_t>(height + 2)] = line[static_cast<std::size_t>(height + 1)];
            line[static_cast<std::size_t>(height + 3)] = line[static_cast<std::size_t>(height + 1)];
            for (int x = xa; x < xb; ++x) {
                if (x < x0 || x >= x1 || z < z0 || z >= z1) continue;
                moistureMap_[static_cast<std::size_t>(x)][static_cast<std::size_t>(z)] =
                    kernel(static_cast<std::size_t>(x - xa));
            }
        }
    }

    PostMessageServerMessage(kWeatherMessageMoistureUpdated,
                             static_cast<cIGZUnknown*>(static_cast<cISC4WeatherSimulator*>(this)), 0, 0);
}

// ---------------------------------------------------------------------------------------------------
// serialization (version 2; Read also accepts the legacy versions 0 and 1)
// ---------------------------------------------------------------------------------------------------

// @ 0x1000e4016.
bool cSC4WeatherSimulator::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    stream.SetUint32(static_cast<std::uint32_t>(cellCountX_));
    stream.SetUint32(static_cast<std::uint32_t>(cellCountZ_));
    stream.SetFloat32(cellWidthX_);
    stream.SetFloat32(cellWidthZ_);
    stream.SetFloat32(inverseCellWidthX_);
    stream.SetFloat32(inverseCellWidthZ_);
    stream.SetFloat32(worldWidth_);
    stream.SetFloat32(worldDepth_);
    stream.SetUint32(static_cast<std::uint32_t>(standardCellsPerCellX_));
    stream.SetUint32(static_cast<std::uint32_t>(standardCellsPerCellZ_));
    stream.SetUint32(static_cast<std::uint32_t>(updateDivisor_));
    stream.SetUint32(static_cast<std::uint32_t>(secondDivisor_));
    stream.SetUint32(static_cast<std::uint32_t>(temperatureWriteRadius_));
    stream.SetUint32(static_cast<std::uint32_t>(lastPublishedSeason_));
    stream.SetFloat32(maxTemperature_);
    stream.SetFloat32(seasonalAmplitude_);
    stream.SetFloat32(temperatureScale_);
    stream.SetFloat32(temperature_);
    stream << temperatureMap_;
    stream << seaModeration_;
    stream.SetUint32(static_cast<std::uint32_t>(waterAreas_.size()));   // a 32-bit count, then one four-integer rect each
    for (const cRZRect& area : waterAreas_) stream << area;
    stream << cellWalk_;
    stream.SetUint32(static_cast<std::uint32_t>(cellsPerUpdate_));
    stream.SetUint32(static_cast<std::uint32_t>(seaModerationRadius_));
    stream.SetFloat32(coastWarming_);
    stream.SetFloat32(altitudeLapse_);
    stream.SetFloat32(windCooling_);
    stream << moistureMap_;
    stream << manualMoisture_;
    stream.SetFloat32(humidity_);
    stream.SetFloat32(baseHumidity_);
    stream.SetFloat32(humidityAmplitude_);
    stream.SetFloat32(humidityScale_);
    stream.SetFloat32(moistureScalar_);
    stream.SetFloat32(peakExtraMoisture_);
    stream.SetFloat32(peakThreshold_);
    stream.SetUint32(unused1_);
    stream.SetFloat32(baseWindSpeed_);
    stream << baseWindDirection_;
    stream.SetFloat32(windSpeed_);
    stream << windDirection_;
    stream.SetFloat32(previousWindSpeed_);
    stream << previousWindDirection_;
    stream.SetFloat32(targetWindSpeed_);
    stream << targetWindDirection_;
    random_.Write(stream);
    stream.SetUint32(static_cast<std::uint32_t>(ambientMonth_));
    // Note: the wind angle spread (+0x1dc) is a tunable and is not saved.
    return !stream.GetError();
}

// @ 0x1000e43a4. A loaded object keeps its grid sizes (Init skips the rebuild); versions below 2 also
// carry a legacy per-cell wind table and two legacy byte grids, both read and discarded.
bool cSC4WeatherSimulator::Read(cIGZIStream& stream) {
    loadedFromStream_ = true;
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version >= 3) return false;
    std::uint32_t value = 0;
    stream.GetUint32(value);
    cellCountX_ = static_cast<int>(value);
    stream.GetUint32(value);
    cellCountZ_ = static_cast<int>(value);
    stream.GetFloat32(cellWidthX_);
    stream.GetFloat32(cellWidthZ_);
    stream.GetFloat32(inverseCellWidthX_);
    stream.GetFloat32(inverseCellWidthZ_);
    stream.GetFloat32(worldWidth_);
    stream.GetFloat32(worldDepth_);
    stream.GetUint32(value);
    standardCellsPerCellX_ = static_cast<int>(value);
    stream.GetUint32(value);
    standardCellsPerCellZ_ = static_cast<int>(value);
    stream.GetUint32(value);
    updateDivisor_ = static_cast<int>(value);
    stream.GetUint32(value);
    secondDivisor_ = static_cast<int>(value);
    stream.GetUint32(value);
    temperatureWriteRadius_ = static_cast<int>(value);
    stream.GetUint32(value);
    lastPublishedSeason_ = static_cast<int>(value);
    stream.GetFloat32(maxTemperature_);
    stream.GetFloat32(seasonalAmplitude_);
    stream.GetFloat32(temperatureScale_);
    stream.GetFloat32(temperature_);
    stream >> temperatureMap_;
    stream >> seaModeration_;
    std::uint32_t areaCount = 0;
    stream.GetUint32(areaCount);
    // (the original resizes straight from the stream; the port refuses absurd counts like the container
    // readers in gz/StreamOps.cpp do)
    if (areaCount >= kStreamVectorLimit) return !stream.GetError();
    waterAreas_.assign(static_cast<std::size_t>(areaCount), cRZRect{});
    for (std::uint32_t i = 0; i < areaCount; ++i) stream >> waterAreas_[static_cast<std::size_t>(i)];
    stream >> cellWalk_;
    stream.GetUint32(value);
    cellsPerUpdate_ = static_cast<int>(value);
    stream.GetUint32(value);
    seaModerationRadius_ = static_cast<int>(value);
    stream.GetFloat32(coastWarming_);
    stream.GetFloat32(altitudeLapse_);
    stream.GetFloat32(windCooling_);
    stream >> moistureMap_;
    stream >> manualMoisture_;
    stream.GetFloat32(humidity_);
    stream.GetFloat32(baseHumidity_);
    stream.GetFloat32(humidityAmplitude_);
    stream.GetFloat32(humidityScale_);
    stream.GetFloat32(moistureScalar_);
    stream.GetFloat32(peakExtraMoisture_);
    stream.GetFloat32(peakThreshold_);
    stream.GetUint32(unused1_);
    stream.GetFloat32(baseWindSpeed_);
    stream >> baseWindDirection_;
    stream.GetFloat32(windSpeed_);
    stream >> windDirection_;
    stream.GetFloat32(previousWindSpeed_);
    stream >> previousWindDirection_;
    stream.GetFloat32(targetWindSpeed_);
    stream >> targetWindDirection_;
    if (version < 2) {
        // Legacy per-cell wind table: (direction, strength) pairs, read and dropped.
        std::uint32_t windCount = 0;
        stream.GetUint32(windCount);
        if (windCount >= kStreamVectorLimit) return !stream.GetError();
        std::vector<cS3DVector2> legacyWind;
        legacyWind.resize(static_cast<std::size_t>(windCount) * 2u);
        for (std::size_t i = 0; i < legacyWind.size(); ++i) stream >> legacyWind[i];
        // Two legacy byte grids (the old moisture / cloud maps), read and dropped.
        std::vector<std::vector<std::uint8_t>> legacyGrid;
        stream >> legacyGrid;
        legacyGrid.clear();
        stream >> legacyGrid;
    }
    random_.Read(stream);
    stream.GetUint32(value);
    ambientMonth_ = static_cast<int>(value);
    return !stream.GetError();
}

}  // namespace sc4
