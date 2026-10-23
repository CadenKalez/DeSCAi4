// cGZLuaScriptServer — the GZ script server (41 functions, 0x1006453b0..0x10064cd42; class id 0xcbd101a9,
// interface 0x4bd10129, service 0x4bd10133 priority -999999). It owns the Lua VM (cIGZLua5), loads script
// chunks on the main thread, turns each chunk into a coroutine (cIGZLua5Thread) and runs the debugger side:
// breakpoints, pause/resume counts, auto-yield and the script-change messages.
//
// Original layout (0x70 bytes): +0 cIGZLuaScriptServer vtable, +8 cRZSystemService (cIGZSystemService view
// at +8, cRZUnknown view at +0x10), +0x2c refcount, +0x30 initialised, +0x34 OnTick re-entry depth,
// +0x38 script counter (the "Main - name - N" chunk name), +0x3c last created script id (-1), +0x40 cIGZLua5,
// +0x48 cGZLua5CommandGenerator, +0x50 std::map<int, LuaThreadData*>, +0x68 currently running LuaThreadData.
//
// LuaThreadData (0xa8 bytes): +0 cIGZLua5Thread (counted), +8 script source (cRZString), +0x30 chunk name,
// +0x58 command-line arguments, +0x80 pause count, +0x84 resume count, +0x88 started, +0x89 auto-yield,
// +0x8c line last seen by the yield hook (-1), +0x90 std::list<BreakpointEntry>.
// BreakpointEntry (0x60 bytes): +0 script id, +8 script name, +0x30 line, +0x34 enabled, +0x38 condition.
//
// Seams (docs/SEAMS.md): the Lua VM and the command generator are made by GZCOM; the command/message servers
// and the tick registration come from cGZFramework; the debugger name/line and the raw lua_sethook /
// lua_getinfo / lua_yield calls come from Lua's debug API. All of that goes through ILuaScriptServerHost.
//
// Quirks kept from the binary (do not "fix"):
//  * SetBreakpointExists' last bool is really "remove": true removes the breakpoint and false creates it
//    (0x100646c8a).
//  * SetBreakpointCondition only assigns (and reports a change) when the new condition already equals the
//    stored one (0x10064715e); a different condition is silently ignored.
//  * ShouldBreak ignores the enabled flag, and KillScript posts the change message with the *thread id* read a
//    second time after DestroyThread, so the message carries 0xffffffff (0x1006467d4). OnTick posts before the
//    destroy and keeps the real id.
//  * OnTick ignores ticks while it is already running (re-entry depth at +0x34).
//  * Init sets the initialised flag before creating the VM, so a failed Init is later Shutdown'ed and removes a
//    tick client that was never added.
//  * A duplicate script id would dereference the released thread in the original (0x100645eb9); ids only grow,
//    so the port keeps the thread and overwrites the map slot instead of crashing.
#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <vector>

#include "gz/CommandServer.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZSystemService.h"
#include "lua/Lua5.h"
#include "rz/cRZSystemService.h"
#include "rz/cRZString.h"

constexpr std::uint32_t kGZSID_cGZLuaScriptServer = 0x4bd10133;
constexpr std::int32_t kGZLuaScriptServerPriority = -999999;
constexpr std::uint32_t kGZIID_cIGZLuaScriptServer = 0x4bd10129;

// Message types posted through the message server (cRZMessage2Standard: type, data1 = script id, data2 = state).
constexpr std::uint32_t kLuaScriptCreatedMessage = 0x6ba990d8;
constexpr std::uint32_t kLuaScriptDestroyedMessage = 0xcba990d3;
constexpr std::uint32_t kLuaScriptPausedMessage = 0xebaaf9be;      // data2 1 = paused, 0 = resumed
constexpr std::uint32_t kLuaBreakpointChangedMessage = 0x8bd13ee2;

// The library mask the server opens (io|string|math|debug|table|Lua4 compat = 0x7e) and the class id of the
// thread objects the original asks GZCOM for (0x2b2810f7).
constexpr int kLuaScriptOpenLibraries = 0x7e;
constexpr unsigned kLuaScriptThreadClassId = 0x2b2810f7;
// Table name handed to cGZLua5CommandGenerator::Init (the original builds cRZString("sc4app") inline).
constexpr const char* kLuaCommandGeneratorTableName = "sc4app";

