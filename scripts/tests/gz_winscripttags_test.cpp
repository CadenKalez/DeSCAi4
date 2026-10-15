#include <cstdio>
#include <string>

#include "gz/WinScriptTags.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;
int main() {
    cRZString out;
    ScriptTagInt(out, cRZString("id"), -4);
    ScriptTagHex(out, cRZString("flags"), 0xab);
    ScriptTagQuad(out, cRZString("area"), 1, 2, 3, 4);
    ScriptTagPair(out, cRZString("gut"), 5, 6);
    ScriptTagColor(out, cRZString("clr"), 255, 0, 7);
    ScriptTagQuoted(out, cRZString("caption"), cRZString("Hello"));
    ScriptTagSymbol(out, cRZString("style"), cRZString("flat"));
    ScriptTagBinary(out, cRZString("on"), true);
    ScriptTagBinary(out, cRZString("off"), false);
    ScriptTagTextKey(out, cRZString("text"), 0x1, 0xfffe0000);
    ScriptTagImageKey(out, cRZString("image"), 0x12, 0x34);
    ScriptTagOptionGroupOption(out, cRZString("opt"), 9, -1, 2);
    ScriptTagSimple(out, cRZString("visible"));
    CHECK(std::string(out.ToChar()) ==
          "id=-4 flags=0x000000ab area=(1,2,3,4) gut=(5,6) clr=(255,0,7) caption=\"Hello\" style=flat on=yes off=no "
          "text={00000001,fffe0000} image={00000012,00000034} opt=\"9:(-1,2)\" visible ");
    std::printf(failures ? "FAILED\n" : "gz_winscripttags: all passed\n");
    return failures ? 1 : 0;
}
