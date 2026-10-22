// SCLuaHelp — helpers that read Lua tables through cISCLua (0x1001e61e0..0x1001e754c, 0x1001e73c8, 0x1001e741c).
// Every ReadTableField pushes `name`, indexes the table at `tableIndex` and pops the result again, so the stack is
// left as found. Number conversions follow the original: floats go through a double, unsigned values truncate the
// double through a 64-bit integer, signed ints round to nearest (the shipped build used the 1.5*2^23 magic-number trick).
//
// Not recovered here (need the resource manager / file system, see SCLUAHELP-1): LoadResourceScript (both overloads),
// LUAExtension_dofile_from_resource, LUAExtension_print (needs the cISCLua-from-lua_State lookup), UpdateResourcesFromDirectory,
// LoadPackageScripts.
#pragma once
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "lua/ISCLua.h"
#include "rz/cRZString.h"

namespace SCLuaHelp {

// Push `name` from the globals table; true (leaving the value on the stack) unless it is nil.
bool GetGlobal(cISCLua* lua, const char* name);
// Push table[name]; true (leaving the value on the stack) unless it is nil.
bool GetField(cISCLua* lua, const char* name, int tableIndex);

// false (and the out value cleared/zero) when the field is nil. The bool overload is true only for a real boolean.
bool ReadTableField(cISCLua* lua, const char* name, cRZString& out, int tableIndex);
bool ReadTableField(cISCLua* lua, const char* name, float& out, int tableIndex);
bool ReadTableField(cISCLua* lua, const char* name, unsigned& out, int tableIndex);
bool ReadTableField(cISCLua* lua, const char* name, int& out, int tableIndex);
bool ReadTableField(cISCLua* lua, const char* name, bool& out, int tableIndex);

// A table becomes its array part (1..getn), nil entries stay default; any other value becomes a one-element array.
// false for an empty table.
bool ReadTableAsArray(cISCLua* lua, std::vector<cRZString>& out, int index);
bool ReadTableAsArray(cISCLua* lua, std::vector<float>& out, int index);
bool ReadTableAsArray(cISCLua* lua, std::vector<unsigned>& out, int index);
bool ReadTableAsArray(cISCLua* lua, std::vector<int>& out, int index);

// Walk every entry with lua_next; true when the result is non-empty. Negative `index` is made absolute first.
bool ReadTableAsList(cISCLua* lua, std::list<cRZString>& out, int index);
bool ReadTableAsList(cISCLua* lua, std::list<unsigned>& out, int index);
// key (as string) -> rounded number; the first entry wins for a repeated key.
bool ReadTableAsMap(cISCLua* lua, std::unordered_map<std::string, int>& out, int index);

// A number becomes (n, n); a table its first two array entries (a one-entry table repeats it). false when missing.
bool ReadTableRange(cISCLua* lua, const char* name, float* range, int tableIndex);
bool ReadTableRange(cISCLua* lua, const char* name, int* range, int tableIndex);

}  // namespace SCLuaHelp
