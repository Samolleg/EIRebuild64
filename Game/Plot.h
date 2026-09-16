#pragma once

#include <math.h>
#include "SimplyArray.h"
#include <DirectXMath.h>
/////////////////////////////////////////////////////////////////////////////////////
#pragma pack(4)
/////////////////////////////////////////////////////////////////////////////////////
class CPlot2D
{
public:
    float x, y;

public:
    CPlot2D() = default;
    explicit CPlot2D(float value)
        : x(value)
        , y(value)
    {
    }
    CPlot2D(float ax, float ay)
        : x(ax)
        , y(ay)
    {
    }

    float GetX() const { return x; }
    float GetY() const { return y; }

    void SetPlot(const float fpX, const float fpY)
    {
        x = fpX;
        y = fpY;
    }
    void SetPlot(const CPlot2D &plot)
    {
        x = plot.x;
        y = plot.y;
    }

    friend CPlot2D operator-(const CPlot2D &A);
    friend CPlot2D operator+(const CPlot2D &A, const CPlot2D &B);
    friend CPlot2D operator-(const CPlot2D &A, const CPlot2D &B);
    friend float operator*(const CPlot2D &A, const CPlot2D &B);
    friend CPlot2D operator*(const CPlot2D &A, const float b);
    friend CPlot2D operator*(const float a, const CPlot2D &B);
    friend CPlot2D operator/(const CPlot2D &A, const float b);
    friend BOOL operator==(const CPlot2D &A, const CPlot2D &B);

    friend float fabs(const CPlot2D &A);
};
/////////////////////////////////////////////////////////////////////////////////////
inline CPlot2D operator-(const CPlot2D &A)
{
    return CPlot2D(-A.x, -A.y);
}
inline CPlot2D operator+(const CPlot2D &A, const CPlot2D &B)
{
    return CPlot2D(A.x + B.x, A.y + B.y);
}
inline CPlot2D operator-(const CPlot2D &A, const CPlot2D &B)
{
    return CPlot2D(A.x - B.x, A.y - B.y);
}
inline float operator*(const CPlot2D &A, const CPlot2D &B)
{
    return (A.x * B.x + A.y * B.y);
}
inline CPlot2D operator*(const CPlot2D &A, const float b)
{
    return CPlot2D(A.x * b, A.y * b);
}
inline CPlot2D operator*(const float a, const CPlot2D &B)
{
    return CPlot2D(B.x * a, B.y * a);
}
inline CPlot2D operator/(const CPlot2D &A, const float b)
{
    return CPlot2D(A.x / b, A.y / b);
}
inline BOOL operator==(const CPlot2D &A, const CPlot2D &B)
{
    return ((A.x == B.x) && (A.y == B.y));
}
inline float fabs(const CPlot2D &A)
{
    return static_cast<float>(hypot(A.x, A.y));
}
/////////////////////////////////////////////////////////////////////////////////////
struct CPlot3D : public DirectX::XMFLOAT3
{
public:
    CPlot3D()
        : DirectX::XMFLOAT3()
    {
    }
    explicit CPlot3D(float x)
        : DirectX::XMFLOAT3(x, x, x)
    {
    }
    CPlot3D(float x, float y, float z)
        : DirectX::XMFLOAT3(x, y, z)
    {
    }
    float GetX() const { return x; }
    float GetY() const { return y; }
    float GetZ() const { return z; }

    void SetPlot(const float fpX, const float fpY, const float fpZ)
    {
        x = fpX;
        y = fpY;
        z = fpZ;
    }
    void SetPlot(const CPlot3D &plot)
    {
        x = plot.x;
        y = plot.y;
        z = plot.z;
    }

    void operator+=(const CPlot3D &A);
    void operator-=(const CPlot3D &A);
    void operator*=(const float a);
    void operator/=(const float a);

