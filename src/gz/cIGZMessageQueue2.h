// cIGZMessageQueue2 — a thread-safe queue of cIGZMessage2 (slots 3..22, +0x18..+0xb0, from
// cGZMessageQueue2, vtable 0x100a6ad70). Interface id 0xa52294cd.
#pragma once
#include <cstdint>

#include "gz/cIGZMessage2.h"

class cIGZMessageQueue2 : public cIGZUnknown {
public:
    virtual bool PushBack(cIGZMessage2* message) = 0;                             // [3]  +0x18
    virtual bool PushFront(cIGZMessage2* message) = 0;                            // [4]  +0x20
    virtual bool RemoveFirstFound(const cIGZMessage2& message) = 0;               // [5]  +0x28
    virtual bool RemoveLastFound(const cIGZMessage2& message) = 0;                // [6]  +0x30
    virtual std::int32_t RemoveAllFound(const cIGZMessage2& message) = 0;         // [7]  +0x38
    virtual bool RemoveFirstFoundType(std::uint32_t type) = 0;                    // [8]  +0x40
    virtual bool RemoveLastFoundType(std::uint32_t type) = 0;                     // [9]  +0x48
    virtual std::int32_t RemoveAllFoundType(std::uint32_t type) = 0;              // [10] +0x50
    virtual std::int32_t RemoveAll() = 0;                                         // [11] +0x58 (returns the count removed)
    virtual std::uint32_t PeekFrontType() = 0;                                    // [12] +0x60 (0 when empty)
    virtual std::uint32_t PeekBackType() = 0;                                     // [13] +0x68
    virtual bool PeekAnywhereType(std::uint32_t type) = 0;                        // [14] +0x70
    virtual bool PeekFrontMessage(cIGZMessage2** out) = 0;                        // [15] +0x78 (AddRef'd)
    virtual bool PeekBackMessage(cIGZMessage2** out) = 0;                         // [16] +0x80
    virtual bool PeekAnywhereMessage(const cIGZMessage2& match, cIGZMessage2** out) = 0;   // [17] +0x88
    virtual bool GetFrontMessage(cIGZMessage2** out) = 0;                         // [18] +0x90 (pops)
    virtual bool GetBackMessage(cIGZMessage2** out) = 0;                          // [19] +0x98
    virtual bool GetFrontMessageBlock(cIGZMessage2** out) = 0;                    // [20] +0xa0 (waits)
    virtual bool GetBackMessageBlock(cIGZMessage2** out) = 0;                     // [21] +0xa8
    virtual std::uint32_t GetMessageCount() = 0;                                  // [22] +0xb0
};
constexpr std::uint32_t kGZIID_cIGZMessageQueue2 = 0xa52294cd;
