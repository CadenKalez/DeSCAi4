// cRZWinKeyAccelerator — the table of keyboard accelerators (18 functions at 0x100511a12..0x100511f70, iid 0xe2c1b3c4).
// A key is composed as (modifiers & 0xffffff8f) << 16 | virtual key | (key up ? 1 << 28 : 0); each entry holds the message
// to send when that key comes in. Registering over an existing key keeps the first entry. The Shift / Control / Alt keys
// themselves (virtual keys 0x10..0x12) and a modifier word of 0xffffffff never match. ProcessAccelerator sends the
// matched message through the (legacy) message server and answers true even when no server is available.
#pragma once
#include <cstdint>
#include <map>

#include "gz/WinInterfaces.h"
#include "gz/cGZMessageServer.h"
#include "rz/cRZSysServPtr.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinKeyAccelerator = 0xe2c1b3c4;

class cRZWinKeyAccelerator final : public cIGZWinKeyAccelerator {
public:
    cRZWinKeyAccelerator() = default;                                           // @ 0x100511a12
    ~cRZWinKeyAccelerator() = default;                                          // @ 0x100511a50

    bool QueryInterface(std::uint32_t iid, void** out) override;                // @ 0x100511a9a
    std::uint32_t AddRef() override { return ++refCount_; }                     // @ 0x100511abc
    std::uint32_t Release() override;                                           // @ 0x100511aca

    bool Init() override;                                                       // @ 0x100511b0e
    bool Shutdown() override;                                                   // @ 0x100511bac
    bool RegisterAccelerator(std::uint32_t key, std::uint32_t command) override;           // @ 0x100511be0
    bool RegisterAccelerator(std::uint32_t key, const cGZMessage& message) override;       // @ 0x100511c0e
    bool UnregisterAccelerator(std::uint32_t key) override;                     // @ 0x100511ce0
    bool IsAcceleratorPresent(std::uint32_t key, bool withModifiers, bool withDirection) override;   // @ 0x100511d28
    bool TestForAccelerator(const cGZMessage& keyMessage, std::uint32_t& command) override;   // @ 0x100511da4
    bool TestForAccelerator(const cGZMessage& keyMessage, cGZMessage& result) override;       // @ 0x100511de8
    bool TestForAccelerator(std::uint32_t key, std::uint32_t modifiers, bool keyDown, std::uint32_t& command) override;   // @ 0x100511e1e
    bool TestForAccelerator(std::uint32_t key, std::uint32_t modifiers, bool keyDown, cGZMessage& result) override;       // @ 0x100511e66
    int GetKeysForAccelerator(std::uint32_t command, std::uint32_t* keys, std::uint32_t& count) override;   // @ 0x100511eec
    bool ProcessAccelerator(const cGZMessage& keyMessage) override;             // @ 0x100511f70

private:
    std::uint32_t refCount_ = 0;                            // +8
    bool initialised_ = false;                              // +0xc
    std::map<std::uint32_t, cGZMessage> accelerators_;      // +0x10
    cIGZMessageServer* server_ = nullptr;                   // +0x28 (a reference)
};

}  // namespace gz
