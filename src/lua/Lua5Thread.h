// cGZLua5Thread — the game's wrapper around one Lua 5.0 thread (0x10069335c..0x1006947a2). Almost every method
// forwards to the Lua C API on the lua_State stored at +0x20; the Lua library itself is reached through the
// ILuaApi seam so the wrapper logic can be tested without linking Lua.
//
// Original layout: +0 vtable (cIGZLua5Thread surface), +0x10 secondary vtable (cIGZLua5Thread interface proper),
// +0x18 thread id (-1 = none), +0x20 lua_State*, +0x28 owner cIGZLua5* (reference counted), +0x30 user context.
// Pseudo-indices: registry = -10000 (0xffffd8f0), globals = -10001 (0xffffd8ef); upvalue i = -10002 - i.
// A thread is valid only while the pointer stored just before the lua_State (state[-1]) is its interface pointer.
#pragma once
#include <cstddef>
#include <climits>
#include <cstdint>

namespace lua5 {

struct lua_State;
class cGZLua5Thread;
class ThreadRegistry;

// cvttsd2si semantics: NaN and out-of-range values give the "integer indefinite" value.
inline std::int32_t CvtTrunc32(double d) {
    return (d >= -2147483648.0 && d < 2147483648.0) ? static_cast<std::int32_t>(d) : INT32_MIN;
}
inline std::int64_t CvtTrunc64(double d) {
    return (d >= -9223372036854775808.0 && d < 9223372036854775808.0) ? static_cast<std::int64_t>(d) : INT64_MIN;
}

using lua_CFunction = int (*)(lua_State*);
using lua_Chunkreader = const char* (*)(lua_State*, void*, std::size_t*);

constexpr int kRegistryIndex = -10000;
constexpr int kGlobalsIndex = -10001;
constexpr int kLuaTypeNone = -1, kLuaTypeNil = 0, kLuaTypeBoolean = 1, kLuaTypeLightUserData = 2,
              kLuaTypeNumber = 3, kLuaTypeString = 4, kLuaTypeTable = 5, kLuaTypeFunction = 6;

// The slice of the Lua 5.0 C API used by the wrapper.
class ILuaApi {
public:
    virtual ~ILuaApi() = default;
    virtual int gettop(lua_State*) = 0;
    virtual void settop(lua_State*, int) = 0;
    virtual void pushvalue(lua_State*, int) = 0;
    virtual void remove(lua_State*, int) = 0;
    virtual void insert(lua_State*, int) = 0;
    virtual void replace(lua_State*, int) = 0;
    virtual void xmove(lua_State* from, lua_State* to, int n) = 0;
    virtual int checkstack(lua_State*, int) = 0;
    virtual int type(lua_State*, int) = 0;
    virtual const char* type_name(lua_State*, int tp) = 0;
    virtual int isnumber(lua_State*, int) = 0;
    virtual int isstring(lua_State*, int) = 0;
    virtual int iscfunction(lua_State*, int) = 0;
    virtual int isuserdata(lua_State*, int) = 0;
    virtual int equal(lua_State*, int, int) = 0;
    virtual int rawequal(lua_State*, int, int) = 0;
    virtual int lessthan(lua_State*, int, int) = 0;
    virtual double tonumber(lua_State*, int) = 0;
    virtual int toboolean(lua_State*, int) = 0;
    virtual const char* tostring(lua_State*, int) = 0;
    virtual std::size_t strlen(lua_State*, int) = 0;
    virtual lua_CFunction tocfunction(lua_State*, int) = 0;
    virtual void* touserdata(lua_State*, int) = 0;
    virtual lua_State* tothread(lua_State*, int) = 0;
    virtual void pushnil(lua_State*) = 0;
    virtual void pushnumber(lua_State*, double) = 0;
    virtual void pushlstring(lua_State*, const char*, std::size_t) = 0;
    virtual void pushstring(lua_State*, const char*) = 0;
    virtual void pushcclosure(lua_State*, lua_CFunction, int n) = 0;
    virtual void pushboolean(lua_State*, int) = 0;
    virtual void pushlightuserdata(lua_State*, void*) = 0;
    virtual void* newuserdata(lua_State*, std::size_t) = 0;
    virtual void gettable(lua_State*, int) = 0;
    virtual void rawget(lua_State*, int) = 0;
    virtual void rawgeti(lua_State*, int, int) = 0;
    virtual void newtable(lua_State*) = 0;
    virtual void getfenv(lua_State*, int) = 0;
    virtual int getmetatable(lua_State*, int) = 0;
    virtual void settable(lua_State*, int) = 0;
    virtual void rawset(lua_State*, int) = 0;
    virtual void rawseti(lua_State*, int, int) = 0;
    virtual int setfenv(lua_State*, int) = 0;
    virtual int setmetatable(lua_State*, int) = 0;
    virtual void call(lua_State*, int nargs, int nresults) = 0;
    virtual int pcall(lua_State*, int nargs, int nresults, int errfunc) = 0;
    virtual int dofile(lua_State*, const char*) = 0;
    virtual int dostring(lua_State*, const char*) = 0;
    virtual int dobuffer(lua_State*, const char*, std::size_t, const char* name) = 0;
    virtual int load(lua_State*, lua_Chunkreader, void*, const char* name) = 0;
    virtual int loadfile(lua_State*, const char*) = 0;      // luaL_loadfile
    virtual int loadbuffer(lua_State*, const char*, std::size_t, const char* name) = 0;
    virtual int error(lua_State*, const char*) = 0;
    virtual int next(lua_State*, int) = 0;
    virtual void concat(lua_State*, int) = 0;
    virtual int ref(lua_State*, int t) = 0;                 // luaL_ref
    virtual void unref(lua_State*, int t, int ref) = 0;     // luaL_unref
    virtual int yield(lua_State*, int nresults) = 0;

