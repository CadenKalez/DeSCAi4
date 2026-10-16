#include <cstdio>
#include <string>
#include <vector>

#include "rz/cRZVariant.h"
#include "sc4/GameDataRegistry.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Node final : cIGZUnknown {
    int refs = 0;
    bool QueryInterface(std::uint32_t iid, void** out) override { if (iid != 1) return false; *out = this; AddRef(); return true; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
struct Target final : cISC4GameDataRegistryNotificationTarget {
    int refs = 0; std::vector<std::pair<int, EntryId>> events;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void OnRegistryEvent(cISC4GameDataRegistry*, int event, EntryId entry) override { events.push_back({event, entry}); }
};
struct MsgTarget final : cIGZMessageTarget2 {
    int refs = 0, seen = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool DoMessage(cIGZMessage2* m) override { if (m->GetType() == kMessageGameDataRegistryEntry) ++seen; return true; }
};
int stored = 7;
bool IntValue(void* context, cIGZVariant& value, bool write, cISC4GameDataRegistry*) {
    int* p = static_cast<int*>(context);
    if (write) *p = static_cast<int>(value.GetValSint32()); else value.SetValSint32(*p);
    return true;
}
int calls = 0;
bool Command(void*, cIGZCommandParameterSet*, cIGZCommandParameterSet*, cISC4GameDataRegistry*) { ++calls; return true; }
std::vector<std::string> visited;
bool Record(EntryId id, void*) {
    visited.push_back(static_cast<cSC4GameDataRegistry::cEntry*>(reinterpret_cast<void*>(id))->name.ToChar());
    return true;
}
}  // namespace

int main() {
    auto* reg = new cSC4GameDataRegistry();
    reg->AddRef();
    CHECK(reg->GetServiceID() == 0xebe0e860 && reg->GetServicePriority() == -1000000);
    void* iface = nullptr;
    CHECK(reg->QueryInterface(kGZIID_cISC4GameDataRegistry, &iface) && iface == static_cast<cISC4GameDataRegistry*>(reg));
    static_cast<cIGZUnknown*>(iface)->Release();

    const EntryId root = reg->GetRootNodeEntry();
    CHECK(root != 0 && reg->IsNodeEntry(root) && std::string(reg->GetEntryName(root)->ToChar()) == "__root__");
    Target target; MsgTarget mt;
    CHECK(reg->AddNotificationTarget(&target) && !reg->AddNotificationTarget(&target));
    CHECK(target.events.size() == 1 && target.events[0].first == kRegistryEntryAdded && target.events[0].second == root);   // existing root announced
    CHECK(reg->AddNotificationTarget(&mt) && mt.seen == 1);

    Node city, sim;
    const EntryId cityId = reg->AddChildEntry(&city, "city", root, 0, nullptr);
    CHECK(cityId != 0 && reg->GetNode(cityId) == &city && city.refs == 1 && reg->GetParentNodeEntry(cityId) == root);
    CHECK(reg->AddChildEntry(&city, "again", root, 0, nullptr) == cityId);            // already registered
    const EntryId simId = reg->AddChildEntry(&sim, "sim", cityId, 0, nullptr);
    CHECK(simId != 0 && reg->GetEntryID(&sim) == simId);

    const EntryId valueId = reg->AddChildEntry(&stored, "funds", &IntValue, simId, 0, &stored);
    CHECK(valueId != 0 && !reg->IsNodeEntry(valueId) && reg->GetValue(valueId) == &stored && reg->GetEntryID(static_cast<void*>(&stored)) == valueId);
    CHECK(reg->AddChildEntry(&stored, "dup", &IntValue, simId, 0, nullptr) == 0);     // handle already registered
    int other = 0;
    CHECK(reg->AddChildEntry(&other, "bad", &IntValue, valueId, 0, nullptr) == 0);    // parent must be a node
    CHECK(reg->GetEntryContext(valueId) == &stored);
    cRZVariant var;
    CHECK(reg->DoValueExchange(valueId, var, false) && var.GetValSint32() == 7);
    var.SetValSint32(42);
    CHECK(reg->DoValueExchange(valueId, var, true) && stored == 42);
    const EntryId readOnly = reg->AddChildEntry(&other, "ro", &IntValue, simId, 2, &other);
    CHECK(reg->CheckEntryFlags(readOnly, 2) && !reg->DoValueExchange(readOnly, var, true) && reg->DoValueExchange(readOnly, var, false));
    int commandHandle = 0;
    const EntryId cmd = reg->AddChildEntry(&commandHandle, "go", &Command, simId, 0, nullptr);
    CHECK(reg->CheckEntryFlags(cmd, 1) && reg->DoValueCall(cmd, nullptr, nullptr) && calls == 1);

    visited.clear(); reg->VisitBreadthFirst(root, &Record, nullptr);
    CHECK((visited == std::vector<std::string>{"__root__", "city", "sim", "funds", "ro", "go"}));
    visited.clear(); reg->VisitDepthFirst(cityId, &Record, nullptr);
    CHECK(visited.size() == 5 && visited[0] == "city" && visited[1] == "sim" && visited[2] == "go");     // last child first
    visited.clear(); reg->VisitImmediateChildren(simId, &Record, nullptr);
    CHECK((visited == std::vector<std::string>{"funds", "ro", "go"}));
    visited.clear(); reg->VisitBreadthFirstReverse(cityId, &Record, nullptr);
    CHECK(visited.size() == 5 && visited.back() == "city" && visited[0] == "funds");
    CHECK(reg->IsValidEntry(valueId) && !reg->IsValidEntry(12345));

    target.events.clear();
    CHECK(reg->RemoveChildEntry(&sim));                                               // takes the value entries with it
    CHECK(target.events.size() == 4 && target.events.front().first == kRegistryEntryRemoved);
    CHECK(reg->GetEntryID(static_cast<void*>(&stored)) == 0 && reg->GetEntryID(&sim) == 0 && sim.refs == 0);
    CHECK(!reg->RemoveEntry(root));

    CHECK(reg->RemoveNotificationTarget(&mt) && !reg->RemoveNotificationTarget(&mt) && mt.refs == 0);
    target.events.clear();
    reg->Shutdown();
    CHECK(target.events.size() == 1 && target.events[0].first == kRegistryShutdown && target.refs == 0 && city.refs == 0);
    reg->Release();
    std::printf(failures ? "FAILED\n" : "sc4_gamedataregistry ok\n");
    return failures != 0;
}
