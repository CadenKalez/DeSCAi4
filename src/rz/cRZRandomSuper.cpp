#include "rz/cRZRandomSuper.h"

#include <ctime>

namespace {
constexpr std::uint32_t kMatrix = 0x9908b0df;
constexpr int kWords = 624, kShift = 397;
constexpr double kToUnit = 2.3283064365386963e-10;   // 2^-32

// twist(m, u, v): m ^ (mix(u, v) >> 1) ^ (low bit of v ? matrix : 0)
inline std::uint32_t Twist(std::uint32_t m, std::uint32_t u, std::uint32_t v) {
    return ((0u - (v & 1u)) & kMatrix) ^ (((v & 0x7ffffffeu) | (u & 0x80000000u)) >> 1) ^ m;
}
std::int32_t g_hashIncrementor = 0;
}  // namespace

cRZRandomSuper::cRZRandomSuper(std::uint32_t seed) {
    next_ = nullptr;
    left_ = 0x270;
    Seed(seed);
}

void cRZRandomSuper::Seed(std::uint32_t seed) {
    if (seed == 0xffffffffu)
        seed = Hash(static_cast<std::int32_t>(std::time(nullptr)), static_cast<std::int32_t>(std::clock()));
    std::uint32_t x = seed | 1u;
    for (int i = 0; i < kWords; ++i) {
        state_[i] = ((x * 0x10dcdu) >> 16) | (x & 0xffff0000u);
        x = x * 0x1c587629u + 0x10dceu;
    }
    Reload();
}

std::uint32_t cRZRandomSuper::Hash(std::int32_t a, std::int32_t b) {
    auto mix = [](std::int32_t value) {
        const std::uint32_t bits = static_cast<std::uint32_t>(value);
        std::uint32_t h = bits & 0xff;
        for (int byte = 1; byte < 4; ++byte) h = ((bits >> (8 * byte)) & 0xff) + h * 0x101u;
        return h;
    };
    const std::int32_t counter = g_hashIncrementor++;
    return (static_cast<std::uint32_t>(counter) + mix(a)) ^ mix(b);
}

void cRZRandomSuper::Reload() {
    auto& s = state_;
    for (int i = 0; i < kWords - kShift; ++i) s[i] = Twist(s[i + kShift], s[i], s[i + 1]);
    for (int i = kWords - kShift; i < kWords - 1; ++i) s[i] = Twist(s[i + kShift - kWords], s[i], s[i + 1]);
    s[kWords - 1] = Twist(s[kShift - 1], s[kWords - 1], s[0]);
    left_ = 0x270;
    next_ = s.data();
}

std::uint32_t cRZRandomSuper::RandomUint32Uniform() {
    const std::int32_t before = left_--;
    if (before < 1) {
        Reload();
        --left_;
    }
    std::uint32_t y = *next_++;
    y ^= y >> 11;
    y ^= (y & 0x13a58adu) << 7;
    y ^= (y & 0x1df8cu) << 15;
    return y ^ (y >> 18);
}

std::uint32_t cRZRandomSuper::RandomUint32Uniform(std::uint32_t range) {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(range) * RandomUint32Uniform()) >> 32);
}

std::uint32_t cRZRandomSuper::RandomUint32WeightedChoice(std::uint32_t count, const double* weights) {
    if (count <= 1) return 0;
    double total = 0.0;
    for (std::uint32_t i = 0; i < count; ++i)
        if (0.0 < weights[i]) total += weights[i];
    const std::uint32_t r = RandomUint32Uniform();
    if (total == 0.0) return static_cast<std::uint32_t>((static_cast<std::uint64_t>(count) * r) >> 32);
    double target = total * (static_cast<double>(static_cast<std::int32_t>(r)) * kToUnit + 0.5) + 0.0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const double w = weights[i];
        if (0.0 < w) {
            if (target < w) return i;
            target -= w;
        }
    }
    return count - 1;
}

double cRZRandomSuper::RandomDoubleRangeUniform(double low, double high) {
    const std::int32_t r = static_cast<std::int32_t>(RandomUint32Uniform());
    return low + (static_cast<double>(r) * kToUnit + 0.5) * (high - low);
}

std::int32_t cRZRandomSuper::RandomSint32RangeUniform(std::int32_t low, std::int32_t high) {
    const std::uint32_t r = RandomUint32Uniform();
    return static_cast<std::int32_t>((static_cast<std::uint64_t>(r) * static_cast<std::uint32_t>(high - low)) >> 32) + low;
}

double cRZRandomSuper::RandomDoubleUniform() {
    return static_cast<double>(static_cast<std::int32_t>(RandomUint32Uniform())) * kToUnit + 0.5;
}
