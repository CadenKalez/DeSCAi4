// The Lua-visible libraries the game installs on top of the C API:
//  * nsGZLua5ThreadLib (0x100697760..0x100698288): the `coroutine` table extensions (resume/create/wrap/destroy/
//    valid/id/status/class/parent) built on ThreadRegistry.
//  * nsGZLua5CompatWithLua4 (0x100696ee0..0x1006976..): Lua 4 tag/tag-method emulation using metatables, plus the
//    globals tag(), settag(), settagmethod(), gettagmethod(), newtag(), copytagmethods(), globals().
// All of them are lua_CFunctions: they only get a lua_State* and reach the API through LuaApi().
#pragma once
#include "lua/Lua5Thread.h"
#include "lua/ThreadRegistry.h"

namespace lua5 {

// The registry of the cGZLua5 that owns `L` (null if the main thread has no registry).
ThreadRegistry* GetThreadRegistry(lua_State* L);

namespace threadlib {
bool OpenLib(cGZLua5Thread& mainThread);
int Resume(lua_State* L);
int Create(lua_State* L);
int Wrap(lua_State* L);
int Destroy(lua_State* L);
int Valid(lua_State* L);
int ID(lua_State* L);
int Status(lua_State* L);
int Class(lua_State* L);
int Parent(lua_State* L);
int WrapInternal(lua_State* L);
int ThreadIdParam(lua_State* L);
}  // namespace threadlib

namespace compat4 {
// Supplies the Lua source the original runs at OpenLib time (kGZLua5CompatWithLua4ScriptString).
bool OpenLib(cGZLua5Thread& mainThread, const char* compatScript);
int NewTag(lua_State* L);
int SetTag(lua_State* L);
void PushTagMetaTable(lua_State* L, int tag);
void SetTagOnTop(lua_State* L, int tag);        // lua_SetTag
int SetTagMethod(lua_State* L);
const char* Lua4EventToMetaMethod(const char* event);
int GetTagMethod(lua_State* L);
void PushTagMetaTableMap(lua_State* L);
int Tag(lua_State* L);
int CopyTagMethods(lua_State* L);
int Globals(lua_State* L);
void PushUserTag(lua_State* L, void* p, int tag);
}  // namespace compat4

// ILuaCompat over the two libraries above; the _ALERT hook is supplied by the host.
class LuaCompat : public ILuaCompat {
public:
    explicit LuaCompat(lua_CFunction alert) : alert_(alert) {}
    lua_CFunction AlertExtension() override { return alert_; }
    lua_CFunction TagFn() override { return compat4::Tag; }
    lua_CFunction GetTagMethodFn() override { return compat4::GetTagMethod; }
    lua_CFunction SetTagMethodFn() override { return compat4::SetTagMethod; }
    lua_CFunction NewTagFn() override { return compat4::NewTag; }
    lua_CFunction CopyTagMethodsFn() override { return compat4::CopyTagMethods; }
    void SetTagDirect(lua_State* L, int tag) override { compat4::SetTagOnTop(L, tag); }
    void PushUserTag(lua_State* L, void* ud, int tag) override { compat4::PushUserTag(L, ud, tag); }
    IThreadRegistry* RegistryFor(lua_State* L) override { return GetThreadRegistry(L); }

private:
    lua_CFunction alert_;
};

}  // namespace lua5
