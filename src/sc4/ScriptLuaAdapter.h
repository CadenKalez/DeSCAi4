// cISCLuaAdapter — implements the advisor/automata script seam (IScriptLua) on the real cISCLua of src/lua. This also settles the vslot
// meanings the seam documents as [inferred]: Type 0x60, IsBoolean 0x90, ToBoolean 0xe8, GetTable 0x130 (the seam's RawGet), DoString
// 0x1b0, DoBuffer 0x1b8 and GetN 0x200 (the seam's ObjLen) all match cISCLua's table.
#pragma once
#include "lua/ISCLua.h"
#include "lua/SCLuaHelp.h"
#include "sc4/ScriptLua.h"

namespace sc4 {

class cISCLuaAdapter final : public IScriptLua {
public:
    explicit cISCLuaAdapter(cISCLua* lua) : lua_(lua) {}
    int GetTop() override { return lua_->GetTop(); }
    void SetTop(int top) override { lua_->SetTop(top); }
    void Pop(int count) override { lua_->Pop(count); }
    int Type(int index) override { return lua_->Type(index); }
    bool IsNumber(int index) override { return lua_->IsNumber(index); }
    bool IsBoolean(int index) override { return lua_->IsBoolean(index); }
    double ToNumber(int index) override { return lua_->ToNumber(index); }
    const char* ToString(int index) override { return lua_->ToString(index); }
    bool ToBoolean(int index) override { return lua_->ToBoolean(index); }
    void PushNumber(double value) override { lua_->PushNumber(value); }
    void PushString(const char* text) override { lua_->PushString(text); }
    void GetGlobal(const char* name) override { lua_->GetGlobal(name); }
    void RawGet(int tableIndex) override { lua_->GetTable(tableIndex); }
    void GetRef(int ref) override { lua_->GetRef(ref); }
    int Ref(bool lockValue) override { return lua_->Ref(lockValue ? 1 : 0); }
    int CallProtected(int argCount, int resultCount) override { return lua_->CallProtected(argCount, resultCount); }
    int DoString(const char* chunk) override { return lua_->DoString(chunk); }
    int DoBuffer(const char* buffer, std::size_t size, const char* name) override { return lua_->DoBuffer(buffer, static_cast<int>(size), name); }
    void Unref(int ref) override { lua_->Unref(ref); }
    int ObjLen(int index) override { return lua_->GetN(index); }
    bool IsFunction(int index) override { return lua_->IsFunction(index); }
    bool IsTable(int index) override { return lua_->IsTable(index); }
    bool GetField(const char* name, int tableIndex) override { return SCLuaHelp::GetField(lua_, name, tableIndex); }
    void ReadTableAsList(std::list<std::uint32_t>& out, int tableIndex) override { SCLuaHelp::ReadTableAsList(lua_, out, tableIndex); }
    cISCLua* Lua() const { return lua_; }

private:
    cISCLua* lua_;
};

}  // namespace sc4
