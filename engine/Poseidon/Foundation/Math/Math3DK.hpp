#include <Poseidon/Foundation/Common/X86IntrinsicsCompat.hpp>
#ifdef _MSC_VER
#pragma once
#endif

#ifndef _MATH3DK_HPP
#define _MATH3DK_HPP

#include <Poseidon/Foundation/Math/MathDefs.hpp>

#ifdef ALIGN_CHECK
#define ASSERT_THIS_XMM() DoAssert(((int)this & 15) == 0)
#else
#define ASSERT_THIS_XMM()
#endif

#if _KNI
#include <xmmintrin.h>
#endif

#include <math.h>
#include <float.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>

#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Containers/TypeOpts.hpp>

#include <Poseidon/Foundation/Math/MathOpt.hpp>

#include <Poseidon/Foundation/Math/Math3DP.hpp>

// use parameter placeholder to explicitly disable initialization of elements

// #define vecAlign __declspec(align(16))
#define vecAlign
// passing by value preferred
#define Vector3KVal const Vector3K&
#define Vector3KPar const Vector3K&

namespace Poseidon::Foundation
{
class Vector3K;
class Matrix3K;
__forceinline __m128 HorizontalSum3(__m128 a);
class Matrix4K;

extern const Vector3K VZeroK;
extern const Vector3K VUpK;
extern const Vector3K VForwardK;
extern const Vector3K VAsideK;

extern const Matrix3K M3ZeroK;
extern const Matrix3K M3IdentityK;

extern const Matrix4K M4ZeroK;
extern const Matrix4K M4IdentityK;

class FloatQuad
{
    __m128 _quad;

  public:
    FloatQuad(float f) : _quad(_mm_set_ps1(f)) {}
    operator const __m128&() const { return _quad; }
};

class Vector3K
{
    friend class Matrix4K;
    friend class Matrix3K;

    // 3D type - used for rendering, screen clipping ...
  private:
    union
    {
        __m128 _k;
        float _e[4];
    };

  protected:
    __forceinline Coord Get(int i) const
    {
#if _DEBUG
        PoseidonAssert(_e[3] != FLT_MAX);
#endif
        return _e[i];
    }
    __forceinline Coord& Set(int i)
    {
#if _DEBUG
        PoseidonAssert(_e[3] != FLT_MAX);
#endif
        return _e[i];
    }

  public:
    __forceinline void Init() { _e[3] = 0; } // init 4th component

    // data initializers
    void SetMultiply(const Matrix3K& a, Vector3KPar v);
    void SetMultiplyLeft(Vector3KPar v, const Matrix3K& a);

    void SetRotate(const Matrix4K& a, Vector3KPar v);
    void SetFastTransform(const Matrix4K& a, Vector3KPar v);
    void SetMultiply(const Matrix4K& a, Vector3KPar v);

    float SetPerspectiveProject(const Matrix4K& a, Vector3KPar o);

    // interoperability between __m128 and Vector3K types
    __forceinline Vector3K(const __m128& src)
    {
        ASSERT_THIS_XMM();
        _k = src;
    }
    __forceinline Vector3K(const Vector3K& src)
    {
        ASSERT_THIS_XMM();
        _k = src._k;
    }

    __forceinline void operator=(const __m128& src)
    {
        ASSERT_THIS_XMM();
        _k = src;
    }
    __forceinline void operator=(const Vector3K& src)
    {
        ASSERT_THIS_XMM();
        _k = src._k;
    }

    __forceinline const __m128& GetM128() const { return _k; }

// avoid denormals
#define INIT() _k = _mm_set_ps1(FLT_MAX)

