#include "lua/SCLuaTableRef.h"

SCLuaTableRef::SCLuaTableRef(cISCLua* lua, int index) : lua_(lua), index_(index), finished_(false) {
    if (index < 0) index_ = lua->GetTop() + 1 + index;
}

SCLuaTableRef::SCLuaTableRef(cISCLua* lua, const char* name) : lua_(lua), name_(name) {
    if (name_.Strlen() == 0) return;
    lua_->GetGlobal(name_.ToChar());
    if (lua_->IsTable(-1)) {
        index_ = lua_->GetTop();
        finished_ = false;
    } else {
        index_ = -1;
        finished_ = true;
        lua_->Pop(1);
    }
}

bool SCLuaTableRef::begin() {
    if (index_ >= 0) {
        lua_->PushNil();
        if (lua_->Next(index_) != 0) {
            finished_ = false;
            return true;
        }
    }
    finished_ = true;
    return false;
}

bool SCLuaTableRef::next() {
    if (!finished_) {
        lua_->Pop(1);
        if (lua_->Next(index_) != 0) return true;
        finished_ = true;
    }
    return false;
}
