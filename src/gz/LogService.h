// nGZLog::cService — the framework logging service (service id 0x9b69c4e, priority 8999999, interface id
// 0x9b69c46). Recovered from Sim City 4 Deluxe Edition 1.2.1 (vtable groups 0x100ade290 / 0x100ade398 /
// 0x100ade3e0; ctor @ 0x10068fe42, real functions 0x10068fe42..0x10069283e).
//
// Original layout (0xc8 bytes; cSC4App::AddSystemServices allocates it with operator_new(200) @ 0x100006601;
// +0 cIGZSystemService via cRZSystemService, +8 cRZUnknown, +0x28 cIService):
//   +0x30  std::list<DeferredOutputData> same-thread re-entrant queue   (size +0x40)
//   +0x48  std::list<DeferredOutputData> other-thread queue             (size +0x58)
//   +0x60  std::list<cRZAutoRefCount<cIOutputBackEnd>> backends         (size +0x70)
//   +0x78  std::map<cRZString,int,cLessRZString> per-group output levels (inline tree, size +0x88)
//   +0x90  int default (unspecified-group) level, initialised to 0x7fffffff
//   +0x94  bool initialised
//   +0x98  std::map<std::string,int>* every group name ever seen
//   +0xa0  std::map<std::string,int>* backend names (case-sensitive set)
//   +0xa8  cRZCriticalSection
//   +0xb8  bool currently calling a backend
//   +0xbc  int output recursion depth (guarded at 4)
//   +0xc0  thread id captured by Init (same-thread messages stay on the +0x30 queue)
//
// Quirks kept:
//  * FilterLevelFromString maps "none" to 0x7fffffff and "all" to 0, while a backend's ShouldOutputLevel
//    compares `filter > messageLevel`; the names are effectively inverted (cOutputBackEnd @ 0x10068f44a
//    compiles to CMP/SETG). StringFromFilterLevel and StringFromLogLevel are different maps: 0..8/2..8
//    are log levels, 0/1..7/0x7fffffff are filter levels. DumpOutputLevels prints *filter* values through
//    StringFromLogLevel, so a group level of 1 prints "invalid filter level" @ 0x100690ed0.
//  * The non-variadic formatting helper (0x1006913c4) calls vsnprintf once into a 0x400 stack buffer; on a
//    >= 1024-byte result it then formats into a single 0x401-byte heap buffer (truncating long messages).
//  * The +0x30 drain (0x1006924aa) copies the front element, recurses, and only then pops it. An entry
//    queued by a re-entrant Output during a backend call is therefore delivered again by the nested call.
//  * RemoveAllOutputBackEnds clears only the backend list, not the +0xa0 name set: the name stays taken.
//  * AddOutputBackEnd's duplicate check is a case-sensitive std::map lookup, while Remove/Get compare the
//    backend Name() with strcasecmp, so "foo" and "FOO" can both be registered.
//  * ~cService does not delete the +0x98/+0xa0 maps (Shutdown does); see Shutdown's comment.
//
// Missing neighbours are seams (docs/SEAMS.md): nGZLog::cOutputBackEnd / cDefaultOutputBackEnd (0x10068f370
// ..0x10068fbea, 0x10068eea0..0x10068f06e) and the framework/command-line/tick calls made by Init/Shutdown.
//
// Port notes: the original stores a 0x10-byte cRZCriticalSection wrapper at +0xa8; the recovered class embeds
// an 80-byte std::recursive_mutex-based cRZCriticalSection (sizeof(cService) is 0x108 here, not 0xc8), so the
// offsets after +0xa8 in this port are shifted (the comments name the original offsets). The thread id is a
// std::thread::id instead of the original uint32 from cRZThread::GetCurrentThreadId (0x100038c76). With no
// host installed Init/Shutdown skip the framework calls and no default backend is created; the original would
// dereference a null framework pointer.
#pragma once

#include <cstdarg>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <thread>

#include "gz/cIGZOStream.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZString.h"
#include "rz/cRZSystemService.h"

