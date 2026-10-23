// Test for cSC4WeatherSimulator (src/sc4/WeatherSimulator.cpp).
//
// The fake host records every call the original makes on the framework / city / simulator / message server
// seams, and the fake terrain is an analytic height/water field, so the recovered formulas can be checked
// against hand-computed values: the seasonal cosine curves, the wind-edge choice and its gaussian rotation,
// the daily wind interpolation, the temperature quantisation and in-place averaging, the weekly random walk
// and its completion message, the water-area moderation and the moisture-grid rebuild.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gz/cIGZMessage2.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZIStream.h"
#include "rz/cRZOStream.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4/WeatherSimulator.h"

using namespace sc4;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {

bool Near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps * (1.0f + std::fabs(b)); }

// ---------------------------------------------------------------------------------------------------
// fake terrain: an analytic height field with a water mask
// ---------------------------------------------------------------------------------------------------
class FakeTerrain final : public IWeatherTerrain {
public:
    static constexpr int kCells = 128;
    FakeTerrain() : heights(kCells * kCells, 0.0f), water(kCells * kCells, 0) {}
    float& Height(int x, int z) { return heights[static_cast<std::size_t>(x) * kCells + z]; }
    std::uint8_t& Water(int x, int z) { return water[static_cast<std::size_t>(x) * kCells + z]; }

    int CellCountX() override { return kCells; }
    int CellCountZ() override { return kCells; }
    void GetAltitudes(int x0, int z0, int x1, int z1, float* out, int stepX, int stepZ) override {
        for (int x = x0; x <= x1; x += stepX)
            for (int z = z0; z <= z1; z += stepZ) *out++ = Height(x, z);
    }
    float AltitudeAtVertex(int x, int z) override {
        if (x < 0 || x >= kCells || z < 0 || z >= kCells) return 0.0f;
        return Height(x, z);
    }
    bool CellHasWater(int x, int z) override {
        if (x < 0 || x >= kCells || z < 0 || z >= kCells) return false;
        return Water(x, z) != 0;
    }
    void VertexNormal(int, int, float out[3]) override {
        out[0] = normal[0];
        out[1] = normal[1];
        out[2] = normal[2];
    }
    float SeaLevel() override { return seaLevel; }

    std::vector<float> heights;
    std::vector<std::uint8_t> water;
    float normal[3] = {1.0f, 0.0f, 0.0f};
    float seaLevel = 0.0f;
};

// ---------------------------------------------------------------------------------------------------
// fake host: records every seam call
// ---------------------------------------------------------------------------------------------------
struct PostedMessage {
    std::uint32_t type = 0;
    std::uint64_t data1 = 0, data2 = 0;
    void* void1 = nullptr;
    cIGZUnknown* unknown = nullptr;
};

class FakeHost final : public IWeatherHost {
public:
    cISCPropertyHolder* SimulatorTunables(const cGZPersistResourceKey& key, std::uint32_t filter) override {
        tunablesKey = key;
        tunablesFilter = filter;
        hasTunables = true;
        return &tunables;
    }
    float CitySizeX() override { return citySizeX; }
    float CitySizeZ() override { return citySizeZ; }
    float CityCellSizeX() override { return cellSizeX; }
    float CityCellSizeZ() override { return cellSizeZ; }
    int CityHemisphere() override { return hemisphere; }
    int SimulatorMonth() override { return month; }
    void RegisterAgent(cIGZMessageTarget2* target, int interval, const char* name) override {
        agents.push_back({target, interval, name ? name : ""});
    }
    void UnregisterAgent(cIGZMessageTarget2* target) override {
        ++unregisterCount;
        lastAgent = target;
    }
    void AddNotification(cIGZMessageTarget2* target, std::uint32_t type) override {
        notifications.push_back({target, type});
    }
    void RemoveNotification(cIGZMessageTarget2* target, std::uint32_t type) override {
        removed.push_back({target, type});
    }
    void PostMessage(cIGZMessage2* message) override {
        auto* standard = static_cast<cIGZMessage2Standard*>(message);
        PostedMessage posted;
        posted.type = standard->GetType();
        posted.data1 = standard->GetData1();
        posted.data2 = standard->GetData2();
        posted.void1 = standard->GetVoid1();
        posted.unknown = standard->GetIGZUnknown();
        posts.push_back(posted);
    }
    void MoistureValueRange(std::uint8_t& low, std::uint8_t& high) override {
        low = moistureLow;
        high = moistureHigh;
    }
    IWeatherTerrain* Terrain() override { return &terrain; }
    void MakeWaterAreaList(IWeatherTerrain&, std::vector<cRZRect>& out, int minWidth, int minDepth, int cellsX,
                           int cellsZ, int cellWidth, int cellDepth, const cRZRect* area) override {
        ++waterAreaCalls;
        waterAreaArgs = {minWidth, minDepth, cellsX, cellsZ, cellWidth, cellDepth, area == nullptr};
        out = waterAreaResult;
    }

