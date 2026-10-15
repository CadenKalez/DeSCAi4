#include <cstdio>

#include "sc4/SimGridOps.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;
namespace {
class City final : public ISimGridCity {
public:
    std::int32_t CitySizeXInCells() const override { return 8; }
    std::int32_t CitySizeZInCells() const override { return 8; }
    float CellSizeXMeters() const override { return 16.0f; }
    float CellSizeZMeters() const override { return 16.0f; }
};
void setValue(std::int16_t& d, std::int16_t v, void*) { d = v; }
void addCount(std::int16_t& d, float v, void*) { d = static_cast<std::int16_t>(d + static_cast<int>(v)); }
}  // namespace

int main() {
    City city;
    SetSimGridCity(&city);
    // equal tract size: cell by cell
    cSC4SimGrid<std::int16_t> a, b;
    a.SetTractSize(1);
    b.SetTractSize(1);
    for (int x = 0; x < 8; ++x)
        for (int z = 0; z < 8; ++z) b.SetTractValue(x, z, static_cast<std::int16_t>(x * 10 + z));
    ProcessTwoGridsWithCallback<std::int16_t, std::int16_t>(a, b, setValue, nullptr);
    CHECK(a.GetTractValue(3, 4) == 34 && a.GetTractValue(7, 7) == 77);
    // finer destination takes the coarse source value over the block
    cSC4SimGrid<std::int16_t> fine, coarse;
    fine.SetTractSize(1);
    coarse.SetTractSize(2);
    coarse.SetTractValue(1, 2, 9);
    ProcessTwoGridsWithCallback<std::int16_t, std::int16_t>(fine, coarse, setValue, nullptr);
    CHECK(fine.GetTractValue(2, 4) == 9 && fine.GetTractValue(3, 5) == 9 && fine.GetTractValue(1, 4) == 0);
    // coarser destination receives the mean of the block
    cSC4SimGrid<std::int16_t> out;
    out.SetTractSize(2);
    fine.SetTractValue(0, 0, 4); fine.SetTractValue(0, 1, 8); fine.SetTractValue(1, 0, 12); fine.SetTractValue(1, 1, 16);
    ProcessTwoGridsWithCallback<std::int16_t, std::int16_t>(out, fine, setValue, nullptr);
    CHECK(out.GetTractValue(0, 0) == 10);
    // float source: truncation in the finer branch, int accumulation in the coarser
    cSC4SimGrid<float> srcF;
    srcF.SetTractSize(2);
    srcF.SetTractValue(0, 0, 3.9f);
    ProcessTwoGridsWithCallback<std::int16_t, float>(fine, srcF, addCount, nullptr);
    CHECK(fine.GetTractValue(0, 0) == 4 + 3);
    // callbacks
    std::uint8_t d = 250;
    DesirabilityContext ctx{2.0f, nullptr};
    AddDesirability(d, 10, &ctx);
    CHECK(d == 255);
    d = 5;
    ctx.factor = -3.0f;
    AddDesirability(d, 10, &ctx);
    CHECK(d == 1);
    std::int16_t s = 10;
    cSC4ResponseCurve curve;
    const float pts[] = {0, 0, 10, 100};
    curve.SetResponseCurveData(pts, 4);
    AddCurveToShort<std::uint8_t>(s, 5, &curve);
    CHECK(s == 60);
    std::int8_t c = 100;
    AddCurveToSignedChar<std::int16_t>(c, 5, &curve);          // 100 + 50 -> clamp 127
    CHECK(c == 127);
    c = -128;
    AddCurveToSignedChar<std::int16_t>(c, 5, nullptr);
    CHECK(c == -128);
    c = 10;
    AddCurveToSignedChar<float>(c, 3.5f, nullptr);
    CHECK(c == 13);
    c = 0;
    AddTripLengthCurve(c, 0, &curve);                          // 0 stands for 255 -> curve end value 100
    CHECK(c == 100);
    float factor = 0.5f;
    s = 7;
    AssignWithFactor(s, 9, &factor);
    CHECK(s == 4);
    AddWithFactor(s, 9, &factor);
    CHECK(s == 8);
    s = 50;
    Divide(s, 200, nullptr);
    CHECK(s == 25);
    Divide(s, 0, nullptr);
    CHECK(s == 0);
    // zone-restricted variant
    cSC4SimGrid<std::int8_t> r;
    r.SetTractSize(1);
    cSC4SimGrid<std::uint8_t> srcU;
    srcU.SetTractSize(1);
    srcU.SetTractValue(2, 2, 7);
    srcU.SetTractValue(3, 3, 9);
    ProcessTwoGridsForROnly<std::uint8_t>(r, srcU,
        [](std::int8_t& dst, std::uint8_t v, void*) { dst = static_cast<std::int8_t>(v); }, nullptr,
        [](int x, int z) { return (x == 2 && z == 2) ? 1 : (x == 3 && z == 3) ? 5 : -1; });
    CHECK(r.GetTractValue(2, 2) == 7 && r.GetTractValue(3, 3) == 0);
    std::printf(failures ? "FAILED\n" : "sc4_simgridops: all passed\n");
    return failures ? 1 : 0;
}
