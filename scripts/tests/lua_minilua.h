// A tiny stack-machine implementation of the ILuaApi seam for tests: enough of Lua 5.0 (values, tables with
// metatables, C functions with upvalues, registry/globals, C-function pcall, coroutine-like threads) to run the
// library code in src/lua without linking the real interpreter.
#pragma once
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "lua/Lua5Thread.h"

namespace lua5 {

struct Table;
struct Closure;
struct UserData { std::vector<char> bytes; std::shared_ptr<Table> meta; };

struct Val {
    enum T { Nil, Bool, Num, Str, Light, Table_, CFn, LuaFn, UData, Thr } t = Nil;
    bool b = false;
    double n = 0;
    std::string s;
    void* p = nullptr;
    std::shared_ptr<Table> tab;
    std::shared_ptr<Closure> fn;
    std::shared_ptr<UserData> ud;
    lua_State* th = nullptr;
    int luaType() const {
        switch (t) {
            case Nil: return 0; case Bool: return 1; case Light: return 2; case Num: return 3; case Str: return 4;
            case Table_: return 5; case CFn: case LuaFn: return 6; case UData: return 7; case Thr: return 8;
        }
        return 0;
    }
};

struct Table {
    std::vector<std::pair<Val, Val>> kv;
    std::shared_ptr<Table> meta;
};

struct lua_State;
struct Closure {
    lua_CFunction cfn = nullptr;
    std::function<int(lua_State*)> body;       // Lua-function stand-in: returns number of results (or -1 to yield)
    std::vector<Val> upvals;
};

inline bool RawEq(const Val& a, const Val& b) {
    if (a.t != b.t) return false;
    switch (a.t) {
        case Val::Nil: return true;
        case Val::Bool: return a.b == b.b;
        case Val::Num: return a.n == b.n;
        case Val::Str: return a.s == b.s;
        case Val::Light: return a.p == b.p;
        case Val::Table_: return a.tab == b.tab;
        case Val::CFn: case Val::LuaFn: return a.fn == b.fn;
        case Val::UData: return a.ud == b.ud;
        case Val::Thr: return a.th == b.th;
    }
    return false;
}

struct LuaError { Val value; };

struct lua_State {
    std::vector<Val> stack;
    size_t base = 0;
    cGZLua5Thread* slot = nullptr;
    lua_State* mainState = nullptr;
    std::shared_ptr<Table> registry, globals;
    std::shared_ptr<Closure> running;
    int ccalls = 0;
    bool suspended = false, yielded = false;
};

class MiniLua : public ILuaApi {
public:
    std::vector<std::unique_ptr<lua_State>> states;
    int gcThreshold = 100;

    // ---- helpers ------------------------------------------------------------------------------------------
    static int Top(lua_State* L) { return static_cast<int>(L->stack.size() - L->base); }
    Val* At(lua_State* L, int idx) {
        static Val nilv;
        if (idx == kRegistryIndex) { tmp = Val(); tmp.t = Val::Table_; tmp.tab = L->registry; return &tmp; }
        if (idx == kGlobalsIndex) { tmp = Val(); tmp.t = Val::Table_; tmp.tab = L->globals; return &tmp; }
        if (idx < kGlobalsIndex) {
            size_t u = static_cast<size_t>(kGlobalsIndex - idx - 1);     // upvalue u+1 -> index u
            if (L->running && u < L->running->upvals.size()) return &L->running->upvals[u];
            nilv = Val(); return &nilv;
        }
        if (idx > 0) {
            size_t i = L->base + static_cast<size_t>(idx) - 1;
            if (i >= L->stack.size()) { nilv = Val(); return &nilv; }
            return &L->stack[i];
        }
        size_t i = L->stack.size() + static_cast<size_t>(idx);
        if (-idx > Top(L)) { nilv = Val(); return &nilv; }
        return &L->stack[i];
    }
    size_t Abs(lua_State* L, int idx) { return idx > 0 ? L->base + idx - 1 : L->stack.size() + idx; }
    Val Pop(lua_State* L) { Val v = L->stack.back(); L->stack.pop_back(); return v; }
    static Val Num(double n) { Val v; v.t = Val::Num; v.n = n; return v; }
    static Val Str(const std::string& s) { Val v; v.t = Val::Str; v.s = s; return v; }
    lua_State* NewState() {
        states.push_back(std::make_unique<lua_State>());
        return states.back().get();
    }
    static Val* Find(Table& t, const Val& k) {
        for (auto& e : t.kv) if (RawEq(e.first, k)) return &e.second;
        return nullptr;
    }
    static void Set(Table& t, const Val& k, const Val& v) {
        if (Val* e = Find(t, k)) { *e = v; return; }
        t.kv.push_back({k, v});
    }
    static Val Get(Table& t, const Val& k) { Val* e = Find(t, k); return e ? *e : Val(); }
    Val Tab(std::shared_ptr<Table> t) { Val v; v.t = Val::Table_; v.tab = std::move(t); return v; }
    Val tmp;

