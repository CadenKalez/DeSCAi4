#include "app/NativeJobs.h"

#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace native {
namespace {
std::unique_ptr<jobs::JobSystem> g_jobs;
std::once_flag g_once;
std::mutex g_swapMutex;
std::map<int, std::function<void()>> g_swaps;
int g_nextHandle = 1;
}  // namespace

jobs::JobSystem& Jobs() {
    std::call_once(g_once, [] {
        const unsigned cores = std::thread::hardware_concurrency();
        g_jobs.reset(new jobs::JobSystem(cores > 2 ? cores - 2 : 1));   // the main and render threads keep their cores
    });
    return *g_jobs;
}

jobs::PathResultQueue& PathResults() {
    static jobs::PathResultQueue queue;
    return queue;
}

int RegisterFrameSwap(std::function<void()> swap) {
    std::lock_guard<std::mutex> lock(g_swapMutex);
    const int handle = g_nextHandle++;
    g_swaps[handle] = std::move(swap);
    return handle;
}

void UnregisterFrameSwap(int handle) {
    std::lock_guard<std::mutex> lock(g_swapMutex);
    g_swaps.erase(handle);
}

void EndFrameJobs() {
    PathResults().Drain();
    std::vector<std::function<void()>> swaps;
    {
        std::lock_guard<std::mutex> lock(g_swapMutex);
        for (auto& entry : g_swaps) swaps.push_back(entry.second);
    }
    for (auto& swap : swaps) swap();
}

void ShutdownJobs() {
    if (g_jobs) g_jobs->WaitIdle();
    g_jobs.reset();
}

}  // namespace native
