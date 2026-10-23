// Tests cGZLuaScriptServer against MiniLua: the script lifecycle (load, coroutine creation, argument table),
// breakpoints (including the two reversed/missing-comparison quirks), pause/resume counts, the tick loop with
// ShouldBreak and auto-finalisation, the message protocol, the yield hook and RunScriptFile.
#include <cstdio>
#include <fstream>
#include <list>
#include <string>
#include <vector>

#include "gz/GZCOM.h"
#include "gz/LuaScriptServer.h"
#include "lua_minilua.h"

using namespace lua5;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static cGZLuaScriptServer* g_server = nullptr;

static int AlertFn(lua_State*) { return 0; }

// ---------------------------------------------------------------------------------------------------------
// fakes
// ---------------------------------------------------------------------------------------------------------
struct FakeCommandServer final : cIGZCommandServer {
    std::uint32_t refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    void CreateCommandParameterSet(cIGZCommandParameterSet** out) override { if (out) *out = nullptr; }
    bool RegisterCommand(std::uint32_t, const char*, const char*) override { return true; }
    bool RegisterCommands(const cGZCommandInfoEntry*, std::uint32_t) override { return true; }
    bool RegisterCommandParameterInfo(std::uint32_t, const char*) override { return true; }
    bool UnregisterCommand(std::uint32_t) override { return true; }
    bool UnregisterCommands(const cGZCommandInfoEntry*, std::uint32_t) override { return true; }
    bool GetCommandInfo(std::uint32_t, cIGZString&, cIGZString*) const override { return false; }
    std::uint32_t GetCommandID(const cIGZString&) const override { return 0; }
    std::uint32_t GetCommandIDFromCommandString(const char*, std::uint32_t) const override { return 0; }
    void EnumerateCommands(std::vector<std::uint32_t>&) const override {}
    bool RegisterCommandGenerator(cIGZCommandGenerator*) override { return true; }
    bool UnregisterCommandGenerator(cIGZCommandGenerator*) override { return true; }
    cIGZCommandDispatcher* GetCommandDispatcher() const override { return nullptr; }
    bool SetCommandDispatcher(cIGZCommandDispatcher*) override { return true; }
    std::uint32_t ExecuteCommand(std::uint32_t, cIGZCommandParameterSet*, cIGZCommandParameterSet*) override { return 0; }
    std::uint32_t ExecuteCommand(const char*, std::uint32_t, cIGZCommandParameterSet*) override { return 0; }
    std::uint32_t ExecuteCommandFormatted(const char*, cIGZCommandParameterSet*, ...) override { return 0; }
    bool ConvertStringToCommand(const char*, std::uint32_t, std::uint32_t&, cIGZCommandParameterSet*&) override { return false; }
    bool ConvertCommandToString(std::uint32_t, cIGZCommandParameterSet*, cIGZString&) override { return false; }
    bool ConvertCommandOutputToString(cIGZCommandParameterSet*, cIGZString&) override { return false; }
};

struct MessageRecord {
    std::uint32_t type = 0;
    std::uint64_t data1 = 0;
    std::uint64_t data2 = 0;
};

struct FakeMessageServer final : cIGZMessageServer2 {
    std::uint32_t refs = 1;
    std::vector<MessageRecord> messages;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool MessageSend(cIGZMessage2*) override { return true; }
    bool MessagePost(cIGZMessage2* message, bool) override {
        auto* standard = static_cast<cIGZMessage2Standard*>(message);
        messages.push_back({message->GetType(), standard->GetData1(), standard->GetData2()});
        return true;
    }
    bool AddNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool RemoveNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool GeneralMessagePostToTarget(cIGZMessage2*, cIGZMessageTarget2*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return refs; }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }
};

