// RZ:: — the framework-service accessors (13 functions 0x1000278a0.. plus ADir / DDir; 0x1001278a0..0x100128020). Each asks
// the framework for one service by (service id, interface id), keeps the answer in a function-local static pointer
// and returns it from then on. The pointer is bare: the reference taken by the lookup is dropped at once, and a
// service that disappears later stays cached (kept; ResetCaches is for tests and shutdown).
#pragma once
#include "gz/WinCtrlMgr.h"
#include "gz/WinInterfaces.h"
#include "gz/cGZMessageServer.h"
#include "gz/cGZMessageServer2.h"
#include "gz/cIGZFileSystem.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/LanguageManager.h"

namespace RZ {

cIGZFileSystem* FS();           // @ 0x1001279a6  file system     (service 0x2073215d, iid 0xe0203660)
cIGZMessageServer* MS();        // @ 0x100127b18  message server  (0x64063387, 0xc1085722)
cIGZMessageServer2* MS2();      // @ 0x100127b84  message server 2 (0x04fa845b, 0x652294c7)
cIGZUnknown* TS();              // @ 0x100127bf0  (0x8bf5e368, 0x8bf5e36d)
cIGZUnknown* RDD();             // @ 0x100127c5c  realtime debug display (0x8414a2c2, 0x6054a2fb)
cIGZUnknown* GS();              // @ 0x100127cc8  graphic system  (0xc416025c, 0x0073283c)
cIGZWinMgr* WM();               // @ 0x100127d34  window manager  (0xa417445e, 0x000005a4)
gz::cIGZWinCtrlMgr* WCM();     // @ 0x100127da0  window control manager (0xc2c2eb0f, 0x22c2eb1f)
cIGZUnknown* SS();              // @ 0x100127e0c  sound system    (0x23cecba0, 0x106077d2)
cIGZUnknown* SM();              // @ 0x100127e78  sound manager   (0x1af85bfb, 0x3af85bb7)
cIGZUnknown* HM();              // @ 0x100127ee4  (0x1b2ce671, 0xbb1bd9a8)
cIGZUnknown* FO();              // @ 0x100127f50  font system     (0x20673960, 0x00673960)
cIGZLanguageManager* LM();      // @ 0x100127fbc  language manager (0x441e5070, 0x0054b7d5)

// The application / data directory (the file system service's answer), assigned to `out`.
void ADir(cIGZString& out);     // @ 0x1001278a0
void DDir(cIGZString& out);     // @ 0x100127a12

void ResetCaches();

}  // namespace RZ
