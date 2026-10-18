// cRZRandom — the RZ framework's pseudo-random generator. Recovered from Sim City 4 Deluxe
// Editionsub 1.2.1 (0x10003d34a..0x10003d70a).
//
// Layout (0x18): +0 uint32 lcgState, +4 int32 mulState, +8 int32 hasSpareGaussian,
// +0x10 double spareGaussian. Two independent generators share one seed:
//   * LCG   x' = x*1103515245 + 12345 (ANSI C constants); integer API uses the bits 16..47 of
//           the 64-bit product (not masked to 32 bits first — kept exactly).
//   * MUL   s' = s*0x278dde6d, lazily seeded from clock() when zero; floating-point API.
#pragma once
#include <cstdint>

class cIGZIStream;
class cIGZOStream;

class cRZRandom {
public:
    explicit cRZRandom(std::uint32_t seed) { Seed(seed); }

    // 0xFFFFFFFF means "seed from the clock and wall time".
    void Seed(std::uint32_t seed);

    // Uniform in [0, range); 0 when range == 0.
    std::uint32_t RandomUint32Uniform(std::uint32_t range) noexcept;
    // Raw LCG output (bits 16..47 of the product, truncated to 32 bits).
    std::uint32_t RandomUint32Uniform() noexcept;
    // Uniform in [lo, hi); lo when hi == lo.
    std::int32_t RandomSint32RangeUniform(std::int32_t lo, std::int32_t hi) noexcept;
    // Sum of four LCG outputs -> bell-shaped value in [lo, hi).
    std::int32_t RandomSint32RangeGaussianFast(std::int32_t lo, std::int32_t hi) noexcept;

    double RandomDoubleUniform() noexcept;                              // [0, 1)
    double RandomDoubleRangeUniform(double lo, double hi) noexcept;
    double RandomDoubleGaussian() noexcept;                             // polar method, caches 2nd
    double RandomDoubleRangeGaussian(double lo, double hi) noexcept;    // clamped to +-3 sigma
    double RandomDoubleRangeGaussianFast(double lo, double hi) noexcept;  // mean of three uniforms

    // Saves / restores the generator (cRZRandomSubclass::Write @ 0x1000ce948, Read @ 0x1000ce8d2): both states, the spare
    // flag as a signed 32-bit value and the cached gaussian as a double.
    void Read(cIGZIStream& stream);
    void Write(cIGZOStream& stream) const;

private:
    float NextMulUnit() noexcept;  // next MUL value as a float in [-0.5, 0.5)

    std::uint32_t lcgState_ = 0;
    std::int32_t mulState_ = 0;
    std::int32_t hasSpareGaussian_ = 0;
    double spareGaussian_ = 0.0;
};
