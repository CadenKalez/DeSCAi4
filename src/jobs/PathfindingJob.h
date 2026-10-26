#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "jobs/JobSystem.h"

namespace jobs {

struct PathPoint {
    std::int32_t x;
    std::int32_t z;
};

struct PathRequest {
    std::uint64_t id;
    PathPoint start;
    PathPoint end;
};

struct PathResult {
    PathResult() : id(0), found(false) {}
    std::uint64_t id;
    bool found;
    std::vector<PathPoint> path;
};

using PathSolver = std::function<std::vector<PathPoint>(const PathRequest&)>;
using PathCallback = std::function<void(PathResult&&)>;

class PathResultQueue {
public:
    void Post(PathResult&& result, PathCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.emplace_back(std::move(result), std::move(callback));
    }

    // Main thread: runs the callbacks of every finished request.
    std::size_t Drain() {
        std::vector<std::pair<PathResult, PathCallback>> ready;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready.swap(pending_);
        }
        for (auto& entry : ready)
            if (entry.second) entry.second(std::move(entry.first));
        return ready.size();
    }

private:
    std::mutex mutex_;
    std::vector<std::pair<PathResult, PathCallback>> pending_;
};

// The request is copied by value into the job; the solver must read only that copy and data it owns or that is immutable.
inline bool SubmitPathJob(JobSystem& system, PathResultQueue& results, PathRequest request, PathSolver solver, PathCallback callback) {
    if (!solver) return false;
    PathResultQueue* queue = &results;
    return system.PushJob([queue, request, solver, callback]() {
        PathResult result;
        result.id = request.id;
        result.path = solver(request);
        result.found = !result.path.empty();
        queue->Post(std::move(result), std::move(callback));
    });
}

}  // namespace jobs