// lua_Debug as Lua 5.0 lays it out on x86_64; the yield hook only reads currentline at +0x28.
struct LuaDebug {
    int event;                 // +0x00
    const char* name;          // +0x08
    const char* namewhat;      // +0x10
    const char* what;          // +0x18
    const char* source;        // +0x20
    int currentline;           // +0x28
    int nups;                  // +0x2c
    int linedefined;           // +0x30
    int lastlinedefined;       // +0x34
    char shortSrc[60];         // +0x38 (LUA_IDSIZE)
    int i_ci;                  // +0x74
};
static_assert(offsetof(LuaDebug, currentline) == 0x28, "lua_Debug::currentline offset");

class cGZLuaScriptServer;
class ILuaCommandGeneratorApi;

// What the server needs from the rest of the engine. Every method mirrors one call of the original.
class ILuaScriptServerHost {
public:
    virtual ~ILuaScriptServerHost() = default;
    // GZCOM()->CreateInstance(0x4b2811aa, 0x6b2811a6, &out): a new, unopened cSCLua as its cIGZLua5 view
    // (counted reference). The port takes the recovered base to stay independent of the cSCLua GDR bridge.
    virtual lua5::cGZLua5* CreateLua() = 0;
    // GZCOM()->CreateInstance(0x2bd13fb4, 0x2bd144df, &out): a counted cGZLua5CommandGenerator.
    virtual ILuaCommandGeneratorApi* CreateCommandGenerator() = 0;
    // RZGetFramework()->GetSystemService(0xeb903a32, 0xcb90253a, &out): counted cIGZCommandServer.
    virtual cIGZCommandServer* GetCommandServer() = 0;
    // RZGetFramework()->GetSystemService(0x04fa845b, 0x652294c7, &out): counted cIGZMessageServer2.
    virtual cIGZMessageServer2* GetMessageServer() = 0;
    // RZGetFramework() vslots 0x48 / 0x50.
    virtual void AddToTick(cIGZSystemService* service) = 0;
    virtual void RemoveFromTick(cIGZSystemService* service) = 0;
    // cGZLua5DebugInfo::GetCurrentScriptNameAndLine(thread, level, name, line) @ 0x10064431c.
    virtual bool GetCurrentScriptNameAndLine(lua5::cGZLua5Thread* thread, int level, cRZString& name, int& line) = 0;
    // The patched Lua's state[-1] slot (ILuaApi::get_thread_slot).
    virtual lua5::cGZLua5Thread* ThreadForState(lua5::lua_State* state) = 0;
    // lua_sethook(state, hook, mask, 0) @ 0x1009192c0 (mask 4 = LUA_MASKLINE).
    virtual void SetLuaHook(lua5::lua_State* state, void (*hook)(lua5::lua_State*, LuaDebug*), int mask) = 0;
    // lua_getinfo(state, "l", ar) @ 0x1009194b0.
    virtual void GetLuaInfoLine(lua5::lua_State* state, LuaDebug* ar) = 0;
    // Lua 5.0's "no non-yieldable C call" guard (lua_State::nny at +0x5a).
    virtual bool IsLuaYieldable(lua5::lua_State* state) = 0;
    // lua_yield(state, 0) @ 0x10091b300.
    virtual void YieldLua(lua5::lua_State* state) = 0;
};
void SetLuaScriptServerHost(ILuaScriptServerHost* host);
ILuaScriptServerHost* GetLuaScriptServerHost();

// The seam for the unrecovered cGZLua5CommandGenerator (clsid 0x2bd13fb4, iid 0x2bd144df).
class ILuaCommandGeneratorApi {
public:
    virtual ~ILuaCommandGeneratorApi() = default;
    virtual std::uint32_t AddRef() = 0;
    virtual std::uint32_t Release() = 0;
    // 0x10069556a: registers itself with the command server and installs the "sc4app" Lua table.
    virtual bool Init(cIGZCommandServer* server, lua5::cGZLua5* lua, const cRZString& tableName) = 0;
    // AsIGZCommandGenerator()->Shutdown(), 0x1006957ee.
    virtual void Shutdown() = 0;
};

// cIGZLuaScriptServer — the primary interface (vtable 0x100ada7c0). The slot order below matches the binary.
class cIGZLuaScriptServer : public cIGZUnknown {
public:
    struct BreakpointEntry {
        int scriptId = -1;          // +0x00
        cRZString name;             // +0x08
        int line = 0;               // +0x30
        bool enabled = true;        // +0x34
        cRZString condition;        // +0x38
    };

