// The control creation entry of the window control manager (RZGetFramework()->GetSystemService(0xc2c2eb0f,
// 0x22c2eb1f), cIGZWinCtrlMgr vslot 0xd0): composite controls such as the option group ask it for their children by
// class id, and it answers with an interface of that child (or false when no creator knows the class).
#pragma once
#include <cstdint>

namespace gz {

class cIGZWinScrollbar;

class IWinCtrlFactory {
public:
    virtual ~IWinCtrlFactory() = default;
    virtual bool CreateCtrl(std::uint32_t classId, std::uint32_t iid, void** out) = 0;
    // The manager's other entries the composite controls use.
    virtual cIGZWinScrollbar* CreateScrollbar(std::uint32_t id, int direction, int low, int high, int value) = 0;   // vslot 0x88
    virtual std::uint32_t DefaultColor(int id) = 0;                                                                // vslot 0xf8: the native colour
    virtual std::uint32_t DoubleClickMilliseconds() = 0;                                                           // vslot 0x140
};
IWinCtrlFactory* WinCtrlFactory();
void SetWinCtrlFactory(IWinCtrlFactory* factory);

}  // namespace gz
