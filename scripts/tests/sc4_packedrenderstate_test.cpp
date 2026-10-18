// cSC4PackedRenderState::PackRenderState bit layout, SC4RenderStateCache's driver calls, and the driver-mirror agreement
// check, over every literal render state the game creates (renderstates.inc) plus random sequences.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "sc4/PackedRenderState.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
class RecordingDriver final : public gd::cGDriverBase {
public:
    std::vector<std::string> calls;
    void ColorMask(bool e) override { calls.push_back("ColorMask(" + std::to_string(e) + ")"); cGDriverBase::ColorMask(e); }
    void DepthMask(bool e) override { calls.push_back("DepthMask(" + std::to_string(e) + ")"); cGDriverBase::DepthMask(e); }
    void DepthFunc(gd::eGDTestFunc f) override { calls.push_back("DepthFunc(" + std::to_string(f) + ")"); cGDriverBase::DepthFunc(f); }
    void BlendFunc(gd::eGDBlend s, gd::eGDBlend d) override { calls.push_back("BlendFunc(" + std::to_string(s) + "," + std::to_string(d) + ")"); cGDriverBase::BlendFunc(s, d); }
    void AlphaFunc(gd::eGDTestFunc f, float r) override { calls.push_back("AlphaFunc(" + std::to_string(f) + ")"); cGDriverBase::AlphaFunc(f, r); }
    void Enable(gd::eGDDriverState s) override { calls.push_back("Enable(" + std::to_string(s) + ")"); cGDriverBase::Enable(s); }
    void Disable(gd::eGDDriverState s) override { calls.push_back("Disable(" + std::to_string(s) + ")"); cGDriverBase::Disable(s); }
    void TexStage(std::uint32_t s) override { calls.push_back("TexStage(" + std::to_string(s) + ")"); cGDriverBase::TexStage(s); }
    void TexEnv(gd::eGDTextureEnvTarget t, gd::eGDTextureEnvParamType p, gd::eGDTextureEnvModeParam m) override { calls.push_back("TexEnv(" + std::to_string(m) + ")"); cGDriverBase::TexEnv(t, p, m); }
};

struct Tuple { bool c, dw, dt, at, bl; int tex; bool cull; int df, bs, bd, af; float ar; int e0, e1; };
const Tuple kCatalogue[] = {
#include "renderstates.inc"
};
cSC4PackedRenderState Pack(const Tuple& t) {
    cSC4PackedRenderState s;
    s.PackRenderState(t.c, t.dw, t.dt, t.at, t.bl, t.tex, t.cull, t.df, t.bs, t.bd, t.af, t.ar, t.e0, t.e1);
    return s;
}
}  // namespace

