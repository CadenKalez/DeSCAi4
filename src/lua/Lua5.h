// cGZLua5 — the Lua VM host (0x1006947b0..0x1006952c2). It is the main thread (derives cGZLua5Thread), owns the
// ThreadRegistry and hands out child threads. Original layout: +0 primary vtable, +0x10 cIGZLua5Thread view,
// +0x18..+0x30 thread fields, +0x38 cIGZLua5 view, +0x40 registry, +0x78 error reporter.
#pragma once
#include "lua/LuaLibs.h"

namespace lua5 {

enum LuaLib : int {
    kOpenIo = 0x2, kOpenString = 0x4, kOpenMath = 0x8, kOpenDebug = 0x10, kOpenTable = 0x20, kOpenCompat4 = 0x40,
};

// What QueryInterface can hand out (each is a different base subobject in the original).
enum class LuaInterface { None, Self, Thread, Lua5, GameDataListener };
constexpr std::uint32_t kIID_cIGZLua5 = 0x6b2811a6, kIID_cIGZLua5Thread = 0xeb2810f1;

class cGZLua5 : public cGZLua5Thread, public ILua5Owner {
public:
    cGZLua5(ILuaApi& api, ILuaCompat& compat, IThreadFactory* factory, const char* compatScript);
    ~cGZLua5() override;
    cGZLua5Thread* AsIGZLua5Thread() { return this; }          // @ 0x1005e8710 (the thread base, +0x10)

    // ILua5Owner (the cIGZLua5 view)
    void AddRef() override { cGZLua5Thread::AddRef(); }
    void Release() override { cGZLua5Thread::Release(); }
    LuaErrorCallback GetErrorCallback() override { return errorReporter_; }
    cGZLua5Thread* AsThread() override { return this; }
    ThreadRegistry* GetRegistry() override { return &registry_; }

    bool Open(int libraries);
    bool Close();
    int CreateThread(cGZLua5Thread** out, int flags, unsigned classId);
    cGZLua5Thread* GetThread(int id);
    void DestroyThread(int id);
    bool ResumeThread(int id, int nargs, int* nresults);
    int GetGCCount() { return api_.getgccount(state_); }
    int GetGCThreshold() { return api_.getgcthreshold(state_); }
    void SetGCThreshold(int n) { api_.setgcthreshold(state_, n); }
    void SetErrorReporter(LuaErrorCallback fn, bool useDefaultWhenNull);
    LuaErrorCallback GetErrorReporter() const { return errorReporter_; }
    virtual LuaInterface QueryInterface(std::uint32_t iid);

    // The default reporter formats a report but never emits it (its output was compiled out of the shipped build).
    static void DefaultErrorReporter(cGZLua5Thread* thread, int status, const char* message);

protected:
    ThreadRegistry registry_;
    LuaErrorCallback errorReporter_;
    const char* compatScript_;
};

}  // namespace lua5
