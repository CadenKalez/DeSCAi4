// cSC4GrowthDeveloper behind plain functions: GrowthDeveloper.h (through TractDeveloper.h) and BuildingDevelopmentSimulator.h
// define the same names (TractOutcome, ConstructionProfile), so the building development host reaches the class from this TU.
#include "app/NativeGrowthDevelopers.h"

#include "sc4/GrowthDeveloper.h"

namespace native {

void* NewGrowthDeveloper() {
    auto* developer = new sc4::cSC4GrowthDeveloper();
    developer->AddRef();
    return developer;
}
bool GrowthDeveloperInit(void* developer, sc4::cISCPropertyHolder* exemplar) {
    return developer && static_cast<sc4::cSC4GrowthDeveloper*>(developer)->Init(exemplar);
}
void GrowthDeveloperStartMapUpdate(void* developer) {
    if (developer) static_cast<sc4::cSC4GrowthDeveloper*>(developer)->StartMapUpdate();
}
bool GrowthDeveloperMapUpdateFinished(void* developer) {
    return !developer || static_cast<sc4::cSC4GrowthDeveloper*>(developer)->IsMapUpdateFinished();
}

}  // namespace native