    __forceinline Vector3K()
    {
        ASSERT_THIS_XMM();
        INIT();
    } // default no init
    Vector3K(Coord x, Coord y, Coord z)
    {
        ASSERT_THIS_XMM();
        _k = _mm_set_ps(0, z, y, x);
    }
    __forceinline Vector3K(enum _noInit)
    {
        ASSERT_THIS_XMM();
        INIT();
    }
    __forceinline Vector3K(enum _vMultiply, const Matrix3K& a, Vector3KPar v)
    {
        ASSERT_THIS_XMM();
        INIT();
        SetMultiply(a, v);
    }
    __forceinline Vector3K(enum _vMultiplyLeft, Vector3KPar v, const Matrix3K& a)
    {
        ASSERT_THIS_XMM();
        INIT();
        SetMultiplyLeft(v, a);
    }
    __forceinline Vector3K(enum _vRotate, const Matrix4K& a, Vector3KPar v)
    {
        ASSERT_THIS_XMM();
        INIT();
        SetRotate(a, v);
    }
    __forceinline Vector3K(enum _vFastTransform, const Matrix4K& a, Vector3KPar v)
    {
        ASSERT_THIS_XMM();
        INIT();
        SetFastTransform(a, v);
    }
    __forceinline Vector3K(enum _vMultiply, const Matrix4K& a, Vector3KPar v)
    {
        ASSERT_THIS_XMM();
        INIT();
        SetFastTransform(a, v);
    }

    // properties
    __forceinline Coord operator[](int i) const { return _e[i]; }
    __forceinline Coord& operator[](int i) { return _e[i]; }

#if _DEBUG
    __forceinline Coord X() const { return Get(0); }
    __forceinline Coord Y() const { return Get(1); }
    __forceinline Coord Z() const { return Get(2); }
#else
    __forceinline Coord X() const { return _e[0]; }
    __forceinline Coord Y() const { return _e[1]; }
    __forceinline Coord Z() const { return _e[2]; }
#endif

  private:
    friend __forceinline __m128 HorizontalSum4(__m128 a)
    {
        return _mm_add_ss(
            a, _mm_add_ss(_mm_shuffle_ps(a, a, 1), _mm_add_ss(_mm_shuffle_ps(a, a, 2), _mm_shuffle_ps(a, a, 3))));
    }
    friend __forceinline __m128 HorizontalSum3(__m128 a)
    {
        return _mm_add_ss(a, _mm_add_ss(_mm_shuffle_ps(a, a, 1), _mm_shuffle_ps(a, a, 2)));
    }

  public:
    Coord SquareSize() const { return ((*this) * (*this)); }
    __forceinline Coord SquareSizeInline() const { return ((*this) * (*this)); }

    Coord Size() const
    {
        float size2 = SquareSize();
        if (size2 == 0)
            return 0;
        Coord invSize = InvSqrt(size2);
        return size2 * invSize;
    }
    Coord InvSize() const { return InvSqrt(SquareSize()); }
    Coord InvSquareSize() const { return Inv(SquareSize()); }

    Coord SquareSizeXZ() const { return X() * X() + Z() * Z(); }
    Coord SizeXZ() const { return (Coord)sqrt(SquareSizeXZ()); }
    Coord InvSizeXZ() const { return InvSqrt(SquareSizeXZ()); }
    Coord InvSquareSizeXZ() const { return Inv(SquareSizeXZ()); }

    // vector arithmetics
    __forceinline Vector3K operator-() const { return _mm_sub_ps(_mm_setzero_ps(), _k); }
    __forceinline Vector3K operator+(Vector3KPar op) const { return _mm_add_ps(_k, op._k); }
    __forceinline Vector3K operator-(Vector3KPar op) const { return _mm_sub_ps(_k, op._k); }
    __forceinline Vector3K operator*(const FloatQuad& op) const { return _mm_mul_ps(op, _k); }
    __forceinline friend Vector3K operator*(const FloatQuad& f, Vector3KPar op) { return _mm_mul_ps(f, op._k); }
    __forceinline Vector3K operator/(Coord f) const { return _mm_div_ps(_k, _mm_set_ps1(f)); }
    __forceinline void operator+=(Vector3KPar op) { _k = _mm_add_ps(_k, op._k); }
    __forceinline void operator-=(Vector3KPar op) { _k = _mm_sub_ps(_k, op._k); }
    __forceinline void operator*=(const FloatQuad& f) { _k = _mm_mul_ps(_k, f); }
    __forceinline void operator/=(Coord f) { _k = _mm_div_ps(_k, _mm_set_ps1(f)); }

