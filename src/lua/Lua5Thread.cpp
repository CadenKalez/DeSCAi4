#include "lua/Lua5Thread.h"

#include <cstring>

namespace lua5 {

static ILuaApi* g_api = nullptr;
void SetLuaApi(ILuaApi* api) { g_api = api; }
ILuaApi& LuaApi() { return *g_api; }

cGZLua5Thread::cGZLua5Thread(ILuaApi& api, ILuaCompat& compat) : api_(api), compat_(compat) {}

cGZLua5Thread::~cGZLua5Thread() {
    if (owner_) owner_->Release();
}

int cGZLua5Thread::Pause(int nresults) { return api_.yield(state_, nresults); }

bool cGZLua5Thread::OnCreate(ILua5Owner* owner, lua_State* state, int id) {
    id_ = id;
    state_ = state;
    ILua5Owner* old = owner_;
    if (old != owner) {
        if (owner) owner->AddRef();
        owner_ = owner;
        if (old) old->Release();
    }
    // rawset(globals, "_ALERT", alert hook)
    PushString("_ALERT");
    PushCClosure(compat_.AlertExtension(), 0);
    RawSet(kGlobalsIndex);
    return true;
}

bool cGZLua5Thread::OnDestroy() {
    state_ = nullptr;
    if (owner_) {
        ILua5Owner* o = owner_;
        owner_ = nullptr;
        o->Release();
    }
    id_ = -1;
    return true;
}

std::uint32_t cGZLua5Thread::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cGZLua5Thread::IsValid() const {
    if (!state_ || api_.get_thread_slot(state_) != this) return false;
    return id_ != -1;
}

bool cGZLua5Thread::Destroy() {
    if (state_) {
        if (IThreadRegistry* reg = compat_.RegistryFor(state_)) return reg->DestroyThread(state_, id_);
    }
    return false;
}

int cGZLua5Thread::Status() {
    if (state_ && id_ != -1) {
        if (IThreadRegistry* reg = compat_.RegistryFor(state_)) return reg->GetThreadStatus(id_);
    }
    return -1;
}

void cGZLua5Thread::ReportLoadError(int status) {
    if (Type(-1) != kLuaTypeString) return;
    const char* msg = ToString(-1);
    if (!owner_) return;
    if (LuaErrorCallback cb = owner_->GetErrorCallback()) cb(this, status, msg);
}

int cGZLua5Thread::CallProtected(int nargs, int nresults, int errfunc, bool popError) {
    int status;
    GetTop();   // the original evaluates GetTop() once up front; the result is unused
    if (errfunc == 0 && owner_ && owner_->GetErrorCallback()) {
        int base = GetTop() - nargs;
        api_.pushlstring(state_, "_TRACEBACK", 10);
        api_.rawget(state_, kGlobalsIndex);
        api_.insert(state_, base);
        status = api_.pcall(state_, nargs, nresults, base);
        api_.remove(state_, base);
    } else {
        status = api_.pcall(state_, nargs, nresults, errfunc);
    }
    if (status != 0) {
        const char* msg = ToString(-1);
        if (owner_) {
            if (LuaErrorCallback cb = owner_->GetErrorCallback()) cb(this, status, msg);
        }
        if (popError) Pop(1);
    }
    return status;
}

int cGZLua5Thread::LoadFile(const char* file) {
    int status = api_.loadfile(state_, file);
    if (status != 0) ReportLoadError(status);
    return status;
}

int cGZLua5Thread::LoadStringA(const char* s) {
    int status = api_.loadbuffer(state_, s, std::strlen(s), s);
    if (status != 0) ReportLoadError(status);
    return status;
}

int cGZLua5Thread::LoadBuffer(const char* buf, int len, const char* name) {
    int status = api_.loadbuffer(state_, buf, static_cast<std::size_t>(len), name);
    if (status != 0) ReportLoadError(status);
    return status;
}

void cGZLua5Thread::Register(lua_CFunction f, const char* name) {
    api_.pushstring(state_, name);
    api_.pushcclosure(state_, f, 0);
    api_.settable(state_, kGlobalsIndex);
}

void cGZLua5Thread::GetGlobal(const char* name) {
    api_.pushstring(state_, name);
    api_.gettable(state_, kGlobalsIndex);
}

void cGZLua5Thread::SetGlobal(const char* name) {
    api_.pushstring(state_, name);
    api_.insert(state_, -2);
    api_.settable(state_, kGlobalsIndex);
}

int cGZLua5Thread::Tag(int index) {
    api_.pushvalue(state_, index);
    api_.pushvalue(state_, -1);
    PushCClosure(compat_.TagFn(), 0);
    api_.replace(state_, -3);
    if (CallProtected(1, 1, 0, true) == 0) {
        int tag = CvtTrunc32(api_.tonumber(state_, -1));
        api_.settop(state_, -2);
        if (tag != 0) return tag;
    }
    return -1;
}

void cGZLua5Thread::GetTagMethod(int tag, const char* event) {
    PushCClosure(compat_.GetTagMethodFn(), 0);
    api_.pushnumber(state_, tag);
    api_.pushstring(state_, event);
    if (CallProtected(2, 1, 0, true) != 0) api_.pushnil(state_);
}

void cGZLua5Thread::SetTagMethod(int tag, const char* event) {
    PushCClosure(compat_.SetTagMethodFn(), 0);
    api_.pushnumber(state_, tag);
    api_.pushstring(state_, event);
    api_.pushvalue(state_, -4);
    CallProtected(3, 0, 0, true);
    api_.settop(state_, -2);
}

int cGZLua5Thread::NewTag() {
    PushCClosure(compat_.NewTagFn(), 0);
    if (CallProtected(0, 1, 0, true) != 0) return -1;
    int tag = CvtTrunc32(api_.tonumber(state_, 1));
    api_.settop(state_, -2);
    return tag > 0 ? tag : -1;
}

int cGZLua5Thread::CopyTagMethods(int dst, int src) {
    PushCClosure(compat_.CopyTagMethodsFn(), 0);
    api_.pushnumber(state_, dst);
    api_.pushnumber(state_, src);
    CallProtected(2, 0, 0, true);
    return dst;
}

}  // namespace lua5
