#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <float.h>
#include <cmath>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>

#pragma optimize("t", on)

#ifndef _KNI

// Fast inverse square root (Graphics Gems V).
// note: LOOKUP_BITS 12 seems to be a little bit faster (1-2% overall)

#define LOOKUP_BITS 12 // Number of mantissa bits for lookup
#define SEED_BITS 16   // Number of mantissa bits for lookup
#define SEED_TYPE unsigned short

#define EXP_POS 23   // Position of the exponent
#define EXP_BIAS 127 // Bias of exponent
#define EXP_BITS 8   // Bits of exponent
// The mantissa is assumed to be just down from the exponent

// Derived parameters
#define SEED_MASK ((1 << SEED_BITS) - 1)
#define EXP_MASK ((1 << EXP_BITS) - 1)
#define LOOKUP_POS (EXP_POS - LOOKUP_BITS)                 // Position of mantissa lookup
#define SEED_POS (EXP_POS - SEED_BITS)                     // Position of mantissa seed
#define TABLE_SIZE (2 << LOOKUP_BITS)                      // Number of entries in table
#define LOOKUP_MASK (TABLE_SIZE - 1)                       // Mask for table input
#define GET_EXP(a) (((a) >> EXP_POS) & EXP_MASK)           // Extract exponent
#define SET_EXP(a) ((a) << EXP_POS)                        // Set exponent
#define GET_EMANT(a) (((a) >> LOOKUP_POS) & LOOKUP_MASK)   // Extended mantissa MSB's
#define SET_MANTSEED(a) (((unsigned long)(a)) << SEED_POS) // Set mantissa SEED_BITS MSB's

namespace Poseidon::Foundation
{
class InverseSqrtCalculator
{
    SEED_TYPE _iSqrt[TABLE_SIZE];

  public:
    float Calculate(float x);
    InverseSqrtCalculator();
};

union FloatInt
{
    unsigned long i;
    float f;
};

InverseSqrtCalculator::InverseSqrtCalculator()
{
    long f;
    FloatInt fi, fo;

    SEED_TYPE* h;
    for (f = 0, h = _iSqrt; f < TABLE_SIZE; f++)
    {
        fi.i = ((EXP_BIAS - 1) << EXP_POS) | (f << LOOKUP_POS);
        fo.f = 1.0 / sqrt(fi.f);
        *h++ = (SEED_TYPE)(((fo.i + (1 << (SEED_POS - 2))) >> SEED_POS) & SEED_MASK); // rounding
    }
    _iSqrt[TABLE_SIZE / 2] = SEED_MASK; // Special case for 1.0
}

// Meyers singleton - no global constructor
static InverseSqrtCalculator& GetInvSqrtCalc()
{
    static InverseSqrtCalculator instance;
    return instance;
}

inline float InverseSqrtCalculator::Calculate(float x)
{
    FloatInt fi;
    fi.f = x;
    unsigned long a = fi.i;

    FloatInt seed;
    int exponent = ((3 * EXP_BIAS - 1) - GET_EXP(a)) >> 1;
    int mantisa = GET_EMANT(a);
    seed.i = SET_EXP(exponent) | SET_MANTSEED(_iSqrt[mantisa]);

    // Seed: accurate to LOOKUP_BITS
    float r = seed.f;
    // First iteration: accurate to 2*LOOKUP_BITS
    r = (3.0 - r * r * x) * r * 0.5;
#if LOOKUP_BITS < 10
    // Second iteration: accurate to 4*LOOKUP_BITS
    r = (3.0 - r * r * x) * r * 0.5;
#endif
    return r;
}

float InvSqrt(float x)
{
    return GetInvSqrtCalc().Calculate(x);
}

#else

float InvSqrt(float f)
{
    const __m128 c0pt5 = _mm_set_ss(0.5);
    const __m128 c3pt0 = _mm_set_ss(3.0);
    __m128 ff = _mm_set_ss(f);
    __m128 ra0 = _mm_rsqrt_ss(ff);
    __m128 res = _mm_mul_ss(_mm_mul_ss(c0pt5, ra0), _mm_sub_ss(c3pt0, _mm_mul_ss(_mm_mul_ss(ff, ra0), ra0)));
    float ret;
    _mm_store_ss(&ret, res);
    return ret;
}

#endif

float Interpolativ(float control, float cMin, float cMax, float vMin, float vMax)
{
    if (control <= cMin)
    {
        return vMin;
    }
    if (control >= cMax)
    {
        return vMax;
    }
    return (control - cMin) / (cMax - cMin) * (vMax - vMin) + vMin;
}

float AngleDifference(float a, float b)
{
    float d = a - b;
    if (fabs(d) <= H_PI)
    {
        return d;
    }
    d = fastFmod(d, 2 * H_PI);
    if (d > +H_PI)
    {
        d -= 2 * H_PI;
    }
    if (d < -H_PI)
    {
        d += 2 * H_PI;
    }
    return d;
}

#ifdef _MSC_VER

bool VerifyFloat(float x)
{
    switch (_fpclass(x))
    {
        case _FPCLASS_SNAN:
        case _FPCLASS_QNAN:
        case _FPCLASS_PINF:
        case _FPCLASS_NINF:
            RptF("Non-finite number %g", x);
            return false;
    }
    // all tests on indefinite number will fail
    if (x <= 0)
    {
        return true;
    }
    if (x >= 0)
    {
        return true;
    }
    // we have indefinite number
    RptF("Invalid number %g", x);
    return false;
}

#else

bool VerifyFloat(float x)
{
    if (_finite(x))
        return true;
    if (isinf(x))
    {
        RptF("Non-finite number %g", x);
        return false;
    }
    RptF("Invalid number %g", x);
    return false;
}

#endif

} // namespace Poseidon::Foundation
