#include "gz/cGZMessageQueue2.h"

#include <chrono>

bool cGZMessageQueue2::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZMessageQueue2) {
        *out = static_cast<cIGZMessageQueue2*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZMessageQueue2::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

std::uint32_t cGZMessageQueue2::GetMessageCount() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return static_cast<std::uint32_t>(messages_.size());
}

bool cGZMessageQueue2::PushBack(cIGZMessage2* message) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const bool wasEmpty = messages_.empty();
    messages_.push_back(cRZAutoRefCount<cIGZMessage2>(message));
    if (wasEmpty) signal_.notify_all();
    return true;
}

bool cGZMessageQueue2::PushFront(cIGZMessage2* message) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const bool wasEmpty = messages_.empty();
    messages_.push_front(cRZAutoRefCount<cIGZMessage2>(message));
    if (wasEmpty) signal_.notify_all();
    return true;
}

bool cGZMessageQueue2::RemoveFirstFound(const cIGZMessage2& message) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto it = messages_.begin(); it != messages_.end(); ++it) {
        if ((*it)->IsEqual(message)) {
            messages_.erase(it);
            return true;
        }
    }
    return false;
}

bool cGZMessageQueue2::RemoveLastFound(const cIGZMessage2& message) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto it = messages_.end(); it != messages_.begin();) {
        --it;
        if ((*it)->IsEqual(message)) {
            messages_.erase(it);
            return true;
        }
    }
    return false;
}

std::int32_t cGZMessageQueue2::RemoveAllFound(const cIGZMessage2& message) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    std::int32_t removed = 0;
    for (auto it = messages_.begin(); it != messages_.end();) {
        if ((*it)->IsEqual(message)) {
            it = messages_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

bool cGZMessageQueue2::RemoveFirstFoundType(std::uint32_t type) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto it = messages_.begin(); it != messages_.end(); ++it) {
        if ((*it)->GetType() == type) {
            messages_.erase(it);
            return true;
        }
    }
    return false;
}

bool cGZMessageQueue2::RemoveLastFoundType(std::uint32_t type) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto it = messages_.end(); it != messages_.begin();) {
        --it;
        if ((*it)->GetType() == type) {
            messages_.erase(it);
            return true;
        }
    }
    return false;
}

std::int32_t cGZMessageQueue2::RemoveAllFoundType(std::uint32_t type) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    std::int32_t removed = 0;
    for (auto it = messages_.begin(); it != messages_.end();) {
        if ((*it)->GetType() == type) {
            it = messages_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

std::int32_t cGZMessageQueue2::RemoveAll() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const auto count = static_cast<std::int32_t>(messages_.size());
    messages_.clear();
    return count;
}

std::uint32_t cGZMessageQueue2::PeekFrontType() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return messages_.empty() ? 0u : messages_.front()->GetType();
}

std::uint32_t cGZMessageQueue2::PeekBackType() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return messages_.empty() ? 0u : messages_.back()->GetType();
}

bool cGZMessageQueue2::PeekAnywhereType(std::uint32_t type) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto& message : messages_) {
        if (message->GetType() == type) return true;
    }
    return false;
}

bool cGZMessageQueue2::PeekFrontMessage(cIGZMessage2** out) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (messages_.empty()) {
        *out = nullptr;
        return false;
    }
    *out = messages_.front().get();
    (*out)->AddRef();
    return true;
}

bool cGZMessageQueue2::PeekBackMessage(cIGZMessage2** out) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (messages_.empty()) {
        *out = nullptr;
        return false;
    }
    *out = messages_.back().get();
    (*out)->AddRef();
    return true;
}

bool cGZMessageQueue2::PeekAnywhereMessage(const cIGZMessage2& match, cIGZMessage2** out) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto& message : messages_) {
        if (message->IsEqual(match)) {
            *out = message.get();
            (*out)->AddRef();
            return true;
        }
    }
    *out = nullptr;
    return false;
}

bool cGZMessageQueue2::GetFrontMessage(cIGZMessage2** out) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (messages_.empty()) {
        *out = nullptr;
        return false;
    }
    *out = messages_.front().get();
    (*out)->AddRef();
    messages_.pop_front();
    return true;
}

bool cGZMessageQueue2::GetBackMessage(cIGZMessage2** out) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (messages_.empty()) {
        *out = nullptr;
        return false;
    }
    *out = messages_.back().get();
    (*out)->AddRef();
    messages_.pop_back();
    return true;
}

bool cGZMessageQueue2::GetAnywhereMessage(const cIGZMessage2& match, cIGZMessage2** out) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto it = messages_.begin(); it != messages_.end(); ++it) {
        if ((*it)->IsEqual(match)) {
            *out = it->get();
            (*out)->AddRef();
            messages_.erase(it);
            return true;
        }
    }
    *out = nullptr;
    return false;
}

// Blocks until a message is available (the original wakes at least once a second).
bool cGZMessageQueue2::GetFrontMessageBlock(cIGZMessage2** out) {
    while (!GetFrontMessage(out)) {
        std::unique_lock<std::recursive_mutex> guard(lock_);
        if (messages_.empty()) signal_.wait_for(guard, std::chrono::seconds(1));
    }
    return true;
}

bool cGZMessageQueue2::GetBackMessageBlock(cIGZMessage2** out) {
    while (!GetBackMessage(out)) {
        std::unique_lock<std::recursive_mutex> guard(lock_);
        if (messages_.empty()) signal_.wait_for(guard, std::chrono::seconds(1));
    }
    return true;
}
