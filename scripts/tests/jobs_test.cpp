#include <atomic>
#include <cstdio>

#include "jobs/DoubleBuffer.h"
#include "jobs/JobSystem.h"
#include "jobs/PathfindingJob.h"

int main() {
    jobs::JobSystem system(4);
    std::atomic<int> counter{0};
    for (int i = 0; i < 1000; ++i) system.PushJob([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    system.WaitIdle();
    if (counter.load() != 1000) { std::printf("FAIL counter %d\n", counter.load()); return 1; }

    jobs::DoubleBuffer<int> grid(16, 16, 0);
    for (int frame = 1; frame <= 50; ++frame) {
        for (int row = 0; row < 16; ++row)
            system.PushJob([&grid, row, frame] { grid.Write([&](std::vector<int>& b) { for (int x = 0; x < 16; ++x) b[row * 16 + x] = frame; }); });
        system.WaitIdle();
        for (int v : grid.ReadAll()) if (v != frame - 1) { std::printf("FAIL read saw %d at frame %d\n", v, frame); return 1; }
        grid.Swap();
    }

    jobs::PathResultQueue results;
    int delivered = 0;
    for (int i = 0; i < 100; ++i) {
        jobs::PathRequest request{static_cast<std::uint64_t>(i), {0, 0}, {i, i}};
        jobs::SubmitPathJob(system, results, request,
            [](const jobs::PathRequest& r) { return std::vector<jobs::PathPoint>{r.start, r.end}; },
            [&delivered](jobs::PathResult&& r) { if (r.found && r.path.size() == 2) ++delivered; });
    }
    system.WaitIdle();
    results.Drain();
    if (delivered != 100) { std::printf("FAIL delivered %d\n", delivered); return 1; }
    std::printf("jobs: ok\n");
    return 0;
}