    virtual bool Init() = 0;                                                                // [3]  +0x18
    virtual bool Shutdown() = 0;                                                            // [4]  +0x20
    virtual lua5::cGZLua5* GetLua() = 0;                                                    // [5]  +0x28
    virtual int RunScriptString(const cIGZString& source, const cIGZString& chunkName,
                                const cIGZString& arguments) = 0;                           // [6]  +0x30
    virtual int RunScriptFile(const cIGZString& path, const cIGZString& arguments) = 0;     // [7]  +0x38
    virtual bool KillScript(int scriptId) = 0;                                              // [8]  +0x40
    virtual int PauseScript(std::uint32_t scriptId) = 0;                                    // [9]  +0x48
    virtual int ResumeScript(std::uint32_t scriptId) = 0;                                   // [10] +0x50
    virtual bool GetBreakpointExists(std::uint32_t scriptId, const cIGZString& script, int line) = 0;   // [11] +0x58
    // The last bool is "remove" in the original (true removes); see the quirks above.
    virtual bool SetBreakpointExists(std::uint32_t scriptId, const cIGZString& script, int line, bool remove) = 0;   // [12] +0x60
    virtual bool ToggleBreakpointExists(std::uint32_t scriptId, const cIGZString& script, int line) = 0;             // [13] +0x68
    virtual bool GetBreakpointEnabled(std::uint32_t scriptId, const cIGZString& script, int line) = 0;               // [14] +0x70
    virtual bool SetBreakpointEnabled(std::uint32_t scriptId, const cIGZString& script, int line, bool enabled) = 0; // [15] +0x78
    virtual bool GetBreakpointCondition(std::uint32_t scriptId, const cIGZString& script, int line, cIGZString& out) = 0;   // [16] +0x80
    virtual bool SetBreakpointCondition(std::uint32_t scriptId, const cIGZString& script, int line,
                                        const cIGZString& condition) = 0;                  // [17] +0x88
    virtual bool GetBreakpointEntryList(std::uint32_t scriptId, const cIGZString& script,
                                        std::list<BreakpointEntry>& out) = 0;              // [18] +0x90
    virtual void EnumerateScriptIDs(std::vector<int>& out) = 0;                            // [19] +0x98
    virtual bool GetScriptInfo(int scriptId, lua5::cGZLua5Thread** thread, cIGZString* script,
                               cIGZString* chunkName, int* pauseCount, int* resumeCount, bool* started) = 0;   // [20] +0xa0
    virtual int GetCurrentRunningScriptID() const = 0;                                     // [21] +0xa8
    virtual bool GetScriptAutoYieldEnabled(int scriptId) const = 0;                        // [22] +0xb0
    virtual bool SetScriptAutoYieldEnabled(int scriptId, bool enabled) = 0;                // [23] +0xb8
    virtual int GetLastCreatedScriptID() const = 0;                                        // [24] +0xc0
    virtual bool OnTick(std::uint32_t elapsedMs) = 0;                                      // [28] +0xe0
};

class cGZLuaScriptServer final : public cIGZLuaScriptServer, public cRZSystemService {
public:
    struct LuaThreadData {
        LuaThreadData() = default;
        ~LuaThreadData();
        // 0x100645418: stores the source and chunk name; false when the thread was already set up.
        bool Init(lua5::cGZLua5* lua, const cIGZString& source, const cIGZString& chunkName);

        lua5::cGZLua5Thread* thread = nullptr;   // +0x00 (counted reference)
        cRZString sourceText;                    // +0x08 (the chunk source)
        cRZString chunkName;                     // +0x30
        cRZString arguments;                     // +0x58
        int pauseCount = 0;                      // +0x80
        int resumeCount = 0;                     // +0x84
        bool started = false;                    // +0x88
        bool autoYield = false;                  // +0x89
        int currentLine = -1;                    // +0x8c
        std::list<BreakpointEntry> breakpoints;  // +0x90
    };

    cGZLuaScriptServer();                    // @ 0x100643fc8 / 0x100645460
    ~cGZLuaScriptServer() override;           // @ 0x1006454ec
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x100645604
    std::uint32_t AddRef() override;                               // @ 0x100645648
    std::uint32_t Release() override;                              // @ 0x100645672
    void FinalRelease();                                           // @ 0x1006456da (vtable slot 27)

