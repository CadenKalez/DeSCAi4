// Compares src/gz/ColorConversion.cpp with the Ghidra decompilation of cGZGraphicSystem's colour functions
// (work/color/oracle.c), over every RGB / HSB / HLS byte triple and a large random sample of float inputs.
#include "gz/ColorConversion.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
typedef unsigned char u8;
extern "C" {
void oracle_1006184aa(void*, u8, u8, u8, u8*, u8*, u8*);
void oracle_1006185c0(void*, u8, u8, u8, float*, float*, float*);
void oracle_1006186c6(void*, u8, u8, u8, u8*, u8*, u8*);
void oracle_1006187d8(void*, u8, u8, u8, float*, float*, float*);
void oracle_100618930(void*, u8, u8, u8, u8*, u8*, u8*);
void oracle_100618c6a(void*, float, float, float, u8*, u8*, u8*);
void oracle_100618f7e(void*, u8, u8, u8, u8*, u8*, u8*);
void oracle_100618f98(void*, u8, u8, u8, u8*, u8*, u8*, u8*);
void oracle_100618fd8(void*, u8, u8, u8, u8, u8*, u8*, u8*);
void oracle_10061faf0(void*, float, float, float, u8*, u8*, u8*);
void oracle_10061fbf0(void*, u8, u8, u8, u8*, u8*, u8*);
void oracle_10061fcc0(void*, u8, u8, u8, u8*, u8*, u8*);
}
static long bad = 0;
static void Fail(const char* what, unsigned a, unsigned b, unsigned c) { if (++bad < 20) std::printf("MISMATCH %s (%u,%u,%u)\n", what, a, b, c); }
static bool SameBits(float a, float b) { return std::memcmp(&a, &b, 4) == 0; }
static float RandFloat(float lo, float hi) { return lo + (hi - lo) * (float(rand()) / float(RAND_MAX)); }

int main() {
    using namespace nGZColor;
    for (unsigned a = 0; a < 256; ++a)
        for (unsigned b = 0; b < 256; ++b)
            for (unsigned c = 0; c < 256; ++c) {
                u8 x[3], y[3];
                RGBToHSB(u8(a), u8(b), u8(c), x[0], x[1], x[2]);
                oracle_1006184aa(nullptr, u8(a), u8(b), u8(c), &y[0], &y[1], &y[2]);
                if (std::memcmp(x, y, 3)) Fail("RGBToHSB u8", a, b, c);
                float fx[3], fy[3];
                RGBToHSB(u8(a), u8(b), u8(c), fx[0], fx[1], fx[2]);
                oracle_1006185c0(nullptr, u8(a), u8(b), u8(c), &fy[0], &fy[1], &fy[2]);
                if (!SameBits(fx[0], fy[0]) || !SameBits(fx[1], fy[1]) || !SameBits(fx[2], fy[2])) Fail("RGBToHSB float", a, b, c);
                HSBToRGB(u8(a), u8(b), u8(c), x[0], x[1], x[2]);
                oracle_1006186c6(nullptr, u8(a), u8(b), u8(c), &y[0], &y[1], &y[2]);
                if (std::memcmp(x, y, 3)) Fail("HSBToRGB u8", a, b, c);
                RGBToHLS(u8(a), u8(b), u8(c), fx[0], fx[1], fx[2]);
                oracle_1006187d8(nullptr, u8(a), u8(b), u8(c), &fy[0], &fy[1], &fy[2]);
                if (!SameBits(fx[0], fy[0]) || !SameBits(fx[1], fy[1]) || !SameBits(fx[2], fy[2])) Fail("RGBToHLS float", a, b, c);
                HLSToRGB(u8(a), u8(b), u8(c), x[0], x[1], x[2]);
                oracle_100618930(nullptr, u8(a), u8(b), u8(c), &y[0], &y[1], &y[2]);
                if (std::memcmp(x, y, 3)) Fail("HLSToRGB u8", a, b, c);
                RGBToHLS(u8(a), u8(b), u8(c), x[0], x[1], x[2]);
                oracle_10061fbf0(nullptr, u8(a), u8(b), u8(c), &y[0], &y[1], &y[2]);
                if (std::memcmp(x, y, 3)) Fail("RGBToHLS u8", a, b, c);
                RGBToCYM(u8(a), u8(b), u8(c), x[0], x[1], x[2]);
                oracle_10061fcc0(nullptr, u8(a), u8(b), u8(c), &y[0], &y[1], &y[2]);
                if (std::memcmp(x, y, 3)) Fail("RGBToCYM", a, b, c);
                CYMToRGB(u8(a), u8(b), u8(c), x[0], x[1], x[2]);
                oracle_100618f7e(nullptr, u8(a), u8(b), u8(c), &y[0], &y[1], &y[2]);
                if (std::memcmp(x, y, 3)) Fail("CYMToRGB", a, b, c);
                u8 kx, ky;
                RGBToCYMK(u8(a), u8(b), u8(c), x[0], x[1], x[2], kx);
                oracle_100618f98(nullptr, u8(a), u8(b), u8(c), &y[0], &y[1], &y[2], &ky);
                if (std::memcmp(x, y, 3) || kx != ky) Fail("RGBToCYMK", a, b, c);
            }
    for (unsigned a = 0; a < 256; a += 5)
        for (unsigned b = 0; b < 256; b += 5)
            for (unsigned c = 0; c < 256; c += 3)
                for (unsigned d = 0; d < 256; d += 51) {
                    u8 x[3], y[3];
                    CYMKToRGB(u8(a), u8(b), u8(c), u8(d), x[0], x[1], x[2]);
                    oracle_100618fd8(nullptr, u8(a), u8(b), u8(c), u8(d), &y[0], &y[1], &y[2]);
                    if (std::memcmp(x, y, 3)) Fail("CYMKToRGB", a, b, c);
                }
    srand(2024);
    for (int i = 0; i < 6000000; ++i) {
        const int mode = i % 4;
        float h, s, v;
        if (mode == 0) { h = RandFloat(0, 360); s = RandFloat(0, 1); v = RandFloat(0, 1); }
        else if (mode == 1) { h = RandFloat(-400, 800); s = RandFloat(-0.2f, 1.2f); v = RandFloat(-0.2f, 1.2f); }
        else if (mode == 2) { h = float(rand() % 361); s = float(rand() % 3) * 0.5f; v = float(rand() % 3) * 0.5f; }
        else { h = RandFloat(0, 360); s = RandFloat(0, 0.002f); v = RandFloat(0, 1); }
        u8 x[3], y[3];
        HSBToRGB(h, s, v, x[0], x[1], x[2]);
        oracle_10061faf0(nullptr, h, s, v, &y[0], &y[1], &y[2]);
        if (std::memcmp(x, y, 3)) Fail("HSBToRGB float", unsigned(h), unsigned(s * 100), unsigned(v * 100));
        if (mode != 1 || (h > -3000 && s == s)) {
            if (v >= 0 && v <= 1.1f) {
                HLSToRGB(h, v, s, x[0], x[1], x[2]);
                oracle_100618c6a(nullptr, h, v, s, &y[0], &y[1], &y[2]);
                if (std::memcmp(x, y, 3)) Fail("HLSToRGB float", unsigned(h), unsigned(v * 100), unsigned(s * 100));
            }
        }
    }
    if (bad) { std::printf("%ld mismatches\n", bad); return 1; }
    std::printf("color conversion oracle ok\n");
}
