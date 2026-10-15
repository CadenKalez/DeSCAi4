// cSCLua — SimCity 4's scripting host (0x1005e669c..0x1005e871a): cGZLua5 plus the cISCLua adapter surface
// (a flat set of forwards onto the thread API) and the Game Data Registry bridge. Original layout: cGZLua5
// base, +0x80/+0x88 registry-notification listener views, +0x90 opened flag, +0x98 call-argument parameter set,
// +0xa0 call-result parameter set, +0xa8 list of registered node ids.
//
// Not recovered here: the Game Data Registry bridge proper (OnGameDataRegistryNotification, GDRCallExtention,
// GDRExchangeExtention, GDRUnregisterNodeVisitor, ToDataRegistry*), which needs cISC4GameDataRegistry.
#pragma once
#include <list>

#include "lua/Lua5.h"

namespace lua5 {

// Reference-counted parameter container (cGZCommandParameterSet).
class ICommandParameterSet {
public:
    virtual ~ICommandParameterSet() = default;
    virtual void AddRef() = 0;
    virtual void Release() = 0;
};

class cSCLua;
// Services cSCLua needs from the framework.
class ISCLuaHost {
public:
    virtual ~ISCLuaHost() = default;
    virtual ICommandParameterSet* NewParameterSet() = 0;                       // new object, refcount 0
    virtual void RegisterRegistryListener(cSCLua* lua) = 0;                    // GameDataRegistry listener list
    virtual void UnregisterRegistryListener(cSCLua* lua) = 0;
};

constexpr std::uint32_t kIID_GameDataRegistryListener = 0xea10c4a2;

class cSCLua : public cGZLua5 {
public:
    cSCLua(ILuaApi& api, ILuaCompat& compat, IThreadFactory* factory, const char* compatScript, ISCLuaHost& host);
    ~cSCLua() override;

    bool Open(int libraries);
    bool Close();
    LuaInterface QueryInterface(std::uint32_t iid) override;
    cGZLua5* AsIGZLua5() { return this; }
    bool IsOpen() const { return opened_; }
    ICommandParameterSet* CallParameters() const { return callParams_; }
    ICommandParameterSet* ResultParameters() const { return resultParams_; }
    std::list<unsigned long>& RegisteredNodes() { return nodes_; }

    // cISCLua adapter surface (each forwards to the thread method of the same meaning)
    int NewTagS() { return NewTag(); }
    void GetGlobals() { GetGlobalTable(); }
    void NewTable() { PushNewTable(); }
    int RefValue() { return Ref(); }
    int CallProtected(int nargs, int nresults) { return cGZLua5Thread::CallProtected(nargs, nresults, 0, true); }
    void RawCall(int nargs, int nresults) { Call(nargs, nresults); }
    void* NewUserData(int size) { return PushNewUserData(size); }
    void PushUserData(void* p, int tag) { PushUserTag(p, tag); }
    // Length of the table at `index` (Lua 4 `getn`): calls the global "getn"; 0 for non-tables or on failure.
    int GetN(int index);

private:
    ISCLuaHost& host_;
    bool opened_ = false;
    ICommandParameterSet* callParams_ = nullptr;
    ICommandParameterSet* resultParams_ = nullptr;
    std::list<unsigned long> nodes_;
};

}  // namespace lua5
