// Tests cGZLua5Thread against a recording fake of the Lua C API.
#include "lua/Lua5Thread.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace lua5;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

struct FakeApi : ILuaApi {
    std::vector<std::string> log;
    int pcallResult = 0, loadResult = 0, topValue = 5, typeValue = kLuaTypeString;
    double numberValue = 0;
    void rec(const std::string& s) { log.push_back(s); }
    std::string str() const { std::string r; for (auto& l : log) r += l + ";"; return r; }
    int gettop(lua_State*) override { rec("gettop"); return topValue; }
    void settop(lua_State*, int i) override { rec("settop" + std::to_string(i)); }
    void pushvalue(lua_State*, int i) override { rec("pushvalue" + std::to_string(i)); }
    void remove(lua_State*, int i) override { rec("remove" + std::to_string(i)); }
    void insert(lua_State*, int i) override { rec("insert" + std::to_string(i)); }
    void replace(lua_State*, int i) override { rec("replace" + std::to_string(i)); }
    void xmove(lua_State*, lua_State*, int) override {}
    int checkstack(lua_State*, int n) override { return n; }
    int type(lua_State*, int) override { rec("type"); return typeValue; }
    const char* type_name(lua_State*, int) override { return "t"; }
    int isnumber(lua_State*, int) override { return 2; }
    int isstring(lua_State*, int) override { return 0; }
    int iscfunction(lua_State*, int) override { return 1; }
    int isuserdata(lua_State*, int) override { return 1; }
    int equal(lua_State*, int, int) override { return 1; }
    int rawequal(lua_State*, int, int) override { return 0; }
    int lessthan(lua_State*, int, int) override { return 3; }
    double tonumber(lua_State*, int) override { return numberValue; }
    int toboolean(lua_State*, int) override { return 1; }
    const char* tostring(lua_State*, int) override { return "errmsg"; }
    std::size_t strlen(lua_State*, int) override { return 6; }
    lua_CFunction tocfunction(lua_State*, int) override { return nullptr; }
    void* touserdata(lua_State*, int) override { return userdata; }
    lua_State* tothread(lua_State*, int) override { return nullptr; }
    void pushnil(lua_State*) override { rec("pushnil"); }
    void pushnumber(lua_State*, double d) override { rec("pushnumber" + std::to_string((int)d)); }
    void pushlstring(lua_State*, const char* s, std::size_t n) override { rec(std::string("pushlstring") + s + std::to_string(n)); }
    void pushstring(lua_State*, const char* s) override { rec(std::string("pushstring") + s); }
    void pushcclosure(lua_State*, lua_CFunction, int n) override { rec("pushcclosure" + std::to_string(n)); }
    void pushboolean(lua_State*, int) override {}
    void pushlightuserdata(lua_State*, void*) override {}
    void* newuserdata(lua_State*, std::size_t) override { return nullptr; }
    void gettable(lua_State*, int i) override { rec("gettable" + std::to_string(i)); }
    void rawget(lua_State*, int i) override { rec("rawget" + std::to_string(i)); }
    void rawgeti(lua_State*, int i, int n) override { rec("rawgeti" + std::to_string(i) + "," + std::to_string(n)); }
    void newtable(lua_State*) override {}
    void getfenv(lua_State*, int) override {}
    int getmetatable(lua_State*, int) override { return 0; }
    void settable(lua_State*, int i) override { rec("settable" + std::to_string(i)); }
    void rawset(lua_State*, int i) override { rec("rawset" + std::to_string(i)); }
    void rawseti(lua_State*, int, int) override {}
    int setfenv(lua_State*, int) override { return 0; }
    int setmetatable(lua_State*, int) override { return 0; }
    void call(lua_State*, int, int) override {}
    int pcall(lua_State*, int a, int r, int e) override { rec("pcall" + std::to_string(a) + "," + std::to_string(r) + "," + std::to_string(e)); return pcallResult; }
    int dofile(lua_State*, const char*) override { return 0; }
    int dostring(lua_State*, const char*) override { return 0; }
    int dobuffer(lua_State*, const char*, std::size_t, const char*) override { return 0; }
    int load(lua_State*, lua_Chunkreader, void*, const char*) override { return 0; }
    int loadfile(lua_State*, const char*) override { return loadResult; }
    int loadbuffer(lua_State*, const char*, std::size_t n, const char*) override { rec("loadbuffer" + std::to_string(n)); return loadResult; }
    int error(lua_State*, const char*) override { return 0; }
    int next(lua_State*, int) override { return 0; }
    void concat(lua_State*, int) override {}
    int ref(lua_State*, int t) override { rec("ref" + std::to_string(t)); return 7; }
    void unref(lua_State*, int t, int r) override { rec("unref" + std::to_string(t) + "," + std::to_string(r)); }
    int yield(lua_State*, int n) override { return n + 1; }
    void* userdata = nullptr;
    // slots / library management are not exercised here
    std::map<lua_State*, cGZLua5Thread*> slots;
    lua_State* open() override { return nullptr; }
    void close(lua_State*) override {}
    void openlib(lua_State*, int) override {}
    int getgccount(lua_State*) override { return 0; }
    int getgcthreshold(lua_State*) override { return 0; }
    void setgcthreshold(lua_State*, int) override {}
    lua_State* newthread(lua_State*) override { return nullptr; }
    int resume(lua_State*, int) override { return 0; }
    bool getstack(lua_State*, int) override { return false; }
    int ccalls(lua_State*) override { return 0; }
    lua_State* main_state(lua_State* l) override { return l; }
    cGZLua5Thread* get_thread_slot(lua_State* l) override { auto it = slots.find(l); return it == slots.end() ? nullptr : it->second; }
    void set_thread_slot(lua_State* l, cGZLua5Thread* t) override { slots[l] = t; }
    void pushfstring(lua_State*, const char*, int) override {}
    void lerror(lua_State*, const char*) override {}
    void checktype(lua_State*, int, int) override {}
    void typerror(lua_State*, int, const char*) override {}
};

