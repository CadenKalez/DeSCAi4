// cGZMessageServer — the original message bus that carries cGZMessage records by value (22 functions,
// 0x10005a0fe..0x10005bcdc, service id 0x64063387 priority 5,000,000, interface iid 0xc1085722 at +0x28). It is the
// predecessor of cGZMessageServer2 and works the same way, with these differences: listeners are plain target
// pointers (the server takes a reference), message type 0 is never delivered and a type-0 listener hears
// nothing, and the walk/defer flag is +0xb0 while "always clear" is +0xb4.
#pragma once
#include <cstdint>
#include <deque>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "gz/MessageQueue.h"
#include "gz/WinInterfaces.h"
#include "gz/cGZMessageServer2.h"
#include "rz/cRZSystemService.h"

constexpr std::uint32_t kGZSID_cGZMessageServer = 0x64063387;
constexpr std::uint32_t kGZIID_cIGZMessageServer = 0xc1085722;

class cIGZMessageTarget;
using cGZMessageTargetLegacy = cIGZMessageTarget;

class cIGZMessageTarget : public cIGZUnknown {
public:
    virtual bool DoMessage(cGZMessage& message) = 0;
};

class cIGZMessageServer : public cIGZUnknown {
public:
    virtual bool MessageSend(const cGZMessage& message) = 0;                                  // [3]
    virtual bool MessagePost(const cGZMessage& message, bool toFront) = 0;                    // [4]
    virtual bool AddNotification(cIGZMessageTarget* target, std::uint32_t messageType) = 0;   // [5]
    virtual bool RemoveNotification(cIGZMessageTarget* target, std::uint32_t messageType) = 0;   // [6]
    virtual bool GeneralMessagePostToTarget(const cGZMessage& message, cIGZMessageTarget* target) = 0;   // [7]
    virtual bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget* target) = 0;            // [8]
    virtual bool OnTick(std::uint32_t elapsedMs) = 0;                                         // [9]
    virtual std::uint32_t GetMessageQueueSize() = 0;                                          // [10]
    virtual void SetAlwaysClearQueueOnTick(bool value) = 0;                                   // [11]
};

class cGZMessageServer final : public cRZSystemService, public cIGZMessageServer {
public:
    cGZMessageServer() : cRZSystemService(kGZSID_cGZMessageServer, 5000000) {}
    ~cGZMessageServer() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }
    std::uint32_t Release() override { return cRZSystemService::Release(); }
    bool Init() override;
    bool Shutdown() override;

    bool MessageSend(const cGZMessage& message) override;
    bool MessagePost(const cGZMessage& message, bool toFront) override;
    bool AddNotification(cIGZMessageTarget* target, std::uint32_t messageType) override;
    bool RemoveNotification(cIGZMessageTarget* target, std::uint32_t messageType) override;
    bool GeneralMessagePostToTarget(const cGZMessage& message, cIGZMessageTarget* target) override;
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget* target) override;
    bool OnTick(std::uint32_t elapsedMs) override;
    std::uint32_t GetMessageQueueSize() override;
    void SetAlwaysClearQueueOnTick(bool value) override { alwaysClearQueue_ = value; }

    void DoDelayedNotificationAdditionsAndRemovals();
    void DoDelayedNotificationAdditionsAndRemovalsImmediately();
    void SendGeneralMessages();

private:
    struct DelayedAddOrRemove {
        bool add;
        cIGZMessageTarget* target;     // holds a reference
        std::uint32_t messageType;
    };
    struct GeneralMessageTargetInfo {
        cIGZMessageTarget* target;
        cGZMessage message;
    };
    void AddNow(cIGZMessageTarget* target, std::uint32_t messageType);
    void RemoveNow(cIGZMessageTarget* target, std::uint32_t messageType);

    std::recursive_mutex notificationLock_, queueLock_, generalLock_;
    gz::cGZMessageQueue queue_;
    std::uint64_t delivered_ = 0;
    int walking_ = 0;
    bool alwaysClearQueue_ = true;
    std::list<DelayedAddOrRemove> delayed_;
    std::unordered_map<std::uint32_t, std::list<cIGZMessageTarget*>> table_;
    std::deque<GeneralMessageTargetInfo> general_;
    int generalCounter_ = 0;
};
