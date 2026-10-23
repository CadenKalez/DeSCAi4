// IScriptLua — the slice of cISCLua (plus the SCLuaHelp free helpers) used by the advisor script tables. The vslot
// offsets are the original cISCLua vtable offsets; the Lua-API meaning of the ones marked [inferred] was derived from
// the callers (Lua 5.0 semantics: indices positive from the bottom, negative from the top).
#pragma once
#include <cstddef>
#include <cstdint>
#include <list>

namespace sc4 {

constexpr int kScriptLuaTypeNumber = 3;   // Type() result for numbers
constexpr int kScriptLuaTypeString = 4;   // Type() result for strings

class IScriptLua {
public:
    virtual ~IScriptLua() = default;
    virtual int GetTop() = 0;                                   // 0x30
    virtual void SetTop(int top) = 0;                           // 0x38
    virtual void Pop(int count) = 0;                            // 0x58
    virtual int Type(int index) = 0;                            // 0x60 [inferred] lua_type
    virtual bool IsNumber(int index) = 0;                       // 0x70
    virtual bool IsBoolean(int index) = 0;                      // 0x90 [inferred]
    virtual double ToNumber(int index) = 0;                     // 0xb8
    virtual const char* ToString(int index) = 0;                // 0xc0
    virtual bool ToBoolean(int index) = 0;                      // 0xe8 [inferred]
    virtual void PushNumber(double value) = 0;                  // 0xf8
    virtual void PushString(const char* text) = 0;              // 0x108
    virtual void GetGlobal(const char* name) = 0;               // 0x128 pushes the global
    virtual void RawGet(int tableIndex) = 0;                    // 0x130 pops the key, pushes table[key]
    virtual void GetRef(int ref) = 0;                           // 0x158 pushes registry[ref]
    virtual int Ref(bool lockValue) = 0;                        // 0x190 pops the top into the registry, returns its ref
    virtual int CallProtected(int argCount, int resultCount) = 0;   // 0x198 0 = ok
    virtual int DoString(const char* chunk) = 0;                // 0x1b0 [inferred] 0 = ok
    virtual int DoBuffer(const char* buffer, std::size_t size, const char* name) = 0;   // 0x1b8 [inferred]
    virtual void Unref(int ref) = 0;                            // 0x1f0
    virtual int ObjLen(int index) = 0;                          // 0x200 [inferred] table length
    virtual bool IsFunction(int index) = 0;                     // 0x238
    virtual bool IsTable(int index) = 0;                        // 0x240
    // SCLuaHelp (free functions in the original)
    virtual bool GetField(const char* name, int tableIndex) = 0;   // SCLuaHelp::GetField @ 0x1001e73c8: pushes table[name], false when nil
    virtual void ReadTableAsList(std::list<std::uint32_t>& out, int tableIndex) = 0;   // @ 0x1001e6dd6 (appends the array part)
};

}  // namespace sc4
