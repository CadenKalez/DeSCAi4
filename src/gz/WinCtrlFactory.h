// The control creation entry of the window control manager (RZGetFramework()->GetSystemService(0xc2c2eb0f,
// 0x22c2eb1f), cIGZWinCtrlMgr vslot 0xd0): composite controls such as the option group ask it for their children by
// class id, and it answers with an interface of that child (or false when no creator knows the class).
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"
#include "gz/StreamOps.h"
#include "rz/cRZPoint.h"

class cIGZBuffer;

namespace gz {

class cIGZWinScrollbar;
class cIGZWinBtn;

class IWinCtrlFactory {
public:
    virtual ~IWinCtrlFactory() = default;
    virtual bool CreateCtrl(std::uint32_t classId, std::uint32_t iid, void** out) = 0;
    // The manager's other entries the composite controls use.
    virtual cIGZWinScrollbar* CreateScrollbar(std::uint32_t id, int direction, int low, int high, int value) = 0;   // vslot 0x88
    virtual std::uint32_t DefaultColor(int id) = 0;                                                                // vslot 0xf8: the native colour
    virtual std::uint32_t DoubleClickMilliseconds() = 0;
    // WCM vslot 0x48: a button of `style` (2 = the title bar's system buttons) showing `area` of `image`; null when unavailable.
    virtual cIGZWinBtn* CreateButton(std::uint32_t style, std::uint32_t id, const char* caption, cIGZBuffer* image, const cRZRect* area, std::uint32_t key) { (void)style; (void)id; (void)caption; (void)image; (void)area; (void)key; return nullptr; }
    // WCM vslot 0x38 / 0x28: a stock image / font looked up by id (borrowed pointers).
    virtual cIGZBuffer* Image(std::uint32_t id) { (void)id; return nullptr; }
    virtual cIGZUnknown* Font(std::uint32_t id) { (void)id; return nullptr; }                                                           // vslot 0x140
};
IWinCtrlFactory* WinCtrlFactory();
// RZ::WCM(): the window control manager service (service 0xc2c2eb0f), the entry the dialogs build their children through.
class cIGZWinCtrlMgr;
cIGZWinCtrlMgr* WinCtrlMgr();
void SetWinCtrlMgr(cIGZWinCtrlMgr* manager);
void SetWinCtrlFactory(IWinCtrlFactory* factory);

}  // namespace gz
