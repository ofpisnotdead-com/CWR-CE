#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>

#ifndef _T_MATH

#pragma optimize("t", on)

#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h> // SSE _mm_* for Vector3P::SetFastTransform
#endif

namespace Poseidon::Foundation
{
bool Matrix4P::IsFinite() const
{
    for (int i = 0; i < 3; i++)
    {
        if (!_finite(Get(i, 0)))
        {
            return false;
        }
        if (!_finite(Get(i, 1)))
        {
            return false;
        }
        if (!_finite(Get(i, 2)))
        {
            return false;
        }
        if (!_finite(GetPos(i)))
        {
            return false;
        }
    }
    return true;
}
bool Matrix3P::IsFinite() const
{
    for (int i = 0; i < 3; i++)
    {
        if (!_finite(Get(i, 0)))
        {
            return false;
        }
        if (!_finite(Get(i, 1)))
        {
            return false;
        }
        if (!_finite(Get(i, 2)))
        {
            return false;
        }
    }
    return true;
}

float Matrix4P::Characteristic() const
{
    // used in fast comparison
    float sum1 = 0; // lower dependency chains
    float sum2 = 0;
    for (int i = 0; i < 3; i++)
    {
        sum1 += Get(i, 0);
        sum2 += Get(i, 1);
        sum1 += Get(i, 2);
        sum2 += GetPos(i);
    }
    return sum1 + sum2;
}

void Matrix4P::SetIdentity()
{
    *this = M4IdentityP;
}

void Matrix4P::SetZero()
{
    *this = M4ZeroP;
}

void Matrix4P::SetTranslation(Vector3PPar offset)
{
    SetIdentity();
    SetPosition(offset);
}

void Matrix4P::SetRotationX(Coord angle)
{
    SetIdentity();
    Coord s = (Coord)sin(angle), c = (Coord)cos(angle);
    Set(1, 1) = +c, Set(1, 2) = -s;
    Set(2, 1) = +s, Set(2, 2) = +c;
}

void Matrix4P::SetRotationY(Coord angle)
{
    SetIdentity();
    Coord s = (Coord)sin(angle), c = (Coord)cos(angle);
    Set(0, 0) = +c, Set(0, 2) = -s;
    Set(2, 0) = +s, Set(2, 2) = +c;
}

void Matrix4P::SetRotationZ(Coord angle)
{
    SetIdentity();
    Coord s = (Coord)sin(angle), c = (Coord)cos(angle);
    Set(0, 0) = +c, Set(0, 1) = -s;
    Set(1, 0) = +s, Set(1, 1) = +c;
}

void Matrix4P::SetScale(Coord x, Coord y, Coord z)
{
    SetZero();
    Set(0, 0) = x;
    Set(1, 1) = y;
    Set(2, 2) = z;
}

void Matrix4P::SetPerspective(Coord cLeft, Coord cTop)
{
    SetZero();
    // xg=x*near/right :: <-1,+1>
    Set(0, 0) = 1.0f / cLeft;
    // yg=y*near/top :: <-1,+1>
    Set(1, 1) = 1.0f / cTop;

    Set(2, 2) = 1.0; // DirectX has Q here, Q = zFar/(zFar-zNear)

    // zg=-w*1
    SetPos(2) = -1; // DirectX has -Q/zNear here

    // wg=z
    // Set(3,2)=1;

    // this gives actual z result (suppose w=1) = zg/wg =
    // zRes(z)=-1/z;
}

void Matrix4P::Orthogonalize()
{
    Vector3PVal dir = Direction();
    Vector3PVal up = DirectionUp();
    SetDirectionAndUp(dir, up);
}
void Matrix3P::Orthogonalize()
{
    Vector3PVal dir = Direction();
    Vector3PVal up = DirectionUp();
    SetDirectionAndUp(dir, up);
}

void Matrix4P::SetOriented(Vector3PPar dir, Vector3PPar up)
{
    SetIdentity();
    SetDirectionAndUp(dir, up);
}

void Matrix4P::SetView(Vector3PPar point, Vector3PPar dir, Vector3PPar up)
{
    Matrix4P translate(MTranslation, -point);
    Matrix4P direction(MDirection, dir, up);
    SetMultiply(direction, translate);
}

void Matrix4P::SetAdd(const Matrix4P& a, const Matrix4P& b)
{
    _orientation._aside = a._orientation._aside + b._orientation._aside;
    _orientation._up = a._orientation._up + b._orientation._up;
    _orientation._dir = a._orientation._dir + b._orientation._dir;
    _position = a._position + b._position;
}

void Matrix4P::SetMultiply(const Matrix4P& a, const Matrix4P& b)
{
    int i, j;
    // b(3,0)=0, b(3,1)=0, b(3,2)=0, b(3,3)=1
    // a(3,0)=0, a(3,1)=0, a(3,2)=0, a(3,3)=1
    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            Set(i, j) = (a.Get(i, 0) * b.Get(0, j) + a.Get(i, 1) * b.Get(1, j) + a.Get(i, 2) * b.Get(2, j));
        }
    }
    for (i = 0; i < 3; i++)
    {
        SetPos(i) = (a.Get(i, 0) * b.GetPos(0) + a.Get(i, 1) * b.GetPos(1) + a.Get(i, 2) * b.GetPos(2) + a.GetPos(i));
    }
}

