// cIGZFrameWork — the framework interface (vtable group 0 of cGZFramework @ 0x100a67ed0, slots 3..39; slots 40+ are
// cGZFramework's own) and the second interface at +8 (iid 0x23ea70a1: AsIGZFrameWork / window handles, group 1 @
// 0x100a68090). Directors, the COM registry and the application reach the framework through these.
#pragma once
#include <cstdint>

#include "gz/cIGZSystemService.h"
#include "gz/cIGZUnknown.h"
#include "gz/cIGZUnknownList.h"

class cIGZApp;
class cIGZCmdLine;
class cIGZCOM;
class cIGZDebugStream;
class cIGZFrameWorkHooks;
class cIGZOStream;
class cIGZIStream;

constexpr std::uint32_t kGZIID_cIGZFrameWork = 0x65;
constexpr std::uint32_t kGZIID_cIGZFrameWorkWindow = 0x23ea70a1;

class cIGZFrameWork : public cIGZUnknown {
public:
    virtual bool AddSystemService(cIGZSystemService* service) = 0;                                  // [3]
    virtual bool RemoveSystemService(cIGZSystemService* service) = 0;                               // [4]
    virtual bool GetSystemService(std::uint32_t serviceId, std::uint32_t iid, void** out) = 0;     // [5]
    virtual bool EnumSystemServices(cIGZUnknownEnumerator& enumerator, cIGZUnknown* context, std::uint32_t flags) = 0;  // [6]
    virtual bool AddHook(cIGZFrameWorkHooks* hooks) = 0;                                            // [7]
    virtual bool RemoveHook(cIGZFrameWorkHooks* hooks) = 0;                                         // [8]
    virtual bool AddToTick(cIGZSystemService* service) = 0;                                         // [9]
    virtual bool RemoveFromTick(cIGZSystemService* service) = 0;                                    // [10]
    virtual bool AddToOnIdle(cIGZSystemService* service) = 0;                                       // [11]
    virtual bool RemoveFromOnIdle(cIGZSystemService* service) = 0;                                  // [12]
    virtual int GetOnIdleInterval() const = 0;                                                      // [13]
    virtual void SetOnIdleInterval(int interval) = 0;                                               // [14]
    virtual void OnTick(std::uint32_t tickNumber) = 0;                                              // [15]
    virtual void OnIdle() = 0;                                                                      // [16]
    virtual bool IsTickEnabled() const = 0;                                                         // [17]
    virtual void ToggleTick(bool enabled) = 0;                                                      // [18]
    virtual void Quit(int exitCode) = 0;                                                            // [19]
    virtual void AbortiveQuit(int exitCode) = 0;                                                    // [20]
    virtual cIGZCmdLine* CommandLine() = 0;                                                         // [21]
    virtual bool IsInstall() const = 0;                                                             // [22]
    virtual cIGZCOM* GetCOMObject() = 0;                                                            // [23]
    virtual int GetState() const = 0;                                                               // [24]
    virtual cIGZDebugStream* GetDebugStream() = 0;                                                  // [25]
    virtual cIGZDebugStream* DefaultDebugStream() = 0;                                              // [26]
    virtual cIGZDebugStream* DebugStream() = 0;                                                     // [27]
    virtual void SetDebugStream(cIGZDebugStream* stream) = 0;                                       // [28]
    virtual void SetDebugLevel(int level) = 0;                                                      // [29]
    virtual int GetDebugLevel() const = 0;                                                          // [30]
    virtual cIGZOStream* StdOut() = 0;                                                              // [31]
    virtual cIGZOStream* StdErr() = 0;                                                              // [32]
    virtual cIGZIStream* StdIn() = 0;                                                               // [33]
    virtual bool GetStream(int which, std::uint32_t iid, void** out) = 0;                           // [34]
    virtual bool SetStream(int which, cIGZUnknown* stream) = 0;                                     // [35]
    virtual void SetApplication(cIGZApp* app) = 0;                                                  // [36]
    virtual cIGZApp* Application() const = 0;                                                       // [37]
    virtual void ReportException(const char* text) = 0;                                             // [38]
    virtual cIGZUnknown* ExceptionNotificationObj() const = 0;                                      // [39]
};

class cIGZFrameWorkWindow : public cIGZUnknown {
public:
    virtual cIGZFrameWork* AsIGZFrameWork() = 0;     // [3]
    virtual void* GetWindowsInstance() const = 0;    // [4]
    virtual void* GetMainHWND() = 0;                 // [5]
    virtual void SetMainHWND(void* hwnd) = 0;        // [6]
};
