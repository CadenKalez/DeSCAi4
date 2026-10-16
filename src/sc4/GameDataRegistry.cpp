#include "sc4/GameDataRegistry.h"

#include <algorithm>
#include <deque>

#include "rz/cRZCOMDllDirector.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZVariant.h"

namespace sc4 {

namespace {
cSC4GameDataRegistry::DumpFn g_dump = nullptr;

// The object the root entry is the node of.
class RootHandle final : public cIGZUnknown {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != kGZIID_cIGZUnknown) return false;
        *out = this;
        AddRef();
        return true;
    }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override { if (refs_ <= 1) { delete this; return 0; } return --refs_; }
private:
    std::uint32_t refs_ = 0;
};

using Entry = cSC4GameDataRegistry::cEntry;
Entry* AsEntry(EntryId id) { return reinterpret_cast<Entry*>(id); }
EntryId AsId(const Entry* entry) { return reinterpret_cast<EntryId>(entry); }
}  // namespace

void cSC4GameDataRegistry::SetDumpFunction(DumpFn fn) { g_dump = fn; }

// ---- entries -----------------------------------------------------------------------------------------------

cSC4GameDataRegistry::cEntry::cEntry(cIGZUnknown* node, const cIGZString& entryName, cEntry* parentEntry, int entryFlags)
    : handle(node), parent(parentEntry), flags(entryFlags), isNode(true) {
    name.Copy(entryName);
    node->AddRef();
    children = new std::list<cEntry*>();
}

cSC4GameDataRegistry::cEntry::cEntry(void* valueHandle, const cIGZString& entryName, GameDataValueFn fn, cEntry* parentEntry, int entryFlags)
    : valueFn(fn), handle(valueHandle), parent(parentEntry), flags(entryFlags & ~1), isNode(false) {
    name.Copy(entryName);
}

cSC4GameDataRegistry::cEntry::cEntry(void* valueHandle, const cIGZString& entryName, GameDataCommandFn fn, cEntry* parentEntry, int entryFlags)
    : commandFn(fn), handle(valueHandle), parent(parentEntry), flags(entryFlags | 1), isNode(false) {
    name.Copy(entryName);
}

cSC4GameDataRegistry::cEntry::~cEntry() {
    if (!isNode) return;
    if (handle) static_cast<cIGZUnknown*>(handle)->Release();
    while (!children->empty()) {
        cEntry* child = children->front();
        delete child;
        children->pop_front();
    }
    delete children;
}

bool cSC4GameDataRegistry::cEntry::DoValueExchange(cIGZVariant& value, bool write, cISC4GameDataRegistry* registry) {
    if (valueFn) return valueFn(handle, value, write, registry);
    cRZVariant scratch;     // the original builds and drops an empty variant here
    return false;
}

// ---- registry ----------------------------------------------------------------------------------------------

cSC4GameDataRegistry::cSC4GameDataRegistry() : cRZSystemService(kGZSID_cSC4GameDataRegistry, kGameDataRegistryPriority) {
    auto* rootHandle = new RootHandle();
    root_ = new cEntry(rootHandle, cRZString("__root__"), nullptr, 0);
    nodeEntries_[rootHandle] = root_;
}

cSC4GameDataRegistry::~cSC4GameDataRegistry() {
    delete root_;
}