namespace nGZLog {

constexpr std::uint32_t kServiceID_cService = 0x9b69c4e;
constexpr std::uint32_t kGZIID_cIService = 0x9b69c46;
// cOutputBackEnd::QueryInterface accepts this id (and kGZIID_cIGZUnknown): 0x10068fbea.
constexpr std::uint32_t kGZIID_cIOutputBackEnd = 0x49b84afb;

// Case-insensitive comparator of the original map (nGZLog::cLessRZString, inlined at 0x10068fcd6: it calls
// cRZString::CompareToString(other, false) through vslot +0x68).
struct LogLessRZString {
    bool operator()(const cRZString& a, const cRZString& b) const {
        return a.CompareToString(b, false) < 0;
    }
};

// Host seam for nGZLog::cOutputBackEnd / cDefaultOutputBackEnd (missing neighbours). The slots are those of
// the cDefaultOutputBackEnd vtable group @ 0x100ade170; the destructor slots [10]/[11] are not modelled here.
// Lifetime contract, as in the original cOutputBackEnd: an object handed out by a factory starts at count 0;
// AddRef increments, Release deletes when the count is <= 1 (0x10068fc0c / 0x10068fc1a).
class ILogOutputBackEndHost : public cIGZUnknown {
public:
    // [0] @ 0x10068fbea: accepts kGZIID_cIOutputBackEnd and kGZIID_cIGZUnknown.
    // [3] +0x18 @ 0x10068f44a: filterLevel > messageLevel (see the quirk note above).
    virtual bool ShouldOutputLevel(const char* group, int level) const = 0;
    // [4] +0x20 @ 0x10068f464: null group sets the backend's default level (clearing its group map).
    virtual bool SetOutputLevel(const char* group, int level) = 0;
    // [5] +0x28 @ 0x10068f726: group map lookup, default when absent.
    virtual int GetOutputLevel(const char* group) const = 0;
    // [6] +0x30 @ 0x10068f83e: writes the backend's table to the stream.
    virtual void DumpOutputLevels(cIGZOStream& stream) const = 0;
    // [7] +0x38 @ 0x10068f06e (cDefaultOutputBackEnd::Output).
    virtual void Output(const char* group, int level, const char* message) = 0;
    // [8] +0x40 @ 0x10068fad6.
    virtual bool SetName(const char* name) = 0;
    // [9] +0x48 @ 0x10068fbce.
    virtual const char* Name() const = 0;
};

// Host seam for everything cService reaches outside itself. Each method names the original call.
class ILogServiceHost {
public:
    virtual ~ILogServiceHost() = default;

    // RZGetFrameWork() (0x10002ab4b) vslot +0xa8 = cIGZFrameWork::CommandLine(), then cIGZCmdLine vslot
    // +0x58 = IsSwitchPresent(cRZString(name), value, stripQuotes=true) @ 0x100015162. `Init` asks for
    // "filterLevel" and "logLevel" (the -filterLevel / -logLevel command-line switches).
    virtual bool GetCommandLineSwitch(const char* name, std::string& value) = 0;
    // Framework vslot +0x48 / +0x50 (AddToTick / RemoveFromTick) @ 0x10002c...; called by Init/Shutdown.
    virtual bool AddToTick(cIGZSystemService* service) = 0;
    virtual bool RemoveFromTick(cIGZSystemService* service) = 0;
    // `new cDefaultOutputBackEnd()` @ 0x10068eea0 (Init's fallback backend when the list is empty).
    virtual ILogOutputBackEndHost* CreateDefaultOutputBackEnd() = 0;
    // `new cDefaultOutputBackEnd(name, stream, append)` @ 0x10068f01e (AddOutputStream).
    virtual ILogOutputBackEndHost* CreateOutputStreamBackEnd(const char* name, cIGZOStream* stream, bool append) = 0;
};

// The process-wide seam instance set by the engine/test (null disables the host-dependent paths).
void SetLogServiceHost(ILogServiceHost* host);
ILogServiceHost* LogServiceHost();

// nGZLog::cIService — the interface at +0x28, queried with iid 0x9b69c46 (vtable group 0x100ade3e0; thunk
// slots are the non-virtual thunks at 0x1006900a4..0x10069283e). Slots [3]..[17] in vtable order.
class cIService : public cIGZUnknown {
public:
    virtual bool AddOutputBackEnd(const char* name, ILogOutputBackEndHost* backend) = 0;              // [3]  +0x18
    virtual bool RemoveOutputBackEnd(const char* name) = 0;                                           // [4]  +0x20
    virtual void RemoveAllOutputBackEnds() = 0;                                                       // [5]  +0x28
    virtual const std::map<std::string, int>* OutputBackEndList() const = 0;                          // [6]  +0x30
    virtual bool AddOutputStream(const char* name, cIGZOStream* stream, bool append,
                                 ILogOutputBackEndHost** out) = 0;                                    // [7]  +0x38
    virtual bool GetOutputBackEnd(const char* name, ILogOutputBackEndHost** out) const = 0;           // [8]  +0x40
    virtual void SetOutputLevel(const char* backendName, const char* group, int level) = 0;           // [9]  +0x48
    virtual int FilterLevelFromString(const char* text) const = 0;                                    // [10] +0x50
    virtual const char* StringFromFilterLevel(int level) const = 0;                                   // [11] +0x58
    virtual int LogLevelFromString(const char* text) const = 0;                                       // [12] +0x60
    virtual const char* StringFromLogLevel(int level) const = 0;                                      // [13] +0x68
    virtual void DumpOutputLevels(cIGZOStream& stream) const = 0;                                     // [14] +0x70
    virtual const std::map<std::string, int>* GroupList() const = 0;                                  // [15] +0x78
    virtual void Output(const char* group, const char* format, ...) = 0;                              // [16] +0x80
    virtual void OutputLevel(const char* group, int level, const char* format, ...) = 0;              // [17] +0x88
};

class cService final : public cRZSystemService, public cIService {
public:
    cService();                                       // C2 @ 0x10068fe42 (C1 alias @ 0x10068fe2c)
    ~cService() override;                             // D2 @ 0x10068ffdc (D1 @ 0x10069009a, D0 @ 0x1006900b2)

