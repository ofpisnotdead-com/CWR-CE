#pragma once

// Single include point for the legacy x86 SIMD intrinsics (MMX/SSE/SSE2) used
// directly by the engine's older vector math and shape code (V3QuadsP3.cpp,
// Math3DK.hpp, Occlusion.cpp, Shape.cpp, etc.). On Apple Silicon there is no
// x86 intrinsic API at all, so this redirects to sse2neon (vendored in
// thirdparty/sse2neon/), which implements the x86 SSE intrinsic surface on
// top of ARM NEON.
//
// sse2neon covers SSE/SSE2/SSE3/SSE4.1 but not every legacy 64-bit MMX (__m64)
// intrinsic; the handful this codebase still calls (_mm_set1_pi8,
// _mm_cmpgt_pi8, _mm_and_si64/_mm_or_si64/_mm_andnot_si64, _mm_packs_pi32,
// _mm_packs_pu16) are implemented below with NEON, matching Intel's
// documented semantics exactly.

#if defined(__aarch64__) || defined(_M_ARM64)

#include <sse2neon/sse2neon.h>

// sse2neon's own FORCE_INLINE macro is push_macro/pop_macro-scoped to inside
// sse2neon.h (see its pop_macro("FORCE_INLINE") near EOF), so it's already
// gone again by here -- define our own with the same meaning.
#define POSEIDON_MMX_INLINE static inline __attribute__((always_inline))

POSEIDON_MMX_INLINE __m64 _mm_set1_pi8(char b)
{
    return vreinterpret_s64_s8(vdup_n_s8(b));
}

// Per-byte signed greater-than: 0xFF where a > b, else 0x00.
POSEIDON_MMX_INLINE __m64 _mm_cmpgt_pi8(__m64 a, __m64 b)
{
    int8x8_t va = vreinterpret_s8_s64(a);
    int8x8_t vb = vreinterpret_s8_s64(b);
    return vreinterpret_s64_u8(vcgt_s8(va, vb));
}

POSEIDON_MMX_INLINE __m64 _mm_and_si64(__m64 a, __m64 b)
{
    return vdup_n_s64(vget_lane_s64(a, 0) & vget_lane_s64(b, 0));
}

POSEIDON_MMX_INLINE __m64 _mm_or_si64(__m64 a, __m64 b)
{
    return vdup_n_s64(vget_lane_s64(a, 0) | vget_lane_s64(b, 0));
}

// Intel semantics: ~a & b (NOT the more intuitive "a andnot b").
POSEIDON_MMX_INLINE __m64 _mm_andnot_si64(__m64 a, __m64 b)
{
    return vdup_n_s64((~vget_lane_s64(a, 0)) & vget_lane_s64(b, 0));
}

// Packs two __m64 of 2x32-bit signed ints each into one __m64 of 4x16-bit
// signed ints, saturating. Result lane order is [a0, a1, b0, b1].
POSEIDON_MMX_INLINE __m64 _mm_packs_pi32(__m64 a, __m64 b)
{
    int32x4_t combined = vcombine_s32(vreinterpret_s32_s64(a), vreinterpret_s32_s64(b));
    return vreinterpret_s64_s16(vqmovn_s32(combined));
}

// Packs two __m64 of 4x16-bit signed ints each into one __m64 of 8x8-bit
// unsigned ints, saturating (negative values clamp to 0).
POSEIDON_MMX_INLINE __m64 _mm_packs_pu16(__m64 a, __m64 b)
{
    int16x8_t combined = vcombine_s16(vreinterpret_s16_s64(a), vreinterpret_s16_s64(b));
    return vreinterpret_s64_u8(vqmovun_s16(combined));
}

#elif defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
