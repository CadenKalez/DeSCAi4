#include <cstdio>
#include "sc4/CityBuildings.h"
int main(int argc, char** argv) {
    std::vector<sc4::BuildingRecord> b; std::string err;
    if (!sc4::LoadCityBuildings(argv[1], b, err)) { std::puts(err.c_str()); return 1; }
    std::printf("%zu buildings\n", b.size());
    for (size_t i = 0; i < b.size() && i < 12; ++i)
        std::printf("  id %llu type %08x exemplar %08x/%08x/%08x orient %d bbox (%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f) lit %d done %.2f\n",
            (unsigned long long)b[i].objectId, b[i].buildingType, b[i].exemplar.type, b[i].exemplar.group, b[i].exemplar.instance,
            b[i].orientation, b[i].min[0], b[i].min[1], b[i].min[2], b[i].max[0], b[i].max[1], b[i].max[2], b[i].lit, b[i].completion);
}