    friend CPlot3D operator-(const CPlot3D &A);
    friend CPlot3D operator+(const CPlot3D &A, const CPlot3D &B);
    friend CPlot3D operator-(const CPlot3D &A, const CPlot3D &B);
    friend CPlot3D operator^(const CPlot3D &A, const CPlot3D &B);
    friend float operator*(const CPlot3D &A, const CPlot3D &B);
    friend CPlot3D operator*(const CPlot3D &A, const float b);
    friend CPlot3D operator*(const float a, const CPlot3D &B);
    friend CPlot3D operator/(const CPlot3D &A, const float b);
    friend BOOL operator==(const CPlot3D &A, const CPlot3D &B);
    bool operator!=(const CPlot3D &a) const { return a.x != x || a.y != y || a.z != z; }

    friend float fabs(const CPlot3D &a);
    friend float fabs2(const CPlot3D &a);
    friend float fabs(const CPlot3D &a, const CPlot3D &b)
    {
        return (float)sqrt(square(a.x - b.x) + square(a.y - b.y) + square(a.z - b.z));
    }
    friend float fabs2(const CPlot3D &a, const CPlot3D &b)
    {
        return square(a.x - b.x) + square(a.y - b.y) + square(a.z - b.z);
    }
    void Normalize();
    void Neg()
    {
        x = FlipSign(x);
        y = FlipSign(y);
        z = FlipSign(z);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
inline void CPlot3D::operator+=(const CPlot3D &A)
{
    x += A.x;
    y += A.y;
    z += A.z;
}
inline void CPlot3D::operator-=(const CPlot3D &A)
{
    x -= A.x;
    y -= A.y;
    z -= A.z;
}
inline void CPlot3D::operator*=(const float a)
{
    x *= a;
    y *= a;
    z *= a;
}
inline void CPlot3D::operator/=(const float a)
{
    x /= a;
    y /= a;
    z /= a;
}
inline CPlot3D operator-(const CPlot3D &A)
{
    return CPlot3D(-A.x, -A.y, -A.z);
}
inline CPlot3D operator+(const CPlot3D &A, const CPlot3D &B)
{
    return CPlot3D(A.x + B.x, A.y + B.y, A.z + B.z);
}
inline CPlot3D operator-(const CPlot3D &A, const CPlot3D &B)
{
    return CPlot3D(A.x - B.x, A.y - B.y, A.z - B.z);
}
inline CPlot3D operator^(const CPlot3D &A, const CPlot3D &B)
{
    return CPlot3D(A.y * B.z - B.y * A.z, A.z * B.x - B.z * A.x, A.x * B.y - B.x * A.y);
}
inline float operator*(const CPlot3D &A, const CPlot3D &B)
{
    return (A.x * B.x + A.y * B.y + A.z * B.z);
}
inline CPlot3D operator*(const CPlot3D &A, const float b)
{
    return CPlot3D(A.x * b, A.y * b, A.z * b);
}
inline CPlot3D operator*(const float a, const CPlot3D &B)
{
    return CPlot3D(B.x * a, B.y * a, B.z * a);
}
inline CPlot3D operator/(const CPlot3D &A, const float b)
{
    return CPlot3D(A.x / b, A.y / b, A.z / b);
}
inline BOOL operator==(const CPlot3D &A, const CPlot3D &B)
{
    return ((A.x == B.x) && (A.y == B.y) && (A.z == B.z));
}
inline float fabs(const CPlot3D &A)
{
    return static_cast<float>(sqrt(A.x * A.x + A.y * A.y + A.z * A.z));
}
inline float fabs2(const CPlot3D &A)
{
    return static_cast<float>(A.x * A.x + A.y * A.y + A.z * A.z);
}
inline void CPlot3D::Normalize()
{
    float fpLen = FSqrt(x * x + y * y + z * z);
    if (fpLen == 0)
        return;
    fpLen = 1 / fpLen;
    x *= fpLen;
    y *= fpLen;
    z *= fpLen;
}
inline float distanceXY2(const CPlot3D &A, const CPlot3D &B)
{
    return (A.x - B.x) * (A.x - B.x) + (A.y - B.y) * (A.y - B.y);
}
/////////////////////////////////////////////////////////////////////////////////////
const CPlot3D VNULL(0.0f, 0.0f, 0.0f);
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
class CTRect
{
public:
    union // left (minimal) x
    {
        TYPE left;
        TYPE x1;
        TYPE minx;
    };
    union // top (minimal) y
    {
        TYPE top;
        TYPE y1;
        TYPE miny;
    };
    union // right (maximal) x
    {
        TYPE right;
        TYPE x2;
        TYPE maxx;
    };
    union // bottom (maximal) y
    {
        TYPE bottom;
        TYPE y2;
        TYPE maxy;
    };

public:
    TYPE Width() const { return (maxx - minx); }
    TYPE Height() const { return (maxy - miny); }
    const CPlot2D &TopLeft() const { return *reinterpret_cast<CPlot2D *>(&minx); }
    const CPlot2D &BottomRight() const { return *reinterpret_cast<CPlot2D *>(&maxx); }

    void SetRectEmpty() { minx = miny = maxx = maxy = 0; }
    BOOL IsRectEmpty() const { return ((minx == maxx) && (miny == maxy)); }
};
/////////////////////////////////////////////////////////////////////////////////////
class CBox
{
public:
    CPlot3D tln, brf; // top-left-near and bottom-right-far
public:
    CBox();
    CBox(const CBox &box);

    void operator=(const CBox &box);
    void operator|=(const CBox &box);

    friend CBox operator+(const CPlot3D &plot);
};
/////////////////////////////////////////////////////////////////////////////////////
inline CBox::CBox()
{
}
inline void CBox::operator=(const CBox &box)
{
    tln.SetPlot(box.tln);
    brf.SetPlot(box.brf);
}
inline CBox::CBox(const CBox &box)
{
    (*this) = box;
}
inline void CBox::operator|=(const CBox &box)
{
    if (box.tln.x < tln.x)
        tln.x = box.tln.x;
    if (box.tln.y < tln.y)
        tln.y = box.tln.y;
    if (box.tln.z < tln.z)
        tln.z = box.tln.z;

    if (box.brf.x > brf.x)
        brf.x = box.brf.x;
    if (box.brf.y > brf.y)
        brf.y = box.brf.y;
    if (box.brf.z > brf.z)
        brf.z = box.brf.z;
}
inline CBox operator+(const CBox &box, const CPlot3D &plot)
{
    CBox ret;
    ret.tln = box.tln + plot;
    ret.brf = box.brf + plot;
    return ret;
}
/////////////////////////////////////////////////////////////////////////////////////
// struct SXYZ4
// special data type to store Pentium II Katmai optimized data for 3D transformation
struct SXYZ4
{
    float x[4];
    float y[4];
    float z[4];
};
/////////////////////////////////////////////////////////////////////////////////////
// struct SXYZW4
// special data type to store Pentium II Katmai optimized 3D data
struct SXYZW4
{
    float x[4];
    float y[4];
    float z[4];
    float w[4];
};
/////////////////////////////////////////////////////////////////////////////////////
// struct SXYZW
// special data type to store homogeneus plot coords
struct SXYZW : public DirectX::XMFLOAT4
{
public:
    SXYZW() = default;
    SXYZW(float fpX, float fpY, float fpZ, float fpW)
    {
        x = fpX;
        y = fpY;
        z = fpZ;
        w = fpW;
    }
    const SXYZW &operator=(const CPlot3D &arg)
    {
        x = arg.x;
        y = arg.y;
        z = arg.z;
        w = 1;
        return *this;
    }
    operator DirectX::XMVECTOR()
    {
        DirectX::XMVECTOR res = { x, y, z, w };
        return res;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
#pragma pack()
/////////////////////////////////////////////////////////////////////////////////////
#pragma pack(2)
/////////////////////////////////////////////////////////////////////////////////////
// struct SVertexComponents
// contain indices from 'plots', 'normales' and 'mappings' arrays to recompose D3DTLVERTEX array
struct SVertexComponents
{
    WORD gv; // geometry vertex index
    WORD nv; // normale vertex index
    WORD tv; // texsture vertex index
};
/////////////////////////////////////////////////////////////////////////////////////
struct SGvNvComponents
{
    WORD gv; // geometry vertex index
    WORD nv; // normale vertex index
};
/////////////////////////////////////////////////////////////////////////////////////
#pragma pack()
/////////////////////////////////////////////////////////////////////////////////////
#pragma pack(4)
/////////////////////////////////////////////////////////////////////////////////////
// struct SHMatrix
// homogeneous matrix (4x4) for any type of transformation
struct SHMatrix
{
public:
    float _11, _12, _13, _14; //
    float _21, _22, _23, _24; //
    float _31, _32, _33, _34; //
    float _41, _42, _43, _44; //
public:
    const CPlot3D &GetXAxis() const { return *reinterpret_cast<const CPlot3D *>(&_11); }
    const CPlot3D &GetYAxis() const { return *reinterpret_cast<const CPlot3D *>(&_21); }
    const CPlot3D &GetZAxis() const { return *reinterpret_cast<const CPlot3D *>(&_31); }
    const SXYZW &GetXHAxis() const { return *reinterpret_cast<const SXYZW *>(&_11); }
    const SXYZW &GetYHAxis() const { return *reinterpret_cast<const SXYZW *>(&_21); }
    const SXYZW &GetZHAxis() const { return *reinterpret_cast<const SXYZW *>(&_31); }
    const SXYZW &GetWHAxis() const { return *reinterpret_cast<const SXYZW *>(&_41); }
    // matrix-matrix and matrix-vertex operators
    friend SHMatrix operator*(const SHMatrix &a, const SHMatrix &b);
    void Multiply(const SHMatrix &a, const SHMatrix &b);
    CPlot3D RotateVector(const CPlot3D &pt) const;
    CPlot3D RotateHVector(const CPlot3D &pt) const;
    SXYZW Rotate4Vector(const CPlot3D &pt) const;
    void RotateVector(const CPlot3D &pt, CPlot3D &result) const;
    void RotateHVector(const CPlot3D &pt, CPlot3D &result) const;
    void Rotate4Vector(const CPlot3D &pt, SXYZW &result) const;
    BOOL HomogeneousInverse(const SHMatrix &m);
    CPlot3D GetTranslation() const { return CPlot3D(_14, _24, _34); }
    // matrix initialization
    friend void Zero(SHMatrix &m);     // fill matrix with '0'
    friend void Identity(SHMatrix &m); // fill matrix with '0', but main diagonal with '1'
    friend void Transpose(SHMatrix &m);
};

inline DirectX::XMMATRIX sh_to_xm(SHMatrix mat)
{
    DirectX::XMMATRIX res;
    memcpy(&res, &mat, sizeof(SHMatrix));
    return res;
}

inline DirectX::XMMATRIX sh_to_xm(SHMatrix &&mat)
{
    DirectX::XMMATRIX res;
    memmove(&res, &mat, sizeof(SHMatrix));
    return res;
    // return reinterpret_cast<DirectX::XMMATRIX>(mat);
    // return (DirectX::XMMATRIX*)(&mat);
}

/////////////////////////////////////////////////////////////////////////////////////
inline SHMatrix operator*(const SHMatrix &a, const SHMatrix &b)
{
    SHMatrix ret = {};

    ret._11 = a._11 * b._11 + a._12 * b._21 + a._13 * b._31 + a._14 * b._41;
    ret._12 = a._11 * b._12 + a._12 * b._22 + a._13 * b._32 + a._14 * b._42;
    ret._13 = a._11 * b._13 + a._12 * b._23 + a._13 * b._33 + a._14 * b._43;
    ret._14 = a._11 * b._14 + a._12 * b._24 + a._13 * b._34 + a._14 * b._44;

    ret._21 = a._21 * b._11 + a._22 * b._21 + a._23 * b._31 + a._24 * b._41;
    ret._22 = a._21 * b._12 + a._22 * b._22 + a._23 * b._32 + a._24 * b._42;
    ret._23 = a._21 * b._13 + a._22 * b._23 + a._23 * b._33 + a._24 * b._43;
    ret._24 = a._21 * b._14 + a._22 * b._24 + a._23 * b._34 + a._24 * b._44;

    ret._31 = a._31 * b._11 + a._32 * b._21 + a._33 * b._31 + a._34 * b._41;
    ret._32 = a._31 * b._12 + a._32 * b._22 + a._33 * b._32 + a._34 * b._42;
    ret._33 = a._31 * b._13 + a._32 * b._23 + a._33 * b._33 + a._34 * b._43;
    ret._34 = a._31 * b._14 + a._32 * b._24 + a._33 * b._34 + a._34 * b._44;

    ret._41 = a._41 * b._11 + a._42 * b._21 + a._43 * b._31 + a._44 * b._41;
    ret._42 = a._41 * b._12 + a._42 * b._22 + a._43 * b._32 + a._44 * b._42;
    ret._43 = a._41 * b._13 + a._42 * b._23 + a._43 * b._33 + a._44 * b._43;
    ret._44 = a._41 * b._14 + a._42 * b._24 + a._43 * b._34 + a._44 * b._44;

    return ret;
}
inline void SHMatrix::Multiply(const SHMatrix &a, const SHMatrix &b)
{
    _11 = a._11 * b._11 + a._12 * b._21 + a._13 * b._31 + a._14 * b._41;
    _12 = a._11 * b._12 + a._12 * b._22 + a._13 * b._32 + a._14 * b._42;
    _13 = a._11 * b._13 + a._12 * b._23 + a._13 * b._33 + a._14 * b._43;
    _14 = a._11 * b._14 + a._12 * b._24 + a._13 * b._34 + a._14 * b._44;

    _21 = a._21 * b._11 + a._22 * b._21 + a._23 * b._31 + a._24 * b._41;
    _22 = a._21 * b._12 + a._22 * b._22 + a._23 * b._32 + a._24 * b._42;
    _23 = a._21 * b._13 + a._22 * b._23 + a._23 * b._33 + a._24 * b._43;
    _24 = a._21 * b._14 + a._22 * b._24 + a._23 * b._34 + a._24 * b._44;

    _31 = a._31 * b._11 + a._32 * b._21 + a._33 * b._31 + a._34 * b._41;
    _32 = a._31 * b._12 + a._32 * b._22 + a._33 * b._32 + a._34 * b._42;
    _33 = a._31 * b._13 + a._32 * b._23 + a._33 * b._33 + a._34 * b._43;
    _34 = a._31 * b._14 + a._32 * b._24 + a._33 * b._34 + a._34 * b._44;

    _41 = a._41 * b._11 + a._42 * b._21 + a._43 * b._31 + a._44 * b._41;
    _42 = a._41 * b._12 + a._42 * b._22 + a._43 * b._32 + a._44 * b._42;
    _43 = a._41 * b._13 + a._42 * b._23 + a._43 * b._33 + a._44 * b._43;
    _44 = a._41 * b._14 + a._42 * b._24 + a._43 * b._34 + a._44 * b._44;
}
inline void Zero(SHMatrix &m)
{
    MemSetDWord(reinterpret_cast<DWORD *>(&m._11), 0, 16);
}
inline void Identity(SHMatrix &m)
{
    MemSetDWord(reinterpret_cast<DWORD *>(&m._11), 0, 16);
    m._11 = m._22 = m._33 = m._44 = 1.0f;
}
inline void Transpose(SHMatrix &m)
{
    float tmp = m._12;
    m._12 = m._21;
    m._21 = tmp;

    tmp = m._13;
    m._13 = m._31;
    m._31 = tmp;

    tmp = m._14;
    m._14 = m._41;
    m._41 = tmp;

    tmp = m._23;
    m._23 = m._32;
    m._32 = tmp;

    tmp = m._24;
    m._24 = m._42;
    m._42 = tmp;

    tmp = m._34;
    m._34 = m._43;
    m._43 = tmp;
}
/////////////////////////////////////////////////////////////////////////////////////
inline CPlot3D SHMatrix::RotateVector(const CPlot3D &pt) const
{
    return CPlot3D(_11 * pt.x + _12 * pt.y + _13 * pt.z, _21 * pt.x + _22 * pt.y + _23 * pt.z,
                   _31 * pt.x + _32 * pt.y + _33 * pt.z);
}
inline CPlot3D SHMatrix::RotateHVector(const CPlot3D &pt) const
{
    return CPlot3D(_11 * pt.x + _12 * pt.y + _13 * pt.z + _14,
                   _21 * pt.x + _22 * pt.y + _23 * pt.z + _24,
                   _31 * pt.x + _32 * pt.y + _33 * pt.z + _34);
}
inline SXYZW SHMatrix::Rotate4Vector(const CPlot3D &pt) const
{
    return SXYZW(_11 * pt.x + _12 * pt.y + _13 * pt.z + _14,
                 _21 * pt.x + _22 * pt.y + _23 * pt.z + _24,
                 _31 * pt.x + _32 * pt.y + _33 * pt.z + _34,
                 _41 * pt.x + _42 * pt.y + _43 * pt.z + _44);
}
/////////////////////////////////////////////////////////////////////////////////////
inline void SHMatrix::RotateVector(const CPlot3D &pt, CPlot3D &result) const
{
    result.x = _11 * pt.x + _12 * pt.y + _13 * pt.z;
    result.y = _21 * pt.x + _22 * pt.y + _23 * pt.z;
    result.z = _31 * pt.x + _32 * pt.y + _33 * pt.z;
}
inline void SHMatrix::RotateHVector(const CPlot3D &pt, CPlot3D &result) const
{
    result.x = _11 * pt.x + _12 * pt.y + _13 * pt.z + _14;
    result.y = _21 * pt.x + _22 * pt.y + _23 * pt.z + _24;
    result.z = _31 * pt.x + _32 * pt.y + _33 * pt.z + _34;
}
inline void SHMatrix::Rotate4Vector(const CPlot3D &pt, SXYZW &result) const
{
    result.x = _11 * pt.x + _12 * pt.y + _13 * pt.z + _14;
    result.y = _21 * pt.x + _22 * pt.y + _23 * pt.z + _24;
    result.z = _31 * pt.x + _32 * pt.y + _33 * pt.z + _34;
    result.w = _41 * pt.x + _42 * pt.y + _43 * pt.z + _44;
}
/////////////////////////////////////////////////////////////////////////////////////
inline BOOL SHMatrix::HomogeneousInverse(const SHMatrix &m)
{
    float det = m._11 * (m._22 * m._33 - m._23 * m._32) + m._21 * (m._13 * m._32 - m._12 * m._33) +
                m._31 * (m._12 * m._23 - m._13 * m._22);
    if (det == 0)
        return FALSE; // singular matrix found !
    det = 1.0f / det;
    // calculate reverse rotation
    _11 = (m._22 * m._33 - m._23 * m._32) * det;
    _12 = (m._13 * m._32 - m._12 * m._33) * det;
    _13 = (m._12 * m._23 - m._13 * m._22) * det;
    _14 = -(m._14 * _11 + m._24 * _12 + m._34 * _13);
    //
    _21 = (m._23 * m._31 - m._21 * m._33) * det;
    _22 = (m._11 * m._33 - m._13 * m._31) * det;
    _23 = (m._13 * m._21 - m._11 * m._23) * det;
    _24 = -(m._14 * _21 + m._24 * _22 + m._34 * _23);
    //
    _31 = (m._21 * m._32 - m._22 * m._31) * det;
    _32 = (m._12 * m._31 - m._11 * m._32) * det;
    _33 = (m._11 * m._22 - m._12 * m._21) * det;
    _34 = -(m._14 * _31 + m._24 * _32 + m._34 * _33);
    //
    _41 = _42 = _43 = 0.0f;
    _44 = 1.0f;
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// struct SMatrix
// simply matrix, what stores main minor of SHMatrix (rotation and scaling capabilities)
struct SMatrix
{
public:
    float _11, _12, _13; //
    float _21, _22, _23; //
    float _31, _32, _33; //
public:
    // matrix-matrix and matrix-vertex operators
    friend SMatrix operator*(const SMatrix &a, const SMatrix &b);
    void Multiply(const SMatrix &a, const SMatrix &b);
    // matrix initialization
    friend void Zero(SMatrix &m);     // fill matrix with '0'
    friend void Identity(SMatrix &m); // fill matrix with '0', but main diagonal with '1'
};
/////////////////////////////////////////////////////////////////////////////////////
inline SMatrix operator*(const SMatrix &a, const SMatrix &b)
{
    SMatrix ret = {};

    ret._11 = a._11 * b._11 + a._12 * b._21 + a._13 * b._31;
    ret._12 = a._11 * b._12 + a._12 * b._22 + a._13 * b._32;
    ret._13 = a._11 * b._13 + a._12 * b._23 + a._13 * b._33;

    ret._21 = a._21 * b._11 + a._22 * b._21 + a._23 * b._31;
    ret._22 = a._21 * b._12 + a._22 * b._22 + a._23 * b._32;
    ret._23 = a._21 * b._13 + a._22 * b._23 + a._23 * b._33;

    ret._31 = a._31 * b._11 + a._32 * b._21 + a._33 * b._31;
    ret._32 = a._31 * b._12 + a._32 * b._22 + a._33 * b._32;
    ret._33 = a._31 * b._13 + a._32 * b._23 + a._33 * b._33;

    return ret;
}
inline void SMatrix::Multiply(const SMatrix &a, const SMatrix &b)
{
    _11 = a._11 * b._11 + a._12 * b._21 + a._13 * b._31;
    _12 = a._11 * b._12 + a._12 * b._22 + a._13 * b._32;
    _13 = a._11 * b._13 + a._12 * b._23 + a._13 * b._33;

    _21 = a._21 * b._11 + a._22 * b._21 + a._23 * b._31;
    _22 = a._21 * b._12 + a._22 * b._22 + a._23 * b._32;
    _23 = a._21 * b._13 + a._22 * b._23 + a._23 * b._33;

    _31 = a._31 * b._11 + a._32 * b._21 + a._33 * b._31;
    _32 = a._31 * b._12 + a._32 * b._22 + a._33 * b._32;
    _33 = a._31 * b._13 + a._32 * b._23 + a._33 * b._33;
}
inline void Zero(SMatrix &m)
{
    MemSetDWord(reinterpret_cast<DWORD *>(&m._11), 0, 9);
}
inline void Identity(SMatrix &m)
{
    MemSetDWord(reinterpret_cast<DWORD *>(&m._11), 0, 9);
    m._11 = m._22 = m._33 = 1.0f;
}
/////////////////////////////////////////////////////////////////////////////////////
#pragma pack()
/////////////////////////////////////////////////////////////////////////////////////
const DWORD BV_SPHERE = 0x00000001;
const DWORD BV_BOX = 0x00000002;
const DWORD BV_BONE = 0x00000004;
const DWORD BV_PASS_MASK = 0x00000038;
const DWORD BV_PASS_SHIFT = 3;
const DWORD BV_OPACITY_MASK = 0x000003c0;
const DWORD BV_OPACITY_SHIFT = 6;
const DWORD BV_BRIDGE = 0x00000400;
const DWORD BV_IGNORE = 0x00000800;
/////////////////////////////////////////////////////////////////////////////////////
// константы проходимости объектов и других модификаторов
const int AI_PASS_NO = 0;
const int AI_PASS_HARD = 1;
const int AI_PASS_EASY = 2;
const int AI_PASS_DANGER = 3;
const int AI_PASS_BRIDGE = 4;
// константы прозрачности объектов
// цифры - степень прозрачности при толщине 5m
const int AI_OPACITY_100 = 0; // opaque
const int AI_OPACITY_50 = 1;  // 50% of object is opaque
const int AI_OPACITY_25 = 2;
const int AI_OPACITY_0 = 3; // object is transparent
/////////////////////////////////////////////////////////////////////////////////////
struct SBoundingVolume
{
public:
    DWORD dwFlags;
    CPlot3D ptCenter;
    float fpRadius;
    CPlot3D ptMin;
    CPlot3D ptMax;
    CPlot3D ptBone;

public:
    SBoundingVolume() { memset(this, 0, sizeof(*this)); }
    // checks
    DWORD IsSphere() const { return dwFlags & BV_SPHERE; }
    DWORD IsBox() const { return dwFlags & BV_BOX; }
    //
    void SetCenter(const CPlot3D &cp) { ptCenter = cp; }
    void SetRadius(const float rd)
    {
        fpRadius = rd;
        dwFlags |= BV_SPHERE;
    }
    void SetBox(const CPlot3D &ptMin, const CPlot3D &ptMax)
    {
        this->ptMin = ptMin;
        this->ptMax = ptMax;
        dwFlags |= BV_BOX;
    }
    void SetBone(const CPlot3D &ptNewBone)
    {
        ptBone = ptNewBone;
        dwFlags |= BV_BONE;
    }
    // some flags functions
    int GetPassability() const { return (dwFlags & BV_PASS_MASK) >> BV_PASS_SHIFT; }
    void SetPassability(int a) { dwFlags |= (a << BV_PASS_SHIFT) & BV_PASS_MASK; }
    int GetOpacity() const { return (dwFlags & BV_OPACITY_MASK) >> BV_OPACITY_SHIFT; }
    void SetOpacity(int a) { dwFlags |= (a << BV_OPACITY_SHIFT) & BV_OPACITY_MASK; }
    BOOL IsBridge() const { return (dwFlags & BV_BRIDGE) != 0; }
    void SetBridgeFlag(BOOL bBridge)
    {
        if (bBridge)
            dwFlags |= BV_BRIDGE;
        else
            dwFlags &= ~BV_BRIDGE;
    }
    BOOL IsIgnored() const { return (dwFlags & BV_IGNORE) != 0; }
    void SetIgnore(bool bIgnore)
    {
        if (bIgnore)
            dwFlags |= BV_IGNORE;
        else
            dwFlags &= ~BV_IGNORE;
    }
};
typedef SBoundingVolume *LPBOUNDINGVOLUME;
/////////////////////////////////////////////////////////////////////////////////////
typedef CSimplyArray<SXYZ4, SXYZ4> CXYZ4Array;
typedef CSimplyArray<SXYZW4, SXYZW4> CXYZW4Array;
typedef CSimplyArray<SXYZW, SXYZW> CXYZWArray;
typedef CSimplyArray<CPlot3D, CPlot3D> CPlot3DArray;
typedef CSimplyArray<CPlot2D, CPlot2D> CPlot2DArray;
typedef CSimplyArray<SGvNvComponents, SGvNvComponents> CGvNvComponentsArray;
typedef CSimplyArray<SVertexComponents, SVertexComponents> CVertexComponentsArray;
