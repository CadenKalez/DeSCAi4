#include <cstdio>
#include <string>

#include "rz/cRZVariant.h"
#include "sc4/GameDataRegistryHelpers.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Owner final : cIGZUnknown {
    int refs = 0, qis = 0;
    bool QueryInterface(std::uint32_t iid, void** out) override { if (iid != 1) return false; ++qis; *out = this; AddRef(); return true; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
bool ValueFn(void*, cIGZVariant&, bool, cISC4GameDataRegistry*) { return true; }
bool CommandFn(void*, cIGZCommandParameterSet*, cIGZCommandParameterSet*, cISC4GameDataRegistry*) { return true; }
cSC4GameDataRegistry::cEntry* Entry(EntryId id) { return static_cast<cSC4GameDataRegistry::cEntry*>(reinterpret_cast<void*>(id)); }
}  // namespace

int main() {
    auto* reg = new cSC4GameDataRegistry();
    reg->AddRef();
    Owner sim, child;
    int value = 5;
    // nodes: under the root, then under another node
    const EntryId simEntry = nsISC4GameDataRegistry::AddNode(&sim, "Sim", static_cast<Owner*>(nullptr), 0, nullptr, reg);
    CHECK(simEntry && reg->IsNodeEntry(simEntry) && Entry(simEntry)->parent == Entry(reg->GetRootNodeEntry()));
    CHECK(sim.refs == 1);
    const EntryId childEntry = nsISC4GameDataRegistry::AddNode(&child, "Child", &sim, 0, nullptr, reg);
    CHECK(childEntry && Entry(childEntry)->parent == Entry(simEntry));
    CHECK(sim.refs == 1);
    CHECK(nsISC4GameDataRegistry::AddNode(static_cast<Owner*>(nullptr), "x", &sim, 0, nullptr, reg) == 0);
    CHECK(nsISC4GameDataRegistry::AddNode(&sim, "x", &sim, 0, nullptr, static_cast<cISC4GameDataRegistry*>(nullptr)) == 0);
    Owner stranger;
    CHECK(nsISC4GameDataRegistry::AddNode(&child, "orphan", &stranger, 0, nullptr, reg) == 0);   // parent unknown to the registry
    CHECK(stranger.refs == 0);

    // values: never a command; commands: always a command
    const EntryId v = nsISC4GameDataRegistry::AddValue(&value, "Value", ValueFn, &sim, 1, nullptr, reg);
    CHECK(v && Entry(v)->flags == 0 && Entry(v)->parent == Entry(simEntry));
    const EntryId c = nsISC4GameDataRegistry::AddValue(&sim, "Cmd", CommandFn, &sim, 2, nullptr, reg);
    CHECK(c && (Entry(c)->flags & 1) && (Entry(c)->flags & 2));
    const EntryId rootValue = nsISC4GameDataRegistry::AddValue(&child, "RootCmd", CommandFn, static_cast<Owner*>(nullptr), 0, nullptr, reg);
    CHECK(rootValue && Entry(rootValue)->parent == Entry(reg->GetRootNodeEntry()));
    CHECK(nsISC4GameDataRegistry::AddValue(&value, "Lost", ValueFn, &stranger, 0, nullptr, reg) == 0);
    CHECK(nsISC4GameDataRegistry::AddValue(&value, "NoReg", ValueFn, &sim, 0, nullptr, static_cast<cISC4GameDataRegistry*>(nullptr)) == 0);

    // exchange functions
    float f = 1.5f;
    std::uint32_t u = 7;
    std::uint8_t b = 3;
    cRZVariant variant;
    CHECK(nsISC4GameDataRegistry::GenericValueExchnageFuncFloat32(&f, variant, false, reg) && variant.GetValFloat32() == 1.5f);
    variant.SetValFloat32(9.0f);
    CHECK(nsISC4GameDataRegistry::GenericValueExchnageFuncFloat32(&f, variant, true, reg) && f == 9.0f);
    CHECK(nsISC4GameDataRegistry::GenericValueExchnageFuncUint32(&u, variant, false, reg) && variant.GetValUint32() == 7);
    variant.SetValUint32(99);
    CHECK(nsISC4GameDataRegistry::GenericValueExchnageFuncUint32(&u, variant, true, reg) && u == 99);
    CHECK(nsISC4GameDataRegistry::GenericValueExchnageFuncUint8(&b, variant, false, reg) && variant.GetValUint8() == 3);
    variant.SetValUint8(200);
    CHECK(nsISC4GameDataRegistry::GenericValueExchnageFuncUint8(&b, variant, true, reg) && b == 200);
    reg->Release();
    if (failures == 0) std::printf("sc4_gamedataregistryhelpers: all passed\n");
    return failures == 0 ? 0 : 1;
}
