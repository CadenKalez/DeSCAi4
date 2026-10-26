#include "jobs/JobSystem.h"

#include <utility>

namespace jobs {

JobSystem::JobSystem(std::size_t threadCount) {
    if (threadCount == 0) threadCount = 1;
    workers_.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) workers_.emplace_back(&JobSystem::WorkerLoop, this);
}

JobSystem::~JobSystem() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (std::thread& worker : workers_) worker.join();
}

bool JobSystem::PushJob(std::function<void()> job) {
    if (!job) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return false;
        queue_.push(std::move(job));
    }
    wake_.notify_one();
    return true;
}

void JobSystem::WaitIdle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return queue_.empty() && active_ == 0; });
}

void JobSystem::WorkerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.pop();
            ++active_;
        }
        job();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_;
            if (queue_.empty() && active_ == 0) idle_.notify_all();
        }
    }
}

}  // namespace jobs
