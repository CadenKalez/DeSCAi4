// cSC4GameDataRegistry — the tree of named game values and commands the Lua bridge and the cheat console read and
// write (0x1006bfdd0..0x1006c1a06; system service 0xebe0e860, priority -1000000, interface 0xe898ed03).
// An entry is a node (a cIGZUnknown handle plus a list of children) or a value / command (a plain pointer plus an
// accessor function). Entry ids are the entry pointers themselves, as in the original. Layout of an entry (0x58):
// +0 children list / accessor, +8 handle, +0x10 name, +0x40 parent, +0x48 context, +0x50 flags (1 command,
// 2 read-only), +0x54 "is node". Registered notification targets hear about additions (3), removals (2) and the
// shutdown (1); message targets get one message (0x2bdfe414) per existing entry when they are added.
#pragma once
#include <cstdint>
#include <list>
#include <map>
#include <vector>

#include "gz/cIGZMessage2.h"
#include "gz/cIGZUnknown.h"
#include "gz/cIGZVariant.h"
#include "rz/cRZString.h"
#include "rz/cRZSystemService.h"

// cIGZCommandParameterSet lives at global scope (gz/CommandParameterSet.h); the declaration has to be made there, not inside
// namespace sc4, or every unqualified use in this namespace would name a different, incomplete type.
class cIGZCommandParameterSet;

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4GameDataRegistry = 0xe898ed03, kGZSID_cSC4GameDataRegistry = 0xebe0e860;
constexpr std::uint32_t kMessageGameDataRegistryEntry = 0x2bdfe414;
constexpr std::int32_t kGameDataRegistryPriority = -1000000;
enum GameDataRegistryEvent : int { kRegistryShutdown = 1, kRegistryEntryRemoved = 2, kRegistryEntryAdded = 3 };

class cISC4GameDataRegistry;

using EntryId = std::uintptr_t;
using GameDataValueFn = bool (*)(void* context, cIGZVariant& value, bool write, cISC4GameDataRegistry* registry);
using GameDataCommandFn = bool (*)(void* context, cIGZCommandParameterSet* in, cIGZCommandParameterSet* out, cISC4GameDataRegistry* registry);
using EntryVisitFn = bool (*)(EntryId entry, void* context);

class cISC4GameDataRegistryNotificationTarget : public cIGZUnknown {
public:
    virtual void OnRegistryEvent(cISC4GameDataRegistry* registry, int event, EntryId entry) = 0;   // [3]
};

class cISC4GameDataRegistry : public cIGZUnknown {
public:
    virtual EntryId AddChildEntry(cIGZUnknown* node, const char* name, EntryId parent, int flags, void* context) = 0;           // [3]
    virtual EntryId AddChildEntry(void* handle, const char* name, GameDataValueFn fn, EntryId parent, int flags, void* context) = 0;     // [4]
    virtual EntryId AddChildEntry(void* handle, const char* name, GameDataCommandFn fn, EntryId parent, int flags, void* context) = 0;   // [5]
    virtual bool RemoveChildEntry(cIGZUnknown* node) = 0;                               // [6]
    virtual bool RemoveChildEntry(void* handle) = 0;                                    // [7]
    virtual EntryId GetEntryID(cIGZUnknown* node) const = 0;                            // [8]
    virtual EntryId GetEntryID(void* handle) const = 0;                                 // [9]
    virtual cIGZUnknown* GetNode(EntryId entry) const = 0;                              // [10]
    virtual void* GetValue(EntryId entry) const = 0;                                    // [11]
    virtual bool IsValidEntry(EntryId entry) const = 0;                                 // [12]
    virtual bool IsNodeEntry(EntryId entry) const = 0;                                  // [13]
    virtual const cRZString* GetEntryName(EntryId entry) const = 0;                     // [14]
    virtual bool DoValueExchange(EntryId entry, cIGZVariant& value, bool write) = 0;    // [15]
    virtual bool DoValueCall(EntryId entry, cIGZCommandParameterSet* in, cIGZCommandParameterSet* out) = 0;   // [16]
    virtual bool SetEntryContext(EntryId entry, void* context) = 0;                     // [17]
    virtual void* GetEntryContext(EntryId entry) const = 0;                             // [18]
    virtual bool CheckEntryFlags(EntryId entry, int flags) const = 0;                   // [19]
    virtual EntryId GetParentNodeEntry(EntryId entry) const = 0;                        // [20]
    virtual EntryId GetRootNodeEntry() const = 0;                                       // [21]
    virtual bool VisitBreadthFirst(EntryId from, EntryVisitFn fn, void* context) = 0;  // [22]
    virtual bool VisitImmediateChildren(EntryId from, EntryVisitFn fn, void* context) = 0;   // [23]
    virtual bool AddNotificationTarget(cISC4GameDataRegistryNotificationTarget* target) = 0;   // [24]
    virtual bool AddNotificationTarget(cIGZMessageTarget2* target) = 0;                 // [25]
    virtual bool RemoveNotificationTarget(cISC4GameDataRegistryNotificationTarget* target) = 0;   // [26]
    virtual bool RemoveNotificationTarget(cIGZMessageTarget2* target) = 0;              // [27]
    virtual void DebugDumpRecursive(EntryId from) = 0;                                  // [28]
};

