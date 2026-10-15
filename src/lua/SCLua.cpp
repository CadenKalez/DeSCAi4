#include "lua/SCLua.h"

namespace lua5 {

cSCLua::cSCLua(ILuaApi& api, ILuaCompat& compat, IThreadFactory* factory, const char* compatScript, ISCLuaHost& host)
    : cGZLua5(api, compat, factory, compatScript), host_(host) {}

cSCLua::~cSCLua() {
    nodes_.clear();
    if (resultParams_) resultParams_->Release();
    if (callParams_) callParams_->Release();
}

static void Assign(ICommandParameterSet*& slot, ICommandParameterSet* fresh) {
    ICommandParameterSet* old = slot;
    if (old != fresh) {
        fresh->AddRef();
        slot = fresh;
        if (old) old->Release();
    }
}

bool cSCLua::Open(int libraries) {
    bool result = true;
    if (!opened_) {
        opened_ = cGZLua5::Open(libraries);
        Assign(callParams_, host_.NewParameterSet());
        Assign(resultParams_, host_.NewParameterSet());
        host_.RegisterRegistryListener(this);
        result = opened_;
    }
    return result;
}

bool cSCLua::Close() {
    if (opened_) {
        host_.UnregisterRegistryListener(this);
        opened_ = false;
        cGZLua5::Close();
        if (callParams_) {
            ICommandParameterSet* p = callParams_;
            callParams_ = nullptr;
            p->Release();
        }
        if (resultParams_) {
            ICommandParameterSet* p = resultParams_;
            resultParams_ = nullptr;
            p->Release();
        }
    }
    return true;
}

LuaInterface cSCLua::QueryInterface(std::uint32_t iid) {
    if (iid == kIID_GameDataRegistryListener) {
        AddRef();
        return LuaInterface::GameDataListener;
    }
    return cGZLua5::QueryInterface(iid);
}

int cSCLua::GetN(int index) {
    if (!IsTable(index)) return 0;
    int result = 0;
    int top = GetTop();
    PushValue(index);
    PushValue(-1);
    GetGlobal("getn");
    if (IsFunction(-1)) {
        Replace(-3);
        if (cGZLua5Thread::CallProtected(1, 1, 0, true) == 0) result = CvtTrunc32(ToNumber(-1));
    }
    SetTop(top);
    return result;
}

}  // namespace lua5