void Matrix4P::SetMultiply(const Matrix4P& a, float b)
{
    _orientation._aside = a._orientation._aside * b;
    _orientation._up = a._orientation._up * b;
    _orientation._dir = a._orientation._dir * b;
    _position = a._position * b;
}

void Matrix4P::AddMultiply(const Matrix4P& a, float b)
{
    _orientation._aside += a._orientation._aside * b;
    _orientation._up += a._orientation._up * b;
    _orientation._dir += a._orientation._dir * b;
    _position += a._position * b;
}

void Matrix4P::SetMultiplyByPerspective(const Matrix4P& a, const Matrix4P& b)
{
    int i, j;
    // b is perspective projection
    // b(3,0)=0, b(3,1)=0, b(3,2)=1, b(3,3)=0
    // a(3,0)=0, a(3,1)=0, a(3,2)=0, a(3,3)=1
    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            Set(i, j) = (a.Get(i, 0) * b.Get(0, j) + a.Get(i, 1) * b.Get(1, j) + a.Get(i, 2) * b.Get(2, j) +
                         // a.Get(i,3)*b.Get(3,j)
                         a.GetPos(i) * (j == 2));
        }
    }
    for (i = 0; i < 3; i++)
    {
        SetPos(i) = (a.Get(i, 0) * b.GetPos(0) + a.Get(i, 1) * b.GetPos(1) + a.Get(i, 2) * b.GetPos(2));
    }
}

inline float InvSquareSize(float x, float y, float z)
{
    return Inv(x * x + y * y + z * z);
}

Vector3P Vector3P::Normalized() const
{
    Coord size2 = SquareSizeInline();
    if (size2 == 0)
    {
        return *this;
    }
    Coord invSize = InvSqrt(size2);
    return Vector3P(X() * invSize, Y() * invSize, Z() * invSize);
}

void Vector3P::Normalize() // no return to avoid using instead of Normalized
{
    Coord size2 = SquareSizeInline();
    if (size2 == 0)
    {
        return;
    }
    Coord invSize = InvSqrt(size2);
    Set(0) *= invSize, Set(1) *= invSize, Set(2) *= invSize;
}

Vector3P Vector3P::CrossProduct(Vector3PPar op) const
{
    float x = Y() * op.Z() - Z() * op.Y();
    float y = Z() * op.X() - X() * op.Z();
    float z = X() * op.Y() - Y() * op.X();
    return Vector3P(x, y, z);
}

