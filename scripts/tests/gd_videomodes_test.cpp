#include <array>
#include <cstdio>
#include <vector>

#include "render/gd/VideoModes.h"

using namespace gd;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Source final : IModeSource {
    std::vector<std::array<int, 3>> modes;
    int maxW = 9999, maxH = 9999;
    int calls = 0;
    int NumModes() override { ++calls; return static_cast<int>(modes.size()); }
    void NthMode(int n, int& w, int& h, int& d) override { w = modes[n][0]; h = modes[n][1]; d = modes[n][2]; }
    void WindowMaxSizes(int& w, int& h) override { w = maxW; h = maxH; }
};
}  // namespace

int main() {
    Source src;
    src.modes = {{800, 600, 32}, {1600, 1200, 32}};
    src.maxW = 1280; src.maxH = 800;
    cVideoModeList list(&src);
    CHECK(list.CountVideoModes() == 6);
    const auto& m = list.Modes();
    // 800x600 fits the window limit: full-screen entry then windowed entry with the next id
    CHECK(m[0].id == 0 && m[0].width == 800 && m[0].height == 600 && m[0].bitsPerPixel == 32 && m[0].flags[0] == kModeFullScreen);
    CHECK(m[1].id == 1 && m[1].width == 800 && m[1].flags[0] == kModeWindowed);
    // 1600x1200 does not fit: the full-screen entry is repeated with the same id
    CHECK(m[2].id == 2 && m[2].flags[0] == kModeFullScreen && m[3].id == 2 && m[3].flags[0] == kModeFullScreen && m[3].width == 1600);
    // stock size that no mode provides and that fits: 1024x768 only (1280 is not < 1280)
    CHECK(m[4].id == 4 && m[4].width == 1024 && m[4].height == 768 && m[4].bitsPerPixel == 32 && m[4].flags[0] == kModeWindowed);
    // the maximum window size itself comes last
    CHECK(m[5].id == 5 && m[5].width == 1280 && m[5].height == 800 && m[5].flags[0] == kModeWindowed);
    CHECK(m[0].flags[1] == 0 && m[0].reserved30 == 0);
    // built once
    src.modes.push_back({1024, 768, 32});
    const int before = src.calls;
    CHECK(list.CountVideoModes() == 6 && src.calls == before);
    sGDMode out{};
    CHECK(list.GetVideoModeInfo(5, out) && out.width == 1280);
    CHECK(!list.GetVideoModeInfo(6, out));
    // unbounded window: no windowed repeats of stock sizes beyond those modes, no trailing max-size entry
    Source open;
    open.modes = {{1024, 768, 32}};
    cVideoModeList l2(&open);
    const int n = l2.CountVideoModes();
    // 1024x768 + windowed twin, then 11 uncovered stock sizes (all < 9999)
    CHECK(n == 2 + 11);
    CHECK(l2.Modes()[1].flags[0] == kModeWindowed && l2.Modes()[2].width == 800);
    CHECK(l2.Modes().back().width == 1680 && l2.Modes().back().height == 1050);
    // empty display list
    Source none;
    none.maxW = 9999;
    cVideoModeList l3(&none);
    CHECK(l3.CountVideoModes() == 12);   // just the stock sizes (the list stays "empty" only if there are no entries at all)
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("OK");
    return 0;
}
