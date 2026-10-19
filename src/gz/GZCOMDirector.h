// cGZCOMDirector — the process-wide COM director (5 functions, 0x1000401f8..0x1000402be; director id 0xa3cad79a): the
// root every other director is added to (RZGetCOMDllDirector()). Its constructor adopts the framework's COM object when
// it has none; InitializeFrameWork binds the framework. Also the three tiny accessors the directors use.
#pragma once
#include "gz/cIGZFrameWork.h"
#include "rz/cRZCOMDllDirector.h"

class cGZCOMDirector final : public cRZCOMDllDirector {
public:
    cGZCOMDirector();                                                       // @ 0x100040202
    std::uint32_t GetDirectorID() const override { return 0xa3cad79a; }     // @ 0x100040284
    bool InitializeFrameWork();                                             // @ 0x1000402be
};

// @ 0x10002ab30 / 0x10002ab4b: the process-wide framework (null until cGZFramework::sSetFrameWork)
cIGZFrameWork* RZGetFramework();
cIGZFrameWork* RZGetFrameWork();
// @ 0x100040180: the function-local static director
cRZCOMDllDirector* RZGetCOMDllDirector();
// @ 0x10003f2d4: RZGetCOMDllDirector()->GZCOM()
cIGZCOM* GZCOM();
// The framework publishes itself here (set by cGZFramework::sSetFrameWork).
void SetRZFramework(cIGZFrameWork* framework);
