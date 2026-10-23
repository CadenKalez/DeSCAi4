// nGZLog::cService — the framework logging service. Every function below is a hand translation of the
// 1.2.1 x86_64 decompile; the original addresses are on the definitions. The missing output-backend
// classes and the framework/command-line access go through the two seams declared in LogService.h.

#include "gz/LogService.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <strings.h>
#include <utility>

namespace nGZLog {

namespace {

// kLogLevelNames @ 0x100ade480 (anonymous namespace): errors, errorsPlus, warnings, warningsPlus, info,
// infoPlus, debug, null. The table is shared by the four level converters.
constexpr const char* kLogLevelNames[] = {
    "errors", "errorsPlus", "warnings", "warningsPlus", "info", "infoPlus", "debug", nullptr,
};

ILogServiceHost* g_host = nullptr;

// The original gates the numeric path on the first byte being a digit in the default rune locale
// (0x100690c76) and then calls atoi; the string path is strcasecmp.
bool StartsWithDigit(const char* text) {
    return std::isdigit(static_cast<unsigned char>(text[0])) != 0;
}

}  // namespace

void SetLogServiceHost(ILogServiceHost* host) { g_host = host; }
ILogServiceHost* LogServiceHost() { return g_host; }

// cService::cService() @ 0x10068fe42 (the complete-object ctor at 0x10068fe2c tail-calls this one).
cService::cService()
    : cRZSystemService(kServiceID_cService, 8999999),
      groupNames_(new std::map<std::string, int>()),
      backendNames_(new std::map<std::string, int>()) {
    // +0x90..+0xbc defaults are member initialisers; the original also heap-allocates the two name maps
    // (+0x98/+0xa0, 0x18 bytes each) and constructs the recursive critical section (+0xa8).
}

// cService::~cService() @ 0x10068ffdc (D1 0x10069009a, D0 0x1006900b2). The original does not free the two
// +0x98/+0xa0 maps here (Shutdown owns them); the port keeps that and only lets the members destroy.
cService::~cService() = default;

// cService::QueryInterface(unsigned int, void**) @ 0x100692792. 1 and kGZIID_cIGZSystemService go through
// cRZUnknown (the +8 subobject); 0x9b69c46 is the cIService subobject at +0x28 and 0x287fb697 is this.
bool cService::QueryInterface(std::uint32_t iid, void** out) {
    if (!out) return false;
    if (iid == kGZIID_cIService) {
        *out = static_cast<cIService*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cIGZSystemService) {
        *out = static_cast<cIGZSystemService*>(this);
        AddRef();
        return true;
    }
    return cRZUnknown::QueryInterface(iid, out);
}

// cService::AddOutputBackEnd(char const*, cIOutputBackEnd*) @ 0x1006900ee.
bool cService::AddOutputBackEnd(const char* name, ILogOutputBackEndHost* backend) {
    if (!name || !backend || !backendNames_) return false;
    // Case-sensitive std::map membership test (the quirk noted in the header).
    if (backendNames_->find(name) != backendNames_->end()) return false;

    (*backendNames_)[name] = 0;
    backend->SetName(name);
    backend->SetOutputLevel(nullptr, defaultLevel_);
    // Match every per-group level already configured on the service.
    for (const auto& entry : groupLevels_) {
        backend->SetOutputLevel(entry.first.ToChar(), entry.second);
    }

    lock_.Lock();
    // emplace_back constructs the cRZAutoRefCount in place: one AddRef for the list. The original's
    // explicit AddRef + stack auto-ref + push + release of the stack auto-ref is the same net +1.
    backends_.emplace_back(backend);
    lock_.Unlock();
    return true;
}

// cService::RemoveOutputBackEnd(char const*) @ 0x100690414.
bool cService::RemoveOutputBackEnd(const char* name) {
    if (!name || !backendNames_) return false;
    if (backendNames_->find(name) == backendNames_->end()) return false;

    lock_.Lock();
    for (auto it = backends_.begin(); it != backends_.end(); ++it) {
        const char* backendName = (*it)->Name();
        // The list lookup is case-insensitive although the +0xa0 membership test above is not.
        if (backendName && ::strcasecmp(backendName, name) == 0) {
            backends_.erase(it);
            const bool erased = backendNames_->erase(name) == 1;
            lock_.Unlock();
            return erased;
        }
    }
    lock_.Unlock();
    return false;
}

// cService::AddOutputStream(char const*, cIGZOStream*, bool, cIOutputBackEnd**) @ 0x1006905f8.
bool cService::AddOutputStream(const char* name, cIGZOStream* stream, bool append,
                               ILogOutputBackEndHost** out) {
    if (out) *out = nullptr;
    ILogServiceHost* host = LogServiceHost();
    if (!name || !host) return false;

    ILogOutputBackEndHost* backend = host->CreateOutputStreamBackEnd(name, stream, append);
    if (!backend) return false;

    // The original news a cDefaultOutputBackEnd (count 0), AddRefs it, registers it (which takes the list
    // reference), hands one reference to the caller and releases its own.
    backend->AddRef();
    (void)AddOutputBackEnd(backend->Name(), backend);
    if (out) {
        *out = backend;
        backend->AddRef();
    }
    backend->Release();
    return true;
}

// cService::RemoveAllOutputBackEnds() @ 0x1006906c8. It clears the list only: the +0xa0 names stay taken.
void cService::RemoveAllOutputBackEnds() {
    lock_.Lock();
    backends_.clear();
    lock_.Unlock();
}

// cService::GetOutputBackEnd(char const*, cIOutputBackEnd**) const @ 0x100690738.
bool cService::GetOutputBackEnd(const char* name, ILogOutputBackEndHost** out) const {
    if (!name || !out || !backendNames_) return false;
    if (backendNames_->find(name) == backendNames_->end()) return false;

    lock_.Lock();
    for (auto it = backends_.begin(); it != backends_.end(); ++it) {
        const char* backendName = (*it)->Name();
        if (backendName && ::strcasecmp(backendName, name) == 0) {
            // The original asks the backend for its cIOutputBackEnd interface (iid 0x49b84afb) and takes
            // the reference that QueryInterface returns.
            const bool ok = (*it)->QueryInterface(kGZIID_cIOutputBackEnd,
                                                  reinterpret_cast<void**>(out));
            lock_.Unlock();
            return ok;
        }
    }
    lock_.Unlock();
    return false;
}

// cService::OutputBackEndList() const @ 0x100690876.
const std::map<std::string, int>* cService::OutputBackEndList() const { return backendNames_; }

// cService::SetOutputLevel(char const*, char const*, int) @ 0x10069088e.
// (backend name or null, group name or null, level)
void cService::SetOutputLevel(const char* backendName, const char* group, int level) {
    if (backendName) {
        ILogOutputBackEndHost* backend = nullptr;
        if (GetOutputBackEnd(backendName, &backend) && backend) {
            backend->SetOutputLevel(group, level);
            backend->Release();
        }
        return;
    }

    if (!group) {
        groupLevels_.clear();
        defaultLevel_ = level;
    } else {
        groupLevels_[cRZString(group)] = level;
    }

    lock_.Lock();
    for (auto& backend : backends_) {
        backend->SetOutputLevel(group, level);
    }
    lock_.Unlock();
}

// cService::FilterLevelFromString(char const*) const @ 0x100690c36.
// Quirk: "none" is 0x7fffffff and "all" is 0, the opposite of what ShouldOutputLevel does with the value.
int cService::FilterLevelFromString(const char* text) const {
    if (!text || !text[0]) return -1;
    if (StartsWithDigit(text)) return static_cast<int>(static_cast<unsigned>(std::atoi(text)) + 1u);
    if (::strcasecmp(text, "none") == 0) return 0x7fffffff;
    if (::strcasecmp(text, "all") == 0) return 0;
    for (int i = 0; kLogLevelNames[i]; ++i) {
        if (::strcasecmp(text, kLogLevelNames[i]) == 0) return i + 1;
    }
    return -1;
}

// cService::StringFromFilterLevel(int) const @ 0x100690cfc.
const char* cService::StringFromFilterLevel(int level) const {
    if (level == 0x7fffffff) return "none";
    if (level == 0) return "all";
    if (static_cast<unsigned>(level) - 1u < 7u) return kLogLevelNames[level - 1];
    return "invalid filter level";
}

// cService::LogLevelFromString(char const*) const @ 0x100690d80.
// The numeric path and the names are offset differently (names -> i+2, numbers -> atoi+1).
int cService::LogLevelFromString(const char* text) const {
    if (!text || !text[0]) return -1;
    if (StartsWithDigit(text)) return static_cast<int>(static_cast<unsigned>(std::atoi(text)) + 1u);
    if (::strcasecmp(text, "none") == 0) return 0;
    if (::strcasecmp(text, "all") == 0) return 0x7fffffff;
    for (int i = 0; kLogLevelNames[i]; ++i) {
        if (::strcasecmp(text, kLogLevelNames[i]) == 0) return i + 2;
    }
    return -1;
}

// cService::StringFromLogLevel(int) const @ 0x100690e48.
const char* cService::StringFromLogLevel(int level) const {
    if (level == 0) return "none";
    if (level == 0x7fffffff) return "all";
    if (static_cast<unsigned>(level) - 2u < 7u) return kLogLevelNames[level - 2];
    return "invalid filter level";
}

// cService::DumpOutputLevels(cIGZOStream&) const @ 0x100690ed0. The level text goes through
// StringFromLogLevel even though the stored values are filter levels (quirk kept; see LogService.h).
void cService::DumpOutputLevels(cIGZOStream& stream) const {
    stream.SetRZCharStr("All Backends:\n");
    stream.SetRZCharStr("-------------\n");

    cRZString line;
    const char* levelText = StringFromLogLevel(defaultLevel_);
    line.AssignFromCString(levelText);
    if (defaultLevel_ != 0x7fffffff && defaultLevel_ != 0) {
        line.AssignFromCString(">= ");
        line.AppendChars(levelText, static_cast<std::uint32_t>(std::strlen(levelText)));
    }
    stream.SetRZCharStr("All unspecified groups showing ");
    stream.SetGZStr(line);
    stream.SetRZCharStr("\n");

    for (const auto& entry : groupLevels_) {
        const int level = entry.second;
        const char* text = StringFromLogLevel(level);
        line.AssignFromCString(text);
        if (level != 0x7fffffff && level != 0) {
            line.AssignFromCString(">= ");
            line.AppendChars(text, static_cast<std::uint32_t>(std::strlen(text)));
        }
        stream.SetRZCharStr("Group '");
        stream.SetGZStr(entry.first);
        stream.SetRZCharStr("' showing ");
        stream.SetGZStr(line);
        stream.SetRZCharStr("\n");
    }

    stream.SetRZCharStr("-------------\n");

    lock_.Lock();
    for (auto& backend : backends_) {
        backend->DumpOutputLevels(stream);
    }
    lock_.Unlock();
}

// cService::Output(char const*, char const*, ...) @ 0x1006911a0. The group name is recorded in the +0x98
// map under the lock, then the message is formatted and dispatched at log level 0.
void cService::Output(const char* group, const char* format, ...) {
    if (!initialised_ || !group || !format || !groupNames_) return;

    lock_.Lock();
    groupNames_->insert(std::make_pair(std::string(group), 0));
    lock_.Unlock();

    std::va_list args;
    va_start(args, format);
    Output(group, 0, format, args);
    va_end(args);
}

// cService::Output(char const*, int, char const*, __va_list_tag*) @ 0x1006913c4.
// The original formats into a zeroed 0x400 stack buffer; on a result >= 0x400 it formats once more into a
// 0x401-byte heap buffer (long messages are therefore still truncated), and on a negative result doubles a
// heap buffer from 0x800 until vsnprintf succeeds or the buffer reaches 50000.
void cService::Output(const char* group, int level, const char* format, std::va_list args) {
    char stackBuffer[1024];
    std::memset(stackBuffer, 0, sizeof(stackBuffer));

    std::va_list copy;
    va_copy(copy, args);
    const int length = std::vsnprintf(stackBuffer, sizeof(stackBuffer), format, copy);
    va_end(copy);

    if (length >= 0 && length < static_cast<int>(sizeof(stackBuffer))) {
        OutputLowLevel(group, level, stackBuffer);
        return;
    }

    if (length >= 0) {
        std::unique_ptr<char[]> heap(new char[0x401]);
        va_copy(copy, args);
        std::vsnprintf(heap.get(), 0x401, format, copy);
        va_end(copy);
        OutputLowLevel(group, level, heap.get());
        return;
    }

    std::unique_ptr<char[]> heap;
    int capacity = 0x800;
    for (;;) {
        heap.reset(new char[capacity]);
        va_copy(copy, args);
        const int result = std::vsnprintf(heap.get(), static_cast<std::size_t>(capacity), format, copy);
        va_end(copy);
        if (result >= 0) break;
        if (capacity >= 50000) break;
        capacity *= 2;
    }
    OutputLowLevel(group, level, heap.get());
}

// cService::OutputLevel(char const*, int, char const*, ...) @ 0x100691748.
void cService::OutputLevel(const char* group, int level, const char* format, ...) {
    if (!initialised_ || !group || !format || !groupNames_) return;

    lock_.Lock();
    groupNames_->insert(std::make_pair(std::string(group), 0));
    lock_.Unlock();

    std::va_list args;
    va_start(args, format);
    Output(group, level, format, args);
    va_end(args);
}

// cService::Init() @ 0x100691b8a.
bool cService::Init() {
    if (initialised_) return true;

    ownerThread_ = std::this_thread::get_id();

    lock_.Lock();
    if (backends_.empty()) {
        if (ILogServiceHost* host = LogServiceHost()) {
            if (ILogOutputBackEndHost* backend = host->CreateDefaultOutputBackEnd()) {
                // The original backend starts at count 0; the in-place auto-ref adds the list's single
                // reference (the original's explicit AddRef + stack auto-ref/push/release nets to +1 too).
                backends_.emplace_back(backend);
            }
        }
    }
    lock_.Unlock();

    ILogServiceHost* host = LogServiceHost();
    if (host) {
        // -filterLevel:<level>[:group=<level>...] then -logLevel:<...>; the second switch overwrites the
        // first one's global level (and a bare level clears the group map).
        const auto applySwitch = [this](const std::string& text, bool filterLevel) {
            cRZString remaining(text.c_str());
            cRZString token;
            while (remaining.SplitTokenSeparated(':', token)) {
                const char* value = token.ToChar();
                const char* equals = std::strchr(value, '=');
                if (equals && equals[1] != '\0') {
                    const std::string key(value, static_cast<std::size_t>(equals - value));
                    const int level = filterLevel ? FilterLevelFromString(equals + 1)
                                                  : LogLevelFromString(equals + 1);
                    SetOutputLevel(nullptr, key.c_str(), level);
                } else {
                    const int level = filterLevel ? FilterLevelFromString(value)
                                                  : LogLevelFromString(value);
                    SetOutputLevel(nullptr, nullptr, level);
                }
            }
        };

        std::string value;
        if (host->GetCommandLineSwitch("filterLevel", value)) applySwitch(value, true);
        if (host->GetCommandLineSwitch("logLevel", value)) applySwitch(value, false);

        host->AddToTick(static_cast<cIGZSystemService*>(this));
    }

    initialised_ = true;
    return true;
}

// cService::Shutdown() @ 0x1006921e2. Clears the backend list and deletes both name maps (+0x98/+0xa0) but
// leaves the per-group level map (+0x78) and the default level alone, exactly as the binary does.
bool cService::Shutdown() {
    if (!initialised_) return true;
    initialised_ = false;

    if (ILogServiceHost* host = LogServiceHost()) {
        host->RemoveFromTick(static_cast<cIGZSystemService*>(this));
    }

    if (!backends_.empty()) {
        lock_.Lock();
        backends_.clear();
        lock_.Unlock();
    }

    delete groupNames_;
    groupNames_ = nullptr;
    delete backendNames_;
    backendNames_ = nullptr;
    return true;
}

// cService::OnTick(unsigned int) @ 0x1006922fc. Drains the other-thread queue, delivering each entry
// through the normal (owner-thread) path.
bool cService::OnTick(std::uint32_t) {
    lock_.Lock();
    while (!otherThreadQueue_.empty()) {
        DeferredOutputData data = otherThreadQueue_.front();
        otherThreadQueue_.pop_front();
        lock_.Unlock();
        OutputLowLevel(data.group.ToChar(), data.level, data.message.ToChar());
        lock_.Lock();
    }
    lock_.Unlock();
    return true;
}

// cService::OutputLowLevel(char const*, int, char const*) @ 0x1006924aa.
//
// Owner thread, not currently outputting: deliver to every backend (ShouldOutputLevel then Output), then
// drain the same-thread queue. The original copies the front element, unlocks, recurses, re-locks and only
// then pops it, so a re-entrant entry is delivered again by the recursive call (quirk kept).
// Owner thread while a backend is being called: append to the +0x30 queue.
// Any other thread: append to the +0x48 queue (drained by OnTick).
void cService::OutputLowLevel(const char* group, int level, const char* message) {
    lock_.Lock();
    if (std::this_thread::get_id() == ownerThread_) {
        if (!outputting_) {
            const int depth = ++outputDepth_;
            if (depth >= 4) {  // the original's guard: 3 nested deliveries, the 4th is dropped
                lock_.Unlock();
                --outputDepth_;
                return;
            }

            for (auto it = backends_.begin(); it != backends_.end(); ++it) {
                ILogOutputBackEndHost* backend = it->get();
                outputting_ = true;
                lock_.Unlock();
                if (backend->ShouldOutputLevel(group, level)) {
                    backend->Output(group, level, message);
                }
                lock_.Lock();
                outputting_ = false;
            }

            while (!sameThreadQueue_.empty()) {
                DeferredOutputData data = sameThreadQueue_.front();
                lock_.Unlock();
                OutputLowLevel(data.group.ToChar(), data.level, data.message.ToChar());
                lock_.Lock();
                if (!sameThreadQueue_.empty()) sameThreadQueue_.pop_front();
            }

            --outputDepth_;
            lock_.Unlock();
            return;
        }

        sameThreadQueue_.emplace_back(group, level, message);
        lock_.Unlock();
        return;
    }

    otherThreadQueue_.emplace_back(group, level, message);
    lock_.Unlock();
}

}  // namespace nGZLog
