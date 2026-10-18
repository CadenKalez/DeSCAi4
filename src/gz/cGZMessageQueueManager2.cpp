#include "gz/cGZMessageQueueManager2.h"

bool cGZMessageQueueManager2::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZMessageQueueManager2) {
        *out = static_cast<cIGZMessageQueueManager2*>(this);
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

bool cGZMessageQueueManager2::GetQueue(std::uint32_t id, cIGZMessageQueue2** out) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto it = queues_.find(id);
    if (it == queues_.end()) {
        *out = nullptr;
        return false;
    }
    *out = it->second.get();
    (*out)->AddRef();
    return *out != nullptr;
}

bool cGZMessageQueueManager2::AddQueue(std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (queues_.find(id) != queues_.end()) return false;
    queues_.emplace(id, cRZAutoRefCount<cIGZMessageQueue2>(new cGZMessageQueue2()));
    return true;
}

bool cGZMessageQueueManager2::AddQueue(std::uint32_t id, cIGZMessageQueue2* queue) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (queues_.find(id) != queues_.end()) return false;
    queues_.emplace(id, cRZAutoRefCount<cIGZMessageQueue2>(queue));
    return true;
}

bool cGZMessageQueueManager2::RemoveQueue(std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto it = queues_.find(id);
    if (it == queues_.end()) return false;
    queues_.erase(it);
    return true;
}

bool cGZMessageQueueManager2::RemoveQueue(cIGZMessageQueue2* queue) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto it = queues_.begin(); it != queues_.end(); ++it) {
        if (it->second.get() == queue) {
            queues_.erase(it);
            return true;
        }
    }
    return false;
}

bool cGZMessageQueueManager2::RemoveAllQueues() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    queues_.clear();
    return true;
}