    // Library / state management (lua_open, lua_close, luaopen_*, GC tuning)
    virtual lua_State* open() = 0;
    virtual void close(lua_State*) = 0;
    virtual void openlib(lua_State*, int lib) = 0;          // 0 base, 1 io, 2 string, 3 math, 4 debug, 5 table
    virtual int getgccount(lua_State*) = 0;
    virtual int getgcthreshold(lua_State*) = 0;
    virtual void setgcthreshold(lua_State*, int) = 0;
    // Coroutines
    virtual lua_State* newthread(lua_State*) = 0;
    virtual int resume(lua_State*, int nargs) = 0;
    virtual bool getstack(lua_State*, int level) = 0;       // lua_getstack with a scratch lua_Debug
    virtual int ccalls(lua_State*) = 0;                     // lua_State::nCcalls
    virtual lua_State* main_state(lua_State*) = 0;          // G(L)->mainthread
    // The game's patched Lua keeps one extra pointer just before every lua_State (state[-1]).
    virtual cGZLua5Thread* get_thread_slot(lua_State*) = 0;
    virtual void set_thread_slot(lua_State*, cGZLua5Thread*) = 0;
    // Auxiliary library
    virtual void pushfstring(lua_State*, const char* fmt, int arg) = 0;
    virtual void lerror(lua_State*, const char* msg) = 0;   // luaL_error
    virtual void checktype(lua_State*, int narg, int type) = 0;
    virtual void typerror(lua_State*, int narg, const char* tname) = 0;
};

// The API instance used by the C functions the library installs into Lua (they only receive a lua_State*).
void SetLuaApi(ILuaApi* api);
ILuaApi& LuaApi();

// Callback the owner exposes (vtable slot 0x70 of cIGZLua5): called with the thread interface, the Lua status
// code and the error message on top of the stack.
using LuaErrorCallback = void (*)(cGZLua5Thread* thread, int status, const char* message);

// Owner (cIGZLua5): reference counted, hands out the error callback (slot 0x70; CallProtected also uses a
// non-null callback as the cue to install the _TRACEBACK error handler).
class ILua5Owner {
public:
    virtual ~ILua5Owner() = default;
    virtual void AddRef() = 0;
    virtual void Release() = 0;
    virtual LuaErrorCallback GetErrorCallback() = 0;
    virtual cGZLua5Thread* AsThread() = 0;                  // main-thread view (slot 0x18)
};

// Thread registry (nsGZLua5ThreadLib), keyed by lua_State.
class IThreadRegistry {
public:
    virtual ~IThreadRegistry() = default;
    virtual bool DestroyThread(lua_State* caller, int id) = 0;
    virtual int GetThreadStatus(int id) = 0;                // 0 dead, 1 suspended, 2 running, -1 invalid
};

// Host-side C functions the wrapper installs (nsGZLua5CompatWithLua4 and the _ALERT hook) and the two Lua-4
// compatibility entry points that act directly on the state.
class ILuaCompat {
public:
    virtual ~ILuaCompat() = default;
    virtual lua_CFunction AlertExtension() = 0;
    virtual lua_CFunction TagFn() = 0;
    virtual lua_CFunction GetTagMethodFn() = 0;
    virtual lua_CFunction SetTagMethodFn() = 0;
    virtual lua_CFunction NewTagFn() = 0;
    virtual lua_CFunction CopyTagMethodsFn() = 0;
    virtual void SetTagDirect(lua_State*, int tag) = 0;
    virtual void PushUserTag(lua_State*, void* ud, int tag) = 0;
    virtual IThreadRegistry* RegistryFor(lua_State*) = 0;
};

class cGZLua5Thread {
public:
    cGZLua5Thread(ILuaApi& api, ILuaCompat& compat);
    virtual ~cGZLua5Thread();

