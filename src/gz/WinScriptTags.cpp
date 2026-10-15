#include "gz/WinScriptTags.h"

#include <cstdarg>
#include <cstdio>
#include <string>

namespace gz {

namespace {
void AppendFormatted(cRZString& out, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    out.Strcat(buffer);
}
}  // namespace

void ScriptTagHex(cRZString& out, const cRZString& name, std::uint32_t value) {
    AppendFormatted(out, "%s=0x%08x ", name.ToChar(), value);
}
void ScriptTagQuad(cRZString& out, const cRZString& name, int a, int b, int c, int d) {
    AppendFormatted(out, "%s=(%d,%d,%d,%d) ", name.ToChar(), a, b, c, d);
}
void ScriptTagColor(cRZString& out, const cRZString& name, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    AppendFormatted(out, "%s=(%u,%u,%u) ", name.ToChar(), r, g, b);
}
void ScriptTagQuoted(cRZString& out, const cRZString& name, const cRZString& value) {
    AppendFormatted(out, "%s=\"%s\" ", name.ToChar(), value.ToChar());
}
void ScriptTagTextKey(cRZString& out, const cRZString& name, std::uint32_t group, std::uint32_t instance) {
    AppendFormatted(out, "%s={%08x,%08x} ", name.ToChar(), group, instance);
}
void ScriptTagBinary(cRZString& out, const cRZString& name, bool value) {
    AppendFormatted(out, "%s=%s ", name.ToChar(), value ? "yes" : "no");
}
void ScriptTagSymbol(cRZString& out, const cRZString& name, const cRZString& value) {
    AppendFormatted(out, "%s=%s ", name.ToChar(), value.ToChar());
}
void ScriptTagPair(cRZString& out, const cRZString& name, int a, int b) {
    AppendFormatted(out, "%s=(%d,%d) ", name.ToChar(), a, b);
}
void ScriptTagSimple(cRZString& out, const cRZString& name) {
    out.Strcat(name.ToChar());
    out.Strcat(" ");
}
void ScriptTagInt(cRZString& out, const cRZString& name, int value) {
    AppendFormatted(out, "%s=%d ", name.ToChar(), value);
}
void ScriptTagImageKey(cRZString& out, const cRZString& name, std::uint32_t group, std::uint32_t instance) {
    AppendFormatted(out, "%s={%08x,%08x} ", name.ToChar(), group, instance);
}
void ScriptTagOptionGroupOption(cRZString& out, const cRZString& name, std::uint32_t id, int a, int b) {
    AppendFormatted(out, "%s=\"%u:(%d,%d)\" ", name.ToChar(), id, a, b);
}

}  // namespace gz
