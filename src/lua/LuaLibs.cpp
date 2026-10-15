#include "lua/LuaLibs.h"

#include <cstring>

namespace lua5 {

ThreadRegistry* GetThreadRegistry(lua_State* L) {
    ILuaApi& api = LuaApi();
    cGZLua5Thread* main = api.get_thread_slot(api.main_state(L));
    return main ? main->GetRegistry() : nullptr;
}

namespace threadlib {

bool OpenLib(cGZLua5Thread& t) {
    int top = t.GetTop();
    t.GetGlobal("coroutine");
    bool ok = t.Type(-1) == kLuaTypeTable;
    if (ok) {
        static const struct { const char* name; lua_CFunction fn; } kFns[] = {
            {"resume", Resume}, {"create", Create}, {"wrap", Wrap},     {"destroy", Destroy}, {"valid", Valid},
            {"id", ID},         {"status", Status}, {"class", Class},   {"parent", Parent},
        };
        for (const auto& f : kFns) {
            t.PushString(f.name);
            t.PushCClosure(f.fn, 0);
            t.RawSet(-3);
        }
    }
    t.SetTop(top);
    return ok;
}

int Resume(lua_State* L) {
    ILuaApi& api = LuaApi();
    int n = api.gettop(L);
    if (n > 0) {
        ThreadRegistry* reg = GetThreadRegistry(L);
        int id = CvtTrunc32(api.tonumber(L, 1));
        if (reg->ResumeThread(L, id, n - 1) == 0) {
            api.pushboolean(L, 1);
            int top = api.gettop(L);
            api.insert(L, 1 - top);
            return top - 1;
        }
    }
    api.pushboolean(L, 0);
    api.insert(L, -2);
    return 2;
}

int Create(lua_State* L) {
    ILuaApi& api = LuaApi();
    int n = api.gettop(L);
    double result;
    if (n < 1 || api.type(L, 1) != kLuaTypeFunction || api.iscfunction(L, 1)) {
        api.lerror(L, "Lua function is expected as the first parameter.");
        result = -1.0;
    } else {
        int classId = static_cast<int>(kDefaultThreadClassId);
        std::uint32_t flags = 0;
        if (n > 1) {
            api.checktype(L, 2, kLuaTypeString);
            for (const char* c = api.tostring(L, 2); *c; ++c) {
                switch (*c) {
                    case 'L': case 'l': flags |= kThreadFlagKeepAlive; break;
                    case 'P': case 'p': flags |= kThreadFlagChild; break;
                    case 'R': case 'r': flags |= kThreadFlagParentResumes; break;
                    default: break;
                }
            }
            // Quirk kept: the class id is read from argument 2 (the flag string), not argument 3.
            if (n > 2) classId = CvtTrunc32(api.tonumber(L, 2));
        }
        ThreadRegistry* reg = GetThreadRegistry(L);
        api.settop(L, -n);
        result = reg->CreateThread(L, static_cast<int>(flags), classId);
    }
    api.pushnumber(L, result);
    return 1;
}

int Wrap(lua_State* L) {
    Create(L);
    LuaApi().pushcclosure(L, WrapInternal, 1);
    return 1;
}

int Destroy(lua_State* L) {
    ILuaApi& api = LuaApi();
    ThreadRegistry* reg = GetThreadRegistry(L);
    cGZLua5Thread* self = api.get_thread_slot(L);
    int current = self ? self->GetID() : -1;
    api.gettop(L);
    int id = ThreadIdParam(L);
    const char* fmt = "Bad Lua thread ID: %d";
    if (id < 1) {
        api.pushboolean(L, 0);
        if (id == 0) {
            api.pushfstring(L, "Destruction of the main Lua thread is disallowed.", 0);
            return 2;
        }
    } else if (reg->IsValidThread(id)) {
        bool ok = reg->DestroyThread(L, id);
        api.pushboolean(L, ok);
        if (ok) {
            if (current != id) return 1;
            if (api.ccalls(L) == 0) return api.yield(L, 1);
            return 1;
        }
        fmt = "Lua thread %d can only be killed by itlself or its parent.";
    } else {
        api.pushboolean(L, 0);
    }
    api.pushfstring(L, fmt, id);
    return 2;
}

int Valid(lua_State* L) {
    int id = ThreadIdParam(L);
    bool valid = id != -1 && GetThreadRegistry(L)->IsValidThread(id);
    LuaApi().pushboolean(L, valid);
    return 1;
}

int ID(lua_State* L) {
    ILuaApi& api = LuaApi();
    cGZLua5Thread* self = api.get_thread_slot(L);
    api.pushnumber(L, self ? self->GetID() : -1.0);
    return 1;
}

int Status(lua_State* L) {
    int id = ThreadIdParam(L);
    unsigned st = static_cast<unsigned>(GetThreadRegistry(L)->GetThreadStatus(id));
    static const char* const kNames[3] = {"dead", "suspended", "running"};
    LuaApi().pushstring(L, st < 3 ? kNames[st] : "invalid");
    return 1;
}

int Class(lua_State* L) {
    ThreadRegistry* reg = GetThreadRegistry(L);
    const ThreadRegistry::Thread* t = reg->Find(ThreadIdParam(L));
    LuaApi().pushnumber(L, t ? t->classId : 0.0);
    return 1;
}

int Parent(lua_State* L) {
    ThreadRegistry* reg = GetThreadRegistry(L);
    LuaApi().pushnumber(L, reg->GetParentID(ThreadIdParam(L)));
    return 1;
}

int WrapInternal(lua_State* L) {
    ILuaApi& api = LuaApi();
    int id = CvtTrunc32(api.tonumber(L, kGlobalsIndex - 1));   // upvalue 1
    api.pushnumber(L, id);
    api.insert(L, 1);
    ThreadRegistry* reg = GetThreadRegistry(L);
    int resumeId = CvtTrunc32(api.tonumber(L, 1));
    reg->ResumeThread(L, resumeId, api.gettop(L) - 1);
    return api.gettop(L) - 1;
}

int ThreadIdParam(lua_State* L) {
    ILuaApi& api = LuaApi();
    if (api.gettop(L) < 1) {
        cGZLua5Thread* self = api.get_thread_slot(L);
        return self ? self->GetID() : -1;
    }
    if (api.type(L, -1) != kLuaTypeNumber) api.typerror(L, 1, "numeric thread ID");
    return CvtTrunc32(api.tonumber(L, -1));
}

}  // namespace threadlib

namespace compat4 {

namespace {
constexpr double kTagKey = 187693250.0;        // metatable[kTagKey] = tag number
constexpr double kMapKey = 1262289877.0;       // registry[kMapKey] = tag -> metatable map
int g_lastTag = 0;
}  // namespace

bool OpenLib(cGZLua5Thread& t, const char* compatScript) {
    t.AddRef();
    bool ok = t.DoString(compatScript) == 0;
    if (ok) {
        t.Register(Tag, "tag");
        t.Register(SetTag, "settag");
        t.Register(SetTagMethod, "settagmethod");
        t.Register(GetTagMethod, "gettagmethod");
        t.Register(NewTag, "newtag");
        t.Register(CopyTagMethods, "copytagmethods");
        t.Register(Globals, "globals");
    }
    t.Release();
    return ok;
}

int NewTag(lua_State* L) {
    ILuaApi& api = LuaApi();
    api.settop(L, 0);
    api.pushnumber(L, ++g_lastTag);
    return 1;
}

int SetTag(lua_State* L) {
    ILuaApi& api = LuaApi();
    api.settop(L, 2);
    int tag = CvtTrunc32(api.tonumber(L, -1));
    api.settop(L, -2);
    PushTagMetaTable(L, tag);
    api.setmetatable(L, -2);
    return 1;
}

void PushTagMetaTable(lua_State* L, int tag) {
    ILuaApi& api = LuaApi();
    PushTagMetaTableMap(L);
    int map = api.gettop(L);
    api.pushnumber(L, tag);
    api.rawget(L, map);
    if (api.type(L, -1) != kLuaTypeTable) {
        api.settop(L, -2);
        api.newtable(L);
        api.pushnumber(L, tag);
        api.pushvalue(L, -2);
        api.rawset(L, map);
        api.pushnumber(L, kTagKey);
        api.pushnumber(L, tag);
        api.rawset(L, -3);
    }
    api.replace(L, -2);
}

void SetTagOnTop(lua_State* L, int tag) {
    ILuaApi& api = LuaApi();
    api.pushcclosure(L, SetTag, 0);
    api.pushvalue(L, -2);
    api.pushnumber(L, tag);
    api.pcall(L, 2, 0, 0);
}

int SetTagMethod(lua_State* L) {
    ILuaApi& api = LuaApi();
    api.settop(L, 3);
    PushTagMetaTable(L, CvtTrunc32(api.tonumber(L, -3)));
    const char* meta = Lua4EventToMetaMethod(api.tostring(L, -3));
    if (!meta) {
        api.pushnil(L);
    } else {
        api.pushstring(L, meta);
        api.rawget(L, -2);
    }
    int ref = api.ref(L, kRegistryIndex);
    api.pushstring(L, meta);
    api.pushvalue(L, -3);
    api.rawset(L, -3);
    api.settop(L, 0);
    api.rawgeti(L, kRegistryIndex, ref);
    api.unref(L, kRegistryIndex, ref);
    return 1;
}

const char* Lua4EventToMetaMethod(const char* event) {
    if (!event) return nullptr;
    static const struct { const char* lua4; const char* meta; } kMap[] = {
        {"index", "__index"},     {"gettable", "__index"}, {"newindex", "__newindex"}, {"settable", "__newindex"},
        {"gc", "__gc"},           {"add", "__add"},        {"sub", "__sub"},           {"mul", "__mul"},
        {"div", "__div"},         {"pow", "__pow"},        {"unm", "__unm"},           {"le", "__le"},
        {"concat", "__concat"},   {"function", "__call"},
    };
    for (const auto& m : kMap)
        if (std::strcmp(event, m.lua4) == 0) return m.meta;
    return nullptr;
}

int GetTagMethod(lua_State* L) {
    ILuaApi& api = LuaApi();
    api.settop(L, 2);
    if (api.type(L, -1) != kLuaTypeNil) {
        const char* event = api.tostring(L, -1);
        int tag = CvtTrunc32(api.tonumber(L, -2));
        PushTagMetaTableMap(L);
        api.pushnumber(L, tag);
        api.rawget(L, -2);
        if (api.type(L, -1) == kLuaTypeTable) {
            if (const char* meta = Lua4EventToMetaMethod(event)) {
                api.pushstring(L, meta);
                api.rawget(L, -2);
                return 1;
            }
        }
        api.pushnil(L);
    }
    return 1;
}

void PushTagMetaTableMap(lua_State* L) {
    ILuaApi& api = LuaApi();
    api.pushnumber(L, kMapKey);
    api.rawget(L, kRegistryIndex);
    if (api.type(L, -1) == kLuaTypeTable) return;
    api.settop(L, -2);
    api.newtable(L);
    api.pushnumber(L, kMapKey);
    api.pushvalue(L, -2);
    api.rawset(L, kRegistryIndex);
}

int Tag(lua_State* L) {
    ILuaApi& api = LuaApi();
    api.settop(L, 1);
    if (api.getmetatable(L, -1) != 0) {
        api.pushnumber(L, kTagKey);
        api.rawget(L, -2);
        api.replace(L, -2);
        if (api.isnumber(L, -1) != 0) return 1;
    }
    api.settop(L, 0);
    api.pushnumber(L, -1.0);
    return 1;
}

int CopyTagMethods(lua_State* L) {
    ILuaApi& api = LuaApi();
    api.settop(L, 2);
    int dst = CvtTrunc32(api.tonumber(L, -2));
    int src = CvtTrunc32(api.tonumber(L, -1));
    PushTagMetaTable(L, src);
    int srcIdx = api.gettop(L);
    PushTagMetaTable(L, dst);
    int dstIdx = api.gettop(L);
    if (api.type(L, -1) == kLuaTypeTable && api.type(L, -1) == kLuaTypeTable) {
        api.pushnil(L);
        while (api.next(L, srcIdx) != 0) {
            if (api.type(L, -2) == kLuaTypeString) {
                api.pushvalue(L, -2);
                api.pushvalue(L, -2);
                api.rawset(L, dstIdx);
            }
            api.settop(L, -2);
        }
    }
    api.settop(L, 0);
    api.pushnumber(L, dst);
    return 1;
}

int Globals(lua_State* L) {
    LuaApi().pushvalue(L, kGlobalsIndex);
    return 1;
}

void PushUserTag(lua_State* L, void* p, int tag) {
    ILuaApi& api = LuaApi();
    api.newuserdata(L, 8);
    *static_cast<void**>(api.touserdata(L, -1)) = p;
    SetTagOnTop(L, tag);
}

}  // namespace compat4
}  // namespace lua5
