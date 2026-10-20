// SimdKernels — float-array kernels for the simulation grids (pollution/land value/crime decay, scaling,
// accumulation). NEON on arm64, SSE2 on x86_64, scalar fallback everywhere; all paths use only exact IEEE
// add/mul/min/max so results are bit-identical to the scalar reference (no FMA contraction, no reassociation).
#pragma once
#include <cstddef>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#define SC4_SIMD_NEON 1
#elif defined(__SSE2__)
#include <emmintrin.h>
#define SC4_SIMD_SSE2 1
#endif

namespace ecs::simd {

// dst[i] = dst[i] * scale + add, clamped to [lo, hi].
inline void ScaleAddClampScalar(float* dst, std::size_t n, float scale, float add, float lo, float hi) {
    for (std::size_t i = 0; i < n; ++i) {
        float p = dst[i] * scale;       // separate mul then add: matches the SIMD paths exactly
        float v = p + add;
        v = v < lo ? lo : v;
        dst[i] = v > hi ? hi : v;
    }
}

inline void ScaleAddClamp(float* dst, std::size_t n, float scale, float add, float lo, float hi) {
    std::size_t i = 0;
#if defined(SC4_SIMD_NEON)
    float32x4_t s = vdupq_n_f32(scale), a = vdupq_n_f32(add), l = vdupq_n_f32(lo), h = vdupq_n_f32(hi);
    for (; i + 4 <= n; i += 4) {
        float32x4_t v = vaddq_f32(vmulq_f32(vld1q_f32(dst + i), s), a);
        vst1q_f32(dst + i, vminq_f32(vmaxq_f32(v, l), h));
    }
#elif defined(SC4_SIMD_SSE2)
    __m128 s = _mm_set1_ps(scale), a = _mm_set1_ps(add), l = _mm_set1_ps(lo), h = _mm_set1_ps(hi);
    for (; i + 4 <= n; i += 4) {
        __m128 v = _mm_add_ps(_mm_mul_ps(_mm_loadu_ps(dst + i), s), a);
        _mm_storeu_ps(dst + i, _mm_min_ps(_mm_max_ps(v, l), h));
    }
#endif
    ScaleAddClampScalar(dst + i, n - i, scale, add, lo, hi);
}

// dst[i] += src[i]
inline void Accumulate(float* dst, const float* src, std::size_t n) {
    std::size_t i = 0;
#if defined(SC4_SIMD_NEON)
    for (; i + 4 <= n; i += 4) vst1q_f32(dst + i, vaddq_f32(vld1q_f32(dst + i), vld1q_f32(src + i)));
#elif defined(SC4_SIMD_SSE2)
    for (; i + 4 <= n; i += 4) _mm_storeu_ps(dst + i, _mm_add_ps(_mm_loadu_ps(dst + i), _mm_loadu_ps(src + i)));
#endif
    for (; i < n; ++i) dst[i] += src[i];
}

}  // namespace ecs::simd
