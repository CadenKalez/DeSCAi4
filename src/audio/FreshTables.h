// Constant tables of the Freshness engine, read from the binary.
#pragma once
#include <array>
#include <cstdint>

namespace Fresh {

// _afFreqMult @ 0x100b0c120: 2^(n/12), n = 0..12 (semitone ratios); entry 13 is 0.0 in the binary
// but is never indexed (the pitch-shift range is at most one octave).
inline constexpr std::array<double, 13> kFrequencyMultipliers = {
    1.0, 1.059463094359295, 1.122462048309373, 1.189207115002721, 1.259921049894873,
    1.334839854170034, 1.414213562373095, 1.498307076876682, 1.587401051968199,
    1.681792830507429, 1.781797436280679, 1.887748625363387, 2.0};

// alCellProbMultiplier @ 0x100a227d0 (21 entries, indexed by the cell's probability 0..20).
inline constexpr std::array<std::int32_t, 21> kCellProbabilityMultiplier = {
    0, 600, 1500, 3000, 7500, 12000, 15000, 18000, 21000, 25500, 32767,
    36000, 45000, 58000, 67500, 90000, 120000, 210000, 300000, 40000000, 40000000};

// Quadrant offsets used by AutoPan when both pan axes are centred and the cell pans randomly
// (DAT_100a22770 / DAT_100a22790): index 0..3 -> (x, z) in units of 100.
inline constexpr std::array<double, 4> kAutoPanQuadrantX = {1.0, -1.0, 0.0, 0.0};
inline constexpr std::array<double, 4> kAutoPanQuadrantZ = {0.0, 0.0, -1.0, 1.0};

}  // namespace Fresh
