// cIGZWinProc — the window procedure interface the UI scripts attach to a window (vtable: QueryInterface /
// AddRef / Release, [3] DoWinProcMessage, [4] DoWinMsg with an overridable default, then the destructors).
// Iid 0x22e85d8e. The window sends 0xa2bf8ad5 once when the proc is installed and 0xa2bf8ad6 when it is removed;
// command messages (type 3) carry the sender class 0x287259f6 in word 1 and the control id in word 2.
#pragma once
#include <cstdint>

#include "gz/WinInterfaces.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinProc = 0x22e85d8e;
constexpr std::uint32_t kWinProcInit = 0xa2bf8ad5, kWinProcShutdown = 0xa2bf8ad6;
constexpr std::uint64_t kWinCommandSender = 0x287259f6;

class cIGZWinProc : public cIGZUnknown {
public:
    virtual bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) = 0;                      // [3]
    virtual bool DoWinMsg(cIGZWin*, std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t) { return false; }   // [4]
};

}  // namespace gz
