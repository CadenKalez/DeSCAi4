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

bool cSC4App::DoMessage(cGZMessage&) { Pending("DoMessage(cGZMessage&)"); }
bool cSC4App::DoMessage(cIGZMessage2*) { Pending("DoMessage(cIGZMessage2*)"); }
bool cSC4App::DoMessage(cIGZWin*, cGZMessage&) { Pending("DoMessage(cIGZWin*, cGZMessage&)"); }
bool cSC4App::OnIdle(std::uint32_t) { Pending("OnIdle"); }
bool cSC4App::RequestNewCity(cRZAutoRefCount<cISC4RegionalCity>*) { Pending("RequestNewCity"); }
bool cSC4App::RequestLoadCity() { Pending("RequestLoadCity"); }
bool cSC4App::RequestCloseCity(bool) { Pending("RequestCloseCity"); }
bool cSC4App::RequestSaveCity(bool, bool) { Pending("RequestSaveCity"); }
bool cSC4App::RequestQuit(bool, bool) { Pending("RequestQuit"); }
bool cSC4App::RequestQuitFromRegion(bool) { Pending("RequestQuitFromRegion"); }
bool cSC4App::RequestGoToRegionView(bool) { Pending("RequestGoToRegionView"); }
bool cSC4App::LoadCity(const cRZString&, cRZAutoRefCount<cISC4RegionalCity>*) { Pending("LoadCity"); }
bool cSC4App::CloseCity() { Pending("CloseCity"); }
bool cSC4App::SaveCity(bool) { Pending("SaveCity(bool)"); }
bool cSC4App::SaveCity(const cRZString&, bool) { Pending("SaveCity(path, bool)"); }
bool cSC4App::AddDynamicLibraryByName(const cIGZString&, cRZString*, bool) { Pending("AddDynamicLibraryByName"); }
bool cSC4App::AddDynamicLibraryByPath(const cIGZString&, bool) { Pending("AddDynamicLibraryByPath"); }
bool cSC4App::Init() { Pending("Init"); }
bool cSC4App::Shutdown() { Pending("Shutdown"); }
bool cSC4App::PostFrameWorkInit() { Pending("PostFrameWorkInit"); }
bool cSC4App::PreFrameWorkShutdown() { Pending("PreFrameWorkShutdown"); }
bool cSC4App::LoadRegistry() { Pending("LoadRegistry"); }
bool cSC4App::OnTick(std::uint32_t) { Pending("OnTick"); }
