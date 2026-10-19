#include "gz/GZCOMDirector.h"

namespace {
cIGZFrameWork* g_framework = nullptr;
}

void SetRZFramework(cIGZFrameWork* framework) { g_framework = framework; }
cIGZFrameWork* RZGetFramework() { return g_framework; }
cIGZFrameWork* RZGetFrameWork() { return g_framework; }

cGZCOMDirector::cGZCOMDirector() {
    if (!GZCOM()) {
        if (cIGZFrameWork* framework = RZGetFramework()) com_ = framework->GetCOMObject();
    }
}

bool cGZCOMDirector::InitializeFrameWork() {
    framework_ = RZGetFramework();
    return framework_ != nullptr;
}

cRZCOMDllDirector* RZGetCOMDllDirector() {
    static cGZCOMDirector director;
    return &director;
}

cIGZCOM* GZCOM() { return RZGetCOMDllDirector()->GZCOM(); }