    // cRZUnknown counting: Release deletes once the count is 0 or 1.
    std::uint32_t AddRef() { return ++refCount_; }
    std::uint32_t Release();
    // Only the main thread (cGZLua5) owns a registry.
    virtual ThreadRegistry* GetRegistry() { return nullptr; }

    ILua5Owner* GetLua() const { return owner_; }
    int Pause(int nresults);
    bool OnCreate(ILua5Owner* owner, lua_State* state, int id);
    bool OnDestroy();
    int GetID() const { return id_; }
    // Valid while the slot before the state still points back at this thread and an id is assigned.
    bool IsValid() const;
    bool Destroy();
    int Status();
    lua_State* GetContext() const { return state_; }
    void* GetUserContext() const { return userContext_; }
    void SetUserContext(void* c) { userContext_ = c; }

    int GetTop() { return api_.gettop(state_); }
    void SetTop(int i) { api_.settop(state_, i); }
    void PushValue(int i) { api_.pushvalue(state_, i); }
    void Remove(int i) { api_.remove(state_, i); }
    void Insert(int i) { api_.insert(state_, i); }
    void Pop(int n) { api_.settop(state_, ~n); }
    void Replace(int i) { api_.replace(state_, i); }
    void Move(lua_State* to, int n) { api_.xmove(state_, to, n); }
    bool GrowStack(int n) { return api_.checkstack(state_, n) != 0; }
    int Type(int i) { return api_.type(state_, i); }
    const char* TypeName(int tp) { return api_.type_name(state_, tp); }
    bool IsNumber(int i) { return api_.isnumber(state_, i) != 0; }
    bool IsString(int i) { return api_.isstring(state_, i) != 0; }
    bool IsCFunction(int i) { return api_.iscfunction(state_, i) != 0; }
    bool IsFunction(int i) { return api_.type(state_, i) == kLuaTypeFunction; }
    bool IsTable(int i) { return api_.type(state_, i) == kLuaTypeTable; }
    bool IsUserData(int i) { return api_.isuserdata(state_, i) != 0; }
    bool IsLightUserData(int i) { return api_.type(state_, i) == kLuaTypeLightUserData; }
    bool IsBoolean(int i) { return api_.type(state_, i) == kLuaTypeBoolean; }
    bool IsNil(int i) { return api_.type(state_, i) == kLuaTypeNil; }
    bool IsNone(int i) { return api_.type(state_, i) == kLuaTypeNone; }
    bool IsNoneOrNil(int i) { return api_.type(state_, i) < 1; }
    bool Equal(int a, int b) { return api_.equal(state_, a, b) != 0; }
    bool RawEqual(int a, int b) { return api_.rawequal(state_, a, b) != 0; }
    bool LessThan(int a, int b) { return api_.lessthan(state_, a, b) != 0; }
    std::int64_t ToUint32(int i) { return CvtTrunc64(api_.tonumber(state_, i)); }  // returns long
    std::int32_t ToSint32(int i) { return CvtTrunc32(api_.tonumber(state_, i)); }
    double ToNumber(int i) { return api_.tonumber(state_, i); }
    bool ToBoolean(int i) { return api_.toboolean(state_, i) != 0; }
    const char* ToString(int i) { return api_.tostring(state_, i); }
    std::size_t Strlen(int i) { return api_.strlen(state_, i); }
    lua_CFunction ToCFunction(int i) { return api_.tocfunction(state_, i); }
    void* ToUserData(int i) { return api_.touserdata(state_, i); }
    lua_State* ToThread(int i) { return api_.tothread(state_, i); }