struct FakeGenerator final : ILuaCommandGeneratorApi {
    std::uint32_t refs = 0;
    int inits = 0, shutdowns = 0;
    cIGZCommandServer* lastServer = nullptr;
    lua5::cGZLua5* lastLua = nullptr;
    std::string lastTableName;
    bool accept = true;
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init(cIGZCommandServer* server, lua5::cGZLua5* lua, const cRZString& tableName) override {
        ++inits;
        lastServer = server;
        lastLua = lua;
        lastTableName = tableName.ToChar();
        return accept;
    }
    void Shutdown() override { ++shutdowns; }
};

struct Factory final : IThreadFactory {
    MiniLua& api;
    LuaCompat& compat;
    int created = 0;
    Factory(MiniLua& a, LuaCompat& c) : api(a), compat(c) {}
    cGZLua5Thread* CreateThreadObject(unsigned) override {
        ++created;
        auto* thread = new cGZLua5Thread(api, compat);
        thread->AddRef();
        return thread;
    }
};

struct Host final : ILuaScriptServerHost {
    MiniLua* api = nullptr;
    LuaCompat* compat = nullptr;
    Factory* factory = nullptr;
    FakeCommandServer* commandServer = nullptr;
    FakeMessageServer* messageServer = nullptr;
    FakeGenerator* generator = nullptr;
    lua5::cGZLua5* lua = nullptr;

    int added = 0, removed = 0;
    cRZString debugName;
    int debugLine = 0;
    int debugCalls = 0;
    bool yieldable = true;
    int hookLine = 0;
    int yields = 0;
    lua_State* hookState = nullptr;
    void (*hookFn)(lua_State*, LuaDebug*) = nullptr;
    int hookMask = 0;

    lua5::cGZLua5* CreateLua() override {
        lua = new cGZLua5(*api, *compat, factory, "");
        lua->AddRef();
        return lua;
    }
    ILuaCommandGeneratorApi* CreateCommandGenerator() override {
        generator->AddRef();
        return generator;
    }
    cIGZCommandServer* GetCommandServer() override { return commandServer->AddRef(), commandServer; }
    cIGZMessageServer2* GetMessageServer() override { return messageServer->AddRef(), messageServer; }
    void AddToTick(cIGZSystemService*) override { ++added; }
    void RemoveFromTick(cIGZSystemService*) override { ++removed; }
    bool GetCurrentScriptNameAndLine(cGZLua5Thread*, int, cRZString& name, int& line) override {
        ++debugCalls;
        name = debugName;
        line = debugLine;
        return true;
    }
    cGZLua5Thread* ThreadForState(lua_State* state) override { return api->get_thread_slot(state); }
    void SetLuaHook(lua_State* state, void (*hook)(lua_State*, LuaDebug*), int mask) override {
        hookState = state;
        hookFn = hook;
        hookMask = mask;
    }
    void GetLuaInfoLine(lua_State*, LuaDebug* ar) override { ar->currentline = hookLine; }
    bool IsLuaYieldable(lua_State*) override { return yieldable; }
    void YieldLua(lua_State*) override { ++yields; }
};

// Body of every loaded chunk. Records the running script id (set only around ResumeThread), recurses into
// OnTick once to check the re-entry guard, and yields on the first run so the script stays alive.
struct ChunkState {
    int calls = 0;
    int runningIds[8] = {};
    int runningCount = 0;
    bool innerTickResult = true;
    int innerTicks = 0;
};
static ChunkState g_chunk;
static int ChunkBody(lua_State* L) {
    ++g_chunk.calls;
    if (g_server && g_chunk.runningCount < 8) {
        g_chunk.runningIds[g_chunk.runningCount++] = g_server->GetCurrentRunningScriptID();
    }
    if (g_server) {
        ++g_chunk.innerTicks;
        g_chunk.innerTickResult = g_server->OnTick(0) && g_chunk.innerTickResult;
    }
    L->yielded = g_chunk.calls == 1;   // the first resume parks the coroutine
    return 0;
}

