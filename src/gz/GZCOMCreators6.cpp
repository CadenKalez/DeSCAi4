// Generated: GZCOM creators of recovered simulators and services (message server, draw context, business unit, industrial simulator).
#include "gz/GZCOM.h"
#include "gz/DrawContextAsBuffer.h"
#include "gz/DrawSystem.h"
#include "gz/cGZMessageQueueManager2.h"
#include "gz/cGZMessageServer2.h"
#include "sc4/BusinessFeature.h"
#include "sc4/IndustrialSimulator.h"

namespace gzcom {

void* GZCOM_CreatecGZDrawContextAsBuffer() { return static_cast<void*>(new gz::cGZDrawContextAsBuffer()); }
void* GZCOM_CreatecGZDrawSystem() { return static_cast<void*>(new cGZDrawSystem()); }
void* GZCOM_CreatecGZMessageQueueManager2() { return static_cast<void*>(new cGZMessageQueueManager2()); }
void* GZCOM_CreatecGZMessageServer2() { return static_cast<void*>(new cGZMessageServer2()); }
void* GZCOM_CreatecSC4BusinessUnit() { return static_cast<void*>(new sc4::cSC4BusinessUnit()); }
void* GZCOM_CreatecSC4IndustrialSimulator() { return static_cast<void*>(new sc4::cSC4IndustrialSimulator()); }

}  // namespace gzcom