    __forceinline Vector3K Modulate(Vector3KPar op) const { return _mm_mul_ps(_k, op._k); }

    Vector3K Normalized() const;
    void Normalize(); // no return to avoid using instead of Normalized
    __forceinline Coord DotProduct(Vector3KPar op) const
    {
        __m128 res = HorizontalSum3(_mm_mul_ps(_k, op._k));
        float ret;
        _mm_store_ss(&ret, res);
        return ret;
    }

    __forceinline Coord operator*(Vector3KPar op) const
    {
        __m128 res = HorizontalSum3(_mm_mul_ps(_k, op._k));
        float ret;
        _mm_store_ss(&ret, res);
        return ret;
    }

    Vector3K operator*(const Matrix3K& op) const { return Vector3K(VMultiplyLeft, *this, op); }
    float Distance(Vector3KPar op) const;
    float Distance2(Vector3KPar op) const;
    __forceinline float Distance2Inline(Vector3KPar op) const { return (op - *this).SquareSizeInline(); }
    float DistanceXZ(Vector3KPar op) const;
    float DistanceXZ2(Vector3KPar op) const;
    float CosAngle(Vector3KPar op) const;
    Vector3K CrossProduct(Vector3KPar op) const;
    Vector3K Project(Vector3KPar op) const;
    Matrix3K Tilda() const;
    bool IsFinite() const;

    bool operator==(Vector3KPar cmp) const { return cmp.X() == X() && cmp.Y() == Y() && cmp.Z() == Z(); }
    bool operator!=(Vector3KPar cmp) const { return cmp.X() != X() || cmp.Y() != Y() || cmp.Z() != Z(); }
};

class Matrix3K
{
    friend class Vector3K;
    friend class Matrix4K;

    // homogenous matrix - transformations
  private:
    vecAlign Vector3K _aside;
    vecAlign Vector3K _up;
    vecAlign Vector3K _dir;

    __forceinline Coord Get(int i, int j) const { return (&_aside)[j][i]; }
    __forceinline Coord& Set(int i, int j) { return (&_aside)[j][i]; }

  public:
    // functions that load matrix with data
    // used internaly in constuctors, but may be useful also to other purpose
    void SetIdentity();
    void SetZero();
    void SetRotationX(Coord angle);
    void SetRotationY(Coord angle);
    void SetRotationZ(Coord angle);
    void SetScale(Coord x, Coord y, Coord z);

    void SetScale(float scale);
    float Scale() const;
    float InvScale() const;

    void SetDirectionAndUp(Vector3KPar dir, Vector3KPar up); // sets only 3x3 submatrix
    void SetUpAndAside(Vector3KPar up, Vector3KPar aside);
    void SetUpAndDirection(Vector3KPar up, Vector3KPar dir);
    void SetDirectionAndAside(Vector3KPar dir, Vector3KPar aside);

    void SetMultiply(const Matrix3K& a, const Matrix3K& b);
    void SetMultiply(const Matrix3K& a, const FloatQuad& f);

    __forceinline void InlineSetMultiply(const Matrix3K& src, const FloatQuad& f)
    {
        _aside = src._aside * f;
        _up = src._up * f;
        _dir = src._dir * f;
    }
    __forceinline void InlineAddMultiply(const Matrix3K& src, const FloatQuad& f)
    {
        _aside += src._aside * f;
        _up += src._up * f;
        _dir += src._dir * f;
    }
    __forceinline void InlineSetAdd(const Matrix3K& a, const Matrix3K& b)
    {
        _aside = a._aside + b._aside;
        _up = a._up + b._up;
        _dir = a._dir + b._dir;
    }

    void SetInvertRotation(const Matrix3K& op);
    void SetInvertScaled(const Matrix3K& op);
    void SetInvertGeneral(const Matrix3K& op);
    void SetNormalTransform(const Matrix3K& op);
    void SetTilda(Vector3KPar a);

    // placeholder parameter describes constructor type
    __forceinline Matrix3K() {} // default no init
    Matrix3K(const Vector3K& aside, const Vector3K& up, const Vector3K& dir) : _aside(aside), _up(up), _dir(dir) {}

