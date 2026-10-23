// Part of gz_gzcomcreators_core: the creator of GZCOMCreatorsLotConfig.cpp (its header clashes with part 1's).
#include <cstdio>

#include "gz/GZCOM.h"
#include "sc4/LotConfigurationManager.h"

int CheckLotConfigCreator(int& created) {
    void* p = gzcom::GZCOM_CreatecSC4LotConfigurationManager();
    const gzcom::ClassFactoryInfo* info = gzcom::FindClassFactory(gzcom::kCLSID_cSC4LotConfigurationManager);
    int failures = 0;
    if (!p || !info || info->create != &gzcom::GZCOM_CreatecSC4LotConfigurationManager) { std::printf("FAIL cSC4LotConfigurationManager\n"); ++failures; }
    if (p) {
        ++created;
        auto* manager = static_cast<sc4::cSC4LotConfigurationManager*>(p);
        if constexpr (requires { delete manager; }) delete manager;
        else manager->Release();
    }
    return failures;
}
