// cSC4AutomataTuningParams — the default values of the automata tuning table (3 functions: constructor 0x1000fd1d8,
// destructors 0x1000fd18a / 0x10010ec78). The scripts overwrite individual fields later; the recovered code only sets the
// defaults, so the fields are named by their original offsets (f<offset>) until the readers are recovered. Layout (size 0x258):
//   +0x00..+0x28 ten 32-bit ints, +0x30/+0x58/+0x80/+0xa8/+0xd0/+0x100 response curves (each followed by one float, except
//   the last), +0x120.. floats and small byte groups, +0x158 a vector, +0x200 map<u32, cSC4ResponseCurve>, +0x230
//   map<eNetworkType, SpawnRecallParams>.
// All six curves start as the two points (0, 0.5), (0, 0.5) (the second one is dropped by SetResponseCurveData because the x
// step is zero).
#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "sc4/cSC4ResponseCurve.h"

namespace sc4 {

struct SpawnRecallParams {};   // value type of the +0x230 map (fields not recovered)

class cSC4AutomataTuningParams {
public:
    cSC4AutomataTuningParams();    // @ 0x1000fd1d8
    ~cSC4AutomataTuningParams();   // @ 0x10010ec78 (0x1000fd18a is the thin wrapper that calls it)

    std::int32_t f000 = 300, f004 = 100, f008 = 120, f00c = 10, f010 = 30, f014 = 300, f018 = 5, f01c = 10, f020 = 5, f024 = 10, f028 = 100;
    cSC4ResponseCurve curve030;  float f050 = 3.0f;
    cSC4ResponseCurve curve058;  float f078 = 0.5f;
    cSC4ResponseCurve curve080;  float f0a0 = 0.1f;
    cSC4ResponseCurve curve0a8;  float f0c8 = 0.1f;
    cSC4ResponseCurve curve0d0;  float f0f0 = 3.0f;
    cSC4ResponseCurve curve100;
    float f120 = 10.0f, f124 = 1.0f, f128 = 5.0f, f12c = 5.0f, f130 = 5.0f, f134 = 1.0f, f138 = 0.7f, f13c = 150.0f, f140 = 0.5f, f144 = 0.7f;
    std::uint8_t b148 = 2;
    std::int32_t f14c = -1, f150 = -1;
    std::uint8_t b154 = 2, b155 = 3;
    std::vector<std::uint32_t> v158;
    float f170 = 1.0f;
    std::uint8_t b174 = 3, b175 = 3;
    std::int32_t f178 = 150;
    std::uint8_t b17c = 4, b17d = 1;
    float f180 = 1.0f, f184 = 7.0f, f188 = 6.0f, f18c = 1.0f, f190 = 0.6f, f194 = 0.0f, f198 = 75.0f;
    float f1a4 = -0.5f, f1a8 = 0.5f, f1ac = 0.0f, f1b0 = 0.0f, f1b4 = 0.7853982f, f1b8 = 0.3f, f1bc = 0.01f;
    std::uint8_t b1c0 = 3, b1c1 = 10, b1c2 = 5, b1c3 = 8, b1c4 = 3, b1c5 = 8;
    std::int32_t f1c8 = 140, f1cc = 150, f1d0 = 40;
    float f1d4 = 0.25f, f1d8 = 1.0f, f1dc = 2.0f;
    std::uint8_t b1f0 = 5;
    float f1f4 = 0.1f;
    std::int32_t f1f8 = 100;
    std::unordered_map<std::uint32_t, cSC4ResponseCurve> curvesById;   // +0x200
    float f220 = 1.0f;
    std::uint8_t b228 = 1;
    std::int32_t f22c = -1;
    std::unordered_map<std::int32_t, SpawnRecallParams> spawnRecall;   // +0x230
    float f250 = 1.0f;
};

}  // namespace sc4