    struct Agent {
        cIGZMessageTarget2* target;
        int interval;
        std::string name;
    };
    struct Notification {
        cIGZMessageTarget2* target;
        std::uint32_t type;
    };

    FakeTerrain terrain;
    cSCPropertyHolder tunables;
    float citySizeX = 1024.0f, citySizeZ = 1024.0f;
    float cellSizeX = 8.0f, cellSizeZ = 8.0f;
    int hemisphere = 1;
    int month = 1;
    std::uint8_t moistureLow = 0x3c, moistureHigh = 100;
    bool hasTunables = false;
    cGZPersistResourceKey tunablesKey;
    std::uint32_t tunablesFilter = 0;
    std::vector<Agent> agents;
    std::vector<Notification> notifications;
    std::vector<Notification> removed;
    std::vector<PostedMessage> posts;
    std::vector<cRZRect> waterAreaResult;
    int waterAreaCalls = 0;
    struct {
        int minWidth = 0, minDepth = 0, cellsX = 0, cellsZ = 0, cellWidth = 0, cellDepth = 0;
        bool nullArea = false;
    } waterAreaArgs;
    int unregisterCount = 0;
    cIGZMessageTarget2* lastAgent = nullptr;

    int CountPosts(std::uint32_t type) const {
        int n = 0;
        for (const auto& p : posts)
            if (p.type == type) ++n;
        return n;
    }
};

// ---------------------------------------------------------------------------------------------------
// in-memory streams (the production cRZIStream/cRZOStream halves)
// ---------------------------------------------------------------------------------------------------
class MemoryOut final : public cRZOStream {
public:
    MemoryOut() : cRZOStream(true) {}
    void Flush() override {}
    bool SetVoid(const void* source, std::uint32_t byteCount) override {
        const auto* bytes = static_cast<const std::uint8_t*>(source);
        data.insert(data.end(), bytes, bytes + byteCount);
        return true;
    }
    bool GetError() const override { return false; }
    std::vector<std::uint8_t> data;
};

class MemoryIn final : public cRZIStream {
public:
    explicit MemoryIn(const std::vector<std::uint8_t>& bytes) : cRZIStream(true), data(bytes) {}
    bool Skip(std::uint32_t byteCount) override {
        if (position + byteCount > data.size()) return false;
        position += byteCount;
        return true;
    }
    bool GetVoid(void* destination, std::uint32_t byteCount) override {
        if (position + byteCount > data.size()) return false;
        std::memcpy(destination, data.data() + position, byteCount);
        position += byteCount;
        return true;
    }
    bool GetError() const override { return false; }
    std::vector<std::uint8_t> data;
    std::size_t position = 0;
};

// A live simulator always has a reference from the city; the message it posts takes (and drops) another
// one through SetIGZUnknown, so an object at reference count zero would delete itself mid-call.
cSC4WeatherSimulator* Create(FakeHost& host) {
    auto* simulator = new cSC4WeatherSimulator(&host);
    simulator->AddRef();
    return simulator;
}

void Destroy(cSC4WeatherSimulator* simulator) { simulator->Release(); }

void SendMessage(cSC4WeatherSimulator& simulator, std::uint32_t type, std::uint64_t data1 = 0,
                 std::uint64_t data3 = 0) {
    auto* message = new cRZMessage2Standard();
    message->AddRef();                    // the message server's own reference
    message->SetType(type);
    if (data1 != 0) message->SetData1(data1);
    if (data3 != 0) message->SetData3(data3);
    simulator.DoMessage(static_cast<cIGZMessage2*>(message));
    message->Release();
}

}  // namespace