    Matrix3K(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22)
    {
        SetDirectionAside(Vector3K(m00, m10, m20));
        SetDirectionUp(Vector3K(m01, m11, m21));
        SetDirection(Vector3K(m02, m12, m22));
    }
    __forceinline Matrix3K(enum _noInit) {}
    __forceinline Matrix3K(enum _mRotationX, Coord angle) { SetRotationX(angle); }
    __forceinline Matrix3K(enum _mRotationY, Coord angle) { SetRotationY(angle); }
    __forceinline Matrix3K(enum _mRotationZ, Coord angle) { SetRotationZ(angle); }
    __forceinline Matrix3K(enum _mScale, Coord x, Coord y, Coord z) { SetScale(x, y, z); }
    __forceinline Matrix3K(enum _mScale, Coord x) { SetScale(x, x, x); }
    __forceinline Matrix3K(enum _mDirection, Vector3KPar dir, Vector3KPar up) { SetDirectionAndUp(dir, up); }
    __forceinline Matrix3K(enum _mUpAndDirection, Vector3KPar dir, Vector3KPar up) { SetUpAndDirection(dir, up); }
    __forceinline Matrix3K(enum _mMultiply, const Matrix3K& a, const Matrix3K& b) { SetMultiply(a, b); }
    __forceinline Matrix3K(enum _mMultiply, const Matrix3K& a, float op) { SetMultiply(a, op); }
    __forceinline Matrix3K(enum _mInverseRotation, const Matrix3K& a) { SetInvertRotation(a); }
    __forceinline Matrix3K(enum _mInverseGeneral, const Matrix3K& a) { SetInvertGeneral(a); }
    __forceinline Matrix3K(enum _mInverseScaled, const Matrix3K& a) { SetInvertScaled(a); }
    __forceinline Matrix3K(enum _mNormalTransform, const Matrix3K& a) { SetNormalTransform(a); }
    __forceinline Matrix3K(enum _mTilda, Vector3KPar a) { SetTilda(a); }

    // following operators are defined so that no copy constuctor is used
    // if they are expanded inline, copy is not needed
    __forceinline Matrix3K operator*(const Matrix3K& op) const { return Matrix3K(MMultiply, *this, op); }
    __forceinline Vector3K operator*(Vector3KPar op) const { return Vector3K(VMultiply, *this, op); }
    __forceinline Matrix3K operator*(float op) const { return Matrix3K(MMultiply, *this, op); }
    __forceinline void InlineMultiply(Matrix3K& dst, float op) const
    {
        dst._aside = _aside * op;
        dst._up = _up * op;
        dst._dir = _dir * op;
    }
    void operator*=(float op);
    Matrix3K operator+(const Matrix3K& a) const;
    Matrix3K operator-(const Matrix3K& a) const;
    Matrix3K& operator+=(const Matrix3K& a);
    Matrix3K& operator-=(const Matrix3K& a);

    Matrix3K InverseRotation() const { return Matrix3K(MInverseRotation, *this); }
    Matrix3K InverseGeneral() const { return Matrix3K(MInverseGeneral, *this); }
    Matrix3K InverseScaled() const { return Matrix3K(MInverseScaled, *this); }
    __forceinline Coord operator()(int i, int j) const { return Get(i, j); }
    __forceinline Coord& operator()(int i, int j) { return Set(i, j); }

    bool IsFinite() const;

    // simple access to generic transformation matrix
    __forceinline const Vector3K& Direction() const { return _dir; }
    __forceinline const Vector3K& DirectionUp() const { return _up; }
    __forceinline const Vector3K& DirectionAside() const { return _aside; }
    __forceinline void SetDirection(const Vector3K& v) { _dir = v; }
    __forceinline void SetDirectionUp(const Vector3K& v) { _up = v; }
    __forceinline void SetDirectionAside(const Vector3K& v) { _aside = v; }

    void Orthogonalize();
};

class Matrix4K
{
    friend class Vector4;
    friend class Vector3K;