bool cSC4GameDataRegistry::QueryInterface(std::uint32_t iid, void** out) {
    *out = nullptr;
    if (iid == kGZIID_cISC4GameDataRegistry) {
        *out = static_cast<cISC4GameDataRegistry*>(this);
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

bool cSC4GameDataRegistry::Init() {
    if (IDirectorFramework* framework = DirectorFramework()) framework->RegisterSystemService(static_cast<cIGZSystemService*>(this));
    return true;
}

bool cSC4GameDataRegistry::Shutdown() {
    if (IDirectorFramework* framework = DirectorFramework()) framework->UnregisterSystemService(static_cast<cIGZSystemService*>(this));
    for (auto* target : targets_) target->OnRegistryEvent(this, kRegistryShutdown, 0);
    nodeEntries_.clear();
    valueEntries_.clear();
    std::list<cEntry*>& rootChildren = *root_->children;
    while (!rootChildren.empty()) {
        delete rootChildren.front();
        rootChildren.pop_front();
    }
    for (auto* target : targets_) target->Release();
    targets_.clear();
    for (auto* target : messageTargets_) target->Release();
    messageTargets_.clear();
    return true;
}

void cSC4GameDataRegistry::Notify(int event, EntryId entry) {
    for (auto it = targets_.begin(); it != targets_.end();) {
        auto* target = *it;
        ++it;
        target->OnRegistryEvent(this, event, entry);
    }
}

EntryId cSC4GameDataRegistry::AddChildEntry(cIGZUnknown* node, const char* name, EntryId parent, int flags, void* context) {
    if (const EntryId existing = GetEntryID(node)) return existing;
    cEntry* parentEntry = AsEntry(parent);
    auto* entry = new cEntry(node, cRZString(name), parentEntry, flags);
    entry->context = context;
    nodeEntries_[node] = entry;
    parentEntry->children->push_back(entry);
    Notify(kRegistryEntryAdded, AsId(entry));
    return AsId(entry);
}

EntryId cSC4GameDataRegistry::AddChildEntry(void* handle, const char* name, GameDataValueFn fn, EntryId parent, int flags, void* context) {
    if (valueEntries_.count(handle)) return 0;
    if (!IsNodeEntry(parent)) return 0;
    cEntry* parentEntry = AsEntry(parent);
    auto* entry = new cEntry(handle, cRZString(name), fn, parentEntry, flags & ~1);
    if (!handle) {
        entry->handle = entry;
        handle = entry;
    }
    entry->context = context;
    valueEntries_[handle] = entry;
    parentEntry->children->push_back(entry);
    Notify(kRegistryEntryAdded, AsId(entry));
    return AsId(entry);
}

EntryId cSC4GameDataRegistry::AddChildEntry(void* handle, const char* name, GameDataCommandFn fn, EntryId parent, int flags, void* context) {
    if (valueEntries_.count(handle)) return 0;
    if (!IsNodeEntry(parent)) return 0;
    cEntry* parentEntry = AsEntry(parent);
    auto* entry = new cEntry(handle, cRZString(name), fn, parentEntry, flags | 1);
    if (!handle) {
        entry->handle = entry;
        handle = entry;
    }
    entry->context = context;
    valueEntries_[handle] = entry;
    parentEntry->children->push_back(entry);
    Notify(kRegistryEntryAdded, AsId(entry));
    return AsId(entry);
}

namespace {
struct CollectContext { std::vector<EntryId>* entries; };
bool CollectEntry(EntryId entry, void* context) {
    static_cast<CollectContext*>(context)->entries->push_back(entry);
    return true;
}
}  // namespace

bool cSC4GameDataRegistry::RemoveChildEntry(cIGZUnknown* node) {
    const EntryId entry = GetEntryID(node);
    return entry != 0 && RemoveEntry(entry);
}

bool cSC4GameDataRegistry::RemoveChildEntry(void* handle) {
    const auto it = valueEntries_.find(handle);
    return it != valueEntries_.end() && RemoveEntry(AsId(it->second));
}

// Removes an entry with its whole subtree (children first), telling the targets about each one.
bool cSC4GameDataRegistry::RemoveEntry(EntryId entryId) {
    cEntry* entry = AsEntry(entryId);
    if (entry == root_) return false;
    std::vector<EntryId> removed;
    CollectContext context{&removed};
    VisitBreadthFirstReverse(entryId, &CollectEntry, &context);
    for (const EntryId id : removed) {
        Notify(kRegistryEntryRemoved, id);
        cEntry* e = AsEntry(id);
        if (!e->isNode) valueEntries_.erase(e->handle);
        else nodeEntries_.erase(static_cast<cIGZUnknown*>(e->handle));
    }
    std::list<cEntry*>& siblings = *entry->parent->children;
    const auto it = std::find(siblings.begin(), siblings.end(), entry);
    if (it != siblings.end()) {
        delete entry;
        siblings.erase(it);
    }
    return true;
}

EntryId cSC4GameDataRegistry::GetEntryID(cIGZUnknown* node) const {
    const auto it = nodeEntries_.find(node);
    if (it != nodeEntries_.end()) return AsId(it->second);
    if (node) {
        // The original probes the node for its base interface and then looks at the smallest registered key,
        // taking its entry when that key is null.
        void* base = nullptr;
        if (node->QueryInterface(kGZIID_cIGZUnknown, &base)) {
            static_cast<cIGZUnknown*>(base)->Release();
            if (!nodeEntries_.empty() && nodeEntries_.begin()->first == nullptr) return AsId(nodeEntries_.begin()->second);
        }
    }
    return 0;
}

EntryId cSC4GameDataRegistry::GetEntryID(void* handle) const {
    const auto it = valueEntries_.find(handle);
    return it == valueEntries_.end() ? 0 : AsId(it->second);
}

cIGZUnknown* cSC4GameDataRegistry::GetNode(EntryId entry) const {
    if (entry == 0 || !AsEntry(entry)->isNode) return nullptr;
    return static_cast<cIGZUnknown*>(AsEntry(entry)->handle);
}

void* cSC4GameDataRegistry::GetValue(EntryId entry) const {
    if (entry == 0 || AsEntry(entry)->isNode) return nullptr;
    return AsEntry(entry)->handle;
}

bool cSC4GameDataRegistry::IsValidEntry(EntryId entry) const {
    for (const auto& item : nodeEntries_) if (AsId(item.second) == entry) return true;
    for (const auto& item : valueEntries_) if (AsId(item.second) == entry) return true;
    return false;
}

bool cSC4GameDataRegistry::IsNodeEntry(EntryId entry) const { return entry != 0 && AsEntry(entry)->isNode; }

const cRZString* cSC4GameDataRegistry::GetEntryName(EntryId entry) const { return entry ? &AsEntry(entry)->name : nullptr; }

bool cSC4GameDataRegistry::DoValueExchange(EntryId entry, cIGZVariant& value, bool write) {
    if (entry == 0) return false;
    cEntry* e = AsEntry(entry);
    if (write && (e->flags & 2)) return false;
    if (e->isNode) return false;
    return e->DoValueExchange(value, write, this);
}

bool cSC4GameDataRegistry::DoValueCall(EntryId entry, cIGZCommandParameterSet* in, cIGZCommandParameterSet* out) {
    if (entry == 0) return false;
    cEntry* e = AsEntry(entry);
    if (e->isNode || !e->commandFn) return false;
    return e->commandFn(e->handle, in, out, this);
}

bool cSC4GameDataRegistry::SetEntryContext(EntryId entry, void* context) {
    if (entry != 0) AsEntry(entry)->context = context;
    return entry != 0;
}

void* cSC4GameDataRegistry::GetEntryContext(EntryId entry) const { return entry ? AsEntry(entry)->context : nullptr; }

bool cSC4GameDataRegistry::CheckEntryFlags(EntryId entry, int flags) const {
    return entry != 0 && (static_cast<unsigned>(AsEntry(entry)->flags) & static_cast<unsigned>(flags)) == static_cast<unsigned>(flags);
}

EntryId cSC4GameDataRegistry::GetParentNodeEntry(EntryId entry) const { return entry ? AsId(AsEntry(entry)->parent) : 0; }

// ---- traversal ---------------------------------------------------------------------------------------------

bool cSC4GameDataRegistry::VisitDepthFirst(EntryId from, EntryVisitFn fn, void* context) {
    if (from == 0) return true;
    std::vector<cEntry*> stack{AsEntry(from)};
    while (!stack.empty()) {
        cEntry* entry = stack.back();
        stack.pop_back();
        if (!fn(AsId(entry), context)) return false;
        if (entry->isNode && entry->children) {
            for (cEntry* child : *entry->children) stack.push_back(child);
        }
    }
    return true;
}

bool cSC4GameDataRegistry::VisitBreadthFirst(EntryId from, EntryVisitFn fn, void* context) {
    if (from == 0) return true;
    std::deque<cEntry*> queue{AsEntry(from)};
    while (!queue.empty()) {
        cEntry* entry = queue.front();
        queue.pop_front();
        if (!fn(AsId(entry), context)) return false;
        if (entry->isNode && entry->children) {
            for (cEntry* child : *entry->children) queue.push_back(child);
        }
    }
    return true;
}

// Children before their parent; stops (false) as soon as a visit says stop.
bool cSC4GameDataRegistry::VisitBreadthFirstReverse(EntryId from, EntryVisitFn fn, void* context) {
    if (from == 0) return true;
    cEntry* entry = AsEntry(from);
    if (entry->isNode && entry->children) {
        for (cEntry* child : *entry->children) {
            if (!VisitBreadthFirstReverse(AsId(child), fn, context)) return false;
        }
    }
    return fn(from, context);
}

bool cSC4GameDataRegistry::VisitImmediateChildren(EntryId from, EntryVisitFn fn, void* context) {
    if (from != 0 && AsEntry(from)->isNode && AsEntry(from)->children) {
        for (cEntry* child : *AsEntry(from)->children) {
            if (!fn(AsId(child), context)) return false;
        }
    }
    return true;
}

// ---- notification targets -------------------------------------------------------------------------------------

namespace {
struct NotifyContext { cISC4GameDataRegistry* registry; void* target; };
bool InitialTargetNotify(EntryId entry, void* context) {
    auto* c = static_cast<NotifyContext*>(context);
    static_cast<cISC4GameDataRegistryNotificationTarget*>(c->target)->OnRegistryEvent(c->registry, kRegistryEntryAdded, entry);
    return true;
}
bool InitialMessageTargetNotify(EntryId entry, void* context) {
    auto* c = static_cast<NotifyContext*>(context);
    auto* message = new cRZMessage2Standard();
    message->AddRef();
    message->SetType(kMessageGameDataRegistryEntry);
    message->SetVoid1(c->registry);
    message->SetData2(kRegistryEntryAdded);
    message->SetData3(entry);
    static_cast<cIGZMessageTarget2*>(c->target)->DoMessage(message);
    message->Release();
    return true;
}
}  // namespace

bool cSC4GameDataRegistry::AddNotificationTarget(cISC4GameDataRegistryNotificationTarget* target) {
    if (std::find(targets_.begin(), targets_.end(), target) != targets_.end()) return false;
    if (target) target->AddRef();
    targets_.push_back(target);
    NotifyContext context{this, target};
    VisitDepthFirst(AsId(root_), &InitialTargetNotify, &context);
    return true;
}

bool cSC4GameDataRegistry::AddNotificationTarget(cIGZMessageTarget2* target) {
    if (std::find(messageTargets_.begin(), messageTargets_.end(), target) != messageTargets_.end()) return false;
    if (target) target->AddRef();
    messageTargets_.push_back(target);
    NotifyContext context{this, target};
    VisitDepthFirst(AsId(root_), &InitialMessageTargetNotify, &context);
    return true;
}

bool cSC4GameDataRegistry::RemoveNotificationTarget(cISC4GameDataRegistryNotificationTarget* target) {
    const auto it = std::find(targets_.begin(), targets_.end(), target);
    if (it == targets_.end()) return false;
    targets_.erase(it);
    if (target) target->Release();
    return true;
}

bool cSC4GameDataRegistry::RemoveNotificationTarget(cIGZMessageTarget2* target) {
    const auto it = std::find(messageTargets_.begin(), messageTargets_.end(), target);
    if (it == messageTargets_.end()) return false;
    messageTargets_.erase(it);
    if (target) target->Release();
    return true;
}

void cSC4GameDataRegistry::DebugDumpRecursive(EntryId from) {
    if (from == 0) return;
    VisitDepthFirst(from, [](EntryId entry, void*) {
        if (g_dump) g_dump(AsEntry(entry)->name, AsEntry(entry)->isNode);
        return true;
    }, nullptr);
}

}  // namespace sc4
