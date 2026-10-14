// Message-server interfaces of the GZ framework: cIGZMessage2 / cIGZMessage2Standard (a typed
// message with up to four integer payloads, a string, an unknown and an extra serializable),
// cIGZMessageTarget2 (receiver) and cIGZMessageServer2 (the dispatcher, service 0x04fa845b,
// iid 0x652294c7). Slot numbers/offsets recovered from cRZMessage2Standard (vtable 0x100a68f80) and
// cGZMessageServer2 (vtable 0x100a6afa8).
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZString;
class cIGZSerializable;

constexpr std::uint32_t kGZIID_cIGZMessage2Standard = 0x65297976;   // cSC4AudioSoundManager::_HandleFocusChangeMessage
constexpr std::uint32_t kGZSID_MessageServer2 = 0x04fa845b;         // service id (cRZSysServPtr<cIGZMessageServer2,...>)
constexpr std::uint32_t kGZIID_cIGZMessageServer2 = 0x652294c7;

// Base message: only the type is common (slots 3..7).
class cIGZMessage2 : public cIGZUnknown {
public:
    virtual bool Create(std::uint32_t iid, void** out) const = 0;       // [3] +0x18 (clone)
    virtual std::uint32_t GetType() const = 0;                          // [4] +0x20
    virtual void SetType(std::uint32_t type) = 0;                       // [5] +0x28
    virtual bool IsEqual(const cIGZMessage2& other) const = 0;          // [6] +0x30 operator==
    virtual bool IsLess(const cIGZMessage2& other) const = 0;           // [7] +0x38 operator<
};

// The payload accessors (slots 8..36) live on the same object; the original reaches them through
// the cIGZMessage2Standard interface id above, so the port gives them their own interface.
class cIGZMessage2Standard : public cIGZMessage2 {
public:
    virtual bool GetHasData1() const = 0;                               // [8]  +0x40
    virtual std::uint64_t GetData1() const = 0;                         // [9]  +0x48
    virtual void* GetVoid1() const = 0;                                 // [10] +0x50
    virtual void SetData1(std::uint64_t value) = 0;                     // [11] +0x58
    virtual void SetVoid1(void* value) = 0;                             // [12] +0x60
    virtual bool GetHasData2() const = 0;                               // [13] +0x68
    virtual std::uint64_t GetData2() const = 0;                         // [14] +0x70
    virtual void* GetVoid2() const = 0;                                 // [15] +0x78
    virtual void SetData2(std::uint64_t value) = 0;                     // [16] +0x80
    virtual void SetVoid2(void* value) = 0;                             // [17] +0x88
    virtual bool GetHasData3() const = 0;                               // [18] +0x90
    virtual std::uint64_t GetData3() const = 0;                         // [19] +0x98
    virtual void* GetVoid3() const = 0;                                 // [20] +0xa0
    virtual void SetData3(std::uint64_t value) = 0;                     // [21] +0xa8
    virtual void SetVoid3(void* value) = 0;                             // [22] +0xb0
    virtual bool GetHasData4() const = 0;                               // [23] +0xb8
    virtual std::uint64_t GetData4() const = 0;                         // [24] +0xc0
    virtual void* GetVoid4() const = 0;                                 // [25] +0xc8
    virtual void SetData4(std::uint64_t value) = 0;                     // [26] +0xd0
    virtual void SetVoid4(void* value) = 0;                             // [27] +0xd8
    virtual bool GetHasString() const = 0;                              // [28] +0xe0
    virtual bool GetString(std::uint32_t iid, void** out) = 0;          // [29] +0xe8
    virtual void SetString(cIGZString* value) = 0;                      // [30] +0xf0
    virtual bool GetHasIGZUnknown() const = 0;                          // [31] +0xf8
    virtual cIGZUnknown* GetIGZUnknown() = 0;                           // [32] +0x100
    virtual void SetIGZUnknown(cIGZUnknown* value) = 0;                 // [33] +0x108
    virtual bool GetHasExtra() const = 0;                               // [34] +0x110
    virtual bool GetExtra(std::uint32_t iid, void** out) = 0;           // [35] +0x118
    virtual void SetExtra(cIGZSerializable* value) = 0;                 // [36] +0x120
};

class cIGZMessageTarget2 : public cIGZUnknown {
public:
    virtual bool DoMessage(cIGZMessage2* message) = 0;                  // [3] +0x18
};

class cIGZMessageServer2 : public cIGZUnknown {
public:
    virtual bool MessageSend(cIGZMessage2* message) = 0;                                    // [3]  +0x18
    virtual bool MessagePost(cIGZMessage2* message, bool arg) = 0;                          // [4]  +0x20
    virtual bool AddNotification(cIGZMessageTarget2* target, std::uint32_t messageType) = 0;      // [5]  +0x28
    virtual bool RemoveNotification(cIGZMessageTarget2* target, std::uint32_t messageType) = 0;   // [6]  +0x30
    virtual bool GeneralMessagePostToTarget(cIGZMessage2* message, cIGZMessageTarget2* target) = 0;  // [7] +0x38
    virtual bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2* target) = 0;         // [8]  +0x40
    virtual bool OnTick(std::uint32_t elapsedMs) = 0;                                       // [9]  +0x48
    virtual std::uint32_t GetMessageQueueSize() = 0;                                        // [10] +0x50
    virtual void SetAlwaysClearQueueOnTick(bool value) = 0;                                 // [11] +0x58
    virtual std::uint32_t GetRefCount() const = 0;                                          // [12] +0x60
    virtual bool CreateMessage(std::uint32_t type, std::uint32_t iid, void** out) = 0;      // [13] +0x68
};
