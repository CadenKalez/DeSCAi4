// cGZMessageQueueManager2 — the service that owns the numbered message queues (service id 0x64fa8434, priority
// 5000000, interface 0x652294bc; ctor @ 0x1000531aa, vtable 0x100a6ae60, 0x60 bytes). Queues are kept in an ordered
// map id -> queue and every operation holds one recursive mutex. Queues are held by reference: AddQueue(id) makes a
// standard cGZMessageQueue2, AddQueue(id, queue) adopts a caller's queue (a null queue is stored as is). Ids are unique:
// adding an existing id fails and changes nothing.
#pragma once
#include <cstdint>
#include <map>
#include <mutex>

#include "gz/cGZMessageQueue2.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZSystemService.h"

constexpr std::uint32_t kGZSID_cGZMessageQueueManager2 = 0x64fa8434;
constexpr std::int32_t kGZMessageQueueManager2Priority = 5000000;
constexpr std::uint32_t kGZIID_cIGZMessageQueueManager2 = 0x652294bc;

class cIGZMessageQueueManager2 : public cIGZUnknown {
public:
    virtual bool GetQueue(std::uint32_t id, cIGZMessageQueue2** out) = 0;     // +0x18
    virtual bool AddQueue(std::uint32_t id) = 0;                              // +0x20
    virtual bool AddQueue(std::uint32_t id, cIGZMessageQueue2* queue) = 0;    // +0x28
    virtual bool RemoveQueue(std::uint32_t id) = 0;                           // +0x30
    virtual bool RemoveQueue(cIGZMessageQueue2* queue) = 0;                   // +0x38
    virtual bool RemoveAllQueues() = 0;                                       // +0x40
    virtual std::uint32_t GetQueueCount() = 0;                                // +0x48
};

class cGZMessageQueueManager2 final : public cIGZMessageQueueManager2, public cRZSystemService {
public:
    cGZMessageQueueManager2() : cRZSystemService(kGZSID_cGZMessageQueueManager2, kGZMessageQueueManager2Priority) {}
    ~cGZMessageQueueManager2() override = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;                       // @ 0x100053350
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }             // @ 0x1000533be
    std::uint32_t Release() override { return cRZSystemService::Release(); }           // @ 0x1000533e4
    bool Init() override { return true; }                                              // @ 0x100053330
    bool Shutdown() override { return true; }                                          // @ 0x100053340

    bool GetQueue(std::uint32_t id, cIGZMessageQueue2** out) override;                 // @ 0x10005340a
    bool AddQueue(std::uint32_t id) override;                                          // @ 0x1000534ae
    bool AddQueue(std::uint32_t id, cIGZMessageQueue2* queue) override;                // @ 0x10005367c
    bool RemoveQueue(std::uint32_t id) override;                                       // @ 0x100053842
    bool RemoveQueue(cIGZMessageQueue2* queue) override;                               // @ 0x1000538d4
    bool RemoveAllQueues() override;                                                   // @ 0x100053984
    std::uint32_t GetQueueCount() override { return static_cast<std::uint32_t>(queues_.size()); }   // @ 0x1000539d6

private:
    std::recursive_mutex lock_;                                       // +0x38
    std::map<std::uint32_t, cRZAutoRefCount<cIGZMessageQueue2>> queues_;   // +0x48
};
