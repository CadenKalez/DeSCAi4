#include <cstdio>

#include "rz/PortMemoryStreams.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/IndustrialSimulator.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

int main() {
    auto* sim = new cSC4IndustrialSimulator();
    sim->AddRef();
    CHECK(sim->GetGZCLSID() == 0xa990c022 && !sim->IsInitialised());
    CHECK(sim->Init() && sim->IsInitialised() && sim->Shutdown() && !sim->IsInitialised());
    PortMemoryOStream out;
    CHECK(sim->Write(out) && out.bytes().size() == 2);
    PortMemoryIStream in(out.bytes().data(), out.bytes().size());
    CHECK(sim->Read(in));
    const unsigned char bad[2] = {2, 0};
    PortMemoryIStream badStream(bad, 2);
    CHECK(!sim->Read(badStream));
    auto* message = new cRZMessage2Standard();
    message->AddRef();
    CHECK(sim->DoMessage(message));
    message->Release();
    void* out2 = nullptr;
    CHECK(sim->QueryInterface(0x477a9e7d, &out2) && out2 && !sim->QueryInterface(5, &out2));
    static_cast<cIGZSerializable*>(out2)->Release();
    sim->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
