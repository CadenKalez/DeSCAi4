#include "sc4/TablePropertyReader.h"

namespace sc4 {

cTablePropertyReader::cTablePropertyReader(IScriptLua* lua, const char* tableName, int rowId)
    : savedTop_(lua->GetTop()), rowId_(rowId), lua_(lua) {
    lua->GetGlobal(tableName);
    const int length = lua->ObjLen(-1);
    valid_ = rowId_ >= 0 && rowId_ <= length;
    if (rowId != 0 && valid_) {
        lua->PushNumber(static_cast<double>(rowId));
        lua->RawGet(-2);
        valid_ = lua->IsTable(-1);
    }
}

cTablePropertyReader::~cTablePropertyReader() {
    if (valid_) lua_->SetTop(savedTop_);
}

static std::uint32_t TruncateNumber(double value) {
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

std::uint32_t cTablePropertyReader::GetNumber(const char* key) {
    if (!valid_) return 0;
    lua_->PushString(key);
    lua_->RawGet(-2);
    const double value = lua_->ToNumber(-1);
    lua_->Pop(1);
    return TruncateNumber(value);
}

std::uint32_t cTablePropertyReader::GetNumber(int index) {
    if (!valid_) return 0;
    lua_->PushNumber(static_cast<double>(index));
    lua_->RawGet(-2);
    const double value = lua_->ToNumber(-1);
    lua_->Pop(1);
    return TruncateNumber(value);
}

cRZString& cTablePropertyReader::GetString(const char* key) {
    if (valid_) {
        string_.Erase(0, 0xffffffffu);
        lua_->PushString(key);
        lua_->RawGet(-2);
        string_.AssignFromCString(lua_->ToString(-1));
        lua_->Pop(1);
    }
    return string_;
}

bool cTablePropertyReader::HasField(const char* key) {
    if (!valid_) return false;
    lua_->PushString(key);
    lua_->RawGet(-2);
    bool has = lua_->Type(-1) == kScriptLuaTypeNumber;
    if (!has) has = lua_->Type(-1) == kScriptLuaTypeString;
    lua_->Pop(1);
    return has;
}

}  // namespace sc4
