#include "gz/WinCtrlFactory.h"

namespace gz {

namespace {
IWinCtrlFactory* g_factory = nullptr;
cIGZWinCtrlMgr* g_manager = nullptr;
}

IWinCtrlFactory* WinCtrlFactory() { return g_factory; }
void SetWinCtrlFactory(IWinCtrlFactory* factory) { g_factory = factory; }
cIGZWinCtrlMgr* WinCtrlMgr() { return g_manager; }
void SetWinCtrlMgr(cIGZWinCtrlMgr* manager) { g_manager = manager; }

}  // namespace gz
