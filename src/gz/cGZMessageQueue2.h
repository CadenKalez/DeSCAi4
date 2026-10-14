// cGZMessageQueue2 — the standard message queue. Recovered from Sim City 4 Deluxe Editionsub 1.2.1
// (vtable 0x100a6ad70, 0x48-byte object, ctor @ 0x10005247e). Messages are held by reference;
// every operation takes the queue lock; blocking gets wait on a signal that PushBack/PushFront
// raise when the queue goes from empty to non-empty (the original polls it with a 1 s timed wait).
#pragma once
#include <condition_variable>
#include <list>
#include <mutex>

#include "gz/cIGZMessageQueue2.h"
#include "rz/cRZAutoRefCount.h"

class cGZMessageQueue2 final : public cIGZMessageQueue2 {
public:
    cGZMessageQueue2() = default;
    ~cGZMessageQueue2() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;   // ids 1, 0xa52294cd @ 0x1000525a8
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override;                              // destroys at count <= 1

    bool PushBack(cIGZMessage2* message) override;                 // @ 0x100052630
    bool PushFront(cIGZMessage2* message) override;                // @ 0x1000526e0
    bool RemoveFirstFound(const cIGZMessage2& message) override;   // @ 0x1000527f2 (uses operator==)
    bool RemoveLastFound(const cIGZMessage2& message) override;    // @ 0x1000528c0
    std::int32_t RemoveAllFound(const cIGZMessage2& message) override;   // @ 0x100052956
    bool RemoveFirstFoundType(std::uint32_t type) override;        // @ 0x1000529f2
    bool RemoveLastFoundType(std::uint32_t type) override;         // @ 0x100052a7c
    std::int32_t RemoveAllFoundType(std::uint32_t type) override;  // @ 0x100052b10
    std::int32_t RemoveAll() override;                             // @ 0x100052baa
    std::uint32_t PeekFrontType() override;                        // @ 0x100052bea
    std::uint32_t PeekBackType() override;                         // @ 0x100052c56
    bool PeekAnywhereType(std::uint32_t type) override;            // @ 0x100052cc2
    bool PeekFrontMessage(cIGZMessage2** out) override;            // @ 0x100052d3c
    bool PeekBackMessage(cIGZMessage2** out) override;             // @ 0x100052db4
    bool PeekAnywhereMessage(const cIGZMessage2& match, cIGZMessage2** out) override;   // @ 0x100052e2c
    bool GetFrontMessage(cIGZMessage2** out) override;             // @ 0x100052ec8
    bool GetBackMessage(cIGZMessage2** out) override;              // @ 0x100052f4c
    bool GetFrontMessageBlock(cIGZMessage2** out) override;        // @ 0x1000530bc
    bool GetBackMessageBlock(cIGZMessage2** out) override;         // @ 0x10005310a
    std::uint32_t GetMessageCount() override;                      // @ 0x1000525f8
    bool GetAnywhereMessage(const cIGZMessage2& match, cIGZMessage2** out);   // @ 0x100053010 (vtable slot 25)

private:
    std::uint32_t refCount_ = 0;                                   // +0x40
    std::list<cRZAutoRefCount<cIGZMessage2>> messages_;            // +8
    mutable std::recursive_mutex lock_;                            // +0x20
    std::condition_variable_any signal_;                           // +0x30
};