    void PushNil() { api_.pushnil(state_); }
    void PushNumber(double n) { api_.pushnumber(state_, n); }
    void PushLString(const char* s, int len) { api_.pushlstring(state_, s, static_cast<std::size_t>(len)); }
    void PushString(const char* s) { api_.pushstring(state_, s); }
    void PushCClosure(lua_CFunction f, int n) { api_.pushcclosure(state_, f, n); }
    void PushBoolean(bool b) { api_.pushboolean(state_, b); }
    void PushLightUserData(void* p) { api_.pushlightuserdata(state_, p); }
    void* PushNewUserData(int size) { return api_.newuserdata(state_, static_cast<std::size_t>(size)); }

    void GetTable(int i) { api_.gettable(state_, i); }
    void RawGet(int i) { api_.rawget(state_, i); }
    void RawGetI(int i, int n) { api_.rawgeti(state_, i, n); }
    void PushNewTable() { api_.newtable(state_); }
    void GetFunctionEnvironment(int i) { api_.getfenv(state_, i); }
    int GetMetaTable(int i) { return api_.getmetatable(state_, i); }
    void GetRegistryTable() { api_.pushvalue(state_, kRegistryIndex); }
    void GetGlobalTable() { api_.pushvalue(state_, kGlobalsIndex); }
    void SetTable(int i) { api_.settable(state_, i); }
    void RawSet(int i) { api_.rawset(state_, i); }
    void RawSetI(int i, int n) { api_.rawseti(state_, i, n); }
    int SetFunctionEnvironment(int i) { return api_.setfenv(state_, i); }
    int SetMetaTable(int i) { return api_.setmetatable(state_, i); }

    void Call(int nargs, int nresults) { api_.call(state_, nargs, nresults); }
    int CallProtected(int nargs, int nresults, int errfunc, bool popError);
    int DoFile(const char* f) { return api_.dofile(state_, f); }
    int DoString(const char* s) { return api_.dostring(state_, s); }
    int DoBuffer(const char* b, int len, const char* name) { return api_.dobuffer(state_, b, static_cast<std::size_t>(len), name); }
    int Load(lua_Chunkreader r, void* ud, const char* name) { return api_.load(state_, r, ud, name); }
    int LoadFile(const char* file);
    int LoadStringA(const char* s);
    int LoadBuffer(const char* buf, int len, const char* name);
    int Error(const char* msg) { return api_.error(state_, msg); }
    int Next(int i) { return api_.next(state_, i); }
    void Concat(int n) { api_.concat(state_, n); }
    void Register(lua_CFunction f, const char* name);
    int GetUpValueIndex(int i) const { return -0x2711 - i; }
    void GetGlobal(const char* name);
    void SetGlobal(const char* name);
    int Ref() { return api_.ref(state_, kRegistryIndex); }
    void GetRef(int r) { api_.rawgeti(state_, kRegistryIndex, r); }
    void Unref(int r) { api_.unref(state_, kRegistryIndex, r); }

    // Lua 4 compatibility
    int Tag(int index);
    void GetTagMethod(int tag, const char* event);
    void SetTagMethod(int tag, const char* event);
    int NewTag();
    void SetTag(int tag) { compat_.SetTagDirect(state_, tag); }
    int CopyTagMethods(int dst, int src);
    int CloneTag(int tag) { return CopyTagMethods(NewTag(), tag); }
    void PushUserTag(void* ud, int tag) { compat_.PushUserTag(state_, ud, tag); }
    std::uint64_t ToUserTag(int index) { return *static_cast<std::uint64_t*>(ToUserData(index)); }

protected:
    void ReportLoadError(int status);

    ILuaApi& api_;
    ILuaCompat& compat_;
    int id_ = -1;
    lua_State* state_ = nullptr;
    ILua5Owner* owner_ = nullptr;
    void* userContext_ = nullptr;
    std::uint32_t refCount_ = 0;
};

}  // namespace lua5
