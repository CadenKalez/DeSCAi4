// The control creation entry of the window control manager (RZGetFramework()->GetSystemService(0xc2c2eb0f,
// 0x22c2eb1f), cIGZWinCtrlMgr vslot 0xd0): composite controls such as the option group ask it for their children by
// class id, and it answers with an interface of that child (or false when no creator knows the class).
#pragma once
#include <cstdint>

namespace gz {

class IWinCtrlFactory {
public:
    virtual ~IWinCtrlFactory() = default;
    virtual bool CreateCtrl(std::uint32_t classId, std::uint32_t iid, void** out) = 0;
};
IWinCtrlFactory* WinCtrlFactory();
void SetWinCtrlFactory(IWinCtrlFactory* factory);

}  // namespace gz
