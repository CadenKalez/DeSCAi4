#include "gz/LuaScriptServer.h"

#include <string>

#include "gz/GZCOM.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZFile.h"
#include "rz/cRZMessage2Standard.h"

namespace {
ILuaScriptServerHost* g_host = nullptr;

// 0x100646663 ReadFileIntoString(cRZString const&, cRZString&, unsigned int): reads at most 1,000,000 bytes
// (the third argument is ignored by the binary, which always uses 0xf4240). A failed open or read clears `out`.
constexpr std::uint32_t kReadFileMaxLength = 0xf4240;
}  // namespace

void SetLuaScriptServerHost(ILuaScriptServerHost* host) { g_host = host; }
ILuaScriptServerHost* GetLuaScriptServerHost() { return g_host; }

bool ReadFileIntoString(const cRZString& path, cRZString& out, unsigned int /*maxLength*/) {
    cRZFile file(path);
    if (!file.Open(1, 2, 1)) {
        out.str().clear();
        return false;
    }
    std::uint32_t length = file.Length();
    std::uint32_t count = length < kReadFileMaxLength ? length : kReadFileMaxLength;
    out.Resize(count);
    bool ok = file.Read(out.str().data(), count);
    file.Close();
    if (!ok) {
        out.str().clear();
        return false;
    }
    return true;
}

// @ 0x1006453b0
cGZLuaScriptServer::LuaThreadData::~LuaThreadData() {
    if (thread) {
        thread->Release();
        thread = nullptr;
    }
}

// @ 0x100645418
bool cGZLuaScriptServer::LuaThreadData::Init(lua5::cGZLua5* /*lua*/, const cIGZString& source,
                                             const cIGZString& chunkNameIn) {
    if (thread) return false;
    sourceText.AssignFromIGZString(source);
    chunkName.AssignFromIGZString(chunkNameIn);
    started = false;
    currentLine = -1;
    return true;
}

// @ 0x100645460 (the forwarding constructor at 0x100643fc8 only calls this one)
cGZLuaScriptServer::cGZLuaScriptServer()
    : cRZSystemService(kGZSID_cGZLuaScriptServer, kGZLuaScriptServerPriority) {}

// @ 0x1006454ec
cGZLuaScriptServer::~cGZLuaScriptServer() {
    // The original relies on Shutdown() having killed every script (its tree destroy does not delete the
    // LuaThreadData objects); release any leftovers so a direct delete does not leak.
    for (auto& entry : threads_) {
        LuaThreadData* data = entry.second;
        if (!data) continue;
        if (data->thread) data->thread->Release();
        data->thread = nullptr;
        delete data;
    }
    threads_.clear();
    if (generator_) {
        generator_->Release();
        generator_ = nullptr;
    }
    if (lua_) {
        lua_->Release();
        lua_ = nullptr;
    }
}

