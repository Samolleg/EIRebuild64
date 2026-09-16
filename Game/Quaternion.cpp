#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Tools.h"
#include "Quaternion.h"
/////////////////////////////////////////////////////////////////////////////////////
static const double epsilon = 1e-05; // cutoff for sin(angle) near zero
/////////////////////////////////////////////////////////////////////////////////////
CQuaternion::CQuaternion(float alpha, const CPlot3D &axis)
{
    alpha *= 0.5f;
    const float sinAlphaDivModule = float(sin(alpha)) / fabs(axis);

    w = float(cos(alpha));
    x = axis.GetX() * sinAlphaDivModule;
    y = axis.GetY() * sinAlphaDivModule;
    z = axis.GetZ() * sinAlphaDivModule;
}
/////////////////////////////////////////////////////////////////////////////////////
CQuaternion::CQuaternion(const CPlot3D &axis, float alpha)
{
    alpha *= 0.5f;
    const float sinAlphaDivModule = float(sin(alpha)) / fabs(axis);

    w = float(cos(alpha));
    x = axis.GetX() * sinAlphaDivModule;
    y = axis.GetY() * sinAlphaDivModule;
    z = axis.GetZ() * sinAlphaDivModule;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CQuaternion::operator/=( const CQuaternion& q )
// divide 'this' quaternion on the 'q'
// stage 1: reverse axis for 'q' ( q1 = reversed 'q' )
// stage 2: 'this' = 'q1' * 'this'
void CQuaternion::operator/=(const CQuaternion &q)
{
    CQuaternion q1 = q;
    q1.ReverseAxis();
    (*this) *= q1;
}
/////////////////////////////////////////////////////////////////////////////////////
// CQuaternion operator/( const CQuaternion& A, const CQuaternion& B )
// divide quaternion 'A' on quaternion 'B'
// stage 1: reverse axis for 'B' ( q1 = reversed 'B' )
// stage 2: return 'q1' * 'A'
CQuaternion operator/(const CQuaternion &A, const CQuaternion &B)
{
    CQuaternion q1 = B;
    q1.ReverseAxis();

    return q1 * A;
}
/////////////////////////////////////////////////////////////////////////////////////
// CQuaternion CQuaternion::Exp() const
// calculate exponent of the quaternion
CQuaternion CQuaternion::Exp() const
{
    // If q = A*(x*i+y*j+z*k) where (x,y,z) is unit length, then
    // exp(q) = cos(A)+sin(A)*(x*i+y*j+z*k).  If sin(A) is near zero,
    // use exp(q) = cos(A)+A*(x*i+y*j+z*k) since A/sin(A) has limit 1.
    double angle = sqrt(x * x + y * y + z * z);
    double sn = sin(angle);
    CQuaternion result;

    result.w = float(cos(angle));

    if (fabs(sn) >= epsilon)
    {
        float coeff = float(sn / angle);
        result.x = coeff * x;
        result.y = coeff * y;
        result.z = coeff * z;
    }
    else
    {
        result.x = x;
        result.y = y;
        result.z = z;
    }

    return result;
}
/////////////////////////////////////////////////////////////////////////////////////
// CQuaternion CQuaternion::Log() const
// calculates natural logarithm of the quaternion
CQuaternion CQuaternion::Log() const
{
    // If q = cos(A)+sin(A)*(x*i+y*j+z*k) where (x,y,z) is unit length, then
    // log(q) = A*(x*i+y*j+z*k).  If sin(A) is near zero, use log(q) =
    // sin(A)*(x*i+y*j+z*k) since sin(A)/A has limit 1.
    CQuaternion result;
    result.w = 0;

    if (fabs(w) < 1.0)
    {
        double angle = acos(w);
        double sn = sin(angle);
        if (fabs(sn) >= epsilon)
        {
            float coeff = float(angle / sn);
            result.x = coeff * x;
            result.y = coeff * y;
            result.z = coeff * z;
            return result;
        }
    }

    result.x = x;
    result.y = y;
    result.z = z;

    return result;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CQuaternion::Intermediate( const CQuaternion& q0, const CQuaternion& q1,
//    														const
//    CQuaternion& q2, CQuaternion& a, CQuaternion& b
//    )
//{
//  // assert:  q0, q1, q2 are unit quaternions
//  CQuaternion q0inv = q0.UnitInverse();
//  CQuaternion q1inv = q1.UnitInverse();
//  CQuaternion p0 = q0inv*q1;
//  CQuaternion p1 = q1inv*q2;
//  CQuaternion arg = 0.25*(p0.Log()-p1.Log());
//  CQuaternion marg = -arg;

//  a = q1*arg.Exp();
//  b = q1*marg.Exp();
//}
///////////////////////////////////////////////////////////////////////////////////////
// void CQuaternion::Squad( double t, const Quaternion& p, const Quaternion& a, const Quaternion& b,
// const Quaternion& q )
//{
//  *this = Slerp(2*t*(1-t),Slerp(t,p,q),Slerp(t,a,b));
//}
///////////////////////////////////////////////////////////////////////////////////////
// void CQuaternion::FromEulerMatrix( const float R[3][3] )
// create quaternion from Euler matrix
void CQuaternion::FromEulerMatrix(const float R[3][3])
{
    // Algorithm in Ken Shoemake's article in 1987 SIGGRAPH course notes
    // article "Quaternion Calculus and Fast Animation".
    float trace = R[0][0] + R[1][1] + R[2][2];
    float root;

    if (trace > 0.0)
    {
        // |w| > 1/2, may as well choose w > 1/2
        root = float(sqrt(trace + 1.0)); // 2w
        w = 0.5f * root;
        root = 0.5f / root; // 1/(4w)
        x = (R[1][2] - R[2][1]) * root;
        y = (R[2][0] - R[0][2]) * root;
        z = (R[0][1] - R[1][0]) * root;
    }
    else
    {
        // |w| <= 1/2
        static int next[3] = { 1, 2, 0 };
        int i = 0;
        if (R[1][1] > R[0][0])
            i = 1;
        if (R[2][2] > R[i][i])
            i = 2;
        int j = next[i];
        int k = next[j];

        root = float(sqrt(R[i][i] - R[j][j] - R[k][k] + 1.0f));
        float *quat[3] = { &x, &y, &z };
        *quat[i] = 0.5f * root;
        root = 0.5f / root;
        w = (R[j][k] - R[k][j]) * root;
        *quat[j] = (R[i][j] + R[j][i]) * root;
        *quat[k] = (R[i][k] + R[k][i]) * root;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CQuaternion::FromAxisAngle( const CPlot3D& axis, float angle )
// create quaternion from rotation axis and angle
void CQuaternion::FromAxisAngle(const CPlot3D &axis, float angle)
{
    angle *= 0.5f;
    const float sinAlphaDivModule = float(sin(angle)) / fabs(axis);

    w = float(cos(angle));
    x = axis.x * sinAlphaDivModule;
    y = axis.y * sinAlphaDivModule;
    z = axis.z * sinAlphaDivModule;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CQuaternion::Slerp( const float factor, const CQuaternion& p, const CQuaternion& q )
// Spherical Linear intERPolation between two quaternions (SLERP) (from 'p' to 'q' with coeff
// 'factor')
static const double FP_QUAT_DELTA = 0.0001; // difference at which to lerp instead of slerp
void CQuaternion::Slerp(const float factor, const CQuaternion &p, const CQuaternion &q)
{
    double omega, cosom, sinom;
    float scale0, scale1;
    CQuaternion q1(q);
    // use the dot product to get the cosine of the angle between the quaternions
    cosom = p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w;
    // adjust signs (if necessary)
    if (cosom < 0.0)
    {
        cosom = -cosom;
        q1.x = -q.x;
        q1.y = -q.y;
        q1.z = -q.z;
        q1.w = -q.w;
    }
    // calculate coefficients
    if ((1.0 - cosom) > FP_QUAT_DELTA) // standard case (slerp)
    {
        omega = acos(cosom);
        sinom = 1.0 / sin(omega);
        scale0 = float(sin((1.0 - factor) * omega) * sinom);
        scale1 = float(sin(factor * omega) * sinom);
    }
    else // "p" and "q" quaternions are very close. so we can do a linear interpolation
    {
        scale0 = 1.0f - factor;
        scale1 = factor;
    }
    // calculate final values
    x = scale0 * p.x + scale1 * q1.x;
    y = scale0 * p.y + scale1 * q1.y;
    z = scale0 * p.z + scale1 * q1.z;
    w = scale0 * p.w + scale1 * q1.w;
}
/////////////////////////////////////////////////////////////////////////////////////
/*
MatToQuat(float m[4][4], QUAT * quat)
{
  float  tr, s, q[4];
  int    i, j, k;

  int nxt[3] = {1, 2, 0};

  tr = m[0][0] + m[1][1] + m[2][2];

  // check the diagonal
  if ( tr > 0.0 ) {
    s = sqrt (tr + 1.0);
    quat->w = s / 2.0;
    s = 0.5 / s;
    quat->x = (m[1][2] - m[2][1]) * s;
    quat->y = (m[2][0] - m[0][2]) * s;
    quat->z = (m[0][1] - m[1][0]) * s;
        } else {
          // diagonal is negative
          i = 0;
          if (m[1][1] > m[0][0]) i = 1;
              if (m[2][2] > m[i][i]) i = 2;
            j = nxt[i];
            k = nxt[j];

            s = sqrt ((m[i][i] - (m[j][j] + m[k][k])) + 1.0);

              q[i] = s * 0.5;

            if (s != 0.0) s = 0.5 / s;

            q[3] = (m[j][k] - m[k][j]) * s;
            q[j] = (m[i][j] + m[j][i]) * s;
            q[k] = (m[i][k] + m[k][i]) * s;

          quat->x = q[0];
          quat->y = q[1];
          quat->z = q[2];
          quat->w = q[3];
  }
}
*/
