// cRZRandomSuper — the framework's high-quality generator, a Mersenne-Twister variant (10 functions,
// 0x10003d7fe..0x10003db84; used by the game as the global "sRandom"). Layout: +0 the 624-word state, +0x9c0 the
// next word to hand out, +0x9c8 words left. Differences from stock MT19937 kept exactly: the seeding recurrence
// (x*0x10dcd >> 16 | x & 0xffff0000 with x' = x*0x1c587629 + 0x10dce, starting from seed | 1), the tempering masks
// (0x13a58ad, 0x1df8c) and the final shift of 18. The twist itself is the textbook one with m = 397.
// Seed 0xffffffff means "from the clock": Hash(time, clock) mixes the four bytes of each argument and a process
// wide counter.
#pragma once
#include <array>
#include <cstdint>

class cRZRandomSuper {
public:
    explicit cRZRandomSuper(std::uint32_t seed);

    void Seed(std::uint32_t seed);
    static std::uint32_t Hash(std::int32_t a, std::int32_t b);
    void Reload();
    std::uint32_t RandomUint32Uniform();
    std::uint32_t RandomUint32Uniform(std::uint32_t range);               // [0, range)
    // index chosen with probability proportional to the positive weights (uniform when none is positive)
    std::uint32_t RandomUint32WeightedChoice(std::uint32_t count, const double* weights);
    double RandomDoubleRangeUniform(double low, double high);
    std::int32_t RandomSint32RangeUniform(std::int32_t low, std::int32_t high);
    double RandomDoubleUniform();                                         // [0, 1)

    const std::array<std::uint32_t, 624>& State() const { return state_; }

private:
    std::array<std::uint32_t, 624> state_{};
    std::uint32_t* next_ = nullptr;      // +0x9c0
    std::int32_t left_ = 0x270;          // +0x9c8
};