    // homogenous matrix - transformations
  private:
    vecAlign Matrix3K _orientation;
    vecAlign Vector3K _position;

    __forceinline Coord Get(int i, int j) const { return _orientation.Get(i, j); }
    __forceinline Coord& Set(int i, int j) { return _orientation.Set(i, j); }

    __forceinline Coord GetPos(int i) const { return _position.Get(i); }
    __forceinline Coord& SetPos(int i) { return _position.Set(i); }

  public:
    // functions that load matrix with data
    // used internaly in constuctors, but may be useful also to other purpose
    void SetIdentity()
    {
        _orientation.SetIdentity();
        _position = VZeroK;
    }
    void SetZero()
    {
        _orientation.SetZero();
        _position = VZeroK;
    }
    void SetTranslation(Vector3KPar offset);
    void SetRotationX(Coord angle);
    void SetRotationY(Coord angle);
    void SetRotationZ(Coord angle);
    void SetScale(Coord x, Coord y, Coord z);
    void SetPerspective(Coord cLeft, Coord cTop);

    // sets only 3x3 submatrix
    __forceinline void SetDirectionAndUp(Vector3KPar dir, Vector3KPar up) { _orientation.SetDirectionAndUp(dir, up); }
    __forceinline void SetUpAndAside(Vector3KPar up, Vector3KPar aside) { _orientation.SetUpAndAside(up, aside); }
    __forceinline void SetUpAndDirection(Vector3KPar up, Vector3KPar dir) { _orientation.SetUpAndDirection(up, dir); }
    __forceinline void SetDirectionAndAside(Vector3KPar dir, Vector3KPar aside)
    {
        _orientation.SetDirectionAndAside(dir, aside);
    }

    __forceinline void SetScale(float scale) { _orientation.SetScale(scale); }
    __forceinline float Scale() const { return _orientation.Scale(); }
    __forceinline float InvScale() const { return _orientation.InvScale(); }

    void SetOriented(Vector3KPar dir, Vector3KPar up); // sets whole 4x4 matrix
    void SetView(Vector3KPar point, Vector3KPar dir, Vector3KPar up);
    void SetMultiply(const Matrix4K& a, const Matrix4K& b);
    void SetMultiply(const Matrix4K& a, const FloatQuad& f);
    void AddMultiply(const Matrix4K& a, float b);
    void SetMultiplyByPerspective(const Matrix4K& A, const Matrix4K& B);
    void SetInvertRotation(const Matrix4K& op);
    void SetInvertScaled(const Matrix4K& op);
    void SetInvertGeneral(const Matrix4K& op);

    // placeholder parameter describes constructor type
    __forceinline Matrix4K() {} // default no init
    __forceinline Matrix4K(const Vector3K& aside, const Vector3K& up, const Vector3K& dir, const Vector3K& pos)
        : _orientation(aside, up, dir), _position(pos)
    {
    }
    Matrix4K(float m00, float m01, float m02, float m03, float m10, float m11, float m12, float m13, float m20,
             float m21, float m22, float m23)
    {
        SetDirectionAside(Vector3K(m00, m10, m20));
        SetDirectionUp(Vector3K(m01, m11, m21));
        SetDirection(Vector3K(m02, m12, m22));
        SetPosition(Vector3K(m03, m13, m23));
    }
    __forceinline Matrix4K(enum _noInit) {}
    __forceinline Matrix4K(enum _mTranslation, Vector3KPar offset) { SetTranslation(offset); }
    __forceinline Matrix4K(enum _mRotationX, Coord angle) { SetRotationX(angle); }
    __forceinline Matrix4K(enum _mRotationY, Coord angle) { SetRotationY(angle); }
    __forceinline Matrix4K(enum _mRotationZ, Coord angle) { SetRotationZ(angle); }
    __forceinline Matrix4K(enum _mScale, Coord x, Coord y, Coord z) { SetScale(x, y, z); }
    __forceinline Matrix4K(enum _mScale, Coord x) { SetScale(x, x, x); }
    __forceinline Matrix4K(enum _mPerspective, Coord cLeft, Coord cTop) { SetPerspective(cLeft, cTop); }
    __forceinline Matrix4K(enum _mDirection, Vector3KPar dir, Vector3KPar up) { SetOriented(dir, up); }
    __forceinline Matrix4K(enum _mView, Vector3KPar point, Vector3KPar dir, Vector3KPar up) { SetView(point, dir, up); }
    __forceinline Matrix4K(enum _mMultiply, const Matrix4K& a, const Matrix4K& b) { SetMultiply(a, b); }
    __forceinline Matrix4K(enum _mMultiply, const Matrix4K& a, const FloatQuad& f) { SetMultiply(a, f); }
    __forceinline Matrix4K(enum _mInverseRotation, const Matrix4K& a) { SetInvertRotation(a); }
    __forceinline Matrix4K(enum _mInverseScaled, const Matrix4K& a) { SetInvertScaled(a); }
    __forceinline Matrix4K(enum _mInverseGeneral, const Matrix4K& a) { SetInvertGeneral(a); }

