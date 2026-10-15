// nsGZLua5ThreadLib::cThreadRegistry (0x100697ecc..0x10069886e) — the table of Lua threads (coroutines) owned by one
// cGZLua5. Thread ids count up from 0 (the main thread). Each entry records the lua_State, its parent state, the
// child states and a flag word:
//   0x001 keep alive after the body finishes     0x010 child of the creating thread (parent-owned)
//   0x100 only resumable by the parent           0x10000000 currently running
// The thread interface object of a state lives in the slot before the lua_State (ILuaApi::get_thread_slot).
#pragma once
#include <unordered_map>
#include <vector>

#include "lua/Lua5Thread.h"

namespace lua5 {

// Creates the interface object for a new thread (the original asks GZCOM for a class id with IID 0xeb2810f1).
// The returned object carries one reference for the caller (released once the registry holds its own).
class IThreadFactory {
public:
    virtual ~IThreadFactory() = default;
    virtual cGZLua5Thread* CreateThreadObject(unsigned classId) = 0;
};

constexpr unsigned kDefaultThreadClassId = 0x2b2810f7;
constexpr std::uint32_t kThreadFlagKeepAlive = 0x1, kThreadFlagChild = 0x10, kThreadFlagParentResumes = 0x100,
                        kThreadFlagRunning = 0x10000000;

class ThreadRegistry : public IThreadRegistry {
public:
    struct Thread {
        lua_State* state = nullptr;
        lua_State* parent = nullptr;
        std::vector<lua_State*> children;
        std::uint32_t flags = 0;
        int classId = 0;
    };

    ThreadRegistry(ILuaApi& api, IThreadFactory* factory) : api_(api), factory_(factory) {}
    ~ThreadRegistry();

    bool Init(ILua5Owner* owner, lua_State* mainState);
    bool Shutdown();
    int CreateThread(lua_State* L, int flags, int classId);   // returns the new id or -1
    int ResumeThread(lua_State* L, int id, int nargs);        // returns the lua_resume status
    bool IsValidThread(int id);
    bool DestroyThread(lua_State* caller, int id) override;
    int GetThreadStatus(int id) override;
    int GetParentID(int id);
    lua_State* GetThreadContext(int id);
    const Thread* Find(int id) const;
    std::size_t Count() const { return threads_.size(); }

private:
    Thread* FindMutable(int id);

    ILuaApi& api_;
    IThreadFactory* factory_;
    std::unordered_map<int, Thread> threads_;   // +0
    ILua5Owner* owner_ = nullptr;               // +0x28
    int nextId_ = 0;                            // +0x30
};

}  // namespace lua5