float Vector3P::CosAngle(Vector3PPar op) const
{
    return DotProduct(op) * InvSqrt(op.SquareSizeInline() * SquareSizeInline());
}

float Vector3P::Distance(Vector3PPar op) const
{
    return (*this - op).Size();
}
float Vector3P::DistanceXZ(Vector3PPar op) const
{
    return (*this - op).SizeXZ();
}
float Vector3P::DistanceXZ2(Vector3PPar op) const
{
    return (*this - op).SquareSizeXZ();
}

Vector3P Vector3P::Project(Vector3PPar op) const
{
    return op * DotProduct(op) * op.InvSquareSize();
}

bool VerifyFloat(float x);

bool Vector3P::IsFinite() const
{
    if (!VerifyFloat(Get(0)))
    {
        return false;
    }
    if (!VerifyFloat(Get(1)))
    {
        return false;
    }
    if (!VerifyFloat(Get(2)))
    {
        return false;
    }
    return true;
}

#if !_PIII // special optimization for PIII

// Note: Use direct _e[] access to avoid operator[] which has FLT_MAX assertion in debug
void Vector3P::SetFastTransform(const Matrix4P& a, Vector3PPar o)
{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    // u = M*v + t. The 3x3 is stored column-major as three contiguous 3-float
    // Vector3P (_aside/_up/_dir = columns 0/1/2), followed by the 3-float
    // _position. Vector3P is 12 bytes, unaligned, no trailing pad, so:
    //  - use UNALIGNED loads (aligned _mm_load_ps would fault);
    //  - a column load's spilled 4th lane lands in the next in-struct member and
    //    is discarded (in-bounds for _aside/_up/_dir);
    //  - _position is the last member, so build it with _mm_setr_ps from its 3
    //    floats rather than loading 4 (which would over-read past the object).
    // Per-component this evaluates ((m0*o0 + m1*o1) + m2*o2) + t, the same op
    // order as the scalar path, so the result is bit-identical. o may alias
    // *this: values are read into registers before *this is written.
    const __m128 vx = _mm_set1_ps(o._e[0]);
    const __m128 vy = _mm_set1_ps(o._e[1]);
    const __m128 vz = _mm_set1_ps(o._e[2]);
    const __m128 c0 = _mm_loadu_ps(&a.DirectionAside()._e[0]);
    const __m128 c1 = _mm_loadu_ps(&a.DirectionUp()._e[0]);
    const __m128 c2 = _mm_loadu_ps(&a.Direction()._e[0]);
    const Vector3P& t = a.Position();
    const __m128 pos = _mm_setr_ps(t._e[0], t._e[1], t._e[2], 0.0f);
    __m128 acc = _mm_add_ps(_mm_mul_ps(vx, c0), _mm_mul_ps(vy, c1));
    acc = _mm_add_ps(acc, _mm_mul_ps(vz, c2));
    acc = _mm_add_ps(acc, pos);
    float r[4];
    _mm_storeu_ps(r, acc);
    _e[0] = r[0];
    _e[1] = r[1];
    _e[2] = r[2];
#else
    float r0 = a.Get(0, 0) * o._e[0] + a.Get(0, 1) * o._e[1] + a.Get(0, 2) * o._e[2] + a.GetPos(0);
    float r1 = a.Get(1, 0) * o._e[0] + a.Get(1, 1) * o._e[1] + a.Get(1, 2) * o._e[2] + a.GetPos(1);
    float r2 = a.Get(2, 0) * o._e[0] + a.Get(2, 1) * o._e[1] + a.Get(2, 2) * o._e[2] + a.GetPos(2);
    _e[0] = r0;
    _e[1] = r1;
    _e[2] = r2;
#endif
}

#endif

void Vector3P::SetMultiplyLeft(Vector3PPar o, const Matrix3P& a)
{ // vector rotation - only 3x3 matrix is used, translation is ignored
    // u=M*v
    float o0 = o[0], o1 = o[1], o2 = o[2];
    for (int i = 0; i < 3; i++)
    {
        Set(i) = a.Get(0, i) * o0 + a.Get(1, i) * o1 + a.Get(2, i) * o2;
    }
}

