#include "gz/WinCtrlFactory.h"

namespace gz {

namespace {
IWinCtrlFactory* g_factory = nullptr;
}

IWinCtrlFactory* WinCtrlFactory() { return g_factory; }
void SetWinCtrlFactory(IWinCtrlFactory* factory) { g_factory = factory; }

}  // namespace gz
