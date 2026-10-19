// cSC4App members whose decompilation has not been ported yet. Each aborts loudly instead of guessing; remove an
// entry here when its real implementation lands in App*.cpp. Nothing in this file is credited in the ledger.
#include <cstdio>
#include <cstdlib>

#include "sc4/App.h"

namespace {
[[noreturn]] void Pending(const char* name) {
    std::fprintf(stderr, "cSC4App::%s is not recovered yet\n", name);
    std::abort();
}
}  // namespace

bool cSC4App::DoMessage(cIGZWin*, cGZMessage&) { Pending("DoMessage(cIGZWin*, cGZMessage&)"); }
bool cSC4App::SaveCity(const cRZString&, bool) { Pending("SaveCity(path, bool)"); }
bool cSC4App::PostFrameWorkInit() { Pending("PostFrameWorkInit"); }
bool cSC4App::LoadRegistry() { Pending("LoadRegistry"); }
void cSC4App::SetupFontSet() { Pending("SetupFontSet"); }
bool cSC4App::DoCityObliteration() { Pending("DoCityObliteration"); }
void cSC4App::LaunchExceptionReportUploader() { Pending("LaunchExceptionReportUploader"); }
