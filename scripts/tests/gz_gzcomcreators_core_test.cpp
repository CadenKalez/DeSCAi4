// Calls every GZCOM creator implemented in GZCOMCreators.cpp (20), GZCOMCreatorsLotConfig.cpp (1) and GZCOMCreators7.cpp (15): each must hand back a
// new object, be the creator the class-factory table lists for that class id, and destroy cleanly (the simulators get
// the default null hosts, which their constructors and destructors must tolerate).
#include <cstdio>

int CheckCoreCreators(int& created);
int CheckSimulatorCreators(int& created);
int CheckLotConfigCreator(int& created);

int main() {
    int created = 0;
    int failures = CheckCoreCreators(created) + CheckSimulatorCreators(created) + CheckLotConfigCreator(created);
    if (created != 36) { std::printf("FAIL created %d of 36\n", created); ++failures; }
    std::printf(failures ? "FAILED %d\n" : "gz_gzcomcreators_core: all passed\n", failures);
    return failures != 0;
}