class cSC4GameDataRegistry final : public cISC4GameDataRegistry, public cRZSystemService {
public:
    cSC4GameDataRegistry();
    ~cSC4GameDataRegistry() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }
    std::uint32_t Release() override { return cRZSystemService::Release(); }

    bool Init() override;
    bool Shutdown() override;
    bool OnTick(std::uint32_t) override { return true; }

    EntryId AddChildEntry(cIGZUnknown* node, const char* name, EntryId parent, int flags, void* context) override;
    EntryId AddChildEntry(void* handle, const char* name, GameDataValueFn fn, EntryId parent, int flags, void* context) override;
    EntryId AddChildEntry(void* handle, const char* name, GameDataCommandFn fn, EntryId parent, int flags, void* context) override;
    bool RemoveChildEntry(cIGZUnknown* node) override;
    bool RemoveChildEntry(void* handle) override;
    EntryId GetEntryID(cIGZUnknown* node) const override;
    EntryId GetEntryID(void* handle) const override;
    cIGZUnknown* GetNode(EntryId entry) const override;
    void* GetValue(EntryId entry) const override;
    bool IsValidEntry(EntryId entry) const override;
    bool IsNodeEntry(EntryId entry) const override;
    const cRZString* GetEntryName(EntryId entry) const override;
    bool DoValueExchange(EntryId entry, cIGZVariant& value, bool write) override;
    bool DoValueCall(EntryId entry, cIGZCommandParameterSet* in, cIGZCommandParameterSet* out) override;
    bool SetEntryContext(EntryId entry, void* context) override;
    void* GetEntryContext(EntryId entry) const override;
    bool CheckEntryFlags(EntryId entry, int flags) const override;
    EntryId GetParentNodeEntry(EntryId entry) const override;
    EntryId GetRootNodeEntry() const override { return reinterpret_cast<EntryId>(root_); }
    bool VisitBreadthFirst(EntryId from, EntryVisitFn fn, void* context) override;
    bool VisitImmediateChildren(EntryId from, EntryVisitFn fn, void* context) override;
    bool AddNotificationTarget(cISC4GameDataRegistryNotificationTarget* target) override;
    bool AddNotificationTarget(cIGZMessageTarget2* target) override;
    bool RemoveNotificationTarget(cISC4GameDataRegistryNotificationTarget* target) override;
    bool RemoveNotificationTarget(cIGZMessageTarget2* target) override;
    void DebugDumpRecursive(EntryId from) override;

    bool VisitDepthFirst(EntryId from, EntryVisitFn fn, void* context);
    bool VisitBreadthFirstReverse(EntryId from, EntryVisitFn fn, void* context);
    bool RemoveEntry(EntryId entry);

    // The debug dump prints one line per entry; where it goes is up to the host.
    using DumpFn = void (*)(const cRZString& name, bool isNode);
    static void SetDumpFunction(DumpFn fn);

    struct cEntry {
        cEntry(cIGZUnknown* node, const cIGZString& name, cEntry* parent, int flags);
        cEntry(void* handle, const cIGZString& name, GameDataValueFn fn, cEntry* parent, int flags);
        cEntry(void* handle, const cIGZString& name, GameDataCommandFn fn, cEntry* parent, int flags);
        ~cEntry();
        bool DoValueExchange(cIGZVariant& value, bool write, cISC4GameDataRegistry* registry);

        std::list<cEntry*>* children = nullptr;      // +0 (nodes)
        GameDataValueFn valueFn = nullptr;           // +0 (values)
        GameDataCommandFn commandFn = nullptr;       // +0 (commands)
        void* handle = nullptr;                      // +8: the node (a counted cIGZUnknown) or the value's pointer
        cRZString name;                              // +0x10
        cEntry* parent = nullptr;                    // +0x40
        void* context = nullptr;                     // +0x48
        int flags = 0;                               // +0x50
        bool isNode = false;                         // +0x54
    };

private:
    void Notify(int event, EntryId entry);

    cEntry* root_ = nullptr;                                   // +0x30
    std::map<cIGZUnknown*, cEntry*> nodeEntries_;              // +0x38
    std::map<void*, cEntry*> valueEntries_;                    // +0x50
    std::list<cISC4GameDataRegistryNotificationTarget*> targets_;   // +0x68 (each holds a reference)
    std::list<cIGZMessageTarget2*> messageTargets_;            // +0x80 (each holds a reference)
};

}  // namespace sc4
