// The surface water's calls into the neighbours simulator (its header cannot share a translation unit with the terrain host).
#include "sc4/NeighborsSimulator.h"

namespace native {

namespace {
sc4::cISC4NeighborsSimulator* Neighbors(cIGZUnknown* component) {
    void* raw = nullptr;
    if (!component || !component->QueryInterface(sc4::kGZIID_cISC4NeighborsSimulator, &raw) || !raw) return nullptr;
    auto* sim = static_cast<sc4::cISC4NeighborsSimulator*>(raw);
    sim->Release();                                   // the city keeps its reference
    return sim;
}
}  // namespace

bool NeighborsHasConnection(cIGZUnknown* component, std::uint32_t type, int x, int z) {
    sc4::cISC4NeighborsSimulator* sim = Neighbors(component);
    return sim && sim->HasConnection(type, x, z);
}
void NeighborsDeleteConnection(cIGZUnknown* component, std::uint32_t type, int x, int z) {
    if (sc4::cISC4NeighborsSimulator* sim = Neighbors(component)) sim->DeleteConnection(type, x, z);
}
void NeighborsCreateConnection(cIGZUnknown* component, std::uint32_t type, int x, int z) {
    if (sc4::cISC4NeighborsSimulator* sim = Neighbors(component)) sim->CreateConnection(type, x, z, false);
}

}  // namespace native