int main() {
    // layout of a fully specified state
    cSC4PackedRenderState full;
    full.PackRenderState(true, true, true, false, true, 1, false, 3, 4, 5, 4, 0.5f, 1, -1);
    CHECK(full.value == (0x1u | 0x2 | 0x4 | 0x10 | 0x20 | 3u << 7 | 4u << 10 | 4u << 13 | 5u << 17 | 8u << 21 | 1u << 25));
    CHECK(full.mask == 0x1fffffff);
    // "don't care" fields clear their mask bits only
    cSC4PackedRenderState loose;
    loose.PackRenderState(true, false, false, false, false, 0, false, -1, -1, -1, -1, -1.0f, -1, -1);
    CHECK(loose.value == 1u);
    CHECK(loose.mask == (0xfffffc7fu & 0xfffe1fff & 0xffe1ffff & 0xffffe3ff & 0xfe1fffff & 0xf1ffffff & 0x1fffffff));
    // two texture stages
    cSC4PackedRenderState two;
    two.PackRenderState(false, false, false, false, false, 2, false, -1, -1, -1, -1, -1.0f, -1, 1);
    CHECK((two.value & 0x10000020) == 0x10000020 && (two.value >> 29) == 1);
    // alpha ref n/15 rounds to nearest (ties to even)
    cSC4PackedRenderState ar;
    ar.PackRenderState(false, false, false, false, false, 0, false, -1, -1, -1, -1, 0.1f, -1, -1);
    CHECK((ar.value >> 21 & 0xf) == 2);   // 0.1 * 15 = 1.5 -> 2
    ar.PackRenderState(false, false, false, false, false, 0, false, -1, -1, -1, -1, 0.5f, -1, -1);
    CHECK((ar.value >> 21 & 0xf) == 8);   // 7.5 -> 8

    // diffing: only changed fields reach the driver
    {
        RecordingDriver driver;
        SC4RenderStateCache cache(&driver, SC4DeviceFlags{});
        cache.SetRenderState(full);
        const auto first = driver.calls;
        CHECK(!first.empty() && first.front() == "TexStage(0)");
        CHECK(std::find(first.begin(), first.end(), "Enable(5)") != first.end());
        CHECK(std::find(first.begin(), first.end(), "BlendFunc(4,5)") != first.end());
        CHECK(std::find(first.begin(), first.end(), "Disable(0)") == first.end());   // alpha test was already off: not sent
        driver.calls.clear();
        cache.SetRenderState(full);
        CHECK(driver.calls.empty());
        cSC4PackedRenderState noBlend;
        noBlend.PackRenderState(true, true, true, false, false, 1, false, 3, -1, -1, -1, -1.0f, 1, -1);
        cache.SetRenderState(noBlend);
        CHECK(driver.calls.size() == 2 && driver.calls[1] == "Disable(4)");
        // device flags: forced-off alpha test / blend are sent as disables
        RecordingDriver d2;
        SC4DeviceFlags flags;
        flags.forceNoAlphaTest = flags.forceNoTranslucency = true;
        SC4RenderStateCache c2(&d2, flags);
        cSC4PackedRenderState on;
        on.PackRenderState(true, true, true, true, true, 1, false, 3, 4, 5, 4, 0.5f, 1, -1);
        c2.SetRenderState(on);
        CHECK(!d2.Mirror().alphaTest && !d2.Mirror().blend);
        CHECK(FirstRenderStateMismatch(d2.Mirror(), c2.Word(), flags).empty());
    }

    // every state the game creates, applied in sequence and in random order, must leave driver and context in agreement
    {
        RecordingDriver driver;
        SC4DeviceFlags flags;
        SC4RenderStateCache cache(&driver, flags);
        std::mt19937 rng(1234);
        const std::size_t n = sizeof kCatalogue / sizeof kCatalogue[0];
        CHECK(n >= 30);
        for (int i = 0; i < 4000; ++i) {
            const cSC4PackedRenderState s = Pack(kCatalogue[rng() % n]);
            cache.SetRenderState(s);
            const std::string bad = FirstRenderStateMismatch(driver.Mirror(), cache.Word(), flags);
            if (!bad.empty()) { std::printf("mismatch %s after state %d\n", bad.c_str(), i); ++failures; break; }
        }
        // and with the second stage reported unavailable
        flags.secondTextureStage = false;
        RecordingDriver d3;
        SC4RenderStateCache c3(&d3, flags);
        for (int i = 0; i < 1000; ++i) {
            c3.SetRenderState(Pack(kCatalogue[rng() % n]));
            CHECK(FirstRenderStateMismatch(d3.Mirror(), c3.Word(), flags).empty());
        }
    }
    // unilateral default: storing ~default first forces a full re-send
    {
        RecordingDriver driver;
        SC4RenderStateCache cache(&driver, SC4DeviceFlags{});
        cSC4PackedRenderState def;
        def.PackRenderState(true, true, true, false, false, 0, false, 3, 4, 5, 1, 0.0f, 1, 0);
        cache.ForceWord(~def.value);
        cache.SetRenderState(def);
        CHECK(FirstRenderStateMismatch(driver.Mirror(), cache.Word(), SC4DeviceFlags{}).empty());
    }
    // value conversions of the shipped driver
    CHECK(gd::QuantizeUnitFloat(1.0f) == 255 && gd::QuantizeUnitFloat(0.5f) == 128 && gd::QuantizeUnitFloat(0.0f) == 0);
    CHECK(gd::PackClearColor(1, 0.5f, 0.25f, 1) == 0xffff8040u);
    CHECK(gd::AlphaRefByte(8.0f * 0.06666667f) == 137);
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("OK");
    return 0;
}
