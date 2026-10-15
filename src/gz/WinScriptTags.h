// Window-script text writers (0x100565174..0x100565f60): each appends one "name=value " attribute to the
// script text being built by the unscript_* functions.
#pragma once
#include <cstdint>

#include "rz/cRZString.h"

namespace gz {

void ScriptTagHex(cRZString& out, const cRZString& name, std::uint32_t value);                   // name=0x%08x
void ScriptTagQuad(cRZString& out, const cRZString& name, int a, int b, int c, int d);           // name=(a,b,c,d)
void ScriptTagColor(cRZString& out, const cRZString& name, std::uint8_t r, std::uint8_t g, std::uint8_t b);
void ScriptTagQuoted(cRZString& out, const cRZString& name, const cRZString& value);             // name="value"
void ScriptTagTextKey(cRZString& out, const cRZString& name, std::uint32_t group, std::uint32_t instance);
void ScriptTagBinary(cRZString& out, const cRZString& name, bool value);                         // name=yes|no
void ScriptTagSymbol(cRZString& out, const cRZString& name, const cRZString& value);             // name=value
void ScriptTagPair(cRZString& out, const cRZString& name, int a, int b);                         // name=(a,b)
void ScriptTagSimple(cRZString& out, const cRZString& name);                                     // name
void ScriptTagInt(cRZString& out, const cRZString& name, int value);                             // name=%d
void ScriptTagImageKey(cRZString& out, const cRZString& name, std::uint32_t group, std::uint32_t instance);
void ScriptTagOptionGroupOption(cRZString& out, const cRZString& name, std::uint32_t id, int a, int b);

}  // namespace gz