    // ---- ILuaApi ------------------------------------------------------------------------------------------
    int gettop(lua_State* L) override { return Top(L); }
    void settop(lua_State* L, int i) override {
        size_t want = i >= 0 ? L->base + i : L->stack.size() + i + 1;
        L->stack.resize(want);
    }
    void pushvalue(lua_State* L, int i) override { Val v = *At(L, i); L->stack.push_back(v); }
    void remove(lua_State* L, int i) override { L->stack.erase(L->stack.begin() + Abs(L, i)); }
    void insert(lua_State* L, int i) override { size_t pos = Abs(L, i); Val v = L->stack.back(); L->stack.pop_back(); L->stack.insert(L->stack.begin() + pos, v); }
    void replace(lua_State* L, int i) override { Val* dst = At(L, i); *dst = L->stack.back(); L->stack.pop_back(); }
    void xmove(lua_State* from, lua_State* to, int n) override {
        std::vector<Val> mv(from->stack.end() - n, from->stack.end());
        from->stack.resize(from->stack.size() - n);
        for (auto& v : mv) to->stack.push_back(v);
    }
    int checkstack(lua_State*, int) override { return 1; }
    int type(lua_State* L, int i) override {
        if (i > 0 && L->base + i > L->stack.size()) return kLuaTypeNone;
        if (i < 0 && i > kGlobalsIndex && -i > Top(L)) return kLuaTypeNone;
        return At(L, i)->luaType();
    }
    const char* type_name(lua_State*, int tp) override {
        static const char* n[] = {"nil", "boolean", "userdata", "number", "string", "table", "function", "userdata", "thread"};
        return tp >= 0 && tp < 9 ? n[tp] : "no value";
    }
    int isnumber(lua_State* L, int i) override { return At(L, i)->t == Val::Num; }
    int isstring(lua_State* L, int i) override { return At(L, i)->t == Val::Str || At(L, i)->t == Val::Num; }
    int iscfunction(lua_State* L, int i) override { return At(L, i)->t == Val::CFn; }
    int isuserdata(lua_State* L, int i) override { return At(L, i)->t == Val::UData || At(L, i)->t == Val::Light; }
    int equal(lua_State* L, int a, int b) override { return RawEq(*At(L, a), *At(L, b)); }
    int rawequal(lua_State* L, int a, int b) override { return RawEq(*At(L, a), *At(L, b)); }
    int lessthan(lua_State* L, int a, int b) override { return At(L, a)->n < At(L, b)->n; }
    double tonumber(lua_State* L, int i) override {
        Val* v = At(L, i);
        if (v->t == Val::Num) return v->n;
        if (v->t == Val::Str) { char* e; double d = std::strtod(v->s.c_str(), &e); return *e ? 0.0 : d; }
        return 0.0;
    }
    int toboolean(lua_State* L, int i) override { Val* v = At(L, i); return !(v->t == Val::Nil || (v->t == Val::Bool && !v->b)); }
    const char* tostring(lua_State* L, int i) override { Val* v = At(L, i); return v->t == Val::Str ? v->s.c_str() : nullptr; }
    std::size_t strlen(lua_State* L, int i) override { return At(L, i)->s.size(); }
    lua_CFunction tocfunction(lua_State* L, int i) override { Val* v = At(L, i); return v->t == Val::CFn ? v->fn->cfn : nullptr; }
    void* touserdata(lua_State* L, int i) override {
        Val* v = At(L, i);
        if (v->t == Val::UData) return v->ud->bytes.data();
        return v->t == Val::Light ? v->p : nullptr;
    }
    lua_State* tothread(lua_State* L, int i) override { return At(L, i)->t == Val::Thr ? At(L, i)->th : nullptr; }
    void pushnil(lua_State* L) override { L->stack.push_back(Val()); }
    void pushnumber(lua_State* L, double d) override { L->stack.push_back(Num(d)); }
    void pushlstring(lua_State* L, const char* s, std::size_t n) override { L->stack.push_back(Str(std::string(s, n))); }
    void pushstring(lua_State* L, const char* s) override { if (s) L->stack.push_back(Str(s)); else L->stack.push_back(Val()); }
    void pushcclosure(lua_State* L, lua_CFunction f, int n) override {
        Val v; v.t = Val::CFn; v.fn = std::make_shared<Closure>(); v.fn->cfn = f;
        v.fn->upvals.assign(L->stack.end() - n, L->stack.end());
        L->stack.resize(L->stack.size() - n);
        L->stack.push_back(v);
    }
    void pushboolean(lua_State* L, int b) override { Val v; v.t = Val::Bool; v.b = b != 0; L->stack.push_back(v); }
    void pushlightuserdata(lua_State* L, void* p) override { Val v; v.t = Val::Light; v.p = p; L->stack.push_back(v); }
    void* newuserdata(lua_State* L, std::size_t n) override {
        Val v; v.t = Val::UData; v.ud = std::make_shared<UserData>(); v.ud->bytes.assign(n, 0);
        L->stack.push_back(v);
        return v.ud->bytes.data();
    }
    Table* AsTable(lua_State* L, int i) { Val* v = At(L, i); return v->t == Val::Table_ ? v->tab.get() : nullptr; }
    void gettable(lua_State* L, int i) override { rawget(L, i); }
    void rawget(lua_State* L, int i) override { Table* t = AsTable(L, i); Val k = Pop(L); L->stack.push_back(t ? Get(*t, k) : Val()); }
    void rawgeti(lua_State* L, int i, int n) override { Table* t = AsTable(L, i); L->stack.push_back(t ? Get(*t, Num(n)) : Val()); }
    void newtable(lua_State* L) override { Val v; v.t = Val::Table_; v.tab = std::make_shared<Table>(); L->stack.push_back(v); }
    void getfenv(lua_State* L, int) override { L->stack.push_back(Tab(L->globals)); }
    int getmetatable(lua_State* L, int i) override {
        Val* v = At(L, i);
        std::shared_ptr<Table> m = v->t == Val::Table_ ? v->tab->meta : v->t == Val::UData ? v->ud->meta : nullptr;
        if (!m) return 0;
        L->stack.push_back(Tab(m));
        return 1;
    }
    void settable(lua_State* L, int i) override { rawset(L, i); }
    void rawset(lua_State* L, int i) override {
        Table* t = AsTable(L, i);
        Val v = Pop(L); Val k = Pop(L);
        if (t) { if (v.t == Val::Nil) { auto& kv = t->kv; for (size_t j = 0; j < kv.size(); ++j) if (RawEq(kv[j].first, k)) { kv.erase(kv.begin() + j); break; } } else Set(*t, k, v); }
    }
    void rawseti(lua_State* L, int i, int n) override { Table* t = AsTable(L, i); Val v = Pop(L); if (t) Set(*t, Num(n), v); }
    int setfenv(lua_State* L, int) override { Pop(L); return 1; }
    int setmetatable(lua_State* L, int i) override {
        Val* v = At(L, i);
        Val m = L->stack.back();
        std::shared_ptr<Table> mt = m.t == Val::Table_ ? m.tab : nullptr;
        if (v->t == Val::Table_) v->tab->meta = mt; else if (v->t == Val::UData) v->ud->meta = mt;
        L->stack.pop_back();
        return 1;
    }
    void call(lua_State* L, int nargs, int nresults) override { runCall(L, nargs, nresults); }
    int pcall(lua_State* L, int nargs, int nresults, int) override {
        size_t fnpos = L->stack.size() - nargs - 1;
        try { runCall(L, nargs, nresults); return 0; }
        catch (LuaError& e) { L->stack.resize(fnpos); L->stack.push_back(e.value); return 1; }   // LUA_ERRRUN
    }
    void runCall(lua_State* L, int nargs, int nresults) {
        size_t fnpos = L->stack.size() - nargs - 1;
        Val f = L->stack[fnpos];
        size_t savedBase = L->base; auto savedRunning = L->running;
        L->base = fnpos + 1; L->running = f.fn;
        int n = 0;
        struct Restore { lua_State* L; size_t b; std::shared_ptr<Closure> r; ~Restore() { L->base = b; L->running = r; } } restore{L, savedBase, savedRunning};
        if (f.t == Val::CFn) n = f.fn->cfn(L);
        else if (f.t == Val::LuaFn && f.fn->body) n = f.fn->body(L);
        std::vector<Val> res(L->stack.end() - n, L->stack.end());
        L->stack.resize(fnpos);
        if (nresults >= 0) res.resize(static_cast<size_t>(nresults));
        for (auto& v : res) L->stack.push_back(v);
    }
    int dofile(lua_State*, const char*) override { return 0; }
    int dostring(lua_State*, const char* s) override { dostrings.push_back(s); return dostringResult; }
    int dobuffer(lua_State*, const char*, std::size_t, const char*) override { return 0; }
    int load(lua_State*, lua_Chunkreader, void*, const char*) override { return 0; }
    int loadfile(lua_State*, const char*) override { return 0; }
    int loadbuffer(lua_State* L, const char*, std::size_t, const char*) override {
        // A chunk is a Lua-function stand-in; `chunkBody` lets a test decide what running it does.
        Val v; v.t = Val::LuaFn; v.fn = std::make_shared<Closure>();
        v.fn->body = chunkBody ? chunkBody : [](lua_State*) { return 0; };
        L->stack.push_back(v);
        return 0;
    }
    int error(lua_State*, const char*) override { return 0; }
    int next(lua_State* L, int i) override {
        Table* t = AsTable(L, i);
        Val k = Pop(L);
        size_t idx = 0;
        if (k.t != Val::Nil) { for (idx = 0; t && idx < t->kv.size(); ++idx) if (RawEq(t->kv[idx].first, k)) { ++idx; break; } }
        if (!t || idx >= t->kv.size()) return 0;
        L->stack.push_back(t->kv[idx].first); L->stack.push_back(t->kv[idx].second);
        return 1;
    }
    void concat(lua_State*, int) override {}
    int ref(lua_State* L, int t) override {
        Table* tab = AsTable(L, t);
        Val v = Pop(L);
        if (v.t == Val::Nil) return -1;
        int r = static_cast<int>(tab->kv.size()) + 1000;
        while (Find(*tab, Num(r))) ++r;
        Set(*tab, Num(r), v);
        return r;
    }
    void unref(lua_State* L, int t, int r) override { Table* tab = AsTable(L, t); auto& kv = tab->kv; for (size_t j = 0; j < kv.size(); ++j) if (RawEq(kv[j].first, Num(r))) { kv.erase(kv.begin() + j); break; } }
    int yield(lua_State* L, int n) override { L->yielded = true; return n; }

