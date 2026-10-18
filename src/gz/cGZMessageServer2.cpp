#include "gz/cGZMessageServer2.h"

#include <algorithm>

#include "gz/GZCOM.h"
#include "rz/cRZMessage2Standard.h"

static IMessageServerFramework* g_framework = nullptr;
void SetMessageServerFramework(IMessageServerFramework* framework) { g_framework = framework; }
IMessageServerFramework* GetMessageServerFramework() { return g_framework; }

cGZMessageServer2::~cGZMessageServer2() {
    std::lock_guard<std::recursive_mutex> guard(notificationLock_);
    walking_ = 0;
    table_.clear();      // releases every listener reference
}

bool cGZMessageServer2::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZMessageServer2) {
        *out = static_cast<cIGZMessageServer2*>(this);
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

bool cGZMessageServer2::Init() {
    if (g_framework) g_framework->AddTickClient(this);
    return true;
}

bool cGZMessageServer2::Shutdown() {
    if (g_framework) g_framework->RemoveTickClient(this);
    return true;
}

// ---- sending ------------------------------------------------------------------------------------------

bool cGZMessageServer2::MessageSend(cIGZMessage2* message) {
    std::vector<cRZAutoRefCount<cIGZMessageTarget2>> targets;
    {
        std::lock_guard<std::recursive_mutex> guard(notificationLock_);
        DoDelayedNotificationAdditionsAndRemovalsImmediately();
        ++walking_;
        std::uint32_t type = message->GetType();
        for (;;) {
            auto found = table_.find(type);
            if (found != table_.end())
                for (const auto& target : found->second) targets.push_back(target);
            if (type == 0) break;
            type = 0;      // then the listeners of every message
        }
        --walking_;
    }
    for (auto& target : targets) target->DoMessage(message);
    return true;
}

void cGZMessageServer2::DoDelayedNotificationAdditionsAndRemovalsImmediately() {
    if (delayed_.empty()) return;
    for (const DelayedAddOrRemove& op : delayed_) {
        if (op.add) AddNow(op.target.get(), op.messageType);
        else RemoveNow(op.target.get(), op.messageType);
    }
    delayed_.clear();
}

void cGZMessageServer2::AddNow(cIGZMessageTarget2* target, std::uint32_t messageType) {
    TargetList& list = table_[messageType];
    for (const auto& existing : list)
        if (existing.get() == target) return;
    list.push_back(cRZAutoRefCount<cIGZMessageTarget2>(target));
}

void cGZMessageServer2::RemoveNow(cIGZMessageTarget2* target, std::uint32_t messageType) {
    auto found = table_.find(messageType);
    if (found == table_.end()) return;
    TargetList& list = found->second;
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->get() == target) {
            list.erase(it);
            if (list.empty()) table_.erase(found);
            return;
        }
    }
}

bool cGZMessageServer2::AddNotification(cIGZMessageTarget2* target, std::uint32_t messageType) {
    std::lock_guard<std::recursive_mutex> guard(notificationLock_);   // (the original defers the lock to the table walk)
    if (walking_ != 0) {
        delayed_.push_back({true, cRZAutoRefCount<cIGZMessageTarget2>(target), messageType});
        return true;
    }
    walking_ = 1;
    AddNow(target, messageType);
    --walking_;
    DoDelayedNotificationAdditionsAndRemovals();
    return true;
}

bool cGZMessageServer2::RemoveNotification(cIGZMessageTarget2* target, std::uint32_t messageType) {
    std::lock_guard<std::recursive_mutex> guard(notificationLock_);
    if (walking_ == 0) {
        walking_ = 1;
        RemoveNow(target, messageType);
        --walking_;
        DoDelayedNotificationAdditionsAndRemovals();
    } else {
        delayed_.push_back({false, cRZAutoRefCount<cIGZMessageTarget2>(target), messageType});
    }
    return true;
}

void cGZMessageServer2::DoDelayedNotificationAdditionsAndRemovals() {
    std::lock_guard<std::recursive_mutex> guard(notificationLock_);
    static bool alreadyHere = false;
    if (alreadyHere) return;
    alreadyHere = true;
    if (walking_ == 0) {
        while (!delayed_.empty()) {
            const DelayedAddOrRemove op = delayed_.front();
            if (op.add) AddNotification(op.target.get(), op.messageType);
            else RemoveNotification(op.target.get(), op.messageType);
            delayed_.pop_front();
        }
    }
    alreadyHere = false;
}

// ---- posting ------------------------------------------------------------------------------------------

bool cGZMessageServer2::MessagePost(cIGZMessage2* message, bool toFront) {
    std::lock_guard<std::recursive_mutex> guard(queueLock_);
    if (toFront) queue_.PushFront(message);
    else queue_.PushBack(message);
    return true;
}

bool cGZMessageServer2::GeneralMessagePostToTarget(cIGZMessage2* message, cIGZMessageTarget2* target) {
    std::lock_guard<std::recursive_mutex> guard(generalLock_);
    general_.push_back({cRZAutoRefCount<cIGZMessageTarget2>(target), cRZAutoRefCount<cIGZMessage2>(message)});
    return true;
}

bool cGZMessageServer2::CancelGeneralMessagePostsToTarget(cIGZMessageTarget2* target) {
    std::lock_guard<std::recursive_mutex> guard(generalLock_);
    general_.erase(std::remove_if(general_.begin(), general_.end(),
                                  [&](const GeneralMessageTargetInfo& info) { return info.target.get() == target; }),
                   general_.end());
    return true;
}

void cGZMessageServer2::SendGeneralMessages() {
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
            info.target->DoMessage(info.message.get());
            lock.lock();
            counter = generalCounter_;
        }
        remaining = counter - 1;
        generalCounter_ = remaining;
        if (!(0 < counter)) break;
    }
}

bool cGZMessageServer2::OnTick(std::uint32_t) {
    cIGZMessage2* message = nullptr;
    std::unique_lock<std::recursive_mutex> lock(queueLock_);
    const std::uint64_t before = delivered_;
    const std::uint64_t limit = queue_.GetMessageCount() + before;
    bool more = queue_.GetFrontMessage(&message);
    while (more) {
        ++delivered_;
        lock.unlock();
        MessageSend(message);
        message->Release();
        if (!alwaysClearQueue_ && limit <= delivered_) goto general;
        lock.lock();
        more = queue_.GetFrontMessage(&message);
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

std::uint32_t cGZMessageServer2::GetMessageQueueSize() {
    std::lock_guard<std::recursive_mutex> guard(queueLock_);
    return queue_.GetMessageCount();
}

bool cGZMessageServer2::CreateMessage(std::uint32_t type, std::uint32_t iid, void** out) {
    if (type != gzcom::kCLSID_cRZMessage2Standard) return false;
    auto* message = new cRZMessage2Standard();
    message->AddRef();
    const bool ok = message->QueryInterface(iid, out);
    message->Release();
    return ok;
}
