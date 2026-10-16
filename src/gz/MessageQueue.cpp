#include "gz/MessageQueue.h"

#include "rz/cRZCriticalSection.h"

namespace gz {

namespace {
struct Guard {
    explicit Guard(cIGZCriticalSection* l) : lock(l) { lock->Lock(); }
    ~Guard() { lock->Unlock(); }
    cIGZCriticalSection* lock;
};
}  // namespace

cGZMessageQueue::cGZMessageQueue()   // @ 0x1000595b0
    : lock_(new cRZCriticalSection()), signal_(new cRZThreadSignal(0)) {}

cGZMessageQueue::~cGZMessageQueue() = default;   // @ 0x100059636

bool cGZMessageQueue::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1000596d2
    if (iid == 1 || iid == kGZIID_cIGZMessageQueue) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZMessageQueue::Release() {   // @ 0x100059702
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

std::uint32_t cGZMessageQueue::GetMessageCount() {   // @ 0x100059722
    Guard guard(lock_.get());
    return static_cast<std::uint32_t>(messages_.size());
}

void cGZMessageQueue::Push(const cGZMessage& message, bool front) {
    Guard guard(lock_.get());
    const bool wasEmpty = messages_.empty();
    if (front) messages_.push_front(message); else messages_.push_back(message);
    if (wasEmpty) signal_->Signal();
}

void cGZMessageQueue::PushBack(const cGZMessage& message) { Push(message, false); }    // @ 0x10005975a
void cGZMessageQueue::PushFront(const cGZMessage& message) { Push(message, true); }    // @ 0x100059810

bool cGZMessageQueue::RemoveFirstFound(const cGZMessage& message) {   // @ 0x1000598c8
    Guard guard(lock_.get());
    for (auto it = messages_.begin(); it != messages_.end(); ++it) {
        if (*it == message) { messages_.erase(it); return true; }
    }
    return false;
}

// @ 0x10005995c: takes and releases the lock and reports false (never implemented in the shipped binary)
bool cGZMessageQueue::RemoveLastFound(const cGZMessage&) { Guard guard(lock_.get()); return false; }

int cGZMessageQueue::RemoveAllFound(const cGZMessage& message) {   // @ 0x100059986
    Guard guard(lock_.get());
    int removed = 0;
    for (auto it = messages_.begin(); it != messages_.end();) {
        if (*it == message) { it = messages_.erase(it); ++removed; } else ++it;
    }
    return removed;
}

bool cGZMessageQueue::RemoveFirstFound(std::uint32_t type) {   // @ 0x100059a26
    Guard guard(lock_.get());
    for (auto it = messages_.begin(); it != messages_.end(); ++it) {
        if (it->type == type) { messages_.erase(it); return true; }
    }
    return false;
}

bool cGZMessageQueue::RemoveLastFound(std::uint32_t) { Guard guard(lock_.get()); return false; }   // @ 0x100059a98

int cGZMessageQueue::RemoveAllFound(std::uint32_t type) {   // @ 0x100059ac2
    Guard guard(lock_.get());
    int removed = 0;
    for (auto it = messages_.begin(); it != messages_.end();) {
        if (it->type == type) { it = messages_.erase(it); ++removed; } else ++it;
    }
    return removed;
}

std::uint32_t cGZMessageQueue::RemoveAll() {   // @ 0x100059b40
    Guard guard(lock_.get());
    const auto count = static_cast<std::uint32_t>(messages_.size());
    messages_.clear();
    return count;
}

std::uint32_t cGZMessageQueue::PeekFrontType() {   // @ 0x100059b80
    Guard guard(lock_.get());
    return messages_.empty() ? 0 : messages_.front().type;
}

std::uint32_t cGZMessageQueue::PeekBackType() {   // @ 0x100059bc6
    Guard guard(lock_.get());
    return messages_.empty() ? 0 : messages_.back().type;
}

bool cGZMessageQueue::PeekAnywhereType(std::uint32_t type) {   // @ 0x100059c0c
    Guard guard(lock_.get());
    for (const cGZMessage& m : messages_) if (m.type == type) return true;
    return false;
}

const cGZMessage* cGZMessageQueue::PeekFrontMessage() {   // @ 0x100059c60
    Guard guard(lock_.get());
    return messages_.empty() ? nullptr : &messages_.front();
}

const cGZMessage* cGZMessageQueue::PeekBackMessage() {   // @ 0x100059ca6
    Guard guard(lock_.get());
    return messages_.empty() ? nullptr : &messages_.back();
}

bool cGZMessageQueue::PeekAnywhereMessage(const cGZMessage& message) {   // @ 0x100059cec
    Guard guard(lock_.get());
    for (const cGZMessage& m : messages_) if (m == message) return true;
    return false;
}

bool cGZMessageQueue::GetAnywhereType(std::uint32_t type) { return RemoveFirstFound(type); }   // @ 0x100059d62

std::uint32_t cGZMessageQueue::GetFrontType() {   // @ 0x100059d70
    Guard guard(lock_.get());
    if (messages_.empty()) return 0;
    const std::uint32_t type = messages_.front().type;
    messages_.pop_front();
    return type;
}

std::uint32_t cGZMessageQueue::GetBackType() {   // @ 0x100059dd4
    Guard guard(lock_.get());
    if (messages_.empty()) return 0;
    const std::uint32_t type = messages_.back().type;
    messages_.pop_back();
    return type;
}

bool cGZMessageQueue::GetFrontMessage(cGZMessage& out) {   // @ 0x100059e38
    Guard guard(lock_.get());
    if (messages_.empty()) return false;
    out = messages_.front();
    messages_.pop_front();
    return true;
}

bool cGZMessageQueue::GetBackMessage(cGZMessage& out) {   // @ 0x100059ebe
    Guard guard(lock_.get());
    if (messages_.empty()) return false;
    out = messages_.back();
    messages_.pop_back();
    return true;
}

bool cGZMessageQueue::GetAnywhereMessage(const cGZMessage& message) { return RemoveFirstFound(message); }   // @ 0x100059f44

// The blocking getters poll once a second (TimedWait 1,000,000 ms) until a message arrives.
std::uint32_t cGZMessageQueue::GetFrontTypeBlock() {   // @ 0x100059fd8
    std::uint32_t type;
    while ((type = GetFrontType()) == 0) signal_->TimedWait(1000000);
    return type;
}

std::uint32_t cGZMessageQueue::GetBackTypeBlock() {   // @ 0x10005a016
    std::uint32_t type;
    while ((type = GetBackType()) == 0) signal_->TimedWait(1000000);
    return type;
}

bool cGZMessageQueue::GetFrontMessageBlock(cGZMessage& out) {   // @ 0x10005a054
    while (!GetFrontMessage(out)) signal_->TimedWait(1000000);
    return true;
}

bool cGZMessageQueue::GetBackMessageBlock(cGZMessage& out) {   // @ 0x10005a0a2
    while (!GetBackMessage(out)) signal_->TimedWait(1000000);
    return true;
}

}  // namespace gz
