// The city view's panel window procedures on the native application: the recovered cSC4MySimWinProc and
// cSC4GraphsViewWinProc behind the interfaces cSC4View3DWin::AddGUI keeps them as, plus the services they reach for.
#pragma once

#include <cstdint>

class cIGZWin;
namespace gz { class cIGZWinGen; }
namespace sc4 { class IMySimWindow; class IGraphsViewWindow; }

namespace native {

sc4::IMySimWindow* CreateMySimWindow(gz::cIGZWinGen* panel);       // one reference
sc4::IGraphsViewWindow* CreateGraphsViewWindow(cIGZWin* window);   // one reference

}  // namespace native