// ---------------------------------------------------------------------------------------------------------
int main() {
    MiniLua api;
    SetLuaApi(&api);
    LuaCompat compat(AlertFn);
    Factory factory(api, compat);
    FakeCommandServer commandServer;
    FakeMessageServer messageServer;
    FakeGenerator generator;
    Host host;
    host.api = &api;
    host.compat = &compat;
    host.factory = &factory;
    host.commandServer = &commandServer;
    host.messageServer = &messageServer;
    host.generator = &generator;
    SetLuaScriptServerHost(&host);
    api.chunkBody = ChunkBody;

    // ---- Init failure path: a refusing command generator tears the VM down again --------------------------
    {
        FakeCommandServer failCommandServer;
        FakeMessageServer failMessageServer;
        FakeGenerator reject;
        reject.accept = false;
        Host failHost;
        failHost.api = &api;
        failHost.compat = &compat;
        failHost.factory = &factory;
        failHost.commandServer = &failCommandServer;
        failHost.messageServer = &failMessageServer;
        failHost.generator = &reject;
        SetLuaScriptServerHost(&failHost);
        api.closed.clear();
        auto* failing = new cGZLuaScriptServer();
        failing->AddRef();
        CHECK(!failing->Init());
        CHECK(failHost.added == 0);                 // no tick registration on failure
        CHECK(reject.inits == 1 && reject.refs == 0);
        CHECK(!api.closed.empty());                 // the Lua VM was closed again
        failing->Release();
        // Quirk: Init set the initialised flag before failing, so Shutdown still removes a tick client that
        // was never added.
        CHECK(failHost.removed == 1);
        SetLuaScriptServerHost(&host);
        api.libs.clear();   // the failed VM opened its libraries too; the main check below counts one open
    }

    auto* server = static_cast<cGZLuaScriptServer*>(gzcom::GZCOM_CreatecGZLuaScriptServer());
    g_server = server;
    server->AddRef();
    CHECK(server->GetServiceID() == kGZSID_cGZLuaScriptServer);
    CHECK(server->GetServicePriority() == kGZLuaScriptServerPriority);
    CHECK(server->GetLua() == nullptr);
    CHECK(server->GetLastCreatedScriptID() == -1);
    CHECK(server->GetCurrentRunningScriptID() == -1);

    void* raw = nullptr;
    CHECK(server->QueryInterface(kGZIID_cIGZLuaScriptServer, &raw) &&
          raw == static_cast<cIGZLuaScriptServer*>(server));
    server->Release();
    CHECK(server->QueryInterface(kGZIID_cIGZUnknown, &raw) && raw == static_cast<cIGZLuaScriptServer*>(server));
    server->Release();
    CHECK(!server->QueryInterface(0x12345678, &raw));
    CHECK(server->QueryInterface(kGZIID_cIGZSystemService, &raw) &&
          raw == static_cast<cIGZSystemService*>(static_cast<cRZSystemService*>(server)));
    server->Release();

    // ---- Init: Lua VM, command generator, arg table, tick registration ------------------------------------
    CHECK(server->Init());
    CHECK(host.added == 1 && host.removed == 0);
    CHECK(server->GetLua() == host.lua);
    CHECK(factory.created == 0);
    CHECK(generator.inits == 1 && generator.lastServer == &commandServer && generator.lastLua == host.lua);
    CHECK(generator.lastTableName == "sc4app");
    CHECK(api.libs == std::vector<int>({0, 1, 2, 3, 4, 5}));
    CHECK(server->Init());   // second call only re-runs ScriptArgSetup
    CHECK(host.added == 1);

    lua_State* main = host.lua->GetContext();
    CHECK(main != nullptr);
    Val argValue = MiniLua::Get(*main->globals, MiniLua::Str("arg"));
    CHECK(argValue.t == Val::UData);
    CHECK(argValue.ud && argValue.ud->meta);
    if (argValue.ud && argValue.ud->meta) {
        Val index = MiniLua::Get(*argValue.ud->meta, MiniLua::Str("__index"));
        CHECK(index.t == Val::CFn && index.fn &&
              index.fn->cfn == &cGZLuaScriptServer::ScriptArgIndexEventHandler);
    }

    // ---- RunScriptString ----------------------------------------------------------------------------------
    cRZString source("return 1");
    cRZString chunkName("testchunk");
    cRZString arguments("-foo:bar baz --- /qux:1 empty:");
    int script1 = server->RunScriptString(source, chunkName, arguments);
    CHECK(script1 == 1);
    CHECK(server->GetLastCreatedScriptID() == 1);
    CHECK(host.hookFn == &cGZLuaScriptServer::LuaYieldHook && host.hookMask == 4);
    CHECK(messageServer.messages.size() == 1);
    CHECK(messageServer.messages.back().type == kLuaScriptCreatedMessage &&
          messageServer.messages.back().data1 == 1 && messageServer.messages.back().data2 == 0);
    Val chunkGlobal = MiniLua::Get(*main->globals, MiniLua::Str("Main - \"testchunk\" - 1"));
    CHECK(chunkGlobal.t == Val::LuaFn);
    CHECK(host.hookState != nullptr);

    std::vector<int> ids;
    server->EnumerateScriptIDs(ids);
    CHECK(ids == std::vector<int>({1}));

    cGZLua5Thread* thread = nullptr;
    cRZString infoSource, infoChunk;
    int pauseCount = -1, resumeCount = -1;
    bool started = false;
    CHECK(server->GetScriptInfo(1, &thread, &infoSource, &infoChunk, &pauseCount, &resumeCount, &started));
    CHECK(thread != nullptr && thread->GetID() == 1);
    CHECK(std::string(infoSource.ToChar()) == "return 1");
    CHECK(std::string(infoChunk.ToChar()) == "testchunk");
    CHECK(pauseCount == 0 && resumeCount == 0 && started);
    CHECK(host.hookState == thread->GetContext());
    CHECK(server->GetCurrentRunningScriptID() == -1);
    CHECK(!server->GetScriptInfo(99, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));

    auto* threadData = static_cast<cGZLuaScriptServer::LuaThreadData*>(thread->GetUserContext());
    CHECK(threadData != nullptr);
    if (threadData) {
        CHECK(threadData->thread == thread && threadData->started && !threadData->autoYield);
        CHECK(threadData->currentLine == -1 && threadData->breakpoints.empty());
        // argument table: [0] = chunk name, parsed "foo" = "bar", [1] = "-foo:bar", "baz" = true, [2], count.
        Val key;
        key.t = Val::Light;
        key.p = thread;
        Val table = MiniLua::Get(*main->registry, key);
        CHECK(table.t == Val::Table_);
        if (table.t == Val::Table_) {
            CHECK(MiniLua::Get(*table.tab, MiniLua::Num(0)).s == "testchunk");
            CHECK(MiniLua::Get(*table.tab, MiniLua::Str("foo")).s == "bar");
            CHECK(MiniLua::Get(*table.tab, MiniLua::Num(1)).s == "-foo:bar");
            Val baz = MiniLua::Get(*table.tab, MiniLua::Str("baz"));
            CHECK(baz.t == Val::Bool && baz.b);
            CHECK(MiniLua::Get(*table.tab, MiniLua::Num(2)).s == "baz");
            CHECK(MiniLua::Get(*table.tab, MiniLua::Str("qux")).s == "1");
            CHECK(MiniLua::Get(*table.tab, MiniLua::Num(3)).s == "/qux:1");
            CHECK(MiniLua::Get(*table.tab, MiniLua::Str("empty")).s.empty());   // "empty:" -> explicit ""
            CHECK(MiniLua::Get(*table.tab, MiniLua::Num(4)).s == "empty:");
            CHECK(MiniLua::Get(*table.tab, MiniLua::Str("count")).n == 5.0);      // "---" is skipped
            CHECK(table.tab->kv.size() == 10);
        }
    }

    // auto-yield flag
    CHECK(!server->GetScriptAutoYieldEnabled(1));
    CHECK(server->SetScriptAutoYieldEnabled(1, true));
    CHECK(server->GetScriptAutoYieldEnabled(1));
    CHECK(!server->GetScriptAutoYieldEnabled(99));
    CHECK(!server->SetScriptAutoYieldEnabled(99, true));
    CHECK(server->SetScriptAutoYieldEnabled(1, false));

    // ---- breakpoints --------------------------------------------------------------------------------------
    cRZString bp("bp.lua");
    std::size_t baseMessages = messageServer.messages.size();
    CHECK(server->SetBreakpointExists(1, bp, 10, false));
    CHECK(messageServer.messages.size() == baseMessages + 1);
    CHECK(messageServer.messages.back().type == kLuaBreakpointChangedMessage &&
          messageServer.messages.back().data1 == 1);
    CHECK(server->GetBreakpointExists(1, bp, 10));
    CHECK(server->GetBreakpointEnabled(1, bp, 10));   // created enabled

    CHECK(server->SetBreakpointEnabled(1, bp, 10, false));
    CHECK(!server->GetBreakpointEnabled(1, bp, 10));
    CHECK(server->GetBreakpointExists(1, bp, 10));    // exists even while disabled
    CHECK(server->SetBreakpointEnabled(1, bp, 10, true));
    CHECK(server->GetBreakpointEnabled(1, bp, 10));

    // SetBreakpointExists on an existing disabled entry re-enables it but reports no change (changed is only
    // set when GetBreakpointEntry creates the entry).
    CHECK(server->SetBreakpointEnabled(1, bp, 10, false));
    baseMessages = messageServer.messages.size();
    CHECK(server->SetBreakpointExists(1, bp, 10, false));
    CHECK(server->GetBreakpointEnabled(1, bp, 10));
    CHECK(messageServer.messages.size() == baseMessages);

    cRZString condition;
    CHECK(server->GetBreakpointCondition(1, bp, 10, condition));
    CHECK(condition.Strlen() == 0);
    baseMessages = messageServer.messages.size();
    CHECK(server->SetBreakpointCondition(1, bp, 10, cRZString("locked")));
    CHECK(messageServer.messages.size() == baseMessages);   // different text: silently ignored
    CHECK(server->GetBreakpointCondition(1, bp, 10, condition));
    CHECK(condition.Strlen() == 0);
    CHECK(server->SetBreakpointCondition(1, bp, 10, cRZString("")));   // equal: posts, but still empty
    CHECK(messageServer.messages.size() == baseMessages + 1);

    std::list<cIGZLuaScriptServer::BreakpointEntry> entries;
    CHECK(server->GetBreakpointEntryList(1, bp, entries));
    CHECK(entries.size() == 1 && entries.front().line == 10 && entries.front().enabled);
    CHECK(server->GetBreakpointEntryList(99, bp, entries) == false && entries.empty());

    CHECK(server->ToggleBreakpointExists(1, bp, 10));   // removes
    CHECK(!server->GetBreakpointExists(1, bp, 10));
    CHECK(server->ToggleBreakpointExists(1, bp, 10));   // creates
    CHECK(server->GetBreakpointExists(1, bp, 10));
    CHECK(server->SetBreakpointExists(1, bp, 10, true));   // quirk: true removes
    CHECK(!server->GetBreakpointExists(1, bp, 10));
    CHECK(server->SetBreakpointExists(1, bp, 10, false));
    CHECK(server->GetBreakpointExists(1, bp, 10));
    CHECK(!server->SetBreakpointExists(99, bp, 10, false));
    CHECK(!server->GetBreakpointExists(99, bp, 10));
    CHECK(!server->GetBreakpointEnabled(99, bp, 10));

    // ---- pause / resume -----------------------------------------------------------------------------------
    CHECK(server->PauseScript(1) == 1);
    CHECK(messageServer.messages.back().type == kLuaScriptPausedMessage &&
          messageServer.messages.back().data2 == 1);
    CHECK(server->PauseScript(1) == 2);
    CHECK(server->ResumeScript(1) == 1);
    CHECK(server->ResumeScript(1) == 0);
    CHECK(messageServer.messages.back().type == kLuaScriptPausedMessage &&
          messageServer.messages.back().data2 == 0);
    CHECK(server->GetScriptInfo(1, nullptr, nullptr, nullptr, &pauseCount, &resumeCount, nullptr));
    CHECK(pauseCount == 0 && resumeCount == 1);
    CHECK(server->PauseScript(0xffffffffu) == 1);
    CHECK(server->ResumeScript(0xffffffffu) == 1);
    CHECK(server->GetScriptInfo(1, nullptr, nullptr, nullptr, &pauseCount, &resumeCount, nullptr));
    CHECK(pauseCount == 0 && resumeCount == 2);
    CHECK(server->PauseScript(99) == 0 && server->ResumeScript(99) == 0);

    // ---- OnTick: ShouldBreak blocks the resume (and ignores the enabled flag) ------------------------------
    host.debugName = cRZString("bp.lua");
    host.debugLine = 42;
    CHECK(server->SetBreakpointExists(1, cRZString("bp.lua"), 42, false));
    CHECK(server->SetBreakpointEnabled(1, cRZString("bp.lua"), 42, false));
    int debugBefore = host.debugCalls;
    int callsBefore = g_chunk.calls;
    CHECK(server->OnTick(16));
    CHECK(host.debugCalls > debugBefore);
    CHECK(g_chunk.calls == callsBefore);   // not resumed
    CHECK(server->GetScriptInfo(1, &thread, nullptr, nullptr, nullptr, nullptr, nullptr));
    CHECK(thread && thread->Status() == 1);

    CHECK(server->SetBreakpointExists(1, cRZString("bp.lua"), 42, true));   // remove the matching breakpoint
    // Remove the line-10 entry too so the list is empty (ShouldBreak's fast path).
    CHECK(server->SetBreakpointExists(1, bp, 10, true));
    CHECK(server->OnTick(16));
    CHECK(g_chunk.calls == callsBefore + 1);
    CHECK(g_chunk.runningIds[0] == 1);          // GetCurrentRunningScriptID during ResumeThread
    CHECK(g_chunk.innerTicks == 1 && g_chunk.innerTickResult);   // recursive OnTick is a no-op
    CHECK(thread && thread->Status() == 1);     // first body run yielded
    CHECK(server->OnTick(16));                  // second run finishes: the script is finalised
    CHECK(g_chunk.calls == callsBefore + 1);    // MiniLua's resume of a yielded body completes it
    CHECK(messageServer.messages.back().type == kLuaScriptDestroyedMessage &&
          messageServer.messages.back().data1 == 1);
    CHECK(!server->GetScriptInfo(1, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
    ids.clear();
    server->EnumerateScriptIDs(ids);
    CHECK(ids.empty());
    CHECK(!server->KillScript(1));
    if (thread) thread->Release();

    // ---- RunScriptFile ------------------------------------------------------------------------------------
    cRZString missing("/tmp/gz_luascriptserver_missing.lua");
    CHECK(server->RunScriptFile(missing, cRZString("")) == 0);
    const char* path = "/tmp/gz_luascriptserver_test.lua";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "return 42\n";
    }
    cRZString pathString(path);
    int script2 = server->RunScriptFile(pathString, cRZString(""));
    CHECK(script2 == 2);
    CHECK(server->GetLastCreatedScriptID() == 2);
    CHECK(factory.created == 2);
    cRZString fileSource, fileChunk;
    CHECK(server->GetScriptInfo(2, nullptr, &fileSource, &fileChunk, nullptr, nullptr, nullptr));
    CHECK(std::string(fileSource.ToChar()) == "return 42\n");
    CHECK(std::string(fileChunk.ToChar()) == path);
    std::remove(path);
    CHECK(server->KillScript(2));
    CHECK(!server->KillScript(2));
    // Quirk (0x1006467d4): KillScript posts the id read *after* DestroyThread, which OnDestroy resets to -1.
    CHECK(messageServer.messages.back().type == kLuaScriptDestroyedMessage &&
          messageServer.messages.back().data1 == 0xffffffffffffffffull);

    // ---- LuaYieldHook / ScriptArgIndexEventHandler --------------------------------------------------------
    g_server = nullptr;   // the handler tests must not recurse into OnTick
    lua_State* isolated = api.NewState();
    isolated->registry = main->registry;
    isolated->globals = main->globals;
    isolated->mainState = main;
    cGZLua5Thread hookThread(api, compat);
    CHECK(hookThread.OnCreate(nullptr, isolated, 7));
    cGZLuaScriptServer::LuaThreadData hookData;
    hookThread.SetUserContext(&hookData);
    api.set_thread_slot(isolated, &hookThread);

    // the argument table the hook handler reads out of the registry
    std::shared_ptr<Table> argTable = std::make_shared<Table>();
    MiniLua::Set(*argTable, MiniLua::Str("foo"), MiniLua::Str("bar"));
    Val key;
    key.t = Val::Light;
    key.p = &hookThread;
    MiniLua::Set(*main->registry, key, api.Tab(argTable));
    api.settop(isolated, 0);
    api.pushnil(isolated);
    api.pushstring(isolated, "foo");
    CHECK(cGZLuaScriptServer::ScriptArgIndexEventHandler(isolated) == 1);
    CHECK(api.gettop(isolated) == 4);
    CHECK(api.type(isolated, -1) == kLuaTypeString);
    CHECK(std::string(api.tostring(isolated, -1)) == "bar");
    api.settop(isolated, 0);
    CHECK(cGZLuaScriptServer::ScriptArgIndexEventHandler(isolated) == 0);   // empty stack
    api.set_thread_slot(isolated, nullptr);
    api.pushnil(isolated);
    api.pushstring(isolated, "foo");
    CHECK(cGZLuaScriptServer::ScriptArgIndexEventHandler(isolated) == 0);   // no thread slot
    api.settop(isolated, 0);

    // yield hook: only auto-yield scripts, only yieldable calls, only when the line changed
    api.set_thread_slot(isolated, &hookThread);
    hookData.autoYield = true;
    hookData.currentLine = -1;
    host.hookLine = 77;
    host.yieldable = true;
    LuaDebug debug{};
    cGZLuaScriptServer::LuaYieldHook(isolated, &debug);
    CHECK(hookData.currentLine == 77 && host.yields == 1);
    cGZLuaScriptServer::LuaYieldHook(isolated, &debug);
    CHECK(host.yields == 1);                     // same line
    host.hookLine = 88;
    hookData.autoYield = false;
    cGZLuaScriptServer::LuaYieldHook(isolated, &debug);
    CHECK(hookData.currentLine == 77 && host.yields == 1);
    hookData.autoYield = true;
    host.yieldable = false;
    cGZLuaScriptServer::LuaYieldHook(isolated, &debug);
    CHECK(hookData.currentLine == 77 && host.yields == 1);
    host.yieldable = true;
    cGZLuaScriptServer::LuaYieldHook(isolated, &debug);
    CHECK(hookData.currentLine == 88 && host.yields == 2);
    api.set_thread_slot(isolated, nullptr);
    host.hookLine = 99;
    cGZLuaScriptServer::LuaYieldHook(isolated, &debug);
    CHECK(host.yields == 2);

    // ---- Shutdown and deletion ----------------------------------------------------------------------------
    server->Release();   // FinalRelease: Shutdown, then delete
    CHECK(host.removed == 1);
    CHECK(generator.shutdowns == 1);
    CHECK(generator.refs == 0);
    CHECK(generator.lastLua == host.lua);

    SetLuaScriptServerHost(nullptr);
    std::printf(g_fail == 0 ? "gz_luascriptserver: all checks passed\n" : "gz_luascriptserver: %d failures\n",
                g_fail);
    return g_fail == 0 ? 0 : 1;
}
