// The city view's tool palettes on the native application (see NativeCatalogUi.cpp).
#pragma once

class cIGZWinMgr;
namespace sc4 { class cISC4WinCalloutBox; }

namespace native {

void InstallCatalogUi(cIGZWinMgr* manager);   // the frame and catalog hosts (replaces the neutral stubs)
sc4::cISC4WinCalloutBox* CreateCalloutBox();   // one reference

}  // namespace native