static int g_cbCount = 0, g_cbStatus = 0;
static std::string g_cbMsg;
static void errCb(cGZLua5Thread*, int status, const char* msg) { ++g_cbCount; g_cbStatus = status; g_cbMsg = msg; }

struct Owner : ILua5Owner {
    int refs = 1;
    LuaErrorCallback cb = errCb;
    void AddRef() override { ++refs; }
    void Release() override { --refs; }
    LuaErrorCallback GetErrorCallback() override { return cb; }
    cGZLua5Thread* AsThread() override { return nullptr; }
};

static int fnAlert(lua_State*) { return 0; }
static int fnTag(lua_State*) { return 0; }
static int fnGet(lua_State*) { return 0; }
static int fnSet(lua_State*) { return 0; }
static int fnNew(lua_State*) { return 0; }
static int fnCopy(lua_State*) { return 0; }

struct Registry : IThreadRegistry {
    int destroyed = -2;
    bool DestroyThread(lua_State*, int id) override { destroyed = id; return true; }
    int GetThreadStatus(int id) override { return id * 10; }
};
struct Compat : ILuaCompat {
    Registry reg; bool haveReg = true; int setTag = -1;
    lua_CFunction AlertExtension() override { return fnAlert; }
    lua_CFunction TagFn() override { return fnTag; }
    lua_CFunction GetTagMethodFn() override { return fnGet; }
    lua_CFunction SetTagMethodFn() override { return fnSet; }
    lua_CFunction NewTagFn() override { return fnNew; }
    lua_CFunction CopyTagMethodsFn() override { return fnCopy; }
    void SetTagDirect(lua_State*, int t) override { setTag = t; }
    void PushUserTag(lua_State*, void*, int) override {}
    IThreadRegistry* RegistryFor(lua_State*) override { return haveReg ? &reg : nullptr; }
};

