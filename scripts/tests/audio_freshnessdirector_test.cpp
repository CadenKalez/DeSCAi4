// The Freshness COM director's registration table: five classes under director id 0xfd53ff17, in the constructor's
// AddCls order, each creator returning a live object of its class.
#include <cstdio>

#include "audio/FreshnessDirector.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main() {
    CHECK(Fresh::kDirectorId == 0xfd53ff17u);
    CHECK(Fresh::kClassFactories.size() == 5);
    CHECK(Fresh::kClassFactories[0].classId == HitIds::kClsidFreshPlayer && Fresh::kClassFactories[1].classId == HitIds::kClsidFreshScore);
    CHECK(Fresh::kClassFactories[2].classId == HitIds::kClsidFreshDimensionPlayer && Fresh::kClassFactories[3].classId == HitIds::kClsidFreshCell);
    CHECK(Fresh::kClassFactories[4].classId == HitIds::kClsidFreshCellPlayer);
    for (const auto& f : Fresh::kClassFactories) {
        cIGZUnknown* object = f.create();
        CHECK(object != nullptr);
        if (object) {
            object->AddRef();
            object->Release();
        }
    }
    std::printf(g_fail ? "FAILED %d\n" : "audio_freshnessdirector OK\n", g_fail);
    return g_fail ? 1 : 0;
}