void Matrix4P::SetInvertRotation(const Matrix4P& op)
{
    // invert orientation
    _orientation.SetInvertRotation(op.Orientation());
    // invert translation
    SetPosition(Rotate(-op.Position()));
}

void Matrix4P::SetInvertScaled(const Matrix4P& op)
{
    // invert orientation
    _orientation.SetInvertScaled(op.Orientation());
    // invert translation
    Vector3P pos;
    pos.SetMultiply(Orientation(), -op.Position());
    SetPosition(pos);
}

void Matrix4P::SetInvertGeneral(const Matrix4P& op)
{
    // invert orientation
    _orientation.SetInvertGeneral(op.Orientation());
    // invert translation
    SetPosition(Rotate(-op.Position()));
}

void Matrix3P::SetNormalTransform(const Matrix3P& op)
{
    // normal transformation for scale matrix (a,b,c) is (1/a,1/b,1/c)
    // all matrices we use are rotation combined with scale
    SetIdentity();
    int j;
    float invRow0size2 = InvSquareSize(op.Get(0, 0), op.Get(0, 1), op.Get(0, 2));
    float invRow1size2 = InvSquareSize(op.Get(1, 0), op.Get(1, 1), op.Get(1, 2));
    float invRow2size2 = InvSquareSize(op.Get(2, 0), op.Get(2, 1), op.Get(2, 2));
    for (j = 0; j < 3; j++)
    {
        Set(0, j) = op.Get(0, j) * invRow0size2;
        Set(1, j) = op.Get(1, j) * invRow1size2;
        Set(2, j) = op.Get(2, j) * invRow2size2;
    }
}

void Matrix3P::SetIdentity()
{
    *this = M3IdentityP;
}

void Matrix3P::SetZero()
{
    *this = M3ZeroP;
}

void Matrix3P::SetRotationX(Coord angle)
{
    Coord s = sin(angle), c = cos(angle);
    SetIdentity();
    Set(1, 1) = +c, Set(1, 2) = -s;
    Set(2, 1) = +s, Set(2, 2) = +c;
}

void Matrix3P::SetRotationY(Coord angle)
{
    Coord s = sin(angle), c = cos(angle);
    SetIdentity();
    Set(0, 0) = +c, Set(0, 2) = -s;
    Set(2, 0) = +s, Set(2, 2) = +c;
}

void Matrix3P::SetRotationZ(Coord angle)
{
    Coord s = (Coord)sin(angle), c = (Coord)cos(angle);
    SetIdentity();
    Set(0, 0) = +c, Set(0, 1) = -s;
    Set(1, 0) = +s, Set(1, 1) = +c;
}

void Matrix3P::SetScale(Coord x, Coord y, Coord z)
{
    SetZero();
    Set(0, 0) = x;
    Set(1, 1) = y;
    Set(2, 2) = z;
}

void Matrix3P::SetScale(float scale)
{
    // note: old scale may be zero
    float invOldScale = InvScale();
    if (invOldScale > 0)
    {
        (*this) *= scale * invOldScale;
    }
    else
    {
        SetScale(scale, scale, scale);
    }
    // any SetDirection will remove scale
}

float Matrix3P::Scale() const
{
    // scale = average of the row magnitudes of orient*transpose(orient), whose
    // off-diagonal terms are ~0 for a rotation*scale matrix
    // optimized matrix transposition + multiplication
    Vector3P sv;
    for (int i = 0; i < 3; i++)
    {
        sv[i] = Square(Get(i, 0)) + Square(Get(i, 1)) + Square(Get(i, 2));
    }

    // calculate average scale
    float scale2 = (sv[0] + sv[1] + sv[2]) * (1.0 / 3);
    return scale2 * InvSqrt(scale2);
}

