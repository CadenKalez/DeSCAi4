// tAutomataDriveParams (0xa0 bytes) and tAutomataPlayerDriveParams (0x94 bytes) — the physics tuning of a flying / floating / player-driven
// automaton, filled from the script tables by cSC4AutomataScriptSystem (ImportAirplaneParams / ImportHelicopterParams / ImportWatercraftParams /
// ImportPlayerDriveParams). The three vehicle kinds share one block of floats (an automaton group owns exactly one kind), so the block is a
// plain float array indexed by byte offset / 4; the importers document which script key lands on which offset.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace sc4 {

struct tAutomataDriveParams {
    std::array<float, 0xa0 / 4> v{};                          // zero-filled like the freshly allocated block
    float& At(unsigned byteOffset) { return v[byteOffset / 4]; }
    float At(unsigned byteOffset) const { return v[byteOffset / 4]; }
};

struct tAutomataPlayerDriveParams {
    std::array<std::uint32_t, 0x94 / 4> w{};
    float F(unsigned byteOffset) const { float f; std::memcpy(&f, &w[byteOffset / 4], 4); return f; }
    void SetF(unsigned byteOffset, float f) { std::memcpy(&w[byteOffset / 4], &f, 4); }
    std::int32_t I(unsigned byteOffset) const { return static_cast<std::int32_t>(w[byteOffset / 4]); }
    void SetI(unsigned byteOffset, std::int32_t i) { w[byteOffset / 4] = static_cast<std::uint32_t>(i); }
};

}  // namespace sc4
