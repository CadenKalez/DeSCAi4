#include "lua/SCLuaHelp.h"

#include <cmath>
#include <cstdint>

namespace SCLuaHelp {

namespace {

// Signed values: the original adds 1.5*2^23 to the float and subtracts its bit pattern, i.e. round-to-nearest-even
// of (float)value (exact only for |value| < 2^22, same as here for the values the game scripts use).
int RoundToInt(double value) { return static_cast<int>(std::nearbyintf(static_cast<float>(value))); }
// Unsigned values: the double is truncated through a 64-bit integer and stored as 32 bits.
unsigned TruncateToUnsigned(double value) { return static_cast<unsigned>(static_cast<std::int32_t>(static_cast<std::int64_t>(value))); }

const char* SafeString(cISCLua* lua, int index) {
    const char* s = lua->ToString(index);
    return s ? s : "";   // the original runs strlen on the pointer unchecked
}

int AbsoluteIndex(cISCLua* lua, int index) { return index < 0 ? index + 1 + lua->GetTop() : index; }

// Shared array walk: `store(i)` reads the value just pushed by RawGetI for element i (0-based).
template <typename Store>
void ReadArrayPart(cISCLua* lua, int index, int n, Store store) {
    for (int i = 0; i < n; ++i) {
        lua->RawGetI(index, i + 1);
        if (!lua->IsNil(-1)) store(i);
        lua->Pop(1);
    }
}

}  // namespace

// 0x1001e741c
bool GetGlobal(cISCLua* lua, const char* name) {
    lua->GetGlobal(name);
    if (lua->IsNil(-1)) {
        lua->Pop(1);
        return false;
    }
    return true;
}

// 0x1001e73c8
bool GetField(cISCLua* lua, const char* name, int tableIndex) {
    lua->PushString(name);
    lua->GetTable(tableIndex);
    if (lua->IsNil(-1)) {
        lua->Pop(1);
        return false;
    }
    return true;
}

// 0x1001e61e0
bool ReadTableField(cISCLua* lua, const char* name, cRZString& out, int tableIndex) {
    lua->PushString(name);
    lua->GetTable(tableIndex);
    bool found = !lua->IsNil(-1);
    if (found) out = cRZString(SafeString(lua, -1));
    else out.Erase(0, out.Strlen());
    lua->Pop(1);
    return found;
}

// 0x1001e636c
bool ReadTableField(cISCLua* lua, const char* name, float& out, int tableIndex) {
    lua->PushString(name);
    lua->GetTable(tableIndex);
    bool found = !lua->IsNil(-1);
    out = found ? static_cast<float>(lua->ToNumber(-1)) : 0.0f;
    lua->Pop(1);
    return found;
}

// 0x1001e63ee
bool ReadTableField(cISCLua* lua, const char* name, unsigned& out, int tableIndex) {
    lua->PushString(name);
    lua->GetTable(tableIndex);
    bool found = !lua->IsNil(-1);
    out = found ? lua->ToUint32(-1) : 0u;
    lua->Pop(1);
    return found;
}

// 0x1001e646c
bool ReadTableField(cISCLua* lua, const char* name, int& out, int tableIndex) {
    lua->PushString(name);
    lua->GetTable(tableIndex);
    bool found = !lua->IsNil(-1);
    out = found ? lua->ToSint32(-1) : 0;
    lua->Pop(1);
    return found;
}

// 0x1001e64ea
bool ReadTableField(cISCLua* lua, const char* name, bool& out, int tableIndex) {
    lua->PushString(name);
    lua->GetTable(tableIndex);
    bool isBool = lua->IsBoolean(-1);
    out = isBool ? lua->ToBoolean(-1) : false;
    lua->Pop(1);
    return isBool;
}

// 0x1001e6568
bool ReadTableAsArray(cISCLua* lua, std::vector<cRZString>& out, int index) {
    if (lua->IsTable(index)) {
        int n = lua->GetN(index);
        if (n < 1) return false;
        out.resize(static_cast<std::size_t>(n));
        ReadArrayPart(lua, index, n, [&](int i) { out[static_cast<std::size_t>(i)] = cRZString(SafeString(lua, -1)); });
        return true;
    }
    out.clear();
    out.push_back(cRZString(SafeString(lua, index)));
    return true;
}

// 0x1001e68e8
bool ReadTableAsArray(cISCLua* lua, std::vector<float>& out, int index) {
    if (lua->IsTable(index)) {
        int n = lua->GetN(index);
        if (n < 1) return false;
        out.resize(static_cast<std::size_t>(n));
        ReadArrayPart(lua, index, n, [&](int i) { out[static_cast<std::size_t>(i)] = static_cast<float>(lua->ToNumber(-1)); });
        return true;
    }
    out.clear();
    out.push_back(static_cast<float>(lua->ToNumber(index)));
    return true;
}

// 0x1001e6a06
bool ReadTableAsArray(cISCLua* lua, std::vector<unsigned>& out, int index) {
    if (lua->IsTable(index)) {
        int n = lua->GetN(index);
        if (n < 1) return false;
        out.resize(static_cast<std::size_t>(n));
        ReadArrayPart(lua, index, n, [&](int i) { out[static_cast<std::size_t>(i)] = TruncateToUnsigned(lua->ToNumber(-1)); });
        return true;
    }
    out.clear();
    out.push_back(TruncateToUnsigned(lua->ToNumber(index)));
    return true;
}

// 0x1001e6b20
bool ReadTableAsArray(cISCLua* lua, std::vector<int>& out, int index) {
    if (lua->IsTable(index)) {
        int n = lua->GetN(index);
        if (n < 1) return false;
        out.resize(static_cast<std::size_t>(n));
        ReadArrayPart(lua, index, n, [&](int i) { out[static_cast<std::size_t>(i)] = RoundToInt(lua->ToNumber(-1)); });
        return true;
    }
    out.clear();
    out.push_back(RoundToInt(lua->ToNumber(index)));
    return true;
}

// 0x1001e6c48
bool ReadTableAsList(cISCLua* lua, std::list<cRZString>& out, int index) {
    out.clear();
    index = AbsoluteIndex(lua, index);
    lua->PushNil();
    while (lua->Next(index)) {
        out.push_back(cRZString(SafeString(lua, -1)));
        lua->Pop(1);
    }
    return !out.empty();
}

// 0x1001e6dd6
bool ReadTableAsList(cISCLua* lua, std::list<unsigned>& out, int index) {
    out.clear();
    index = AbsoluteIndex(lua, index);
    lua->PushNil();
    while (lua->Next(index)) {
        out.push_back(TruncateToUnsigned(lua->ToNumber(-1)));
        lua->Pop(1);
    }
    return !out.empty();
}

// 0x1001e6e8e
bool ReadTableAsMap(cISCLua* lua, std::unordered_map<std::string, int>& out, int index) {
    out.clear();
    index = AbsoluteIndex(lua, index);
    lua->PushNil();
    while (lua->Next(index)) {
        std::string key = SafeString(lua, -2);
        out.emplace(std::move(key), RoundToInt(lua->ToNumber(-1)));   // an existing key keeps its first value
        lua->Pop(1);
    }
    return !out.empty();
}

// 0x1001e745e
bool ReadTableRange(cISCLua* lua, const char* name, float* range, int tableIndex) {
    if (!GetField(lua, name, tableIndex)) return false;
    if (!lua->IsTable(-1)) {
        range[0] = range[1] = static_cast<float>(lua->ToNumber(-1));
    } else {
        std::vector<float> values;
        ReadTableAsArray(lua, values, -1);
        if (!values.empty()) {   // the original dereferences begin() of an empty vector here
            range[0] = values[0];
            range[1] = values.size() >= 2 ? values[1] : range[0];
        }
    }
    lua->Pop(1);
    return true;
}

// 0x1001e754c
bool ReadTableRange(cISCLua* lua, const char* name, int* range, int tableIndex) {
    if (!GetField(lua, name, tableIndex)) return false;
    if (!lua->IsTable(-1)) {
        range[0] = range[1] = RoundToInt(lua->ToNumber(-1));
    } else {
        std::vector<int> values;
        ReadTableAsArray(lua, values, -1);
        if (!values.empty()) {   // the original dereferences begin() of an empty vector here
            range[0] = values[0];
            range[1] = values.size() >= 2 ? values[1] : range[0];
        }
    }
    lua->Pop(1);
    return true;
}

}  // namespace SCLuaHelp
