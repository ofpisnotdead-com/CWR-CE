#pragma once

#ifdef __aarch64__
#include <sse2neon/sse2neon.h>
#else
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <xmmintrin.h>
#endif
#endif

namespace Poseidon::Foundation
{
typedef __m128 Vec4M128[4];

struct Quatrix4
{
    Vec4M128 qa[3];

    operator Vec4M128*() { return qa; }
    operator const Vec4M128*() const { return qa; }
};

typedef __m128 Vec3M128[3];

struct Quatrix3
{
    Vec3M128 qa[3];

    operator Vec3M128*() { return qa; }
    operator const Vec3M128*() const { return qa; }
};

} // namespace Poseidon::Foundation

using ::Poseidon::Foundation::Quatrix3;
using ::Poseidon::Foundation::Quatrix4;
