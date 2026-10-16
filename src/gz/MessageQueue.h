// cGZMessageQueue (0x1000595b0..0x10005a0f0, 32 functions) — a thread-safe deque of cGZMessage. Layout: vptr, a
// std::list<cGZMessage> at +8 (sentinel, size at +0x18), the recursive lock at +0x20, the "not empty" signal at
// +0x30 and the reference count at +0x40. Producers PushBack/PushFront and signal when the queue goes from empty to
// non-empty; consumers Get* (destructive) or Peek* (non-destructive) from the front, back or anywhere, with
// *Block variants that wait on the signal in one-second slices.
//
// Quirks kept from the binary: RemoveLastFound (both overloads) only takes and releases the lock and reports false,
// the "front" is the head of the list and the "back" the tail, and an empty queue reports message type 0.
#pragma once
#include <list>
#include <memory>

#include "gz/WinInterfaces.h"
#include "gz/cIGZCriticalSection.h"
#include "gz/cIGZUnknown.h"
#include "rz/Threading.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZMessageQueue = 0x298b2f5e;

inline bool operator==(const cGZMessage& a, const cGZMessage& b) {
    return a.type == b.type && a.param1 == b.param1 && a.param2 == b.param2 && a.param3 == b.param3;
}

class cIGZMessageQueue : public cIGZUnknown {
public:
    virtual void PushBack(const cGZMessage& message) = 0;                 // [3]
    virtual void PushFront(const cGZMessage& message) = 0;                // [4]
    virtual bool RemoveFirstFound(const cGZMessage& message) = 0;         // [5]
    virtual bool RemoveLastFound(const cGZMessage& message) = 0;          // [6]
    virtual int RemoveAllFound(const cGZMessage& message) = 0;            // [7]
    virtual bool RemoveFirstFound(std::uint32_t type) = 0;                // [8]
    virtual bool RemoveLastFound(std::uint32_t type) = 0;                 // [9]
    virtual int RemoveAllFound(std::uint32_t type) = 0;                   // [10]
    virtual std::uint32_t RemoveAll() = 0;                                // [11] number removed
    virtual std::uint32_t PeekFrontType() = 0;                            // [12]
    virtual std::uint32_t PeekBackType() = 0;                             // [13]
    virtual bool PeekAnywhereType(std::uint32_t type) = 0;                // [14]
    virtual const cGZMessage* PeekFrontMessage() = 0;                     // [15]
    virtual const cGZMessage* PeekBackMessage() = 0;                      // [16]
    virtual bool PeekAnywhereMessage(const cGZMessage& message) = 0;      // [17]
    virtual std::uint32_t GetFrontType() = 0;                             // [18]
    virtual std::uint32_t GetBackType() = 0;                              // [19]
    virtual bool GetAnywhereType(std::uint32_t type) = 0;                 // [20]
    virtual bool GetFrontMessage(cGZMessage& out) = 0;                    // [21]
    virtual bool GetBackMessage(cGZMessage& out) = 0;                     // [22]
    virtual bool GetAnywhereMessage(const cGZMessage& message) = 0;       // [23]
    virtual std::uint32_t GetFrontTypeBlock() = 0;                        // [24]
    virtual std::uint32_t GetBackTypeBlock() = 0;                         // [25]
    virtual bool GetFrontMessageBlock(cGZMessage& out) = 0;               // [26]
    virtual bool GetBackMessageBlock(cGZMessage& out) = 0;                // [27]
    virtual std::uint32_t GetMessageCount() = 0;                          // [28]
};

class cGZMessageQueue : public cIGZMessageQueue {
public:
    cGZMessageQueue();
    virtual ~cGZMessageQueue();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    void PushBack(const cGZMessage& message) override;
    void PushFront(const cGZMessage& message) override;
    bool RemoveFirstFound(const cGZMessage& message) override;
    bool RemoveLastFound(const cGZMessage& message) override;
    int RemoveAllFound(const cGZMessage& message) override;
    bool RemoveFirstFound(std::uint32_t type) override;
    bool RemoveLastFound(std::uint32_t type) override;
    int RemoveAllFound(std::uint32_t type) override;
    std::uint32_t RemoveAll() override;
    std::uint32_t PeekFrontType() override;
    std::uint32_t PeekBackType() override;
    bool PeekAnywhereType(std::uint32_t type) override;
    const cGZMessage* PeekFrontMessage() override;
    const cGZMessage* PeekBackMessage() override;
    bool PeekAnywhereMessage(const cGZMessage& message) override;
    std::uint32_t GetFrontType() override;
    std::uint32_t GetBackType() override;
    bool GetAnywhereType(std::uint32_t type) override;
    bool GetFrontMessage(cGZMessage& out) override;
    bool GetBackMessage(cGZMessage& out) override;
    bool GetAnywhereMessage(const cGZMessage& message) override;
    std::uint32_t GetFrontTypeBlock() override;
    std::uint32_t GetBackTypeBlock() override;
    bool GetFrontMessageBlock(cGZMessage& out) override;
    bool GetBackMessageBlock(cGZMessage& out) override;
    std::uint32_t GetMessageCount() override;

private:
    void Push(const cGZMessage& message, bool front);

    std::list<cGZMessage> messages_;                 // +8
    std::unique_ptr<cIGZCriticalSection> lock_;      // +0x20 (recursive)
    std::unique_ptr<cRZThreadSignal> signal_;        // +0x30
    std::uint32_t refCount_ = 0;                     // +0x40
};

}  // namespace gz