int main() {
    // -----------------------------------------------------------------------------------------------
    // A. constructor defaults
    // -----------------------------------------------------------------------------------------------
    FakeHost host;
    auto* sim = Create(host);
    CHECK(sim->RefCount() == 1);
    CHECK(!sim->IsInitialized());
    CHECK(sim->CellWidthX() == 16.0f && sim->CellWidthZ() == 16.0f);
    CHECK(sim->CellCountX() == 0 && sim->CellCountZ() == 0);
    CHECK(sim->MaxTemperature() == 24.0f);
    CHECK(sim->SeasonalAmplitude() == 0.5f);
    CHECK(sim->CurrentTemperatureValue() == 20.0f);
    CHECK(sim->CurrentHumidityValue() == 0.5f);
    CHECK(sim->BaseHumidity() == 0.5f);
    CHECK(sim->HumidityAmplitude() == 0.5f);
    CHECK(sim->HumidityScale() == 0.4f);
    CHECK(sim->BaseWindSpeed() == 3.0f);
    CHECK(sim->CurrentWindSpeed() == 3.0f);
    CHECK(sim->UpdateDivisor() == 8 && sim->SecondDivisor() == 8);
    CHECK(sim->TemperatureWriteRadius() == 1);
    CHECK(sim->SeaModerationRadius() == 6.0f);
    CHECK(sim->AltitudeLapse() == 0.0035f);
    CHECK(sim->MoistureScalar() == 128.0f);
    CHECK(sim->PeakExtraMoisture() == 0.12f);
    CHECK(sim->PeakThreshold() == 4.0f);
    CHECK(sim->WindAngleSpread() == 0.25f);
    CHECK(sim->AmbientMonth() == -1);
    CHECK(sim->LastPublishedSeason() == -1);
    // Before Init the moisture query answers 0.
    CHECK(sim->GetMoistureValue(10.0f, 10.0f) == 0);

    // -----------------------------------------------------------------------------------------------
    // B. QueryInterface / AddRef / Release
    // -----------------------------------------------------------------------------------------------
    void* out = nullptr;
    CHECK(sim->QueryInterface(kGZIID_cIGZUnknown, &out) && out != nullptr);
    CHECK(sim->RefCount() == 2);
    out = nullptr;
    CHECK(sim->QueryInterface(kGZIID_cISC4WeatherSimulator, &out));
    CHECK(out == static_cast<cIGZUnknown*>(static_cast<cISC4WeatherSimulator*>(sim)));
    CHECK(sim->RefCount() == 3);
    out = nullptr;
    CHECK(sim->QueryInterface(kGZIID_cIGZMessageTargetLegacy, &out));
    CHECK(out == static_cast<cIGZMessageTarget*>(sim));
    out = nullptr;
    CHECK(sim->QueryInterface(kGZIID_cIGZMessageTarget2, &out));
    CHECK(out == static_cast<cIGZMessageTarget2*>(sim));
    out = nullptr;
    CHECK(sim->QueryInterface(kGZIID_cIGZSerializable, &out));
    CHECK(out == static_cast<cIGZSerializable*>(sim));
    CHECK(sim->RefCount() == 6);
    out = reinterpret_cast<void*>(0x1);
    CHECK(!sim->QueryInterface(0x12345678, &out));
    CHECK(sim->RefCount() == 6);
    CHECK(sim->GetGZCLSID() == kGZCLSID_cSC4WeatherSimulator);
    for (int i = 0; i < 5; ++i) sim->Release();
    CHECK(sim->RefCount() == 1);

    // -----------------------------------------------------------------------------------------------
    // C. Init: grids, registration, the random walk
    // -----------------------------------------------------------------------------------------------
    CHECK(sim->Init());
    CHECK(sim->IsInitialized());
    CHECK(sim->CellCountX() == 64 && sim->CellCountZ() == 64);      // 1024 m / 16 m
    CHECK(sim->StandardCellsPerCellX() == 2 && sim->StandardCellsPerCellZ() == 2);   // 16 m / 8 m
    CHECK(sim->CellsPerUpdate() == (64 * 64) / 8);
    CHECK(sim->TemperatureMap().size() == 64 && sim->TemperatureMap()[0].size() == 64);
    CHECK(sim->MoistureMap().size() == 64 && sim->MoistureMap()[0].size() == 64);
    CHECK(sim->ManualMoistureMap().size() == 64);
    CHECK(sim->SeaModeration().size() == 64 && sim->SeaModeration()[0].size() == 64);
    CHECK(sim->CurrentTemperatureValue() == 24.0f);            // Init copies the max temperature
    CHECK(sim->CurrentHumidityValue() == 0.5f);
    // The moisture-variation walk stays inside the land simulator's range.
    for (const auto& column : sim->MoistureMap())
        for (std::uint8_t v : column) CHECK(v >= host.moistureLow && v <= host.moistureHigh);
    CHECK(host.agents.size() == 3);
    CHECK(host.agents.size() == 3 && host.agents[0].interval == 6 && host.agents[1].interval == 5 &&
          host.agents[2].interval == 4);
    CHECK(host.agents[0].name == "cSC4WeatherSimulator");
    CHECK(host.notifications.size() == 1 && host.notifications[0].type == kWeatherMessageCityStarted);
    CHECK(host.hasTunables && host.tunablesKey.type == 0x6534284a && host.tunablesKey.group == 0x7a4a8458 &&
          host.tunablesKey.instance == 0x1a2fdb6b && host.tunablesFilter == 0xa52160f5u);
    // A second Init does nothing.
    CHECK(sim->Init());
    CHECK(host.agents.size() == 3 && host.notifications.size() == 1);
    // The random walk covers every cell exactly once per pass.
    {
        RandomIterator walk;
        walk.SetRect(0, 0, 63, 63);
        walk.SetModular(true);
        walk.Reset(false);
        std::vector<int> seen(64 * 64, 0);
        int x = 0, z = 0;
        int count = 0;
        while (walk.Next(x, z)) {
            CHECK(x >= 0 && x < 64 && z >= 0 && z < 64);
            ++seen[static_cast<std::size_t>(x) * 64 + z];
            ++count;
        }
        CHECK(count == 64 * 64);
        int visited = 0;
        for (int n : seen) visited += n == 1 ? 1 : 0;
        CHECK(visited == 64 * 64);
    }

    // -----------------------------------------------------------------------------------------------
    // D. geometry, conversions and their quirks
    // -----------------------------------------------------------------------------------------------
    {
        int cellX = -1, cellZ = -1;
        sim->PositionToCell(100.0f, 40.0f, cellX, cellZ);
        CHECK(cellX == 6 && cellZ == 2);
        float x = 0.0f, z = 0.0f;
        sim->CellToPosition(6, 2, x, z);
        CHECK(x == 96.0f && z == 32.0f);
        int stdX = -1, stdZ = -1;
        sim->CellToStandardCityCell(3, 5, stdX, stdZ);
        CHECK(stdX == 6);
        CHECK(stdZ == 6);                       // quirk: computed from cellX again, not cellZ
        sim->StandardCityCellToCell(6, 7, cellX, cellZ);
        CHECK(cellX == 3 && cellZ == 3);
    }

    // -----------------------------------------------------------------------------------------------
    // E. seasons (hemisphere shift)
    // -----------------------------------------------------------------------------------------------
    host.month = 1;
    host.hemisphere = 1;
    CHECK(sim->GetCurrentSeasonMonth() == 1 && sim->GetCurrentSeason() == 0);
    host.month = 4;
    CHECK(sim->GetCurrentSeasonMonth() == 4 && sim->GetCurrentSeason() == 1);
    host.month = 12;
    CHECK(sim->GetCurrentSeason() == 3);
    host.hemisphere = 2;                        // southern: (m + 5) % 12 + 1
    host.month = 1;
    CHECK(sim->GetCurrentSeasonMonth() == 7 && sim->GetCurrentSeason() == 2);
    host.hemisphere = 1;
    host.month = 1;

    // -----------------------------------------------------------------------------------------------
    // F. moisture queries, manual additions, humidity
    // -----------------------------------------------------------------------------------------------
    sim->MoistureMap()[2][3] = 200;
    CHECK(sim->GetMoistureValue(2 * 16.0f + 0.5f, 3 * 16.0f + 0.5f) == 200);
    CHECK(sim->GetMoistureType(2 * 16.0f + 0.5f, 3 * 16.0f + 0.5f) == 200 >> 4);
    // Clamped into the city: negative -> 0, past the far edge -> last cell.
    sim->MoistureMap()[0][0] = 11;
    sim->MoistureMap()[63][63] = 22;
    CHECK(sim->GetMoistureValue(-1000.0f, -1000.0f) == 11);
    CHECK(sim->GetMoistureValue(1e6f, 1e6f) == 22);
    {
        std::vector<std::vector<std::uint8_t>>* map = nullptr;
        sim->GetMoistureValueMapPtr(map);
        CHECK(map == &sim->MoistureMap());
    }
    // Manual additions: value * 1.275 stored as a byte (200 -> 255).
    sim->SetManualMoistureAddition(0.0f, 0.0f, 64.0f, 32.0f, 100.0f);    // x [0,4), z [0,2)
    CHECK(sim->ManualMoistureMap()[0][0] == static_cast<std::uint8_t>(128));   // round(127.5) = 128
    CHECK(sim->ManualMoistureMap()[3][1] == 128);
    CHECK(sim->ManualMoistureMap()[4][0] == 0);
    sim->SetManualMoistureAddition(0.0f, 0.0f, 16.0f, 16.0f, 250.0f);    // round(318.75) = 319 -> 63
    CHECK(sim->ManualMoistureMap()[0][0] == 63);
    sim->ManualMoistureMap()[0][0] = 0;
    // GetHumidity ignores the position and uses the *base* humidity: (1 + base) / 2.
    CHECK(Near(sim->GetHumidity(0.0f, 0.0f), 0.75f));
    sim->SetHumidityForTest(0.9f);
    CHECK(Near(sim->GetHumidity(500.0f, 700.0f), 0.75f));
    sim->SetHumidityForTest(0.5f);

    // -----------------------------------------------------------------------------------------------
    // G. ambient values: the two cosine curves, the wind edge and its gaussian rotation
    // -----------------------------------------------------------------------------------------------
    {
        sim->Random().Seed(1234);
        cRZRandom expectedRandom(1234);
        sim->GetNewCurrentAmbientValues(6);
        const float seasonCos = std::cos(static_cast<float>(6 - 8) * 6.2831855f / 12.0f);
        const float expectTemp = ((0.5f * 0.5f * seasonCos) + 1.0f) * 24.0f;
        CHECK(Near(sim->CurrentTemperatureValue(), expectTemp, 1e-4f));
        const float humidityCos = std::cos(static_cast<float>(6 - 1) * 6.2831855f / 12.0f);
        const float expectHumidity = ((0.5f * 0.4f * humidityCos) + 1.0f) * 0.5f;
        CHECK(Near(sim->CurrentHumidityValue(), expectHumidity, 1e-4f));
        CHECK(sim->AmbientMonth() == 6);
        // Previous = the values before the call (speed 3, direction (1,0)).
        CHECK(sim->PreviousWindSpeed() == 3.0f);
        CHECK(sim->PreviousWindDirection().x == 1.0f && sim->PreviousWindDirection().y == 0.0f);
        // Target speed is uniform in [2,4), then the angle loop: the all-land city sorts the four edge
        // directions by (water, x, z), so the east edge (-1,0) comes first.
        const double expectSpeed = expectedRandom.RandomDoubleRangeUniform(2.0, 4.0);
        CHECK(Near(sim->TargetWindSpeed(), static_cast<float>(expectSpeed), 1e-4f));
        float angle = 0.0f;
        do {
            angle = static_cast<float>(expectedRandom.RandomDoubleGaussian() * static_cast<double>(0.25f));
        } while (std::fabs(angle) > 1.0f);
        const float cosAngle = static_cast<float>(std::sqrt(1.0 - static_cast<double>(angle * angle)));
        CHECK(Near(sim->TargetWindDirection().x, -cosAngle, 1e-4f));
        CHECK(Near(sim->TargetWindDirection().y, -angle, 1e-4f));
    }
    // Ambient wind: the direction is scaled to the speed.
    {
        float direction[2] = {0.0f, 0.0f};
        const float speed = sim->GetAmbientWind(direction);
        CHECK(speed == sim->CurrentWindSpeed());
        CHECK(Near(direction[0], sim->WindDirectionX() * speed));
        CHECK(Near(direction[1], sim->WindDirectionZ() * speed));
        CHECK(Near(std::sqrt(direction[0] * direction[0] + direction[1] * direction[1]), speed, 1e-4f));
        // GetWindAtCell ignores the cell and returns the ambient wind.
        float cellDirection[2] = {0.0f, 0.0f};
        CHECK(sim->GetWindAtCell(5, 7, cellDirection) == speed);
        CHECK(cellDirection[0] == direction[0] && cellDirection[1] == direction[1]);
        CHECK(sim->GetWindAtCell(60, 60, nullptr) == speed);
    }

    // -----------------------------------------------------------------------------------------------
    // H. the day message interpolates the current wind across the month
    // -----------------------------------------------------------------------------------------------
    {
        sim->Random().Seed(99);
        sim->GetNewCurrentAmbientValues(6);
        const float fromSpeed = sim->PreviousWindSpeed();
        const float fromX = sim->PreviousWindDirection().x;
        const float fromZ = sim->PreviousWindDirection().y;
        const float toSpeed = sim->TargetWindSpeed();
        const float toX = sim->TargetWindDirection().x;
        const float toZ = sim->TargetWindDirection().y;
        const std::uint32_t julian = cRZDate::ComputeJulianDay(6, 16, 2000);
        SendMessage(*sim, kWeatherMessageDay, julian);
        const float fraction = 15.0f / 30.0f;
        const float ix = fraction * (toSpeed * toX - fromSpeed * fromX) + fromSpeed * fromX;
        const float iz = fraction * (toSpeed * toZ - fromSpeed * fromZ) + fromSpeed * fromZ;
        const float length = std::sqrt(iz * iz + ix * ix);
        CHECK(Near(sim->WindDirectionX(), ix / length, 1e-4f));
        CHECK(Near(sim->WindDirectionZ(), iz / length, 1e-4f));
        CHECK(Near(sim->CurrentWindSpeed(), (toSpeed - fromSpeed) * fraction + fromSpeed, 1e-4f));
        // A day in another month leaves the wind alone.
        const float keptX = sim->WindDirectionX();
        SendMessage(*sim, kWeatherMessageDay, cRZDate::ComputeJulianDay(7, 3, 2000));
        CHECK(sim->WindDirectionX() == keptX);
    }

    // -----------------------------------------------------------------------------------------------
    // I. temperature per cell, quantisation and the write radius
    // -----------------------------------------------------------------------------------------------
    {
        FakeHost tempHost;
        auto* t = new cSC4WeatherSimulator(&tempHost);
        CHECK(t->Init());
        for (int x = 0; x < 128; ++x)
            for (int z = 0; z < 128; ++z) tempHost.terrain.Height(x, z) = 100.0f;
        t->SetTemperatureWriteRadius(0);
        t->SetAirTemperatureForTest(20.0f);
        t->SimulateCellTemperature(5, 7);
        // 20 C + 273 - 100 * 0.0035 - 3 * 0.25 - 273 = 18.9 C -> round((18.9 + 15) * 4.25) = 144.
        CHECK(t->TemperatureMap()[5][7] == 144);
        // Sea moderation pulls the temperature towards the maximum by 0.95.
        t->SeaModeration()[5][7] = true;
        t->SimulateCellTemperature(5, 7);
        // 20 + (24 - 20) * 0.95 = 23.8 C -> round((22.7 + 15) * 4.25) = 160.
        CHECK(t->TemperatureMap()[5][7] == 160);
        t->SeaModeration()[5][7] = false;
        // Clamping: a very cold cell saturates at -15 C (byte 0), a hot one at 45 C (byte 255).
        t->SetAirTemperatureForTest(-100.0f);
        t->SimulateCellTemperature(9, 9);
        CHECK(t->TemperatureMap()[9][9] == 0);
        t->SetAirTemperatureForTest(100.0f);
        t->SimulateCellTemperature(9, 9);
        CHECK(t->TemperatureMap()[9][9] == 255);
        // WriteTemperatureValue with a radius averages in place: (127 + 163) / 2 = 145.
        for (auto& column : t->TemperatureMap())
            for (std::uint8_t& v : column) v = 127;
        t->SetTemperatureWriteRadius(1);
        t->WriteTemperatureValue(10, 10, 163);
        for (int x = 9; x < 11; ++x)
            for (int z = 9; z < 11; ++z) CHECK(t->TemperatureMap()[x][z] == 145);
        CHECK(t->TemperatureMap()[8][8] == 127);
        CHECK(t->TemperatureMap()[11][11] == 127);
        // A radius that would leave the grid only touches the cells inside it.
        t->SetTemperatureWriteRadius(4);
        t->WriteTemperatureValue(1, 1, 255);
        CHECK(t->TemperatureMap()[0][0] == 191);      // (127 + 255) / 2
        CHECK(t->TemperatureMap()[4][3] == 191);      // x = 4 is still inside [1 - 4, 1 + 4)
        CHECK(t->TemperatureMap()[4][4] == 191);
        CHECK(t->TemperatureMap()[5][4] == 127);
        (void)t->Release();
    }

    // -----------------------------------------------------------------------------------------------
    // J. the weekly walk, its completion message and its temperature sweep
    // -----------------------------------------------------------------------------------------------
    {
        FakeHost weekHost;
        auto* week = Create(weekHost);
        CHECK(week->Init());
        for (auto& column : week->TemperatureMap())
            for (std::uint8_t& v : column) v = 127;
        week->SetAirTemperatureForTest(24.0f);
        const int before = weekHost.CountPosts(kWeatherMessageWeeklyPassDone);
        // 4096 cells / 512 per pass: the eighth call exhausts the walk, the ninth rewinds it and posts.
        for (int i = 0; i < 8; ++i) CHECK(!week->DoWeeklySimulation(i + 1));
        CHECK(weekHost.CountPosts(kWeatherMessageWeeklyPassDone) == before);
        CHECK(week->DoWeeklySimulation(9));
        CHECK(weekHost.CountPosts(kWeatherMessageWeeklyPassDone) == before + 1);
        CHECK(weekHost.CountPosts(kWeatherMessageMoistureUpdated) == 9);
        // Every cell was visited in the first eight passes: the flat terrain at 24 C with a 3 m/s wind
        // writes 163, so no cell can still hold its initial 127 and none can exceed 163.
        for (const auto& column : week->TemperatureMap()) {
            for (std::uint8_t v : column) {
                CHECK(v >= 145);
                CHECK(v <= 163);
            }
        }
        Destroy(week);
    }

    // -----------------------------------------------------------------------------------------------
    // K. monthly step: water areas, sea moderation and the season message
    // -----------------------------------------------------------------------------------------------
    {
        FakeHost monthHost;
        auto* m = Create(monthHost);
        CHECK(m->Init());
        monthHost.month = 6;
        monthHost.waterAreaResult = {{0, 5, 10, 10}, {5, 5, 60, 60}};
        const int seasonPosts = monthHost.CountPosts(kWeatherMessageSeasonChanged);
        m->DoMonthlySimulation(1);
        CHECK(monthHost.waterAreaCalls == 1);
        CHECK(monthHost.waterAreaArgs.minWidth == 3 && monthHost.waterAreaArgs.minDepth == 5);
        CHECK(monthHost.waterAreaArgs.cellsX == 64 && monthHost.waterAreaArgs.cellsZ == 64);
        CHECK(monthHost.waterAreaArgs.cellWidth == 16 && monthHost.waterAreaArgs.cellDepth == 16);
        CHECK(monthHost.waterAreaArgs.nullArea);
        // The inner area (5,5)-(60,60) was dropped: it does not touch the city edge.
        CHECK(m->WaterAreas().size() == 1);
        CHECK(m->WaterAreas()[0].left == 0 && m->WaterAreas()[0].right == 10);
        // (0,5)-(10,10) grown by six cells, clamped to the grid: x 0..16, z 0..16.
        int moderated = 0;
        for (const auto& column : m->SeaModeration())
            for (bool v : column) moderated += v ? 1 : 0;
        CHECK(moderated == 17 * 17);
        CHECK(m->SeaModeration()[0][0] && m->SeaModeration()[16][16] && !m->SeaModeration()[17][0]);
        // Month 6 -> season month 6 -> season 1; the first publication posts once.
        CHECK(m->LastPublishedSeason() == 1);
        CHECK(monthHost.CountPosts(kWeatherMessageSeasonChanged) == seasonPosts + 1);
        CHECK(monthHost.posts.back().data2 == 1);      // the season travels in data 2
        CHECK(monthHost.posts.back().void1 == static_cast<void*>(static_cast<cIGZUnknown*>(static_cast<cISC4WeatherSimulator*>(m))));
        CHECK(monthHost.posts.back().unknown == static_cast<cIGZUnknown*>(static_cast<cISC4WeatherSimulator*>(m)));
        // The new season resets the wind to the base values.
        CHECK(m->CurrentWindSpeed() == m->BaseWindSpeed());
        CHECK(m->WindDirectionX() == m->BaseWindDirection().x && m->WindDirectionZ() == m->BaseWindDirection().y);
        // The same season again: no second publication and no new water scan.
        m->DoMonthlySimulation(2);
        CHECK(monthHost.CountPosts(kWeatherMessageSeasonChanged) == seasonPosts + 1);
        CHECK(monthHost.waterAreaCalls == 1);
        monthHost.month = 10;
        m->DoMonthlySimulation(3);
        CHECK(m->LastPublishedSeason() == 3);
        CHECK(monthHost.CountPosts(kWeatherMessageSeasonChanged) == seasonPosts + 2);
        Destroy(m);
    }
    // RemoveWaterThatIsNotAtCityEdge on its own.
    {
        std::vector<cRZRect> areas = {{0, 5, 10, 10}, {1, 1, 62, 62}, {5, 5, 60, 60}, {30, 0, 40, 40}};
        sim->RemoveWaterThatIsNotAtCityEdge(areas);
        // Only the areas touching (or crossing) the border survive: right 62 < 63 is still inside.
        CHECK(areas.size() == 2);
        CHECK(areas[0].left == 0 && areas[1].top == 0 && areas[1].left == 30);
    }

    // -----------------------------------------------------------------------------------------------
    // L. SimulateMoisture
    // -----------------------------------------------------------------------------------------------
    {
        FakeHost moistureHost;
        auto* w = Create(moistureHost);
        CHECK(w->Init());
        for (int x = 0; x < 128; ++x)
            for (int z = 0; z < 128; ++z) {
                moistureHost.terrain.Height(x, z) = 0.0f;
                moistureHost.terrain.Water(x, z) = false;
            }
        w->SetHumidityForTest(0.5f);
        const int posts = moistureHost.CountPosts(kWeatherMessageMoistureUpdated);
        w->SimulateMoisture(WeatherRect{0, 0, 64, 64});
        CHECK(moistureHost.CountPosts(kWeatherMessageMoistureUpdated) == posts + 1);
        // Flat, dry land: every cell gets round(0.5 * 255) = 128 and the blur leaves it unchanged.
        for (const auto& column : w->MoistureMap())
            for (std::uint8_t v : column) CHECK(v == 128);

        // A water strip on the west (weather cells 0..5): deep water stays 0xff, far land stays 128 and the
        // shore is in between.
        for (int x = 0; x < 12; ++x)
            for (int z = 0; z < 128; ++z) moistureHost.terrain.Water(x, z) = true;
        w->SimulateMoisture(WeatherRect{0, 0, 64, 64});
        CHECK(w->MoistureMap()[1][30] == 255);
        CHECK(w->MoistureMap()[0][0] == 255);
        CHECK(w->MoistureMap()[30][30] == 128);
        CHECK(w->MoistureMap()[6][30] > 128 && w->MoistureMap()[6][30] < 255);

        // A sharp peak on an otherwise flat field takes the wind/rain branch: the fake normal (1,0,0)
        // dotted with the (1,0)-ish wind gives the maximum rain subtraction, so the peak is drier.
        for (int x = 0; x < 128; ++x)
            for (int z = 0; z < 128; ++z) moistureHost.terrain.Water(x, z) = false;
        moistureHost.terrain.Height(60, 60) = 500.0f;
        moistureHost.terrain.Height(61, 61) = 500.0f;
        w->SimulateMoisture(WeatherRect{0, 0, 64, 64});
        CHECK(w->MoistureMap()[30][30] < 128);
        CHECK(w->MoistureMap()[2][2] == 128);
        Destroy(w);
    }
    // The common case (16 m city cells): one standard cell per weather cell takes the GetAltitudes path.
    {
        FakeHost flatHost;
        flatHost.cellSizeX = 16.0f;
        flatHost.cellSizeZ = 16.0f;
        auto* w = Create(flatHost);
        CHECK(w->Init());
        CHECK(w->StandardCellsPerCellX() == 1 && w->StandardCellsPerCellZ() == 1);
        for (int x = 0; x < 128; ++x)
            for (int z = 0; z < 128; ++z) {
                flatHost.terrain.Height(x, z) = 0.0f;
                flatHost.terrain.Water(x, z) = false;
            }
        for (int x = 0; x < 12; ++x)
            for (int z = 0; z < 128; ++z) flatHost.terrain.Water(x, z) = true;
        w->SetHumidityForTest(0.5f);
        w->SimulateMoisture(WeatherRect{0, 0, 64, 64});
        CHECK(w->MoistureMap()[1][30] == 255);
        CHECK(w->MoistureMap()[40][40] == 128);
        Destroy(w);
    }

    // -----------------------------------------------------------------------------------------------
    // M. serialization round trip (version 2)
    // -----------------------------------------------------------------------------------------------
    {
        MemoryOut out;
        CHECK(sim->Write(out));
        CHECK(out.data.size() > 64);
        // The version is the first uint16.
        std::uint16_t version = 0xffff;
        std::memcpy(&version, out.data.data(), 2);
        CHECK(version == 2);
        FakeHost otherHost;
        otherHost.citySizeX = 512.0f;                    // a different city: the read sizes must win
        otherHost.citySizeZ = 512.0f;
        auto* loaded = Create(otherHost);
        MemoryIn in(out.data);
        CHECK(loaded->Read(in));
        CHECK(loaded->LoadedFromStream());
        CHECK(loaded->CellCountX() == sim->CellCountX() && loaded->CellCountZ() == sim->CellCountZ());
        CHECK(loaded->CurrentTemperatureValue() == sim->CurrentTemperatureValue());
        CHECK(loaded->CurrentHumidityValue() == sim->CurrentHumidityValue());
        CHECK(loaded->CurrentWindSpeed() == sim->CurrentWindSpeed());
        CHECK(loaded->AmbientMonth() == sim->AmbientMonth());
        CHECK(loaded->MoistureMap() == sim->MoistureMap());
        CHECK(loaded->TemperatureMap() == sim->TemperatureMap());
        CHECK(loaded->WaterAreas().size() == sim->WaterAreas().size());
        CHECK(loaded->CellsPerUpdate() == sim->CellsPerUpdate());
        // Init on a loaded object keeps the streamed grid sizes.
        CHECK(loaded->Init());
        CHECK(loaded->CellCountX() == sim->CellCountX());
        CHECK(loaded->MoistureMap().size() == sim->MoistureMap().size());
        // A future version is refused without touching the stream further.
        std::vector<std::uint8_t> future(out.data);
        const std::uint16_t v3 = 3;
        std::memcpy(future.data(), &v3, 2);
        MemoryIn futureIn(future);
        CHECK(!loaded->Read(futureIn));
        Destroy(loaded);
    }

    // -----------------------------------------------------------------------------------------------
    // N. the city-started message runs the catch-up simulation once
    // -----------------------------------------------------------------------------------------------
    {
        FakeHost catchHost;
        auto* c = Create(catchHost);
        CHECK(c->Init());
        catchHost.month = 3;
        const int posts = catchHost.CountPosts(kWeatherMessageWeeklyPassDone);
        SendMessage(*c, kWeatherMessageCityStarted);
        CHECK(posts < catchHost.CountPosts(kWeatherMessageWeeklyPassDone));
        const int after = catchHost.CountPosts(kWeatherMessageWeeklyPassDone);
        SendMessage(*c, kWeatherMessageCityStarted);
        CHECK(catchHost.CountPosts(kWeatherMessageWeeklyPassDone) == after);
        Destroy(c);
    }

    // -----------------------------------------------------------------------------------------------
    // O. Shutdown removes the registration and the notification
    // -----------------------------------------------------------------------------------------------
    CHECK(sim->Shutdown());
    CHECK(!sim->IsInitialized());
    CHECK(host.unregisterCount == 1);
    CHECK(host.removed.size() == 1 && host.removed[0].type == kWeatherMessageCityStarted);

    CHECK(sim->Release() == 0);      // the last reference deletes the object

    if (failures == 0) std::printf("sc4_weathersimulator_test: all checks passed\n");
    else std::printf("sc4_weathersimulator_test: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