int main() {
    FakeApi api; Compat compat; Owner owner;
    cGZLua5Thread* t = new cGZLua5Thread(api, compat);
    lua_State* st = reinterpret_cast<lua_State*>(0x1000);
    CHECK(!t->IsValid());
    CHECK(t->OnCreate(&owner, st, 3));
    CHECK(owner.refs == 2);
    CHECK(api.str() == "pushstring_ALERT;pushcclosure0;rawset-10001;");
    CHECK(t->GetID() == 3 && t->GetContext() == st && t->GetLua() == &owner);
    CHECK(!t->IsValid());                      // no slot back-pointer yet
    api.set_thread_slot(st, t);
    CHECK(t->IsValid());
    CHECK(t->Status() == 30);
    CHECK(t->Destroy() && compat.reg.destroyed == 3);
    compat.haveReg = false;
    CHECK(!t->Destroy() && t->Status() == -1);
    compat.haveReg = true;

    // simple forwards
    api.log.clear();
    t->Pop(2);
    t->GetRegistryTable(); t->GetGlobalTable();
    t->GetGlobal("g"); t->SetGlobal("s");
    CHECK(api.str() == "settop-3;pushvalue-10000;pushvalue-10001;pushstringg;gettable-10001;pushstrings;insert-2;settable-10001;");
    CHECK(t->GetUpValueIndex(1) == -10002);
    api.typeValue = kLuaTypeNone; CHECK(t->IsNone(1) && t->IsNoneOrNil(1) && !t->IsNil(1));
    api.typeValue = kLuaTypeNil; CHECK(t->IsNil(1) && t->IsNoneOrNil(1));
    api.typeValue = kLuaTypeFunction; CHECK(t->IsFunction(1) && !t->IsTable(1));
    CHECK(t->IsNumber(1) && !t->IsString(1) && t->Equal(1, 2) && !t->RawEqual(1, 2) && t->LessThan(1, 2));
    api.numberValue = -3.7; CHECK(t->ToSint32(1) == -3);
    api.numberValue = 4000000000.0; CHECK(t->ToUint32(1) == 4000000000LL);
    api.log.clear();
    CHECK(t->Ref() == 7); t->GetRef(7); t->Unref(7);
    CHECK(api.str() == "ref-10000;rawgeti-10000,7;unref-10000,7;");
    CHECK(t->Pause(2) == 3);

    // CallProtected: traceback handler installed when errfunc==0 and the owner has a callback
    api.log.clear(); api.topValue = 5;
    CHECK(t->CallProtected(2, 1, 0, true) == 0);
    CHECK(api.str() == "gettop;gettop;pushlstring_TRACEBACK10;rawget-10001;insert3;pcall2,1,3;remove3;");
    api.log.clear();
    CHECK(t->CallProtected(2, 1, 9, false) == 0);
    CHECK(api.str() == "gettop;pcall2,1,9;");
    // failure -> callback, optional pop
    api.pcallResult = 2; api.log.clear(); g_cbCount = 0;
    CHECK(t->CallProtected(0, 0, 9, true) == 2);
    CHECK(g_cbCount == 1 && g_cbStatus == 2 && g_cbMsg == "errmsg");
    CHECK(api.log.back() == "settop-2");
    api.log.clear();
    t->CallProtected(0, 0, 9, false);
    CHECK(api.log.back() != "settop-2");
    api.pcallResult = 0;
    owner.cb = nullptr; api.log.clear();
    t->CallProtected(1, 0, 0, false);
    CHECK(api.str() == "gettop;pcall1,0,0;");
    owner.cb = errCb;

    // loads: callback only when the error value is a string
    api.loadResult = 3; g_cbCount = 0; api.typeValue = kLuaTypeString;
    CHECK(t->LoadStringA("abcd") == 3 && g_cbCount == 1 && g_cbStatus == 3);
    api.typeValue = kLuaTypeTable; g_cbCount = 0;
    CHECK(t->LoadBuffer("abc", 3, "n") == 3 && g_cbCount == 0);
    api.loadResult = 0; api.typeValue = kLuaTypeString;
    CHECK(t->LoadFile("f") == 0 && g_cbCount == 0);

    // tag compat
    api.log.clear(); api.numberValue = 9;
    CHECK(t->Tag(4) == 9);
    CHECK(api.str() == "pushvalue4;pushvalue-1;pushcclosure0;replace-3;gettop;gettop;pushlstring_TRACEBACK10;rawget-10001;insert4;pcall1,1,4;remove4;settop-2;");
    api.numberValue = 0; CHECK(t->Tag(4) == -1);
    api.numberValue = 5; api.log.clear();
    CHECK(t->NewTag() == 5);
    api.numberValue = -1; CHECK(t->NewTag() == -1);
    api.pcallResult = 1; api.log.clear();
    t->GetTagMethod(3, "ev");
    CHECK(api.log.back() == "pushnil");
    api.pcallResult = 0;
    CHECK(t->CopyTagMethods(8, 9) == 8);
    t->SetTag(6); CHECK(compat.setTag == 6);
    int ud[2] = {0x1234, 0x5678}; api.userdata = ud;
    CHECK(t->ToUserTag(1) == 0x567800001234ULL);

    // destroy
    CHECK(t->OnDestroy());
    CHECK(owner.refs == 1 && t->GetID() == -1 && t->GetContext() == nullptr && !t->IsValid());
    t->OnCreate(&owner, st, 1);
    delete t;
    CHECK(owner.refs == 1);
    if (!g_fail) std::printf("lua_lua5thread ok\n");
    return g_fail != 0;
}
