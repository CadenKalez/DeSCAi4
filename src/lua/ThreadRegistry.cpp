#include "lua/ThreadRegistry.h"

#include <algorithm>

namespace lua5 {

ThreadRegistry::~ThreadRegistry() {
    if (owner_) owner_->Release();
}

const ThreadRegistry::Thread* ThreadRegistry::Find(int id) const {
    auto it = threads_.find(id);
    return it == threads_.end() ? nullptr : &it->second;
}

ThreadRegistry::Thread* ThreadRegistry::FindMutable(int id) {
    auto it = threads_.find(id);
    return it == threads_.end() ? nullptr : &it->second;
}

lua_State* ThreadRegistry::GetThreadContext(int id) {
    const Thread* t = Find(id);
    return t ? t->state : nullptr;
}

bool ThreadRegistry::Init(ILua5Owner* owner, lua_State* mainState) {
    if (owner_ != owner) {
        if (owner) owner->AddRef();
        ILua5Owner* old = owner_;
        owner_ = owner;
        if (old) old->Release();
    }
    cGZLua5Thread* main = owner_->AsThread();
    if (!main->OnCreate(owner, mainState, nextId_)) return false;
    // registry[mainState] = this registry
    main->PushLightUserData(mainState);
    main->PushLightUserData(this);
    main->RawSet(kRegistryIndex);
    api_.set_thread_slot(mainState, main);
    main->AddRef();
    Thread t;
    t.state = mainState;
    t.flags = kThreadFlagRunning;
    threads_[nextId_++] = t;
    return true;
}

bool ThreadRegistry::Shutdown() {
    owner_->AsThread();   // the original evaluates this slot and ignores the result
    while (!threads_.empty()) DestroyThread(nullptr, threads_.begin()->first);
    if (owner_) {
        ILua5Owner* o = owner_;
        owner_ = nullptr;
        o->Release();
    }
    return true;
}

int ThreadRegistry::CreateThread(lua_State* L, int flags, int classId) {
    lua_State* nl = api_.newthread(L);
    api_.xmove(L, nl, 2);

    int id = -1;
    lua_State* parent = nullptr;
    cGZLua5Thread* obj = factory_ ? factory_->CreateThreadObject(static_cast<unsigned>(classId)) : nullptr;
    if (obj && obj->OnCreate(owner_, nl, nextId_)) {
        bool ok = true;
        if (flags & kThreadFlagChild) {
            cGZLua5Thread* parentObj = api_.get_thread_slot(L);
            Thread* p = parentObj ? FindMutable(parentObj->GetID()) : nullptr;
            if (!p) {
                ok = false;
            } else {
                parent = L;
                p->children.push_back(nl);
            }
        }
        if (ok) id = nextId_++;
    }
    if (id != -1) {
        Thread t;
        t.state = nl;
        t.parent = parent;
        t.flags = static_cast<std::uint32_t>(flags);
        t.classId = classId;
        threads_[id] = std::move(t);
        api_.set_thread_slot(nl, obj);
        obj->AddRef();
        api_.pushlightuserdata(nl, nl);
        api_.pushvalue(nl, -2);
        api_.rawset(nl, kRegistryIndex);
        api_.settop(nl, 1);
    }
    if (obj) obj->Release();
    return id;
}

int ThreadRegistry::ResumeThread(lua_State* L, int id, int nargs) {
    Thread* t = FindMutable(id);
    const char* msg;
    if (!t) {
        msg = "Resuming Lua thread with invalid ID: %d.";
    } else if (t->flags & kThreadFlagRunning) {
        msg = "Lua thread %d is already running.";
    } else {
        std::uint32_t flags = t->flags;
        lua_State* ts = t->state;
        if (!(flags & kThreadFlagParentResumes) || t->parent == L) {
            api_.xmove(L, ts, nargs);
            t->flags |= kThreadFlagRunning;
            int status = api_.resume(ts, nargs);
            if (Thread* again = FindMutable(id)) again->flags &= ~kThreadFlagRunning;
            api_.xmove(ts, L, api_.gettop(ts));
            if (flags & kThreadFlagKeepAlive) return status;
            if (GetThreadStatus(id) != 0) return status;
            DestroyThread(ts, id);
            return status;
        }
        msg = "Lua thread %d is only resumable by its parent.";
    }
    api_.pushfstring(L, msg, id);
    return 1;
}

bool ThreadRegistry::IsValidThread(int id) {
    Thread* t = FindMutable(id);
    if (!t) return false;
    cGZLua5Thread* obj = api_.get_thread_slot(t->state);
    return obj && obj->GetID() == id;
}

bool ThreadRegistry::DestroyThread(lua_State* caller, int id) {
    Thread* t = FindMutable(id);
    if (!t) return false;
    const std::uint32_t flags = t->flags;
    if ((flags & kThreadFlagChild) && t->state != caller && caller != nullptr && t->parent != caller) return false;

    // Children go first (a child without an interface object would stall the original; treat it as failure).
    while (!t->children.empty()) {
        cGZLua5Thread* child = api_.get_thread_slot(t->children.back());
        if (!child) return false;
        DestroyThread(t->state, child->GetID());
        t = FindMutable(id);
        if (!t) return false;
    }
    lua_State* ts = t->state;
    cGZLua5Thread* obj = api_.get_thread_slot(ts);
    if (!obj) return false;
    if (!obj->OnDestroy()) return false;
    if ((flags & kThreadFlagChild) && t->parent) {
        if (cGZLua5Thread* parentObj = api_.get_thread_slot(t->parent)) {
            if (Thread* p = FindMutable(parentObj->GetID())) {
                auto it = std::find(p->children.begin(), p->children.end(), ts);
                if (it != p->children.end()) {
                    *it = p->children.back();
                    p->children.pop_back();
                }
            }
        }
    }
    api_.pushlightuserdata(ts, ts);
    api_.pushnil(ts);
    api_.rawset(ts, kRegistryIndex);
    obj->Release();
    api_.set_thread_slot(ts, nullptr);
    threads_.erase(id);
    return true;
}

int ThreadRegistry::GetThreadStatus(int id) {
    Thread* t = FindMutable(id);
    if (!t) return -1;
    if (t->flags & kThreadFlagRunning) return 2;
    if (!api_.getstack(t->state, 0) && api_.gettop(t->state) == 0) return 0;
    return 1;
}

int ThreadRegistry::GetParentID(int id) {
    Thread* t = FindMutable(id);
    if (!t || id == -1 || !t->parent) return -1;
    cGZLua5Thread* parentObj = api_.get_thread_slot(t->parent);
    if (!parentObj) return -1;
    return FindMutable(parentObj->GetID()) ? parentObj->GetID() : -1;
}

}  // namespace lua5
