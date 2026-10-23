// Part of gz_gzcomcreators_core: the creators of GZCOMCreators.cpp (its headers cannot share a translation unit
// with the other creator file's).
#include <cstdio>

#include "gz/GZCOM.h"
#include "gz/SimHostRegistry.h"
#include "sc4/AdviceSubject.h"
#include "sc4/BuildingDevelopmentSimulator.h"
#include "sc4/BuildingOccupant.h"
#include "sc4/FloraOccupant.h"
#include "sc4/FloraSimulator.h"
#include "sc4/FoundationOccupant.h"
#include "sc4/LandfillOccupant.h"
#include "sc4/Lot.h"
#include "sc4/NetworkOccupant.h"
#include "sc4/Occupant.h"
#include "sc4/OccupantManager.h"
#include "sc4/PowerLineOccupant.h"
#include "sc4/PropManager.h"
#include "sc4/PropOccupant.h"
#include "sc4/cSC424HourClock.h"
#include "sc4/cSC4Demand.h"
#include "sc4/cSTETerrain.h"

namespace {
template <class T>
void Destroy(T* object) {
    if constexpr (requires { delete object; }) delete object;
    else object->Release();
}
}  // namespace

int CheckCoreCreators(int& created) {
    int failures = 0;
#define CREATE(Type, Name) do { \
        void* p = gzcom::GZCOM_Create##Name(); \
        const gzcom::ClassFactoryInfo* info = gzcom::FindClassFactory(gzcom::kCLSID_##Name); \
        if (!p || !info || info->create != &gzcom::GZCOM_Create##Name) { std::printf("FAIL %s\n", #Name); ++failures; } \
        if (p) { ++created; Destroy(static_cast<Type*>(p)); } \
    } while (0)
    CREATE(sc4::cSC4OccupantFilter, cSC4OccupantFilter);
    CREATE(sc4::cSC4DefaultOccupantFilter, cSC4DefaultOccupantFilter);
    CREATE(sc4::cSC4OccupantTypeFilter, cSC4OccupantTypeFilter);
    CREATE(sc4::cSC4LandfillOccupant, cSC4LandfillOccupant);
    CREATE(sc4::cSC4Occupant, cSC4Occupant);
    CREATE(sc4::cSC4FloraSimulator, cSC4FloraSimulator);
    CREATE(sc4::cSC4FloraOccupant, cSC4FloraOccupant);
    CREATE(sc4::cSC4BuildingDevelopmentSimulator, cSC4BuildingDevelopmentSimulator);
    CREATE(sc4::cSC4PropOccupant, cSC4PropOccupant);
    CREATE(sc4::cSC4FoundationOccupant, cSC4FoundationOccupant);
    CREATE(sc4::cSC4Lot, cSC4Lot);
    CREATE(sc4::cSC424HourClock, cSC424HourClock);
    CREATE(sc4::cSC4PropManager, cSC4PropManager);
    CREATE(sc4::cSC4BuildingOccupant, cSC4BuildingOccupant);
    CREATE(sc4::cSC4NetworkOccupant, cSC4NetworkOccupant);
    CREATE(sc4::cSC4Demand, cSC4Demand);
    CREATE(sc4::cSC4OccupantManager, cSC4OccupantManager);
    CREATE(sc4::cSC4PowerLineOccupant, cSC4PowerLineOccupant);
    CREATE(sc4::cSC4AdviceSubject, cSC4AdviceSubject);
    CREATE(sc4::cSTETerrain, cSTETerrain);
#undef CREATE
    return failures;
}
