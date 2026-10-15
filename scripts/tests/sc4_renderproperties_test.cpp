#include <cstdio>
#include <string>

#include "sc4/RenderProperties.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

int main() {
    const RenderPropertyRegistry& defaults = DefaultRenderProperties();
    CHECK(defaults.bools.size() == 38 && defaults.ints.size() == 24 && defaults.floats.size() == 15);
    CHECK(defaults.bools[0].name == "kForceSoftwareDriver" && defaults.floats[0].name.size() > 1);

    auto* p = new cSC4RenderProperties();
    p->AddRef();
    CHECK(p->NumBoolProperties() == 38 && p->NumIntProperties() == 24 && p->NumFloatProperties() == 15);
    CHECK(std::string(p->NameFromBoolPropertyID(0)) == "forcesoftwaredriver");
    CHECK(cSC4RenderProperties::MakeShortcut("ForceSoftwareDriver") == "fsd" && cSC4RenderProperties::MakeShortcut("abc") == "");
    CHECK(p->BoolPropertyIDFromName("FORCESOFTWAREDRIVER") == 0 && p->BoolPropertyIDFromName("fsd") == 0);
    CHECK(p->BoolPropertyIDFromName("renderdynamicview") == 1);
    CHECK(p->BoolPropertyIDFromName("nonsense") == 0xffffffffu);
    CHECK(!p->BoolValue(0) && p->BoolValue(1));
    p->SetBoolValue(0, true);
    CHECK(p->BoolValue(0) && !DefaultRenderProperties().bools[0].value);   // instances don't touch the registry
    std::uint32_t tb = p->IntPropertyIDFromName("texturebits");
    CHECK(tb != 0xffffffffu && p->IntValue(tb) == 32);
    p->SetIntValue(tb, 16);
    CHECK(p->IntValue(tb) == 16 && std::string(p->NameFromIntPropertyID(tb)) == "texturebits");
    std::uint32_t splat = p->FloatPropertyIDFromName("splattexturescale");
    CHECK(splat != 0xffffffffu && p->FloatValue(splat) == 50.0f && defaults.floats[splat].minimum == 1.0f && defaults.floats[splat].maximum == 128.0f);
    std::uint32_t eff = p->IntPropertyIDFromName("effectprioritylevel");
    CHECK(eff != 0xffffffffu && defaults.ints[eff].value == 5 && defaults.ints[eff].minimum == 1 && defaults.ints[eff].maximum == 5);
    p->Release();
    std::printf(failures ? "FAILED\n" : "sc4_renderproperties ok\n");
    return failures ? 1 : 0;
}