    // following operators are defined so that no copy constuctor is used
    // if they are expanded inline, copy is not needed
    __forceinline Matrix4K operator*(const Matrix4K& op) const { return Matrix4K(MMultiply, *this, op); }
    __forceinline Matrix4K operator*(const FloatQuad& f) const { return Matrix4K(MMultiply, *this, f); }
    __forceinline void InlineSetMultiply(const Matrix4K& src, const FloatQuad& f)
    {
        _orientation.InlineSetMultiply(src._orientation, f);
        _position = src._position * f;
    }
    __forceinline void InlineSetAdd(const Matrix4K& a, const Matrix4K& b)
    {
        _orientation.InlineSetAdd(a._orientation, b._orientation);
        _position = a._position + b._position;
    }
    __forceinline void InlineAddMultiply(const Matrix4K& src, const FloatQuad& f)
    {
        _orientation.InlineAddMultiply(src._orientation, f);
        _position += src._position * f;
    }

    Matrix4K operator+(const Matrix4K& op) const;
    Matrix4K operator-(const Matrix4K& op) const;
    void operator-=(const Matrix4K& op);
    void operator+=(const Matrix4K& op);

    Vector3K Rotate(Vector3KPar op) const;
    Vector3K FastTransform(Vector3KPar op) const;
    __forceinline Vector3K operator*(Vector3KPar op) const { return Vector3K(VMultiply, *this, op); }
    __forceinline Matrix4K InverseRotation() const { return Matrix4K(MInverseRotation, *this); }
    __forceinline Matrix4K InverseScaled() const { return Matrix4K(MInverseScaled, *this); }
    __forceinline Matrix4K InverseGeneral() const { return Matrix4K(MInverseGeneral, *this); }

    __forceinline Coord operator()(int i, int j) const { return Get(i, j); }
    __forceinline Coord& operator()(int i, int j) { return Set(i, j); }

    float Characteristic() const; // used in fast comparison
    bool IsFinite() const;

    // simple access to generic transformation matrix
    __forceinline const Matrix3K& Orientation() const { return _orientation; }
    void SetOrientation(const Matrix3K& m) { _orientation = m; }

    __forceinline const Vector3K& Position() const { return _position; }
    __forceinline const Vector3K& Direction() const { return _orientation._dir; }
    __forceinline const Vector3K& DirectionUp() const { return _orientation._up; }
    __forceinline const Vector3K& DirectionAside() const { return _orientation._aside; }
    __forceinline void SetPosition(const Vector3K& v) { _position = v; }
    __forceinline void SetDirection(const Vector3K& v) { _orientation._dir = v; }
    __forceinline void SetDirectionUp(const Vector3K& v) { _orientation._up = v; }
    __forceinline void SetDirectionAside(const Vector3K& v) { _orientation._aside = v; }