    // --- cIGZUnknown ---
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x100692792
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }     // @ 0x100692800
    std::uint32_t Release() override { return cRZSystemService::Release(); }   // @ 0x100692826

    // --- cIGZSystemService ---
    bool Init() override;                             // @ 0x100691b8a
    bool Shutdown() override;                         // @ 0x1006921e2
    bool OnTick(std::uint32_t elapsedMs) override;    // @ 0x1006922fc

    // --- cIService (vtable slots match the interface above) ---
    bool AddOutputBackEnd(const char* name, ILogOutputBackEndHost* backend) override;   // @ 0x1006900ee
    bool RemoveOutputBackEnd(const char* name) override;                                // @ 0x100690414
    void RemoveAllOutputBackEnds() override;                                            // @ 0x1006906c8
    const std::map<std::string, int>* OutputBackEndList() const override;               // @ 0x100690876
    bool AddOutputStream(const char* name, cIGZOStream* stream, bool append,
                         ILogOutputBackEndHost** out) override;                         // @ 0x1006905f8
    bool GetOutputBackEnd(const char* name, ILogOutputBackEndHost** out) const override;  // @ 0x100690738
    void SetOutputLevel(const char* backendName, const char* group, int level) override;  // @ 0x10069088e
    int FilterLevelFromString(const char* text) const override;                         // @ 0x100690c36
    const char* StringFromFilterLevel(int level) const override;                        // @ 0x100690cfc
    int LogLevelFromString(const char* text) const override;                            // @ 0x100690d80
    const char* StringFromLogLevel(int level) const override;                           // @ 0x100690e48
    void DumpOutputLevels(cIGZOStream& stream) const override;                          // @ 0x100690ed0
    const std::map<std::string, int>* GroupList() const override { return groupNames_; }  // @ 0x100691188
    void Output(const char* group, const char* format, ...) override;                   // @ 0x1006911a0
    void OutputLevel(const char* group, int level, const char* format, ...) override;   // @ 0x100691748

    // Non-virtual overload @ 0x1006913c4: formats then hands the text to OutputLowLevel.
    void Output(const char* group, int level, const char* format, std::va_list args);
    // @ 0x1006924aa: the re-entrancy/thread dispatch. Public only so the nested-queue test can drive it
    // through Output/OnTick; callers use Output/OutputLevel/OnTick like the original.
    void OutputLowLevel(const char* group, int level, const char* message);

private:
    // +0x30/+0x48 queue element: two cRZString members around an int (0x58 bytes; cRZString is 0x28:
    // vptr +8, std::string +8, refcount +0x20). @ 0x100692bac.
    struct DeferredOutputData {
        DeferredOutputData(const char* groupName, int outputLevel, const char* message)
            : group(groupName), level(outputLevel), message(message) {}
        cRZString group;      // +0x00
        int level;            // +0x28
        cRZString message;    // +0x30
    };

    std::list<DeferredOutputData> sameThreadQueue_;                              // +0x30
    std::list<DeferredOutputData> otherThreadQueue_;                             // +0x48
    std::list<cRZAutoRefCount<ILogOutputBackEndHost>> backends_;                 // +0x60
    std::map<cRZString, int, LogLessRZString> groupLevels_;                      // +0x78
    int defaultLevel_ = 0x7fffffff;                                              // +0x90
    bool initialised_ = false;                                                   // +0x94
    std::map<std::string, int>* groupNames_ = nullptr;                           // +0x98
    std::map<std::string, int>* backendNames_ = nullptr;                         // +0xa0
    mutable cRZCriticalSection lock_;                                            // +0xa8
    bool outputting_ = false;                                                    // +0xb8
    int outputDepth_ = 0;                                                        // +0xbc
    std::thread::id ownerThread_;                                                // +0xc0 (original: uint32 thread id)
};

}  // namespace nGZLog
