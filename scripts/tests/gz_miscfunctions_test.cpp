#include <cstdio>
#include <cstring>
#include <string>

#include "gz/MiscFunctions.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Terrain final : ISTETerrainAltitude {
    float lastX = -1, lastZ = -1;
    float GetCellSize() override { return 16.0f; }
    int GetWidthInCells() override { return 4; }
    int GetDepthInCells() override { return 2; }
    float GetAltitude(float x, float z) override { lastX = x; lastZ = z; return x + z; }
};
std::string Trim(const char* in) {
    char buf[64];
    std::strcpy(buf, in);
    RemoveUnnecessaryFloatingPointDigits(buf, static_cast<int>(std::strlen(buf)));
    return buf;
}
}  // namespace

int main() {
    CHECK(HTMLIsSpace(" x", 2) && HTMLIsSpace("\t", 1) && HTMLIsSpace("\n", 1) && HTMLIsSpace("\r", 1) && HTMLIsSpace("\f", 1));
    CHECK(!HTMLIsSpace("a", 1) && !HTMLIsSpace(" ", 0) && !HTMLIsSpace("\v", 1) && !HTMLIsSpace("\xe2\x80\x8b", 3));
    cRZString text("ab \xc3\xa9z");
    CHECK(HTMLIsSpace(text, 2) && !HTMLIsSpace(text, 0));
    CHECK(HTMLAdvance(text, 0) == 1 && HTMLAdvance(text, 3) == 5);
    CHECK(HTMLBackspace(text, 5) == 3 && HTMLBackspace(text, 2) == 1);
    CHECK(gCombineGUIDs(0x0f0f, 0x00ff) == 0x0ff0);

    cRZString out;
    CHECK(gConvertURLFromEndodedForm(cRZString("a%20b%2Fc"), out) && std::string(out.ToChar()) == "a b/c");
    gConvertURLFromEndodedForm(cRZString("100%"), out);
    CHECK(std::string(out.ToChar()) == "100%");
    gConvertURLFromEndodedForm(cRZString("%zz%4"), out);
    CHECK(std::string(out.ToChar()) == "%zz%4");
    gConvertURLFromEndodedForm(cRZString("x%41"), out);
    CHECK(std::string(out.ToChar()) == "xA");
    gConvertURLFromEndodedForm(cRZString(""), out);
    CHECK(out.Strlen() == 0);

    CHECK(Trim("1.500") == "1.5" && Trim("2.000") == "2" && Trim("10") == "1" && Trim("100.0") == "100");
    CHECK(Trim("0.000") == "0" && Trim("0.5e+10") == "0.5e+10" && Trim("1.2500e+05") == "1.25e+05" && Trim("3.0e+02") == "3.e+02");

    int x0 = -10, y0 = 5, x1 = 20, y1 = 5;
    CHECK(ClipLineToRect(x0, y0, x1, y1, 0, 0, 10, 10) && x0 == 0 && x1 == 10 && y0 == 5 && y1 == 5);
    x0 = -5; y0 = -5; x1 = -1; y1 = 20;
    CHECK(!ClipLineToRect(x0, y0, x1, y1, 0, 0, 10, 10));
    x0 = 2; y0 = 2; x1 = 8; y1 = 8;
    CHECK(ClipLineToRect(x0, y0, x1, y1, 0, 0, 10, 10) && x0 == 2 && y1 == 8);
    x0 = 5; y0 = -5; x1 = 5; y1 = 15;
    CHECK(ClipLineToRect(x0, y0, x1, y1, 0, 0, 10, 10) && y0 == 0 && y1 == 10 && x0 == 5);
    x0 = -4; y0 = -4; x1 = 14; y1 = 14;
    CHECK(ClipLineToRect(x0, y0, x1, y1, 0, 0, 10, 10) && x0 == 0 && y0 == 0 && x1 == 10 && y1 == 10);

    SC4RectInt in{1, 2, 3, 4}, r;
    HillToTerrainRect(0, 10, 20, in, r);
    CHECK(r.left == 2 && r.top == 1 && r.right == 4 && r.bottom == 3);
    HillToTerrainRect(1, 10, 20, in, r);
    CHECK(r.left == 6 && r.right == 8 && r.top == 2 && r.bottom == 4);
    HillToTerrainRect(2, 10, 20, in, r);
    CHECK(r.left == 5 && r.right == 7 && r.top == 16 && r.bottom == 18);
    HillToTerrainRect(3, 10, 20, in, r);
    CHECK(r.left == 1 && r.right == 3 && r.top == 15 && r.bottom == 17);
    r = {9, 9, 9, 9};
    HillToTerrainRect(7, 10, 20, in, r);
    CHECK(r.left == 9 && r.bottom == 9);

    std::uint16_t v[7] = {1, 2, 3, 4, 5, 65535, 0};
    FastAddOffset(v, 7, 3);
    CHECK(v[0] == 4 && v[4] == 8 && v[5] == 2 && v[6] == 3);
    FastAddOffset(v, 0, 100);
    CHECK(v[0] == 4);

    Terrain t;
    CHECK(SafeGetAltitude(&t, 10, 5) == 15 && t.lastX == 10 && t.lastZ == 5);
    SafeGetAltitude(&t, -3, -9);
    CHECK(t.lastX == 0 && t.lastZ == 0);
    SafeGetAltitude(&t, 1000, 1000);
    CHECK(t.lastX == 64 && t.lastZ == 32);
    std::printf(failures ? "FAILED\n" : "gz_miscfunctions ok\n");
    return failures != 0;
}
