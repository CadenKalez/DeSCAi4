// Two small window procedures:
//  * cSC4PanelUIWinProc (7 functions, 0x1003585ba..0x100358638): the toolbar's panel buttons forward to commands of the
//    command server (only while the 3D view exists): 0x26c10a3d -> 0x6a935e36, 0x26c10a3e -> 0x6a935e3c,
//    0x8a1da655 -> 0x6a935e4b, 0xca1da670 -> 0x6a935e45, 0xc98f4f48 -> 0x6a935e39; a forwarded button counts as handled.
//  * cSC4RegionSignpostAnimatorWinProc (6 functions, 0x10075a442): bobs the signpost child 0x4a630000 up and down with
//    |sin| over a 1500 ms cycle on every tick message (message 0xa2bf8acd).
#pragma once
#include <cstdint>
#include <functional>

#include "gz/WinProc.h"

namespace sc4 {

struct PanelServices {
    std::function<bool()> hasView3D;                              // SL::spView3D
    std::function<void(std::uint32_t command)> executeCommand;    // command server ExecuteCommand(id, null, null)
    std::function<std::uint32_t()> nowMs;                         // cRZTime::GetMillisecondSystemTime
};
PanelServices& GetPanelServices();

class cSC4PanelUIWinProc final : public gz::cIGZWinProc {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) override;
private:
    std::uint32_t refCount_ = 0;
};

constexpr std::uint32_t kMessageSignpostTick = 0xa2bf8acd;

class cSC4RegionSignpostAnimatorWinProc final : public gz::cIGZWinProc {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) override;
private:
    std::uint32_t refCount_ = 0;
    std::uint32_t startMs_ = 0;           // +0x14
    cIGZWin* signpost_ = nullptr;         // +0x18
};

}  // namespace sc4
