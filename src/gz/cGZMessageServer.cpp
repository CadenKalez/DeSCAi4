#include "gz/cGZMessageServer.h"

#include <algorithm>

cGZMessageServer::~cGZMessageServer() {
    std::lock_guard<std::recursive_mutex> guard(notificationLock_);
    walking_ = 0;
    for (auto& entry : table_)
        for (cIGZMessageTarget* target : entry.second) target->Release();
    table_.clear();
}

bool cGZMessageServer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZMessageServer) {
        *out = static_cast<cIGZMessageServer*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cIGZSystemService) {
        *out = static_cast<cIGZSystemService*>(this);
        AddRef();
        return true;
    }
    return cRZUnknown::QueryInterface(iid, out);
}

bool cGZMessageServer::Init() {
    if (GetMessageServerFramework()) GetMessageServerFramework()->AddTickClient(this);
    return true;
}

bool cGZMessageServer::Shutdown() {
    if (GetMessageServerFramework()) GetMessageServerFramework()->RemoveTickClient(this);
    return true;
}

// ---- sending ------------------------------------------------------------------------------------------

bool cGZMessageServer::MessageSend(const cGZMessage& message) {
    std::vector<cIGZMessageTarget*> targets;
    {
        std::lock_guard<std::recursive_mutex> guard(notificationLock_);
        DoDelayedNotificationAdditionsAndRemovalsImmediately();
        ++walking_;
        if (message.type != 0) {
            auto found = table_.find(message.type);
            if (found != table_.end()) {
                for (cIGZMessageTarget* target : found->second) {
                    target->AddRef();
                    targets.push_back(target);
                }
            }
        }
        --walking_;
    }
    cGZMessage copy = message;
    for (cIGZMessageTarget* target : targets) {
        target->DoMessage(copy);
        target->Release();
    }
    return true;
}

void cGZMessageServer::DoDelayedNotificationAdditionsAndRemovalsImmediately() {
    if (delayed_.empty()) return;
    for (const DelayedAddOrRemove& op : delayed_) {
        if (op.add) AddNow(op.target, op.messageType);
        else RemoveNow(op.target, op.messageType);
        op.target->Release();
    }
    delayed_.clear();
}

void cGZMessageServer::AddNow(cIGZMessageTarget* target, std::uint32_t messageType) {
    std::list<cIGZMessageTarget*>& list = table_[messageType];
    for (cIGZMessageTarget* existing : list)
        if (existing == target) return;
    list.push_back(target);
    target->AddRef();
}

void cGZMessageServer::RemoveNow(cIGZMessageTarget* target, std::uint32_t messageType) {
    auto found = table_.find(messageType);
    if (found == table_.end()) return;
    auto& list = found->second;
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (*it == target) {
            list.erase(it);
            target->Release();
            if (list.empty()) table_.erase(found);
            return;
        }
    }
}

bool cGZMessageServer::AddNotification(cIGZMessageTarget* target, std::uint32_t messageType) {
    std::lock_guard<std::recursive_mutex> guard(notificationLock_);
    if (walking_ != 0) {
        target->AddRef();
        delayed_.push_back({true, target, messageType});
        return true;
    }
    walking_ = 1;
    AddNow(target, messageType);
    --walking_;
    DoDelayedNotificationAdditionsAndRemovals();
    return true;
}

bool cGZMessageServer::RemoveNotification(cIGZMessageTarget* target, std::uint32_t messageType) {
    std::lock_guard<std::recursive_mutex> guard(notificationLock_);
    if (walking_ == 0) {
        walking_ = 1;
        RemoveNow(target, messageType);
        --walking_;
        DoDelayedNotificationAdditionsAndRemovals();
    } else {
        target->AddRef();
        delayed_.push_back({false, target, messageType});
    }
    return true;
}

void cGZMessageServer::DoDelayedNotificationAdditionsAndRemovals() {
    std::lock_guard<std::recursive_mutex> guard(notificationLock_);
    static bool alreadyHere = false;
    if (alreadyHere) return;
    alreadyHere = true;
    if (walking_ == 0) {
        while (!delayed_.empty()) {
            const DelayedAddOrRemove op = delayed_.front();
            if (op.add) AddNotification(op.target, op.messageType);
            else RemoveNotification(op.target, op.messageType);
            op.target->Release();
            delayed_.pop_front();
        }
    }
    alreadyHere = false;
}

// ---- posting ------------------------------------------------------------------------------------------

bool cGZMessageServer::MessagePost(const cGZMessage& message, bool toFront) {
    std::lock_guard<std::recursive_mutex> guard(queueLock_);
    if (toFront) queue_.PushFront(message);
    else queue_.PushBack(message);
    return true;
}

bool cGZMessageServer::GeneralMessagePostToTarget(const cGZMessage& message, cIGZMessageTarget* target) {
    std::lock_guard<std::recursive_mutex> guard(generalLock_);
    general_.push_back({target, message});
    return true;
}

bool cGZMessageServer::CancelGeneralMessagePostsToTarget(cIGZMessageTarget* target) {
    std::lock_guard<std::recursive_mutex> guard(generalLock_);
    general_.erase(std::remove_if(general_.begin(), general_.end(),
                                  [&](const GeneralMessageTargetInfo& info) { return info.target == target; }),
                   general_.end());
    return true;
}

void cGZMessageServer::SendGeneralMessages() {
    std::unique_lock<std::recursive_mutex> lock(generalLock_);
    int remaining;
    if (generalCounter_ < 1) {
        remaining = static_cast<int>(general_.size()) - 1;
        generalCounter_ = remaining;
        if (general_.empty()) return;
    } else {
        remaining = generalCounter_ - 1;
        generalCounter_ = remaining;
    }
    for (;;) {
        int counter = remaining;
        if (!general_.empty()) {
            GeneralMessageTargetInfo info = general_.front();
            general_.pop_front();
            lock.unlock();
            info.target->DoMessage(info.message);
            lock.lock();
            counter = generalCounter_;
        }
        remaining = counter - 1;
        generalCounter_ = remaining;
        if (!(0 < counter)) break;
    }
}

bool cGZMessageServer::OnTick(std::uint32_t) {
    cGZMessage message;
    std::unique_lock<std::recursive_mutex> lock(queueLock_);
    const std::uint64_t limit = queue_.GetMessageCount() + delivered_;
    bool more = queue_.GetFrontMessage(message);
    while (more) {
        ++delivered_;
        lock.unlock();
        MessageSend(message);
        if (!alwaysClearQueue_ && limit <= delivered_) goto general;
        lock.lock();
        more = queue_.GetFrontMessage(message);
    }
    lock.unlock();
general:
    std::size_t pending;
    {
        std::lock_guard<std::recursive_mutex> guard(generalLock_);
        pending = general_.size();
    }
    if (pending != 0) SendGeneralMessages();
    return true;
}

std::uint32_t cGZMessageServer::GetMessageQueueSize() {
    std::lock_guard<std::recursive_mutex> guard(queueLock_);
    return queue_.GetMessageCount();
}
