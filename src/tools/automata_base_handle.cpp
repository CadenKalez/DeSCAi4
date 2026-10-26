#include "tools/automata_base_handle.h"

#include "sc4/AutomataManager.h"

namespace sc4 {

void* AutomataBaseCreate() {
    auto* manager = new cSC4AutomataManager();
    manager->AddRef();
    return manager;
}
void AutomataBaseDestroy(void* base) { if (base) static_cast<cSC4AutomataManager*>(base)->Release(); }
bool AutomataBaseReadInternal(void* base, cIGZIStream& stream, std::uint16_t version) {
    return base && static_cast<cSC4AutomataManager*>(base)->ReadInternal(stream, version);
}
bool AutomataBaseRead(void* base, cIGZIStream& stream) { return base && static_cast<cSC4AutomataManager*>(base)->Read(stream); }
bool AutomataBaseWrite(void* base, cIGZOStream& stream) { return base && static_cast<cSC4AutomataManager*>(base)->Write(stream); }

}  // namespace sc4
