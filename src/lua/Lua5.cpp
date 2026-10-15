#include "lua/Lua5.h"

namespace lua5 {

cGZLua5::cGZLua5(ILuaApi& api, ILuaCompat& compat, IThreadFactory* factory, const char* compatScript)
    : cGZLua5Thread(api, compat), registry_(api, factory), errorReporter_(DefaultErrorReporter),
      compatScript_(compatScript) {}

cGZLua5::~cGZLua5() {
    if (state_) Close();
}

bool cGZLua5::Open(int libs) {
    if (state_ != nullptr) return false;
    lua_State* L = api_.open();
    if (!L) return false;
    if (!registry_.Init(this, L)) return false;
    api_.openlib(state_, 0);
    if (libs & kOpenIo) api_.openlib(state_, 1);
    if (libs & kOpenString) api_.openlib(state_, 2);
    if (libs & kOpenMath) api_.openlib(state_, 3);
    if (libs & kOpenDebug) api_.openlib(state_, 4);
    if (libs & kOpenTable) api_.openlib(state_, 5);
    if (libs & kOpenCompat4) compat4::OpenLib(*this, compatScript_);
    threadlib::OpenLib(*this);
    api_.settop(state_, 0);
    return true;
}

bool cGZLua5::Close() {
    lua_State* L = state_;
    if (L) {
        registry_.Shutdown();
        api_.close(L);
    }
    return true;
}

int cGZLua5::CreateThread(cGZLua5Thread** out, int flags, unsigned classId) {
    *out = nullptr;
    int id = registry_.CreateThread(state_, flags, static_cast<int>(classId));
    if (id != -1) {
        lua_State* ctx = registry_.GetThreadContext(id);
        cGZLua5Thread* t = api_.get_thread_slot(ctx);
        *out = t;
        t->AddRef();
    }
    return id;
}

cGZLua5Thread* cGZLua5::GetThread(int id) {
    lua_State* ctx = registry_.GetThreadContext(id);
    return ctx ? api_.get_thread_slot(ctx) : nullptr;
}

void cGZLua5::DestroyThread(int id) { registry_.DestroyThread(state_, id); }

bool cGZLua5::ResumeThread(int id, int nargs, int* nresults) {
    int before = api_.gettop(state_);
    int status = registry_.ResumeThread(state_, id, nargs);
    if (status != 0 && errorReporter_) {
        lua_State* ctx = registry_.GetThreadContext(id);
        cGZLua5Thread* t = ctx ? api_.get_thread_slot(ctx) : nullptr;
        const char* msg = api_.tostring(state_, -1);
        errorReporter_(t, status, msg ? msg : "<unknown description>");
    }
    unsigned count = static_cast<unsigned>(nargs - before + api_.gettop(state_));
    if (nresults) *nresults = static_cast<int>(count);
    else api_.settop(state_, static_cast<int>(~count));
    return status == 0;
}

void cGZLua5::SetErrorReporter(LuaErrorCallback fn, bool useDefaultWhenNull) {
    LuaErrorCallback chosen = useDefaultWhenNull ? &DefaultErrorReporter : fn;
    if (fn) chosen = fn;
    errorReporter_ = chosen;
}

LuaInterface cGZLua5::QueryInterface(std::uint32_t iid) {
    switch (iid) {
        case kIID_cIGZLua5: AddRef(); return LuaInterface::Lua5;
        case kIID_cIGZLua5Thread: AddRef(); return LuaInterface::Thread;
        case 1: AddRef(); return LuaInterface::Self;     // cIGZUnknown
        default: return LuaInterface::None;
    }
}

void cGZLua5::DefaultErrorReporter(cGZLua5Thread*, int, const char*) {}

}  // namespace lua5
