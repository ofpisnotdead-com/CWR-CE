#pragma once
int toInt(float fval);
int toInt(double f);

#include <Poseidon/Foundation/Common/X86IntrinsicsCompat.hpp>
#include <Poseidon/Foundation/platform.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>

#define R_EYE 0.299f
#define G_EYE 0.587f
#define B_EYE 0.114f

// passing by reference preferred
#define ColorVal const ColorK&

namespace Poseidon
{
class ColorK
{
    // color used for shading calculations
  private:
    // valid color is in range <0,1)
    union
    {
        __m128 _k;
        struct
        {
            float _a, _r, _g, _b;
        };
    };

  public:
    explicit __forceinline ColorK(const __m128& src) { _k = src; }
    __forceinline const __m128& GetM128() { return _k; }

    __forceinline ColorK(const ColorK& src) { _k = src._k; }
    __forceinline void operator=(const ColorK& src) { _k = src._k; }

    __forceinline ColorK() {}             // default is uninitialized
    __forceinline ColorK(enum _noInit) {} // default is uninitialized
    __forceinline ColorK(float r, float g, float b) { _k = _mm_set_ps(b, g, r, 1); }
    __forceinline ColorK(float r, float g, float b, float a) { _k = _mm_set_ps(b, g, r, a); }
    explicit ColorK(long rgb)
    {
        _a = ((rgb >> 24) & 0xff) * (1 / 255.0f);
        _r = ((rgb >> 16) & 0xff) * (1 / 255.0f);
        _g = ((rgb >> 8) & 0xff) * (1 / 255.0f);
        _b = ((rgb >> 0) & 0xff) * (1 / 255.0f);
    }
    __forceinline float R() const { return _r; }
    __forceinline float G() const { return _g; }
    __forceinline float B() const { return _b; }
    __forceinline float A() const { return _a; }
    __forceinline int R8() const { return toInt(_r * 255); }
    __forceinline int G8() const { return toInt(_g * 255); }
    __forceinline int B8() const { return toInt(_b * 255); }
    __forceinline int A8() const { return toInt(_a * 255); }
    __forceinline void SetA(float a) { _a = a; }

    __forceinline ColorK operator*(ColorVal op) const { return ColorK(_mm_mul_ps(_k, op._k)); }
    __forceinline ColorK operator*(float c) const { return ColorK(_mm_mul_ps(_k, _mm_set_ps1(c))); }
    __forceinline void operator+=(ColorVal op) { _k = _mm_add_ps(_k, op._k); }

    __forceinline ColorK operator+(ColorVal op) const { return ColorK(_mm_add_ps(_k, op._k)); }
    __forceinline ColorK operator-(ColorVal op) const { return ColorK(_mm_sub_ps(_k, op._k)); }
    float Brightness() const { return _r * R_EYE + _g * G_EYE + _b * B_EYE; }

    void Saturate()
    {
        _k = _mm_min_ps(_k, _mm_set_ps1(1.0f));
        // alpha never overflows
    }
    void SaturateMinMax()
    {
        _k = _mm_max_ps(_k, _mm_setzero_ps());
        _k = _mm_min_ps(_k, _mm_set_ps1(1.0f));
    }
};

extern const ColorK HBlackK;
extern const ColorK HWhiteK;

class PackedColor
{ // packed color - for efficient storing, no calculations
    DWORD _value;

  public:
    __forceinline PackedColor() {}
    __forceinline PackedColor(int r, int g, int b, int a) // no clipping
    {
        _value = (a << 24) | (r << 16) | (g << 8) | b;
    }
    __forceinline explicit PackedColor(DWORD value) { _value = value; }
    explicit PackedColor(ColorVal color) // full clipping
    {                                    // convert with saturation
        ColorK temp = color * 255;
        // SSE saturation

        __m128 tk = temp.GetM128();
        tk = _mm_max_ps(tk, _mm_setzero_ps());
        tk = _mm_min_ps(tk, _mm_set_ps1(255.0f));

        ColorK t(tk);

        int r = toInt(t.R());
        int g = toInt(t.G());
        int b = toInt(t.B());
        int a = toInt(t.A());
        _value = (a << 24) | (r << 16) | (g << 8) | b;
    }
    void SetA8(int val) { _value = (_value & 0xffffff) | (val << 24); }
    __forceinline int A8() const { return (_value >> 24) & 0xff; }
    __forceinline int R8() const { return (_value >> 16) & 0xff; }
    __forceinline int G8() const { return (_value >> 8) & 0xff; }
    __forceinline int B8() const { return (_value >> 0) & 0xff; }
    operator ColorK() const
    {
        ColorK temp(R8(), G8(), B8(), A8());
        return temp * (1.0f / 255);
    }
    __forceinline operator DWORD() const { return _value; }
    friend PackedColor PackedColorRGB(ColorVal rgb, int a);
    friend PackedColor PackedColorRGB(PackedColor rgb, int a);
};

inline PackedColor PackedColorRGB(ColorVal color, int a = 255)
{
    ColorK temp = color * 255;
    // SSE saturation

    __m128 tk = temp.GetM128();
    tk = _mm_max_ps(tk, _mm_setzero_ps());
    tk = _mm_min_ps(tk, _mm_set_ps1(255.0f));

    ColorK t(tk);

    int r = toInt(t.R());
    int g = toInt(t.G());
    int b = toInt(t.B());

    PackedColor ret;
    ret._value = (a << 24) | (r << 16) | (g << 8) | b;
    return ret;
}

inline PackedColor PackedColorRGB(PackedColor color, int a = 255)
{
    PackedColor ret;
    ret._value = (color._value & 0xffffff) | (a << 24);
    return ret;
}

#define PackedWhite PackedColor(0xffffffff)
#define PackedBlack PackedColor(0xff000000)

// binary copy will do
} // namespace Poseidon

using Poseidon::PackedColor;