float Matrix3P::InvScale() const
{
    // scale = average of the row magnitudes of orient*transpose(orient)
    Vector3P sv;
    for (int i = 0; i < 3; i++)
    {
        sv[i] = Square(Get(i, 0)) + Square(Get(i, 1)) + Square(Get(i, 2));
    }

    // calculate average scale
    float scale2 = (sv[0] + sv[1] + sv[2]) * (1.0 / 3);

    if (scale2 <= 0)
    {
        return 0; // singular matrix
    }
    return InvSqrt(scale2);
}

void Matrix3P::SetDirectionAndUp(Vector3PPar dir, Vector3PPar up)
{
    SetDirection(dir.Normalized());
    // Project into the plane
    Coord t = up * Direction();
    SetDirectionUp((up - Direction() * t).Normalized());
    // Calculate the vector pointing along the x axis (i.e. aside)
    // sign chosen empirically
    // no need to normalize - cross product of two perpendicular unit vectors is unit vector
    SetDirectionAside(DirectionUp().CrossProduct(Direction()));
}

void Matrix3P::SetDirectionAndAside(Vector3PPar dir, Vector3PPar aside)
{
    SetDirection(dir.Normalized());
    // Project into the plane
    Coord t = aside * Direction();
    SetDirectionAside((aside - Direction() * t).Normalized());
    // Calculate the vector pointing along the x axis (i.e. aside)
    // sign chosen empirically
    // no need to normalize - cross product of two perpendicular unit vectors is unit vector
    SetDirectionUp(Direction().CrossProduct(DirectionAside()));
}

void Matrix3P::SetUpAndAside(Vector3PPar up, Vector3PPar aside)
{
    SetDirectionUp(up.Normalized());
    // Project into the plane
    Coord t = DirectionUp() * aside;
    SetDirectionAside((aside - DirectionUp() * t).Normalized());
    // Calculate the vector pointing along the x axis (i.e. aside)
    // sign chosen empirically
    // no need to normalize - cross product of two perpendicular unit vectors is unit vector
    SetDirection(DirectionAside().CrossProduct(DirectionUp()));
}

void Matrix3P::SetUpAndDirection(Vector3PPar up, Vector3PPar dir)
{
    SetDirectionUp(up.Normalized());
    // Project into the plane
    Coord t = DirectionUp() * dir;
    // Calculate the vector pointing along the x axis (i.e. aside)
    // sign chosen empirically
    // no need to normalize - cross product of two perpendicular unit vectors is unit vector
    SetDirection((dir - DirectionUp() * t).Normalized());
    SetDirectionAside(DirectionUp().CrossProduct(Direction()));
}

void Matrix3P::SetTilda(Vector3PPar a)
{
    SetZero();
    Set(0, 1) = -a[2], Set(0, 2) = +a[1];
    Set(1, 0) = +a[2], Set(1, 2) = -a[0];
    Set(2, 0) = -a[1], Set(2, 1) = +a[0];
}

void Matrix3P::operator*=(float op)
{
    _aside *= op;
    _up *= op;
    _dir *= op;
}

void Matrix3P::SetAdd(const Matrix3P& a, const Matrix3P& b)
{
    _aside = a._aside + b._aside;
    _up = a._up + b._up;
    _dir = a._dir + b._dir;
}

void Matrix3P::SetMultiply(const Matrix3P& a, const Matrix3P& b)
{
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            Set(i, j) = (a.Get(i, 0) * b.Get(0, j) + a.Get(i, 1) * b.Get(1, j) + a.Get(i, 2) * b.Get(2, j));
        }
    }
}

void Matrix3P::SetMultiply(const Matrix3P& a, float op)
{
    _aside = a._aside * op;
    _up = a._up * op;
    _dir = a._dir * op;
}

void Matrix3P::AddMultiply(const Matrix3P& a, float op)
{
    _aside += a._aside * op;
    _up += a._up * op;
    _dir += a._dir * op;
}

