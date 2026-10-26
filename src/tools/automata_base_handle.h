// A handle to a real cSC4AutomataManager for the watercraft manager's inherited half. The two recovered headers (AutomataManager.h,
// WatercraftManager.h) cannot be included in one translation unit, so the base is reached through these plain functions.
#pragma once
#include <cstdint>

class cIGZIStream;
class cIGZOStream;

namespace sc4 {
void* AutomataBaseCreate();
void AutomataBaseDestroy(void* base);
bool AutomataBaseReadInternal(void* base, cIGZIStream& stream, std::uint16_t version);
bool AutomataBaseRead(void* base, cIGZIStream& stream);
bool AutomataBaseWrite(void* base, cIGZOStream& stream);
}  // namespace sc4
