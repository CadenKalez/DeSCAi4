// cTablePropertyReader — reads fields of one row of a global script table, e.g. advices[<id>] (6 functions:
// ctor 0x100298108, dtor 0x1007aaec0, GetNumber(char const*) 0x100441360, GetNumber(int) 0x10044310a,
// GetString 0x10043daea, HasField 0x1006aa80c). Layout: +0 saved stack top, +4 row id, +8 lua, +0x10 valid, +0x18 a
// cRZString that GetString fills.
//
// The constructor pushes the global table; the row is selected only when id != 0 (id 0 keeps the table itself) and
// only when 0 <= id <= the table length. Quirks kept: an unset result is 0 (numbers) or the previous string content
// (GetString never clears it when the reader is invalid); a number field is truncated through a 64-bit integer.
#pragma once
#include <cstdint>

#include "rz/cRZString.h"
#include "sc4/ScriptLua.h"

namespace sc4 {

class cTablePropertyReader {
public:
    cTablePropertyReader(IScriptLua* lua, const char* tableName, int rowId);   // @ 0x100298108
    ~cTablePropertyReader();                                                   // @ 0x1007aaec0 (restores the stack top when valid)
    cTablePropertyReader(const cTablePropertyReader&) = delete;
    cTablePropertyReader& operator=(const cTablePropertyReader&) = delete;

    bool IsValid() const { return valid_; }                  // byte at +0x10
    std::uint32_t GetNumber(const char* key);                // @ 0x100441360
    std::uint32_t GetNumber(int index);                      // @ 0x10044310a
    cRZString& GetString(const char* key);                   // @ 0x10043daea
    bool HasField(const char* key);                          // @ 0x1006aa80c (number or string value)
    IScriptLua* Lua() const { return lua_; }

private:
    int savedTop_;       // +0x00
    int rowId_;          // +0x04
    IScriptLua* lua_;    // +0x08
    bool valid_ = false; // +0x10
    cRZString string_;   // +0x18
};

}  // namespace sc4