    lua_State* open() override {
        lua_State* L = NewState();
        L->registry = std::make_shared<Table>();
        L->globals = std::make_shared<Table>();
        L->mainState = L;
        return L;
    }
    void close(lua_State* L) override { closed.push_back(L); }
    void openlib(lua_State* L, int lib) override {
        libs.push_back(lib);
        if (lib == 0) { Val g = Tab(L->globals); Val co; co.t = Val::Table_; co.tab = std::make_shared<Table>(); Set(*L->globals, Str("coroutine"), co); }
    }
    int getgccount(lua_State*) override { return 42; }
    int getgcthreshold(lua_State*) override { return gcThreshold; }
    void setgcthreshold(lua_State*, int n) override { gcThreshold = n; }
    lua_State* newthread(lua_State* L) override {
        lua_State* t = NewState();
        t->registry = L->registry; t->globals = L->globals; t->mainState = L->mainState;
        Val v; v.t = Val::Thr; v.th = t;
        L->stack.push_back(v);
        return t;
    }
    int resume(lua_State* L, int nargs) override {
        size_t fnpos = L->stack.size() - nargs - 1;
        if (L->suspended) fnpos = L->stack.size();   // resuming a suspended body: args are the yield results
        Val f = L->suspended ? Val() : L->stack[fnpos];
        L->yielded = false;
        if (!L->suspended && f.t == Val::LuaFn && f.fn->body) {
            size_t savedBase = L->base; L->base = fnpos + 1;
            int n = f.fn->body(L);
            L->base = savedBase;
            std::vector<Val> res(L->stack.end() - std::max(n, 0), L->stack.end());
            L->stack.resize(fnpos);
            for (auto& v : res) L->stack.push_back(v);
            if (L->yielded) { L->suspended = true; return 0; }   // Lua 5.0: a yield reports status 0
            return 0;
        }
        L->suspended = false;
        L->stack.clear();
        return 0;
    }
    bool getstack(lua_State* L, int) override { return L->suspended; }
    int ccalls(lua_State* L) override { return L->ccalls; }
    lua_State* main_state(lua_State* L) override { return L->mainState; }
    cGZLua5Thread* get_thread_slot(lua_State* L) override { return L->slot; }
    void set_thread_slot(lua_State* L, cGZLua5Thread* t) override { L->slot = t; }
    void pushfstring(lua_State* L, const char* fmt, int arg) override {
        char buf[256]; std::snprintf(buf, sizeof buf, fmt, arg); L->stack.push_back(Str(buf));
    }
    void lerror(lua_State*, const char* msg) override { throw LuaError{Str(msg)}; }
    void checktype(lua_State* L, int narg, int tp) override { if (At(L, narg)->luaType() != tp) throw LuaError{Str("bad argument type")}; }
    void typerror(lua_State*, int, const char* tname) override { throw LuaError{Str(std::string("typerror ") + tname)}; }

    std::vector<std::string> dostrings;
    std::function<int(lua_State*)> chunkBody;   // body of every loadbuffer chunk
    int dostringResult = 0;
    std::vector<lua_State*> closed;
    std::vector<int> libs;

    // test helper: a Lua-function stand-in
    Val MakeLuaFn(std::function<int(lua_State*)> body) {
        Val v; v.t = Val::LuaFn; v.fn = std::make_shared<Closure>(); v.fn->body = std::move(body); return v;
    }
};

}  // namespace lua5
