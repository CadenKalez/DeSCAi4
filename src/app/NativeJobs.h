// The game's worker pool: one JobSystem sized to the machine, the path-result queue and the frame-end publish step.
// Simulation passes that become parallel register their double-buffered grids and submit jobs from here.
#pragma once
#include <functional>

#include "jobs/DoubleBuffer.h"
#include "jobs/JobSystem.h"
#include "jobs/PathfindingJob.h"

namespace native {

jobs::JobSystem& Jobs();
jobs::PathResultQueue& PathResults();
// `swap` runs on the main thread at the end of every frame (typically grid.Swap()). Returns a handle for UnregisterFrameSwap.
int RegisterFrameSwap(std::function<void()> swap);
void UnregisterFrameSwap(int handle);
// Main thread, once per frame: runs finished path callbacks, then the registered swaps.
void EndFrameJobs();
void ShutdownJobs();

}  // namespace native
