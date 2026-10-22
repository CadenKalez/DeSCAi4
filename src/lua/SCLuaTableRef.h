// SCLuaTableRef — cursor over a Lua table (0x1001e763a..0x1001e78b2). Original layout (0x38 bytes): +0 cISCLua*,
// +8 cRZString (the global's name), +0x30 absolute stack index of the table, +0x34 "finished" flag.
//
// Constructed from a stack index (negative = relative to the top) or from a global's name. The name form pushes the
// global; when it is a table the cursor points at it (the table stays on the stack, like the original), otherwise the
// cursor is finished at once and the pushed value is popped. Quirk kept: with an empty name the original leaves the
// index and flag uninitialised; here they default to "finished" (SCLUAHELP-2).
#pragma once
#include "lua/ISCLua.h"
#include "rz/cRZString.h"

class SCLuaTableRef {
public:
    SCLuaTableRef(cISCLua* lua, int index);          // 0x1001e763a
    SCLuaTableRef(cISCLua* lua, const char* name);   // 0x1001e76bc

    // Start iterating: pushes nil and fetches the first entry (key, value left on the stack). false when empty.
    bool begin();   // 0x1001e786e
    // Pop the previous value and fetch the next entry. false (and finished) at the end.
    bool next();    // 0x1001e78b2

    int TableIndex() const { return index_; }
    bool Finished() const { return finished_; }
    const cRZString& Name() const { return name_; }

private:
    cISCLua* lua_;
    cRZString name_;
    int index_ = -1;
    bool finished_ = true;
};