// @ 0x100645604
bool cGZLuaScriptServer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZLuaScriptServer) {
        if (!out) return false;
        *out = static_cast<cIGZLuaScriptServer*>(this);
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

// @ 0x100645648
std::uint32_t cGZLuaScriptServer::AddRef() { return ++refCount_; }

// @ 0x100645672
std::uint32_t cGZLuaScriptServer::Release() {
    if (refCount_ > 1) return --refCount_;
    FinalRelease();
    return 0;
}

// @ 0x1006456da: Shutdown() then the deleting destructor.
void cGZLuaScriptServer::FinalRelease() {
    Shutdown();
    delete this;
}

// @ 0x100645708
bool cGZLuaScriptServer::Init() {
    if (!initialised_) {
        initialised_ = true;
        if (!g_host) return false;
        if (!lua_) {
            lua_ = g_host->CreateLua();
            if (lua_ && !lua_->Open(kLuaScriptOpenLibraries)) {
                lua_->Release();
                lua_ = nullptr;
            }
            if (!lua_) return false;
            generator_ = g_host->CreateCommandGenerator();
            if (generator_) {
                cIGZCommandServer* server = g_host->GetCommandServer();
                bool ok = generator_->Init(server, lua_, cRZString(kLuaCommandGeneratorTableName));
                if (server) server->Release();
                if (!ok) {
                    generator_->Release();
                    generator_ = nullptr;
                }
            }
            if (!generator_) {
                lua_->Close();
                lua_->Release();
                lua_ = nullptr;
                return false;
            }
        }
        g_host->AddToTick(static_cast<cIGZSystemService*>(this));
    }
    return ScriptArgSetup();
}

// @ 0x100645924
bool cGZLuaScriptServer::ScriptArgSetup() {
    if (!lua_) return false;
    lua5::cGZLua5Thread* thread = lua_->AsThread();
    int top = thread->GetTop();
    thread->PushString("arg");
    thread->PushNewUserData(1);
    thread->PushValue(-1);
    thread->Insert(-3);
    thread->RawSet(lua5::kGlobalsIndex);
    thread->PushNewTable();
    thread->PushString("__index");
    thread->PushCClosure(&cGZLuaScriptServer::ScriptArgIndexEventHandler, 0);
    thread->RawSet(-3);
    thread->SetMetaTable(-2);
    thread->SetTop(top);
    return true;
}

// @ 0x100645a20
bool cGZLuaScriptServer::Shutdown() {
    if (initialised_) {
        initialised_ = false;
        if (g_host) g_host->RemoveFromTick(static_cast<cIGZSystemService*>(this));
        while (!threads_.empty()) {
            auto it = threads_.begin();
            if (!KillScript(it->first)) threads_.erase(it);   // a null record cannot be killed
        }
        if (generator_) {
            generator_->Shutdown();
            generator_->Release();
            generator_ = nullptr;
        }
        if (lua_) {
            lua_->Close();
            lua_->Release();
            lua_ = nullptr;
        }
    }
    return true;
}

// @ 0x100645ad8
void cGZLuaScriptServer::LuaYieldHook(lua5::lua_State* state, LuaDebug* ar) {
    if (!g_host || !state) return;
    lua5::cGZLua5Thread* thread = g_host->ThreadForState(state);
    if (!thread) return;
    auto* data = static_cast<LuaThreadData*>(thread->GetUserContext());
    if (!data || !g_host->IsLuaYieldable(state) || !data->autoYield) return;
    g_host->GetLuaInfoLine(state, ar);
    if (data->currentLine != ar->currentline) {
        data->currentLine = ar->currentline;
        g_host->YieldLua(state);
    }
}

// @ 0x100645b56
int cGZLuaScriptServer::RunScriptString(const cIGZString& source, const cIGZString& chunkName,
                                        const cIGZString& arguments) {
    if (!lua_ || !g_host) return -1;
    auto* data = new LuaThreadData();
    ++scriptCounter_;
    if (!data->Init(lua_, source, chunkName)) {
        delete data;
        return -1;
    }

    lua5::cGZLua5Thread* mainThread = lua_->AsThread();
    int loadStatus = mainThread->LoadBuffer(source.Data(), static_cast<int>(source.Strlen()), chunkName.Data());
    if (loadStatus != 0) {
        mainThread->Pop(1);   // the original drops one stack slot on a load error
    } else {
        // Chunk display name: "Main - <path or name> - <script counter>".
        cRZString displayPath;
        if (cRZFile::FileExists(chunkName)) {
            cRZFile::GetPathName(chunkName, displayPath, false);
        } else {
            displayPath.AssignFromIGZString(chunkName);
        }
        cRZString displayName;
        displayName.Sprintf("Main - \"%s\" - %d", displayPath.ToChar(), scriptCounter_);
        mainThread->PushString(displayName.ToChar());
        mainThread->PushValue(-2);
        mainThread->RawSet(lua5::kGlobalsIndex);

        lua5::cGZLua5Thread* thread = nullptr;
        int scriptId = lua_->CreateThread(&thread, 1, kLuaScriptThreadClassId);
        if (scriptId == -1) {
            if (thread) thread->Release();
            delete data;
            return -1;
        }
        data->thread = thread;
        thread->SetUserContext(data);   // the yield hook and ShouldBreak find the record through this
        lastCreatedScriptId_ = scriptId;
        data->arguments.AssignFromIGZString(arguments);
        ScriptArgSetupThreadArguments(data);
        data->started = true;

        auto found = threads_.find(scriptId);
        if (found == threads_.end()) {
            threads_.emplace(scriptId, data);
        } else {
            // Unreachable in the original (ids only grow); it would use the released thread pointer.
            LuaThreadData* old = found->second;
            if (old) {
                if (old->thread) old->thread->Release();
                delete old;
            }
            found->second = data;
        }
        g_host->SetLuaHook(thread->GetContext(), &cGZLuaScriptServer::LuaYieldHook, 4);
        PostScriptChangeMessage(scriptId, kLuaScriptCreatedMessage, 0);
        return scriptId;
    }

    // Load failure: the original releases the thread (still null here) and deletes the record.
    if (data->thread) data->thread->Release();
    delete data;
    return -1;
}

// @ 0x100645f56
bool cGZLuaScriptServer::ScriptArgSetupThreadArguments(const LuaThreadData* data) const {
    if (!data || !data->thread) return false;
    lua5::cGZLua5Thread* thread = data->thread;
    thread->PushLightUserData(thread);
    thread->PushNewTable();
    thread->PushNumber(0.0);
    thread->PushString(data->chunkName.ToChar());
    thread->RawSet(-3);                     // arguments[0] = chunk name (mirrors the original)

    int count = 1;
    if (data->arguments.Strlen() != 0) {
        cRZCmdLine commandLine(data->arguments.ToChar());
        int argc = commandLine.argc();
        for (int i = 0; i < argc; ++i) {
            cRZString argument(commandLine.argv(i));
            const char* text = argument.ToChar();
            int length = static_cast<int>(argument.Strlen());
            // The original skips leading '-' and '/' (the (c | 2) == '/' test) and drops an argument made
            // only of those characters.
            int start = 0;
            while (start < length && ((text[start] | 2) == 0x2f)) ++start;
            if (start >= length) continue;

            bool hasColon = false;
            int colon = start;
            while (colon < length) {
                if (text[colon] == ':') {
                    hasColon = true;
                    break;
                }
                ++colon;
            }
            cRZString key = argument.Mid(static_cast<std::size_t>(start), static_cast<std::size_t>(colon - start));
            cRZString value;
            if (hasColon) {
                value = argument.Mid(static_cast<std::size_t>(colon + 1),
                                     static_cast<std::size_t>(length - 1 - colon));
            }
            thread->PushString(key.ToChar());
            if (value.Strlen() != 0) {
                thread->PushString(value.ToChar());
            } else if (hasColon) {
                thread->PushString("");
            } else {
                thread->PushBoolean(true);
            }
            thread->RawSet(-3);             // arguments[key] = value (or true)

            thread->PushNumber(static_cast<double>(count));
            thread->PushString(argument.ToChar());
            thread->RawSet(-3);             // arguments[count] = the raw argument
            ++count;
        }
    }
    thread->PushString("count");
    thread->PushNumber(static_cast<double>(count));
    thread->RawSet(-3);
    thread->RawSet(lua5::kRegistryIndex);   // registry[light userdata thread] = the argument table
    return true;
}

// @ 0x1006463fa
void cGZLuaScriptServer::PostScriptChangeMessage(int scriptId, std::uint32_t type, int data2) {
    if (!g_host) return;
    cIGZMessageServer2* server = g_host->GetMessageServer();
    if (!server) return;
    auto* message = new cRZMessage2Standard();
    message->AddRef();
    message->SetType(type);
    // Both payloads are ints in the original and are sign-extended into the 64-bit message fields (movslq).
    message->SetData1(static_cast<std::uint64_t>(static_cast<std::int64_t>(scriptId)));
    message->SetData2(static_cast<std::uint64_t>(static_cast<std::int64_t>(data2)));
    server->MessagePost(message, false);
    message->Release();
    server->Release();
}

// @ 0x1006464fa
int cGZLuaScriptServer::RunScriptFile(const cIGZString& path, const cIGZString& arguments) {
    cRZString text;
    if (!ReadFileIntoString(cRZString(path), text)) return 0;
    return RunScriptString(text, path, arguments);
}

// @ 0x10064675a
bool cGZLuaScriptServer::KillScript(int scriptId) {
    auto found = threads_.find(scriptId);
    if (found == threads_.end()) return false;
    LuaThreadData* data = found->second;
    if (!data) return false;
    if (lua_ && data->thread) lua_->DestroyThread(data->thread->GetID());
    int threadId = data->thread ? data->thread->GetID() : scriptId;
    PostScriptChangeMessage(threadId, kLuaScriptDestroyedMessage, 0);
    if (data->thread) {
        data->thread->Release();
        data->thread = nullptr;
    }
    delete data;
    threads_.erase(found);
    return true;
}

// @ 0x10064683a
int cGZLuaScriptServer::PauseScript(std::uint32_t scriptId) {
    if (scriptId == 0xffffffffu) {
        std::vector<int> ids;
        EnumerateScriptIDs(ids);
        for (int id : ids) PauseScript(static_cast<std::uint32_t>(id));
        return 1;
    }
    LuaThreadData* data = FindThread(static_cast<int>(scriptId));
    if (!data) return 0;
    int previous = data->pauseCount;
    int count = previous + 1;
    data->pauseCount = count;
    if (previous == 0) {
        PostScriptChangeMessage(static_cast<int>(scriptId), kLuaScriptPausedMessage, 1);
        count = data->pauseCount;
    }
    return count;
}

// @ 0x100646944
int cGZLuaScriptServer::ResumeScript(std::uint32_t scriptId) {
    if (scriptId == 0xffffffffu) {
        std::vector<int> ids;
        EnumerateScriptIDs(ids);
        for (int id : ids) ResumeScript(static_cast<std::uint32_t>(id));
        return 1;
    }
    LuaThreadData* data = FindThread(static_cast<int>(scriptId));
    if (!data) return 0;
    int count = data->pauseCount - 1;
    data->pauseCount = count;
    if (count == 0) {
        PostScriptChangeMessage(static_cast<int>(scriptId), kLuaScriptPausedMessage, 0);
        ++data->resumeCount;
        count = data->pauseCount;
    }
    return count;
}

// 0x100646ade
cIGZLuaScriptServer::BreakpointEntry* cGZLuaScriptServer::GetBreakpointEntry(
    LuaThreadData* data, const cIGZString& script, int line, bool create, bool& created) {
    created = false;
    if (!data) return nullptr;
    for (BreakpointEntry& entry : data->breakpoints) {
        if (entry.name.IsEqualToStringRef(script, false) && entry.line == line) return &entry;
    }
    if (create) {
        BreakpointEntry entry;
        entry.scriptId = data->thread ? data->thread->GetID() : -1;
        entry.name.AssignFromIGZString(script);
        entry.line = line;
        entry.enabled = true;
        data->breakpoints.push_back(entry);
        created = true;
        return &data->breakpoints.back();
    }
    return nullptr;
}

// @ 0x100646a52
bool cGZLuaScriptServer::GetBreakpointExists(std::uint32_t scriptId, const cIGZString& script, int line) {
    LuaThreadData* data = FindThread(static_cast<int>(scriptId));
    if (!data) return false;
    for (const BreakpointEntry& entry : data->breakpoints) {
        if (entry.name.IsEqualToStringRef(script, false) && entry.line == line) return true;
    }
    return false;
}

// @ 0x100646c3c
bool cGZLuaScriptServer::SetBreakpointExists(std::uint32_t scriptId, const cIGZString& script, int line,
                                             bool remove) {
    LuaThreadData* data = FindThread(static_cast<int>(scriptId));
    if (!data) return false;
    bool changed = false;
    if (remove) {
        RemoveBreakpointEntry(data, script, line, changed);
    } else {
        BreakpointEntry* entry = GetBreakpointEntry(data, script, line, true, changed);
        if (entry) entry->enabled = true;
    }
    if (changed) PostBreakpointChangedMessage(scriptId);
    return true;
}

// @ 0x100646cca
bool cGZLuaScriptServer::RemoveBreakpointEntry(LuaThreadData* data, const cIGZString& script, int line,
                                               bool& removed) {
    removed = false;
    if (!data) return false;
    for (auto it = data->breakpoints.begin(); it != data->breakpoints.end(); ++it) {
        if (it->name.IsEqualToStringRef(script, false) && it->line == line) {
            data->breakpoints.erase(it);
            removed = true;
            return true;
        }
    }
    return false;
}

// @ 0x100646d42
void cGZLuaScriptServer::PostBreakpointChangedMessage(std::uint32_t scriptId) {
    if (!g_host) return;
    cIGZMessageServer2* server = g_host->GetMessageServer();
    if (!server) return;
    auto* message = new cRZMessage2Standard();
    message->AddRef();
    message->SetType(kLuaBreakpointChangedMessage);
    message->SetData1(scriptId);
    server->MessagePost(message, false);
    message->Release();
    server->Release();
}

// @ 0x100646e22
bool cGZLuaScriptServer::ToggleBreakpointExists(std::uint32_t scriptId, const cIGZString& script, int line) {
    LuaThreadData* data = FindThread(static_cast<int>(scriptId));
    if (!data) return false;
    bool changed = false;
    RemoveBreakpointEntry(data, script, line, changed);
    if (!changed) {
        GetBreakpointEntry(data, script, line, true, changed);
        if (!changed) return true;   // could not create: the original returns without a message
    }
    PostBreakpointChangedMessage(scriptId);
    return true;
}

// @ 0x100646ed8
bool cGZLuaScriptServer::GetBreakpointEnabled(std::uint32_t scriptId, const cIGZString& script, int line) {
    LuaThreadData* data = FindThread(static_cast<int>(scriptId));
    if (!data) return false;
    for (const BreakpointEntry& entry : data->breakpoints) {
        if (entry.name.IsEqualToStringRef(script, false) && entry.line == line) return entry.enabled;
    }
    return false;
}

// @ 0x100646f6a
bool cGZLuaScriptServer::SetBreakpointEnabled(std::uint32_t scriptId, const cIGZString& script, int line,
                                              bool enabled) {
    LuaThreadData* data = FindThread(static_cast<int>(scriptId));
    if (!data) return false;
    bool changed = false;
    BreakpointEntry* entry = GetBreakpointEntry(data, script, line, true, changed);
    if (!entry || entry->enabled == enabled) {
        if (changed) PostBreakpointChangedMessage(scriptId);
        return true;
    }
    entry->enabled = enabled;
    PostBreakpointChangedMessage(scriptId);
    return true;
}

// @ 0x100647004
bool cGZLuaScriptServer::GetBreakpointCondition(std::uint32_t scriptId, const cIGZString& script, int line,
                                                cIGZString& out) {
    out.Resize(0);
    LuaThreadData* data = FindThread(static_cast<int>(scriptId));
    if (!data) return false;
    for (const BreakpointEntry& entry : data->breakpoints) {
        if (entry.name.IsEqualToStringRef(script, false) && entry.line == line) {
            out.AssignFromChars(entry.condition.Data(), entry.condition.Strlen());
            return true;
        }
    }
    return false;
}

// @ 0x1006470de
bool cGZLuaScriptServer::SetBreakpointCondition(std::uint32_t scriptId, const cIGZString& script, int line,
                                                const cIGZString& condition) {
    LuaThreadData* data = FindThread(static_cast<int>(scriptId));
    if (!data) return false;
    bool changed = false;
    BreakpointEntry* entry = GetBreakpointEntry(data, script, line, true, changed);
    if (entry && entry->condition.IsEqualToStringRef(condition, true)) {
        // Original quirk (0x10064715e): the assignment only happens when both strings are already equal.
        entry->condition.AssignFromIGZString(condition);
        changed = true;
    }
    if (!changed) return true;
    PostBreakpointChangedMessage(scriptId);
    return true;
}

// @ 0x100647194
bool cGZLuaScriptServer::GetBreakpointEntryList(std::uint32_t scriptId, const cIGZString& script,
                                                std::list<BreakpointEntry>& out) {
    out.clear();
    LuaThreadData* data = FindThread(static_cast<int>(scriptId));
    if (!data) return false;
    for (const BreakpointEntry& entry : data->breakpoints) {
        if (entry.name.IsEqualToStringRef(script, false)) out.push_back(entry);
    }
    return true;
}

// @ 0x1006473b0
bool cGZLuaScriptServer::ShouldBreak(LuaThreadData* data) const {
    if (!data || data->breakpoints.empty() || !g_host) return false;
    cRZString name;
    int line = 0;
    // The original ignores the debug-info result and compares whatever name/line were left in the locals.
    g_host->GetCurrentScriptNameAndLine(data->thread, 0, name, line);
    for (const BreakpointEntry& entry : data->breakpoints) {
        // The original ignores BreakpointEntry::enabled here.
        if (entry.name.IsEqualToStringRef(name, false) && entry.line == line) return true;
    }
    return false;
}

// @ 0x100647486
bool cGZLuaScriptServer::OnTick(std::uint32_t /*elapsedMs*/) {
    const int depth = tickDepth_++;
    if (depth == 0) {
        for (auto it = threads_.begin(); it != threads_.end();) {
            LuaThreadData* data = it->second;
            if (!data || data->pauseCount >= 1 || !data->started) {
                ++it;
                continue;
            }
            lua5::cGZLua5Thread* thread = data->thread;
            if (!thread) {
                ++it;
                continue;
            }
            int status = thread->Status();
            if (status == 1 && !ShouldBreak(data)) {
                currentRunning_ = data;
                if (lua_) lua_->ResumeThread(thread->GetID(), 0, nullptr);
                currentRunning_ = nullptr;
            }
            status = thread->Status();
            if (status == 1) {
                ++it;
                continue;
            }
            const int threadId = thread->GetID();
            PostScriptChangeMessage(threadId, kLuaScriptDestroyedMessage, 0);
            if (status == 0 && lua_) lua_->DestroyThread(threadId);
            thread->Release();
            delete data;
            it = threads_.erase(it);
        }
    }
    --tickDepth_;
    return true;
}

// @ 0x100647684
void cGZLuaScriptServer::EnumerateScriptIDs(std::vector<int>& out) {
    for (const auto& entry : threads_) out.push_back(entry.first);
}

// @ 0x10064771a
bool cGZLuaScriptServer::GetScriptInfo(int scriptId, lua5::cGZLua5Thread** thread, cIGZString* script,
                                       cIGZString* chunkNameOut, int* pauseCount, int* resumeCount,
                                       bool* started) {
    LuaThreadData* data = FindThread(scriptId);
    if (!data) return false;
    if (thread) {
        *thread = data->thread;
        if (data->thread) data->thread->AddRef();
    }
    if (script) script->AssignFromIGZString(data->sourceText);
    if (chunkNameOut) chunkNameOut->AssignFromIGZString(data->chunkName);
    if (pauseCount) *pauseCount = data->pauseCount;
    if (resumeCount) *resumeCount = data->resumeCount;
    if (started) *started = data->started;
    return true;
}

// @ 0x100647814
int cGZLuaScriptServer::GetCurrentRunningScriptID() const {
    if (currentRunning_ && currentRunning_->thread) return currentRunning_->thread->GetID();
    return -1;
}

// @ 0x100647838
bool cGZLuaScriptServer::GetScriptAutoYieldEnabled(int scriptId) const {
    LuaThreadData* data = FindThread(scriptId);
    return data ? data->autoYield : false;
}

// @ 0x100647882
bool cGZLuaScriptServer::SetScriptAutoYieldEnabled(int scriptId, bool enabled) {
    LuaThreadData* data = FindThread(scriptId);
    if (!data) return false;
    data->autoYield = enabled;
    return true;
}

// @ 0x1006478ca
int cGZLuaScriptServer::ScriptArgIndexEventHandler(lua5::lua_State* state) {
    if (!g_host || !state) return 0;
    lua5::cGZLua5Thread* thread = g_host->ThreadForState(state);
    if (!thread) return 0;
    if (thread->GetTop() <= 0) return 0;
    thread->PushLightUserData(thread);
    thread->RawGet(lua5::kRegistryIndex);
    if (thread->Type(-1) == lua5::kLuaTypeTable) {
        thread->PushValue(-2);
        thread->RawGet(-2);
    } else {
        thread->PushNil();
    }
    return 1;
}

// @ 0x10064795e
int cGZLuaScriptServer::GetLastCreatedScriptID() const { return lastCreatedScriptId_; }

// @ 0x10064cd42
lua5::cGZLua5* cGZLuaScriptServer::GetLua() { return lua_; }

// @ 0x1006473b0 helper
cGZLuaScriptServer::LuaThreadData* cGZLuaScriptServer::FindThread(int scriptId) const {
    auto found = threads_.find(scriptId);
    return found == threads_.end() ? nullptr : found->second;
}

// @ 0x100643f90: GZCOM_CreatecGZLuaScriptServer() — operator new(0x70) plus the constructor. This strong
// definition replaces the weak fallback GZCOM.cpp emits for classes without an implementation.
namespace gzcom {
void* GZCOM_CreatecGZLuaScriptServer() { return static_cast<void*>(new cGZLuaScriptServer()); }
}  // namespace gzcom
