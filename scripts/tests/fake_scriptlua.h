// A tiny fake of cISCLua good enough to drive the advisor script tables in tests: a value stack, global tables with
// number/string keys, Lua-style negative indices, and "_atrg_ = function () return (<literal>); end" chunks.
#pragma once
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sc4/ScriptLua.h"

namespace fakelua {

struct Value;
struct Table {
    std::map<std::string, std::shared_ptr<Value>> byName;
    std::map<int, std::shared_ptr<Value>> byIndex;
    int length = 0;   // the array part length reported by ObjLen
};
struct Value {
    enum Kind { Nil, Number, String, Boolean, TableK, Function } kind = Nil;
    double number = 0;
    std::string text;
    bool boolean = false;
    std::shared_ptr<Table> table;
    std::string chunk;   // Function: the literal returned
};
using ValuePtr = std::shared_ptr<Value>;

inline ValuePtr Num(double n) { auto v = std::make_shared<Value>(); v->kind = Value::Number; v->number = n; return v; }
inline ValuePtr Str(const std::string& s) { auto v = std::make_shared<Value>(); v->kind = Value::String; v->text = s; return v; }
inline ValuePtr Fn(const std::string& literal) { auto v = std::make_shared<Value>(); v->kind = Value::Function; v->chunk = literal; return v; }
inline ValuePtr Tab() { auto v = std::make_shared<Value>(); v->kind = Value::TableK; v->table = std::make_shared<Table>(); return v; }

class FakeLua final : public sc4::IScriptLua {
public:
    std::map<std::string, ValuePtr> globals;
    std::map<int, ValuePtr> registry;
    int nextRef = 1;
    std::vector<std::string> buffers;   // DoBuffer chunks
    bool callFails = false;

    int GetTop() override { return static_cast<int>(stack_.size()); }
    void SetTop(int top) override { stack_.resize(static_cast<std::size_t>(top)); }
    void Pop(int count) override { stack_.resize(stack_.size() - static_cast<std::size_t>(count)); }
    int Type(int index) override {
        ValuePtr v = At(index);
        switch (v->kind) { case Value::Number: return 3; case Value::String: return 4; case Value::Nil: return 0; case Value::Boolean: return 1; case Value::TableK: return 5; default: return 6; }
    }
    bool IsNumber(int index) override { return At(index)->kind == Value::Number; }
    bool IsBoolean(int index) override { return At(index)->kind == Value::Boolean; }
    double ToNumber(int index) override { ValuePtr v = At(index); return v->kind == Value::Number ? v->number : 0.0; }
    const char* ToString(int index) override { ValuePtr v = At(index); return v->kind == Value::String ? v->text.c_str() : nullptr; }
    bool ToBoolean(int index) override { ValuePtr v = At(index); return v->kind == Value::Boolean && v->boolean; }
    void PushNumber(double value) override { stack_.push_back(Num(value)); }
    void PushString(const char* text) override { stack_.push_back(Str(text)); }
    void GetGlobal(const char* name) override { auto it = globals.find(name); stack_.push_back(it == globals.end() ? std::make_shared<Value>() : it->second); }
    void RawGet(int tableIndex) override {
        ValuePtr table = At(tableIndex);
        ValuePtr key = stack_.back();
        stack_.pop_back();
        ValuePtr result = std::make_shared<Value>();
        if (table->kind == Value::TableK) {
            if (key->kind == Value::String) { auto it = table->table->byName.find(key->text); if (it != table->table->byName.end()) result = it->second; }
            else if (key->kind == Value::Number) { auto it = table->table->byIndex.find(static_cast<int>(key->number)); if (it != table->table->byIndex.end()) result = it->second; }
        }
        stack_.push_back(result);
    }
    void GetRef(int ref) override { stack_.push_back(registry.at(ref)); }
    int Ref(bool) override { ValuePtr v = stack_.back(); stack_.pop_back(); registry[nextRef] = v; return nextRef++; }
    int CallProtected(int, int) override {
        ValuePtr fn = stack_.back();
        stack_.pop_back();
        if (callFails) { stack_.push_back(Str("error")); return 2; }   // lua_pcall leaves the error object
        auto result = std::make_shared<Value>();
        if (fn->chunk == "true") { result->kind = Value::Boolean; result->boolean = true; }
        else if (fn->chunk == "false") { result->kind = Value::Boolean; }
        else if (!fn->chunk.empty() && fn->chunk[0] == '"') { result->kind = Value::String; result->text = fn->chunk.substr(1, fn->chunk.size() - 2); }
        else { result->kind = Value::Number; result->number = std::atof(fn->chunk.c_str()); }
        stack_.push_back(result);
        return 0;
    }
    int DoString(const char* chunk) override {
        const std::string s = chunk;
        const std::string head = "_atrg_ = function () return (";
        if (s.compare(0, head.size(), head) != 0 || s.size() < head.size() + 7) return 1;
        auto fn = std::make_shared<Value>();
        fn->kind = Value::Function;
        fn->chunk = s.substr(head.size(), s.size() - head.size() - std::strlen("); end"));
        globals["_atrg_"] = fn;
        lastChunk = s;
        return 0;
    }
    int DoBuffer(const char* buffer, std::size_t size, const char*) override { buffers.emplace_back(buffer, size); return 0; }
    void Unref(int ref) override { registry.erase(ref); unrefs.push_back(ref); }
    int ObjLen(int index) override { ValuePtr v = At(index); return v->kind == Value::TableK ? v->table->length : 0; }
    bool IsFunction(int index) override { return At(index)->kind == Value::Function; }
    bool IsTable(int index) override { return At(index)->kind == Value::TableK; }
    bool GetField(const char* name, int tableIndex) override {
        ValuePtr table = At(tableIndex);
        ValuePtr result;
        if (table->kind == Value::TableK) { auto it = table->table->byName.find(name); if (it != table->table->byName.end()) result = it->second; }
        if (!result || result->kind == Value::Nil) return false;
        stack_.push_back(result);
        return true;
    }
    void ReadTableAsList(std::list<std::uint32_t>& out, int tableIndex) override {
        ValuePtr table = At(tableIndex);
        for (int i = 1; i <= table->table->length; ++i) out.push_back(static_cast<std::uint32_t>(table->table->byIndex[i]->number));
    }

    std::string lastChunk;
    std::vector<int> unrefs;

private:
    ValuePtr At(int index) {
        const int abs = index < 0 ? static_cast<int>(stack_.size()) + index : index - 1;
        if (abs < 0 || abs >= static_cast<int>(stack_.size())) return std::make_shared<Value>();
        return stack_[static_cast<std::size_t>(abs)];
    }
    std::vector<ValuePtr> stack_;
};

}  // namespace fakelua
