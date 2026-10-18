#include "rz/cRZRandom.h"

#include <bit>
#include <cmath>
#include <ctime>

namespace {
constexpr std::uint64_t kLcgMul = 0x41c64e6d;
constexpr std::uint64_t kLcgInc = 0x3039;
constexpr std::int32_t kMulFactor = 0x278dde6d;
constexpr float kUnitScale = 2.3283064e-10f;  // 2^-32
// Largest double the Gaussian clamp can produce just below 6.0 (0x4017ffe5c91d14e4).
const double kGaussianClampTop = std::bit_cast<double>(0x4017ffe5c91d14e4ull);
}  // namespace

// cRZRandom::cRZRandom(unsigned int) @ 0x10003d34a, Seed @ 0x10003d396
void cRZRandom::Seed(std::uint32_t seed) {
    if (seed == 0xFFFFFFFFu) {
        const auto t = static_cast<std::uint32_t>(std::time(nullptr));
        const auto c = static_cast<std::uint32_t>(std::clock());
        seed = ((c << 16) | c) ^ t;
    }
    lcgState_ = seed;
    mulState_ = static_cast<std::int32_t>(seed);
    hasSpareGaussian_ = 0;
}

// cRZRandom::RandomUint32Uniform(unsigned int) @ 0x10003d366
std::uint32_t cRZRandom::RandomUint32Uniform(std::uint32_t range) noexcept {
    if (range == 0) return 0;
    const std::uint64_t t = static_cast<std::uint64_t>(lcgState_) * kLcgMul + kLcgInc;
    lcgState_ = static_cast<std::uint32_t>(t);
    const std::uint64_t r = (t >> 16) & 0xffffffffull;
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(range) * r) >> 32);
}

// cRZRandom::RandomUint32Uniform() @ 0x10003d3c8
std::uint32_t cRZRandom::RandomUint32Uniform() noexcept {
    const std::uint64_t t = static_cast<std::uint64_t>(lcgState_) * kLcgMul + kLcgInc;
    lcgState_ = static_cast<std::uint32_t>(t);
    return static_cast<std::uint32_t>(t >> 16);
}

// cRZRandom::RandomSint32RangeUniform(int, int) @ 0x10003d44c
std::int32_t cRZRandom::RandomSint32RangeUniform(std::int32_t lo, std::int32_t hi) noexcept {
    const auto span = static_cast<std::uint32_t>(hi - lo);
    if (span == 0) return lo;
    const std::uint64_t t = static_cast<std::uint64_t>(lcgState_) * kLcgMul + kLcgInc;
    lcgState_ = static_cast<std::uint32_t>(t);
    const std::uint64_t r = (t >> 16) & 0xffffffffull;
    return static_cast<std::int32_t>((static_cast<std::uint64_t>(span) * r) >> 32) + lo;
}

// cRZRandom::RandomSint32RangeGaussianFast(int, int) @ 0x10003d47e
std::int32_t cRZRandom::RandomSint32RangeGaussianFast(std::int32_t lo, std::int32_t hi) noexcept {
    std::uint64_t sum = 2;
    std::uint64_t state = lcgState_;
    for (int i = 0; i < 4; ++i) {
        state = (state & 0xffffffffull) * kLcgMul + kLcgInc;
        sum += (state >> 16) & 0xffffffffull;
    }
    lcgState_ = static_cast<std::uint32_t>(state);
    const std::uint64_t mean = (sum >> 2) & 0xffffffffull;
    return static_cast<std::int32_t>((mean * static_cast<std::uint32_t>(hi - lo)) >> 32) + lo;
}

// Shared by the floating-point API: advances MUL (seeding from clock() if zero) and maps the
// int to a float in [-0.5, 0.5).
float cRZRandom::NextMulUnit() noexcept {
    std::int32_t s = mulState_;
    if (s == 0) s = static_cast<std::int32_t>(std::clock());
    s = static_cast<std::int32_t>(static_cast<std::uint32_t>(s) * static_cast<std::uint32_t>(kMulFactor));
    mulState_ = s;
    return static_cast<float>(s) * kUnitScale;
}

// cRZRandom::RandomDoubleUniform() @ 0x10003d500
double cRZRandom::RandomDoubleUniform() noexcept {
    return static_cast<double>(NextMulUnit()) + 0.5;
}

// cRZRandom::RandomDoubleRangeUniform(double, double) @ 0x10003d3e4
double cRZRandom::RandomDoubleRangeUniform(double lo, double hi) noexcept {
    return (hi - lo) * (static_cast<double>(NextMulUnit()) + 0.5) + lo;
}

// cRZRandom::RandomDoubleGaussian() @ 0x10003d540 — Marsaglia polar method; the second value of
// each pair is returned on the next call.
double cRZRandom::RandomDoubleGaussian() noexcept {
    if (hasSpareGaussian_ != 0) {
        hasSpareGaussian_ = 0;
        return spareGaussian_;
    }
    double u, v, s;
    do {
        u = static_cast<double>(NextMulUnit()) + 0.5;
        u = u + u - 1.0;
        v = static_cast<double>(NextMulUnit()) + 0.5;
        v = v + v - 1.0;
        s = v * v + u * u;
    } while (s >= 1.0 || s == 0.0);
    const double scale = std::sqrt((std::log(s) * -2.0) / s);
    spareGaussian_ = u * scale;
    hasSpareGaussian_ = 1;
    return v * scale;
}

// cRZRandom::RandomDoubleRangeGaussian(double, double) @ 0x10003d69e
double cRZRandom::RandomDoubleRangeGaussian(double lo, double hi) noexcept {
    double t = RandomDoubleGaussian() + 3.0;
    if (!(t < 6.0)) t = kGaussianClampTop;
    if (t < 0.0) t = 0.0;
    return lo + t * (1.0 / 6.0) * (hi - lo);
}

// cRZRandom::RandomDoubleRangeGaussianFast(double, double) @ 0x10003d70a
double cRZRandom::RandomDoubleRangeGaussianFast(double lo, double hi) noexcept {
    const double a = static_cast<double>(NextMulUnit()) + 0.5;
    const double b = static_cast<double>(NextMulUnit()) + 0.5;
    const double c = static_cast<double>(NextMulUnit()) + 0.5;
    return (a + b + c) * (hi - lo) * (1.0 / 3.0) + lo;
}

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"

void cRZRandom::Read(cIGZIStream& stream) {
    stream.GetUint32(lcgState_);
    std::uint32_t mul = 0;
    stream.GetUint32(mul);
    mulState_ = static_cast<std::int32_t>(mul);
    std::int32_t spare = 0;
    stream.GetSint32(spare);
    hasSpareGaussian_ = spare;
    stream.GetFloat64(spareGaussian_);
}

void cRZRandom::Write(cIGZOStream& stream) const {
    stream.SetUint32(lcgState_);
    stream.SetUint32(static_cast<std::uint32_t>(mulState_));
    stream.SetSint32(hasSpareGaussian_);
    stream.SetFloat64(spareGaussian_);
}