Matrix3P Matrix3P::operator+(const Matrix3P& a) const
{
    Matrix3P res;
    res._aside = _aside + a._aside;
    res._up = _up + a._up;
    res._dir = _dir + a._dir;
    return res;
}

Matrix3P Matrix3P::operator-(const Matrix3P& a) const
{
    Matrix3P res;
    res._aside = _aside - a._aside;
    res._up = _up - a._up;
    res._dir = _dir - a._dir;
    return res;
}

Matrix3P& Matrix3P::operator-=(const Matrix3P& a)
{
    _aside -= a._aside;
    _up -= a._up;
    _dir -= a._dir;
    return *this;
}

Matrix3P& Matrix3P::operator+=(const Matrix3P& a)
{
    _aside += a._aside;
    _up += a._up;
    _dir += a._dir;
    return *this;
}

Matrix4P Matrix4P::operator+(const Matrix4P& a) const
{
    Matrix4P res;
    res._orientation._aside = _orientation._aside + a._orientation._aside;
    res._orientation._up = _orientation._up + a._orientation._up;
    res._orientation._dir = _orientation._dir + a._orientation._dir;
    res._position = _position + a._position;
    return res;
}

Matrix4P Matrix4P::operator-(const Matrix4P& a) const
{
    Matrix4P res;
    res._orientation._aside = _orientation._aside - a._orientation._aside;
    res._orientation._up = _orientation._up - a._orientation._up;
    res._orientation._dir = _orientation._dir - a._orientation._dir;
    res._position = _position - a._position;
    return res;
}

void Matrix4P::operator+=(const Matrix4P& op)
{
    _orientation._aside += op._orientation._aside;
    _orientation._up += op._orientation._up;
    _orientation._dir += op._orientation._dir;
    _position += op._position;
}
void Matrix4P::operator-=(const Matrix4P& op)
{
    _orientation._aside -= op._orientation._aside;
    _orientation._up -= op._orientation._up;
    _orientation._dir -= op._orientation._dir;
    _position -= op._position;
}

void Matrix3P::SetInvertRotation(const Matrix3P& op)
{
    // matrix inversion is calculated based on these prepositions:
    SetIdentity();
    for (int i = 0; i < 3; i++)
    {
        Set(i, 0) = op.Get(0, i);
        Set(i, 1) = op.Get(1, i);
        Set(i, 2) = op.Get(2, i);
    }
}

void Matrix3P::SetInvertScaled(const Matrix3P& op)
{
    // matrix inversion is calculated based on these prepositions:
    // matrix is S*R, where S is scale, R is rotation
    // inversion of such matrix is Inv(S)*Inv(R)
    // Inv(R) is Transpose(R), Inv(S) is C: C(i,i)=1/S(i,i)
    // sizes of row(i) are scale coeficients a,b,c
    // all members are set below, so SetIdentity() is unnecessary
    // calculate scale
    float invScale0 = InvSquareSize(op.Get(0, 0), op.Get(0, 1), op.Get(0, 2));
    float invScale1 = InvSquareSize(op.Get(1, 0), op.Get(1, 1), op.Get(1, 2));
    float invScale2 = InvSquareSize(op.Get(2, 0), op.Get(2, 1), op.Get(2, 2));
    // invert rotation and scale
    for (int i = 0; i < 3; i++)
    {
        Set(i, 0) = op.Get(0, i) * invScale0;
        Set(i, 1) = op.Get(1, i) * invScale1;
        Set(i, 2) = op.Get(2, i) * invScale2;
    }
}

#define swap(a, b) \
    {              \
        float p;   \
        p = a;     \
        a = b;     \
        b = p;     \
    }

