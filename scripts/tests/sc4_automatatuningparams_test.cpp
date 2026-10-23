#include <cstdio>
#include <cstring>

#include "sc4/AutomataTuningParams.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

static std::uint32_t Bits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }

int main() {
    auto* p = new cSC4AutomataTuningParams();
    // the integer block (offsets 0x00..0x28)
    CHECK(p->f000 == 300 && p->f004 == 100 && p->f008 == 120 && p->f00c == 10 && p->f010 == 30 && p->f014 == 300);
    CHECK(p->f018 == 5 && p->f01c == 10 && p->f020 == 5 && p->f024 == 10 && p->f028 == 100);
    // floats compared bit for bit with the constants stored by the original
    CHECK(Bits(p->f050) == 0x40400000 && Bits(p->f078) == 0x3f000000 && Bits(p->f0a0) == 0x3dcccccd && Bits(p->f0c8) == 0x3dcccccd && Bits(p->f0f0) == 0x40400000);
    CHECK(Bits(p->f120) == 0x41200000 && Bits(p->f124) == 0x3f800000 && Bits(p->f12c) == 0x40a00000 && Bits(p->f138) == 0x3f333333 &&
          Bits(p->f13c) == 0x43160000 && Bits(p->f140) == 0x3f000000 && Bits(p->f144) == 0x3f333333);
    CHECK(Bits(p->f190) == 0x3f19999a && Bits(p->f198) == 0x42960000 && Bits(p->f1a4) == 0xbf000000 && Bits(p->f1a8) == 0x3f000000);
    CHECK(Bits(p->f1b4) == 0x3f490fdb && Bits(p->f1b8) == 0x3e99999a && Bits(p->f1bc) == 0x3c23d70a && Bits(p->f1f4) == 0x3dcccccd);
    CHECK(p->b148 == 2 && p->f14c == -1 && p->f150 == -1 && p->b154 == 2 && p->b155 == 3 && p->f178 == 150 && p->b17c == 4 && p->b17d == 1);
    CHECK(p->b1c0 == 3 && p->b1c1 == 10 && p->b1c2 == 5 && p->b1c3 == 8 && p->b1c4 == 3 && p->b1c5 == 8 && p->f1c8 == 140 && p->f1cc == 150 && p->f1d0 == 40);
    CHECK(p->b1f0 == 5 && p->f1f8 == 100 && p->b228 == 1 && p->f22c == -1 && p->f220 == 1.0f && p->f250 == 1.0f);
    CHECK(p->v158.empty() && p->curvesById.empty() && p->spawnRecall.empty());
    // every curve is flat at 0.5 (the duplicate point is dropped)
    const cSC4ResponseCurve* curves[] = {&p->curve030, &p->curve058, &p->curve080, &p->curve0a8, &p->curve0d0, &p->curve100};
    for (const cSC4ResponseCurve* c : curves) CHECK(c->GetOutputValue(-5.0f) == 0.5f && c->GetOutputValue(100.0f) == 0.5f);
    delete p;
    std::printf(failures ? "FAILED\n" : "sc4_automatatuningparams OK\n");
    return failures ? 1 : 0;
}