    void Orthogonalize();
};

// Point and Vector are deliberately one type. Some operations are meaningless on a
// point (Point+Point, Point dot Point, Point cross Point, translating a vector), but
// building the arithmetic twice is not worth it, so the types are treated as equal.

inline Vector3K sign(Vector3KPar v)
{
    return Vector3K(fSign(v[0]), fSign(v[1]), fSign(v[2]));
}

Vector3K VectorMin(Vector3KPar a, Vector3KPar b);
Vector3K VectorMax(Vector3KPar a, Vector3KPar b);

#define Limit(speed, min, max) saturateFast(speed, min, max)

float Interpolativ(float control, float cMin, float cMax, float vMin, float vMax);
float AngleDifference(float a, float b);

void CheckMinMax(Vector3K& min, Vector3K& max, Vector3KPar val);
void SaturateMin(Vector3K& min, Vector3KPar val);
void SaturateMax(Vector3K& min, Vector3KPar val);

__forceinline void CheckMinMaxInline(Vector3K& min, Vector3K& max, Vector3KPar val)
{
    min = _mm_min_ps(min.GetM128(), val.GetM128());
    max = _mm_max_ps(max.GetM128(), val.GetM128());
}

__forceinline Matrix3K Vector3K::Tilda() const
{
    return Matrix3K(MTilda, *this);
}

__forceinline void Vector3K::SetRotate(const Matrix4K& a, Vector3KPar v)
{
    SetMultiply(a.Orientation(), v);
}

inline float Vector3K::SetPerspectiveProject(const Matrix4K& a, Vector3KPar o)
{
    // optimize: suppose that matrix is scaled and shifted perspective projection, i.e.
    // member [3,2] is 1.0
    // zero members:
    // [0,1], [0,3],
    // [1,0], [1,3], [2,0], [2,1], [2,2]
    // [3,0], [3,1], [3,3]

    // note: [0,0] and [1,1] need not be 1,
    Coord oow = coord(1) / o.Get(2);
    Set(0) = a.Get(0, 2) + a.Get(0, 0) * o.Get(0) * oow;
    Set(1) = a.Get(1, 2) + a.Get(1, 1) * o.Get(1) * oow;
    Set(2) = a.Get(2, 2) + a.GetPos(2) * oow;
    return oow;
}

inline void Vector3K::SetMultiply(const Matrix4K& a, Vector3KPar o)
{
    // same as FastTransform, but inlined
    // matrix stored in major-column format
    __m128 t1, t2, t3;

    t1 = _mm_set_ps1(o.X());
    t2 = _mm_set_ps1(o.Y());

    t1 = _mm_mul_ps(t1, a._orientation._aside._k);
    t2 = _mm_mul_ps(t2, a._orientation._up._k);

    t3 = _mm_set_ps1(o.Z());

    t1 = _mm_add_ps(t1, t2);
    t3 = _mm_mul_ps(t3, a._orientation._dir._k);

    // sum a...
    t3 = _mm_add_ps(t3, a._position._k);
    _k = _mm_add_ps(t1, t3);
}

inline void Vector3K::SetMultiply(const Matrix3K& a, Vector3KPar o)
{ // vector rotation - only 3x3 matrix is used, translation is ignored
    // u=M*v
    // matrix stored in major-column format
    __m128 t1, t2, t3;

    t1 = _mm_set_ps1(o.X());
    t2 = _mm_set_ps1(o.Y());

    t1 = _mm_mul_ps(t1, a._aside._k);
    t2 = _mm_mul_ps(t2, a._up._k);

    t3 = _mm_set_ps1(o.Z());

    t1 = _mm_add_ps(t1, t2);
    t3 = _mm_mul_ps(t3, a._dir._k);

    // sum a...
    _k = _mm_add_ps(t1, t3);
}

Matrix4K ConvertPToK(const Matrix4P& m);
Matrix4P ConvertKToP(const Matrix4K& m);

Matrix3K ConvertPToK(const Matrix3P& m);
Matrix3P ConvertKToP(const Matrix3K& m);

} // namespace Poseidon::Foundation

using ::Poseidon::Foundation::ConvertKToP;
using ::Poseidon::Foundation::ConvertPToK;
using ::Poseidon::Foundation::Matrix3K;
using ::Poseidon::Foundation::Matrix4K;
using ::Poseidon::Foundation::Vector3K;

#endif
