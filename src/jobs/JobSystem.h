#pragma once
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace jobs {

class JobSystem {
public:
    explicit JobSystem(std::size_t threadCount);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    bool PushJob(std::function<void()> job);
    void WaitIdle();
    std::size_t ThreadCount() const { return workers_.size(); }

private:
    void WorkerLoop();

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::queue<std::function<void()>> queue_;
    std::size_t active_ = 0;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

}  // namespace jobs
