// Generated: GZCOM creators of recovered classes (budget, disaster and buffer classes; first batch: scripts/gen_gzcom.py).
#include "gz/GZCOM.h"
#include "gz/Buffer.h"
#include "sc4/Budget.h"
#include "sc4/BudgetSimulator.h"
#include "sc4/Disaster.h"

namespace gzcom {

void* GZCOM_CreatecSC4BudgetSimulator() { return static_cast<void*>(new sc4::cSC4BudgetSimulator()); }
void* GZCOM_CreatecSC4DepartmentBudget() { return static_cast<void*>(new sc4::cSC4DepartmentBudget()); }
void* GZCOM_CreatecGZBuffer() { return static_cast<void*>(new cGZBuffer()); }
void* GZCOM_CreatecSC4DisasterManagerEffect() { return static_cast<void*>(new sc4::cSC4DisasterManagerEffect()); }
void* GZCOM_CreatecSC4DisasterLayer() { return static_cast<void*>(new sc4::cSC4DisasterLayer()); }
void* GZCOM_CreatecSC4DisasterManagerToxic() { return static_cast<void*>(new sc4::cSC4DisasterManagerToxic()); }

}  // namespace gzcom
