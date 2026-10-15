// exemplar_dump <GameData dir> <group-hex> <instance-hex>: print an exemplar's properties (merged with cohorts)
#include <cstdio>
#include <cstdlib>
#include "game/GameData.h"
using namespace sc4;
int main(int argc, char** argv) {
    game::GameData data;
    std::string err;
    if (!data.Open(argv[1], err)) { std::puts(err.c_str()); return 1; }
    cGZPersistResourceKey key{kResourceTypeExemplar, (std::uint32_t)std::strtoul(argv[2], 0, 16), (std::uint32_t)std::strtoul(argv[3], 0, 16)};
    cISCResExemplar* exemplar = data.GetExemplar(key);
    if (!exemplar) { std::puts("exemplar not found"); return 1; }
    cIGZUnknownList* list = nullptr;
    exemplar->AsISCPropertyHolder()->GetPropertyList(&list);
    for (std::uint32_t i = 0; list && i < list->Size(); ++i) {
        void* p = nullptr; list->GetElement(i, kGZIID_cISCProperty, &p);
        auto* prop = static_cast<cISCProperty*>(p);
        cIGZVariant* v = prop->GetPropertyValue();
        std::printf("  0x%08x type 0x%04x count %u :", prop->GetPropertyID(), v->GetType(), v->GetCount());
        const unsigned t = v->GetType() & 0x7fff;
        if (t == 6 || t == 7) { for (unsigned k = 0; k < v->GetCount() && k < 8; ++k) std::printf(" %08x", v->RefUint32()[k]); }
        else if (t == 10) { for (unsigned k = 0; k < v->GetCount() && k < 8; ++k) std::printf(" %g", v->RefFloat32()[k]); }
        else if (t == 2) { for (unsigned k = 0; k < v->GetCount() && k < 16; ++k) std::printf(" %02x", v->RefUint8()[k]); }
        else if (v->GetType() == 0x800c || t == 12) { cRZString s; v->GetValString(s); std::printf(" \"%s\"", s.ToChar()); }
        std::printf("\n");
    }
    exemplar->Release();
}
