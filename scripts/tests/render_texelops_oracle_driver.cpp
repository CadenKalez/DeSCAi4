// Compares nsTexelOp against the Ghidra-derived oracle (work/texel/oracle_raw.c), bit for bit.
#include "render/TexelOps.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" {
void oracle_Convert_ARGB8_to_ARGB4(void*, void*, int, int, int, int);
void oracle_Convert_ARGB8_to_RGB565(void*, void*, int, int, int, int);
void oracle_Convert_ARGB8_to_A1RGB5(void*, void*, int, int, int, int);
void oracle_Convert_ARGB8_to_RGB8(void*, void*, int, int, int, int);
void oracle_Convert_ARGB8_to_ARGB8(void*, void*, int, int, int, int);
void oracle_Convert_ABGR8_to_ARGB4(void*, void*, int, int, int, int);
void oracle_Convert_ABGR8_to_RGB565(void*, void*, int, int, int, int);
void oracle_Convert_ABGR8_to_A1RGB5(void*, void*, int, int, int, int);
void oracle_Convert_ABGR8_to_RGB8(void*, void*, int, int, int, int);
void oracle_Convert_ABGR8_to_ARGB8(void*, void*, int, int, int, int);
void oracle_Convert_RGB8_to_ARGB4(void*, void*, int, int, int, int);
void oracle_Convert_RGB8_to_RGB565(void*, void*, int, int, int, int);
void oracle_Convert_RGB8_to_A1RGB5(void*, void*, int, int, int, int);
void oracle_Convert_RGB8_to_RGB8(void*, void*, int, int, int, int);
void oracle_Convert_RGB8_to_ARGB8(void*, void*, int, int, int, int);
void oracle_Convert_BGR8_to_ARGB4(void*, void*, int, int, int, int);
void oracle_Convert_BGR8_to_RGB565(void*, void*, int, int, int, int);
void oracle_Convert_BGR8_to_A1RGB5(void*, void*, int, int, int, int);
void oracle_Convert_BGR8_to_RGB8(void*, void*, int, int, int, int);
void oracle_Convert_BGR8_to_ARGB8(void*, void*, int, int, int, int);
void oracle_Convert_ARGB4_to_ARGB4(void*, void*, int, int, int, int);
void oracle_Convert_ARGB4_to_RGB565(void*, void*, int, int, int, int);
void oracle_Convert_ARGB4_to_A1RGB5(void*, void*, int, int, int, int);
void oracle_Convert_ARGB4_to_RGB8(void*, void*, int, int, int, int);
void oracle_Convert_ARGB4_to_ARGB8(void*, void*, int, int, int, int);
void oracle_Convert_ABGR4_to_ARGB4(void*, void*, int, int, int, int);
void oracle_Convert_ABGR4_to_RGB565(void*, void*, int, int, int, int);
void oracle_Convert_ABGR4_to_A1RGB5(void*, void*, int, int, int, int);
void oracle_Convert_ABGR4_to_RGB8(void*, void*, int, int, int, int);
void oracle_Convert_ABGR4_to_ARGB8(void*, void*, int, int, int, int);
void oracle_Convert_A1RGB5_to_ARGB4(void*, void*, int, int, int, int);
void oracle_Convert_A1RGB5_to_RGB565(void*, void*, int, int, int, int);
void oracle_Convert_A1RGB5_to_A1RGB5(void*, void*, int, int, int, int);
void oracle_Convert_A1RGB5_to_RGB8(void*, void*, int, int, int, int);
void oracle_Convert_A1RGB5_to_ARGB8(void*, void*, int, int, int, int);
void oracle_Convert_A1BGR5_to_ARGB4(void*, void*, int, int, int, int);
void oracle_Convert_A1BGR5_to_RGB565(void*, void*, int, int, int, int);
void oracle_Convert_A1BGR5_to_A1RGB5(void*, void*, int, int, int, int);
void oracle_Convert_A1BGR5_to_RGB8(void*, void*, int, int, int, int);
void oracle_Convert_A1BGR5_to_ARGB8(void*, void*, int, int, int, int);
void oracle_Convert_RGB565_to_ARGB4(void*, void*, int, int, int, int);
void oracle_Convert_RGB565_to_RGB565(void*, void*, int, int, int, int);
void oracle_Convert_RGB565_to_A1RGB5(void*, void*, int, int, int, int);
void oracle_Convert_RGB565_to_RGB8(void*, void*, int, int, int, int);
void oracle_Convert_RGB565_to_ARGB8(void*, void*, int, int, int, int);
void oracle_Convert_BGR565_to_ARGB4(void*, void*, int, int, int, int);
void oracle_Convert_BGR565_to_RGB565(void*, void*, int, int, int, int);
void oracle_Convert_BGR565_to_A1RGB5(void*, void*, int, int, int, int);
void oracle_Convert_BGR565_to_RGB8(void*, void*, int, int, int, int);
void oracle_Convert_BGR565_to_ARGB8(void*, void*, int, int, int, int);
}
struct Case { const char* name; int srcBytes, dstBytes; nsTexelOp::ConvertFn mine; void (*ref)(void*, void*, int, int, int, int); };
static const Case kCases[] = {
    {"ARGB8_to_ARGB4", 4, 2, nsTexelOp::ConvertARGB8ToARGB4, oracle_Convert_ARGB8_to_ARGB4},
    {"ARGB8_to_RGB565", 4, 2, nsTexelOp::ConvertARGB8ToRGB565, oracle_Convert_ARGB8_to_RGB565},
    {"ARGB8_to_A1RGB5", 4, 2, nsTexelOp::ConvertARGB8ToA1RGB5, oracle_Convert_ARGB8_to_A1RGB5},
    {"ARGB8_to_RGB8", 4, 4, nsTexelOp::ConvertARGB8ToRGB8, oracle_Convert_ARGB8_to_RGB8},
    {"ARGB8_to_ARGB8", 4, 4, nsTexelOp::ConvertARGB8ToARGB8, oracle_Convert_ARGB8_to_ARGB8},
    {"ABGR8_to_ARGB4", 4, 2, nsTexelOp::ConvertABGR8ToARGB4, oracle_Convert_ABGR8_to_ARGB4},
    {"ABGR8_to_RGB565", 4, 2, nsTexelOp::ConvertABGR8ToRGB565, oracle_Convert_ABGR8_to_RGB565},
    {"ABGR8_to_A1RGB5", 4, 2, nsTexelOp::ConvertABGR8ToA1RGB5, oracle_Convert_ABGR8_to_A1RGB5},
    {"ABGR8_to_RGB8", 4, 3, nsTexelOp::ConvertABGR8ToRGB8, oracle_Convert_ABGR8_to_RGB8},
    {"ABGR8_to_ARGB8", 4, 4, nsTexelOp::ConvertABGR8ToARGB8, oracle_Convert_ABGR8_to_ARGB8},
    {"RGB8_to_ARGB4", 3, 2, nsTexelOp::ConvertRGB8ToARGB4, oracle_Convert_RGB8_to_ARGB4},
    {"RGB8_to_RGB565", 3, 2, nsTexelOp::ConvertRGB8ToRGB565, oracle_Convert_RGB8_to_RGB565},
    {"RGB8_to_A1RGB5", 3, 2, nsTexelOp::ConvertRGB8ToA1RGB5, oracle_Convert_RGB8_to_A1RGB5},
    {"RGB8_to_RGB8", 3, 3, nsTexelOp::ConvertRGB8ToRGB8, oracle_Convert_RGB8_to_RGB8},
    {"RGB8_to_ARGB8", 3, 4, nsTexelOp::ConvertRGB8ToARGB8, oracle_Convert_RGB8_to_ARGB8},
    {"BGR8_to_ARGB4", 3, 2, nsTexelOp::ConvertBGR8ToARGB4, oracle_Convert_BGR8_to_ARGB4},
    {"BGR8_to_RGB565", 3, 2, nsTexelOp::ConvertBGR8ToRGB565, oracle_Convert_BGR8_to_RGB565},
    {"BGR8_to_A1RGB5", 3, 2, nsTexelOp::ConvertBGR8ToA1RGB5, oracle_Convert_BGR8_to_A1RGB5},
    {"BGR8_to_RGB8", 3, 3, nsTexelOp::ConvertBGR8ToRGB8, oracle_Convert_BGR8_to_RGB8},
    {"BGR8_to_ARGB8", 3, 4, nsTexelOp::ConvertBGR8ToARGB8, oracle_Convert_BGR8_to_ARGB8},
    {"ARGB4_to_ARGB4", 2, 2, nsTexelOp::ConvertARGB4ToARGB4, oracle_Convert_ARGB4_to_ARGB4},
    {"ARGB4_to_RGB565", 2, 2, nsTexelOp::ConvertARGB4ToRGB565, oracle_Convert_ARGB4_to_RGB565},
    {"ARGB4_to_A1RGB5", 2, 2, nsTexelOp::ConvertARGB4ToA1RGB5, oracle_Convert_ARGB4_to_A1RGB5},
    {"ARGB4_to_RGB8", 2, 3, nsTexelOp::ConvertARGB4ToRGB8, oracle_Convert_ARGB4_to_RGB8},
    {"ARGB4_to_ARGB8", 2, 4, nsTexelOp::ConvertARGB4ToARGB8, oracle_Convert_ARGB4_to_ARGB8},
    {"ABGR4_to_ARGB4", 2, 2, nsTexelOp::ConvertABGR4ToARGB4, oracle_Convert_ABGR4_to_ARGB4},
    {"ABGR4_to_RGB565", 2, 2, nsTexelOp::ConvertABGR4ToRGB565, oracle_Convert_ABGR4_to_RGB565},
    {"ABGR4_to_A1RGB5", 2, 2, nsTexelOp::ConvertABGR4ToA1RGB5, oracle_Convert_ABGR4_to_A1RGB5},
    {"ABGR4_to_RGB8", 2, 3, nsTexelOp::ConvertABGR4ToRGB8, oracle_Convert_ABGR4_to_RGB8},
    {"ABGR4_to_ARGB8", 2, 4, nsTexelOp::ConvertABGR4ToARGB8, oracle_Convert_ABGR4_to_ARGB8},
    {"A1RGB5_to_ARGB4", 2, 2, nsTexelOp::ConvertA1RGB5ToARGB4, oracle_Convert_A1RGB5_to_ARGB4},
    {"A1RGB5_to_RGB565", 2, 2, nsTexelOp::ConvertA1RGB5ToRGB565, oracle_Convert_A1RGB5_to_RGB565},
    {"A1RGB5_to_A1RGB5", 2, 2, nsTexelOp::ConvertA1RGB5ToA1RGB5, oracle_Convert_A1RGB5_to_A1RGB5},
    {"A1RGB5_to_RGB8", 2, 3, nsTexelOp::ConvertA1RGB5ToRGB8, oracle_Convert_A1RGB5_to_RGB8},
    {"A1RGB5_to_ARGB8", 2, 4, nsTexelOp::ConvertA1RGB5ToARGB8, oracle_Convert_A1RGB5_to_ARGB8},
    {"A1BGR5_to_ARGB4", 2, 2, nsTexelOp::ConvertA1BGR5ToARGB4, oracle_Convert_A1BGR5_to_ARGB4},
    {"A1BGR5_to_RGB565", 2, 2, nsTexelOp::ConvertA1BGR5ToRGB565, oracle_Convert_A1BGR5_to_RGB565},
    {"A1BGR5_to_A1RGB5", 2, 2, nsTexelOp::ConvertA1BGR5ToA1RGB5, oracle_Convert_A1BGR5_to_A1RGB5},
    {"A1BGR5_to_RGB8", 2, 3, nsTexelOp::ConvertA1BGR5ToRGB8, oracle_Convert_A1BGR5_to_RGB8},
    {"A1BGR5_to_ARGB8", 2, 4, nsTexelOp::ConvertA1BGR5ToARGB8, oracle_Convert_A1BGR5_to_ARGB8},
    {"RGB565_to_ARGB4", 2, 2, nsTexelOp::ConvertRGB565ToARGB4, oracle_Convert_RGB565_to_ARGB4},
    {"RGB565_to_RGB565", 2, 2, nsTexelOp::ConvertRGB565ToRGB565, oracle_Convert_RGB565_to_RGB565},
    {"RGB565_to_A1RGB5", 2, 2, nsTexelOp::ConvertRGB565ToA1RGB5, oracle_Convert_RGB565_to_A1RGB5},
    {"RGB565_to_RGB8", 2, 3, nsTexelOp::ConvertRGB565ToRGB8, oracle_Convert_RGB565_to_RGB8},
    {"RGB565_to_ARGB8", 2, 4, nsTexelOp::ConvertRGB565ToARGB8, oracle_Convert_RGB565_to_ARGB8},
    {"BGR565_to_ARGB4", 2, 2, nsTexelOp::ConvertBGR565ToARGB4, oracle_Convert_BGR565_to_ARGB4},
    {"BGR565_to_RGB565", 2, 2, nsTexelOp::ConvertBGR565ToRGB565, oracle_Convert_BGR565_to_RGB565},
    {"BGR565_to_A1RGB5", 2, 2, nsTexelOp::ConvertBGR565ToA1RGB5, oracle_Convert_BGR565_to_A1RGB5},
    {"BGR565_to_RGB8", 2, 3, nsTexelOp::ConvertBGR565ToRGB8, oracle_Convert_BGR565_to_RGB8},
    {"BGR565_to_ARGB8", 2, 4, nsTexelOp::ConvertBGR565ToARGB8, oracle_Convert_BGR565_to_ARGB8},
};
int main() {
    int bad = 0;
    srand(1234);
    for (const Case& c : kCases) {
        // 2 rows; source gets random bytes (all 16-bit values appear via the exhaustive sweep for 2-byte sources).
        const int w = 257, h = 3;
        std::vector<uint8_t> src(size_t(w) * h * c.srcBytes + 4);
        std::vector<uint8_t> a(size_t(w) * h * c.dstBytes + 4), b(a.size());
        long mismatches = 0;
        for (int iter = 0; iter < 400; ++iter) {
            for (auto& v : src) v = uint8_t(rand());
            if (c.srcBytes == 2 && iter == 0) {
                // exhaustive: every 16-bit value
                for (int base = 0; base < 65536; base += w * h) {
                    for (int i = 0; i < w * h; ++i) { uint16_t v = uint16_t(base + i); std::memcpy(&src[size_t(i) * 2], &v, 2); }
                    std::fill(a.begin(), a.end(), 0xAA); std::fill(b.begin(), b.end(), 0xAA);
                    c.mine(a.data(), src.data(), w * c.dstBytes, w * c.srcBytes, w, h);
                    c.ref(b.data(), src.data(), w * c.dstBytes, w * c.srcBytes, w, h);
                    if (a != b) ++mismatches;
                }
                continue;
            }
            std::fill(a.begin(), a.end(), 0xAA); std::fill(b.begin(), b.end(), 0xAA);
            c.mine(a.data(), src.data(), w * c.dstBytes, w * c.srcBytes, w, h);
            c.ref(b.data(), src.data(), w * c.dstBytes, w * c.srcBytes, w, h);
            if (a != b) ++mismatches;
        }
        if (mismatches) { std::printf("MISMATCH %s (%ld)\n", c.name, mismatches); ++bad; }
    }
    // Copies
    {
        std::vector<uint8_t> src(4096), a(4096, 1), b(4096, 1);
        for (auto& v : src) v = uint8_t(rand());
        for (int w : {1, 7, 8, 31, 32, 40}) {
            std::fill(a.begin(), a.end(), 1); std::fill(b.begin(), b.end(), 1);
            nsTexelOp::Copy1UB(a.data(), src.data(), 100, 120, w, 5);
            std::vector<uint8_t> e(4096, 1);
            for (int y = 0; y < 5; ++y) std::memcpy(&e[size_t(y) * 100], &src[size_t(y) * 120], size_t(w));
            if (a != e) { std::printf("MISMATCH Copy1UB w=%d\n", w); ++bad; }
            std::fill(a.begin(), a.end(), 1);
            nsTexelOp::Copy4UB(a.data(), src.data(), 200, 220, w, 4);
            std::fill(e.begin(), e.end(), 1);
            for (int y = 0; y < 4; ++y) std::memcpy(&e[size_t(y) * 200], &src[size_t(y) * 220], size_t(w) * 4);
            if (a != e) { std::printf("MISMATCH Copy4UB w=%d\n", w); ++bad; }
        }
    }
    if (bad) return 1;
    std::printf("texelops oracle ok\n");
    return 0;
}
