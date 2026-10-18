// cGZMessageServer2 — the game's message bus (24 functions, 0x100053a1a..0x1000556be, service id 0x04fa845b priority
// 5,000,000, iid 0x652294c7). Listeners register for a message type (0 = every type); MessageSend delivers at
// once, MessagePost queues the message and OnTick drains the queue; GeneralMessagePostToTarget queues a message for
// one specific target. Layout: +0x30 / +0x40 / +0x50 recursive locks (notification table / queue / general posts),
// +0x60 the queue, +0xa8 messages delivered so far, +0xb0 "always clear the queue on a tick" (default on), +0xb4
// the notification table is being walked, +0xb8 additions / removals deferred while it is walked, +0xd0 the table,
// +0x100 general posts (+0xf8 their delivery counter).
// MessageSend gathers the listeners of the type, then of type 0, under the lock and calls them after releasing it.
// A notification added or removed while the table is walked is deferred and applied when the walk ends.
#pragma once
#include <cstdint>
#include <deque>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "gz/cGZMessageQueue2.h"
#include "gz/cIGZMessage2.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZSystemService.h"

constexpr std::uint32_t kGZSID_cGZMessageServer2 = 0x04fa845b;
constexpr std::int32_t kGZMessageServer2Priority = 5000000;

// RZGetFramework() vslots 0x48 / 0x50: the message server asks to be ticked / not ticked any more.
class IMessageServerFramework {
public:
    virtual ~IMessageServerFramework() = default;
    virtual void AddTickClient(cIGZSystemService* service) = 0;
    virtual void RemoveTickClient(cIGZSystemService* service) = 0;
};
void SetMessageServerFramework(IMessageServerFramework* framework);
IMessageServerFramework* GetMessageServerFramework();

class cGZMessageServer2 final : public cIGZMessageServer2, public cRZSystemService {
public:
    cGZMessageServer2() : cRZSystemService(kGZSID_cGZMessageServer2, kGZMessageServer2Priority) {}
    ~cGZMessageServer2() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }
    std::uint32_t Release() override { return cRZSystemService::Release(); }
    bool Init() override;
    bool Shutdown() override;

    bool MessageSend(cIGZMessage2* message) override;
    bool MessagePost(cIGZMessage2* message, bool toFront) override;
    bool AddNotification(cIGZMessageTarget2* target, std::uint32_t messageType) override;
    bool RemoveNotification(cIGZMessageTarget2* target, std::uint32_t messageType) override;
    bool GeneralMessagePostToTarget(cIGZMessage2* message, cIGZMessageTarget2* target) override;
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2* target) override;
    bool OnTick(std::uint32_t elapsedMs) override;
    std::uint32_t GetMessageQueueSize() override;
    void SetAlwaysClearQueueOnTick(bool value) override { alwaysClearQueue_ = value; }
    std::uint32_t GetRefCount() const override { return const_cast<cGZMessageServer2*>(this)->cRZUnknown::RefCount(); }
    bool CreateMessage(std::uint32_t type, std::uint32_t iid, void** out) override;

    void DoDelayedNotificationAdditionsAndRemovals();
    void DoDelayedNotificationAdditionsAndRemovalsImmediately();
    void SendGeneralMessages();

private:
    struct DelayedAddOrRemove {
        bool add;
        cRZAutoRefCount<cIGZMessageTarget2> target;
        std::uint32_t messageType;
    };
    struct GeneralMessageTargetInfo {
        cRZAutoRefCount<cIGZMessageTarget2> target;
        cRZAutoRefCount<cIGZMessage2> message;
    };
    using TargetList = std::list<cRZAutoRefCount<cIGZMessageTarget2>>;

    void AddNow(cIGZMessageTarget2* target, std::uint32_t messageType);
    void RemoveNow(cIGZMessageTarget2* target, std::uint32_t messageType);

    std::recursive_mutex notificationLock_, queueLock_, generalLock_;
    cGZMessageQueue2 queue_;
    std::uint64_t delivered_ = 0;
    bool alwaysClearQueue_ = true;
    int walking_ = 0;
    std::list<DelayedAddOrRemove> delayed_;
    std::unordered_map<std::uint32_t, TargetList> table_;
    std::deque<GeneralMessageTargetInfo> general_;
    int generalCounter_ = 0;
};