    bool Init() override;                                          // @ 0x100645708
    bool Shutdown() override;                                      // @ 0x100645a20
    bool OnTick(std::uint32_t elapsedMs) override;                 // @ 0x100647486
    lua5::cGZLua5* GetLua() override;                              // @ 0x10064cd42
    int RunScriptString(const cIGZString& source, const cIGZString& chunkName,
                        const cIGZString& arguments) override;     // @ 0x100645b56
    int RunScriptFile(const cIGZString& path, const cIGZString& arguments) override;   // @ 0x1006464fa
    bool KillScript(int scriptId) override;                        // @ 0x10064675a
    int PauseScript(std::uint32_t scriptId) override;              // @ 0x10064683a
    int ResumeScript(std::uint32_t scriptId) override;             // @ 0x100646944
    bool GetBreakpointExists(std::uint32_t scriptId, const cIGZString& script, int line) override;
    bool SetBreakpointExists(std::uint32_t scriptId, const cIGZString& script, int line, bool remove) override;
    bool ToggleBreakpointExists(std::uint32_t scriptId, const cIGZString& script, int line) override;
    bool GetBreakpointEnabled(std::uint32_t scriptId, const cIGZString& script, int line) override;
    bool SetBreakpointEnabled(std::uint32_t scriptId, const cIGZString& script, int line, bool enabled) override;
    bool GetBreakpointCondition(std::uint32_t scriptId, const cIGZString& script, int line, cIGZString& out) override;
    bool SetBreakpointCondition(std::uint32_t scriptId, const cIGZString& script, int line,
                                const cIGZString& condition) override;
    bool GetBreakpointEntryList(std::uint32_t scriptId, const cIGZString& script,
                                std::list<BreakpointEntry>& out) override;
    void EnumerateScriptIDs(std::vector<int>& out) override;       // @ 0x100647684
    bool GetScriptInfo(int scriptId, lua5::cGZLua5Thread** thread, cIGZString* script,
                       cIGZString* chunkName, int* pauseCount, int* resumeCount, bool* started) override;
    int GetCurrentRunningScriptID() const override;                // @ 0x100647814
    bool GetScriptAutoYieldEnabled(int scriptId) const override;   // @ 0x100647838
    bool SetScriptAutoYieldEnabled(int scriptId, bool enabled) override;   // @ 0x100647882
    int GetLastCreatedScriptID() const override;                   // @ 0x10064795e

    // The two raw Lua callbacks the server installs (they only receive a lua_State).
    static void LuaYieldHook(lua5::lua_State* state, LuaDebug* ar);                 // @ 0x100645ad8
    static int ScriptArgIndexEventHandler(lua5::lua_State* state);                  // @ 0x1006478ca

private:
    bool ScriptArgSetup();                                                          // @ 0x100645924
    bool ScriptArgSetupThreadArguments(const LuaThreadData* data) const;             // @ 0x100645f56
    void PostScriptChangeMessage(int scriptId, std::uint32_t type, int data2);       // @ 0x1006463fa
    void PostBreakpointChangedMessage(std::uint32_t scriptId);                       // @ 0x100646d42
    BreakpointEntry* GetBreakpointEntry(LuaThreadData* data, const cIGZString& script, int line,
                                        bool create, bool& created);                 // @ 0x100646ade
    bool RemoveBreakpointEntry(LuaThreadData* data, const cIGZString& script, int line, bool& removed);   // @ 0x100646cca
    bool ShouldBreak(LuaThreadData* data) const;                                     // @ 0x1006473b0
    LuaThreadData* FindThread(int scriptId) const;

    std::uint32_t refCount_ = 0;                     // +0x2c (the original's only reference count)
    bool initialised_ = false;                       // +0x30
    int tickDepth_ = 0;                              // +0x34
    int scriptCounter_ = 0;                          // +0x38
    int lastCreatedScriptId_ = -1;                   // +0x3c
    lua5::cGZLua5* lua_ = nullptr;                   // +0x40 (counted reference)
    ILuaCommandGeneratorApi* generator_ = nullptr;      // +0x48 (counted reference)
    std::map<int, LuaThreadData*> threads_;          // +0x50
    LuaThreadData* currentRunning_ = nullptr;        // +0x68
};

// The port keeps the original object sizes (the class is what GZCOM allocates: 0x70 bytes).
static_assert(sizeof(cIGZLuaScriptServer::BreakpointEntry) == 0x60, "BreakpointEntry size");
static_assert(sizeof(cGZLuaScriptServer::LuaThreadData) == 0xa8, "LuaThreadData size");
static_assert(sizeof(cGZLuaScriptServer) == 0x70, "cGZLuaScriptServer size");

// Original free function (0x100646663, static in the binary) used by RunScriptFile.
bool ReadFileIntoString(const cRZString& path, cRZString& out, unsigned int maxLength = 1000000);