void Matrix3P::SetInvertGeneral(const Matrix3P& op)
{
#if 1
    // check if they are really necessary
    // check if matrix is really general
    // if not, we can use scaled version
    // scaled matrix has form S*R, where S is scale matrix and R is rotation
    // (S*R)*(RT*ST)=S*ST=S*S
    //
    Matrix3P t;
    for (int i = 0; i < 3; i++)
    {
        t(0, i) = op(i, 0), t(1, i) = op(i, 1), t(2, i) = op(i, 2);
    }
    Matrix3PVal se = op * t;
    // check if se is diagonal
    const float max = 1e-6;
    if (fabs(se(0, 1)) < max && fabs(se(0, 2)) < max && fabs(se(1, 0)) < max && fabs(se(1, 2)) < max &&
        fabs(se(2, 0)) < max && fabs(se(2, 1)) < max)
    {
        // matrix is diagonal - use special case inversion
        SetInvertScaled(op);
        return;
    }
#endif

    // calculate inversion using Gauss-Jordan elimination
    Matrix3P a = op;
    // load result with identity
    SetIdentity();
    int row, col;
    // construct result by pivoting
    // pivot column
    for (col = 0; col < 3; col++)
    {
        // use maximal number as pivot
        float max = 0;
        int maxRow = col;
        for (row = col; row < 3; row++)
        {
            float mag = fabs(a.Get(row, col));
            if (max < mag)
            {
                max = mag, maxRow = row;
            }
        }
        if (max <= 0.0)
        {
            continue; // no pivot exists
        }
        // swap lines col and maxRow
        swap(a.Set(col, 0), a.Set(maxRow, 0));
        swap(a.Set(col, 1), a.Set(maxRow, 1));
        swap(a.Set(col, 2), a.Set(maxRow, 2));
        swap(Set(col, 0), Set(maxRow, 0));
        swap(Set(col, 1), Set(maxRow, 1));
        swap(Set(col, 2), Set(maxRow, 2));
        // use a(col,col) as pivot
        float quotient = 1 / a.Get(col, col);
        // make pivot 1
        a.Set(col, 0) *= quotient, a.Set(col, 1) *= quotient, a.Set(col, 2) *= quotient;
        Set(col, 0) *= quotient, Set(col, 1) *= quotient, Set(col, 2) *= quotient;
        // use pivot line to zero all other lines
        for (row = 0; row < 3; row++)
        {
            if (row != col)
            {
                float factor = a.Get(row, col);
                a.Set(row, 0) -= a.Get(col, 0) * factor;
                a.Set(row, 1) -= a.Get(col, 1) * factor;
                a.Set(row, 2) -= a.Get(col, 2) * factor;
                Set(row, 0) -= Get(col, 0) * factor;
                Set(row, 1) -= Get(col, 1) * factor;
                Set(row, 2) -= Get(col, 2) * factor;
            }
        }
    }
}

void SaturateMin(Vector3P& min, Vector3PPar val)
{
    saturateMin(min[0], val[0]);
    saturateMin(min[1], val[1]);
    saturateMin(min[2], val[2]);
}
void SaturateMax(Vector3P& max, Vector3PPar val)
{
    saturateMax(max[0], val[0]);
    saturateMax(max[1], val[1]);
    saturateMax(max[2], val[2]);
}

void CheckMinMax(Vector3P& min, Vector3P& max, Vector3PPar val)
{
    saturateMin(min[0], val[0]), saturateMax(max[0], val[0]);
    saturateMin(min[1], val[1]), saturateMax(max[1], val[1]);
    saturateMin(min[2], val[2]), saturateMax(max[2], val[2]);
}

Vector3P VectorMin(Vector3PPar a, Vector3PPar b)
{
    return Vector3P(floatMin(a[0], b[0]), floatMin(a[1], b[1]), floatMin(a[2], b[2]));
}

Vector3P VectorMax(Vector3PPar a, Vector3PPar b)
{
    return Vector3P(floatMax(a[0], b[0]), floatMax(a[1], b[1]), floatMax(a[2], b[2]));
}

#endif

} // namespace Poseidon::Foundation
