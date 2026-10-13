#pragma once

#include "Plot.h"
/////////////////////////////////////////////////////////////////////////////////////
class CQuaternion
{
private:
    // main quaternion data
    float w;       // cos(theta/2)                                   <- theta is angle
    float x, y, z; // x*sin(theta/2), y*sin(theta/2), z*sin(theta/2) <- of the rotation
public:
    CQuaternion();
    CQuaternion(const float w, const float x, const float y, const float z);
    CQuaternion(float angle, const CPlot3D &pt);
    CQuaternion(const CPlot3D &pt, float angle);
    // some neccessary operators
    void operator*=(const CQuaternion &quat);
    void operator/=(const CQuaternion &quat);
    CQuaternion operator*(const float c) const;
    CQuaternion operator+(const CQuaternion &quat) const;
    CQuaternion operator-() const; // unary minus
    friend CQuaternion operator*(const float c, const CQuaternion &quat);
    friend CQuaternion operator*(const CQuaternion &A, const CQuaternion &B);
    friend CQuaternion operator/(const CQuaternion &A, const CQuaternion &B);
    float Norm() const;
    CQuaternion Inverse() const;
    CQuaternion UnitInverse() const;
    float Dot(const CQuaternion &quat) const;
    void GetComponents(float& outW, float& outX, float& outY, float& outZ) const {
        outW = w; outX = x; outY = y; outZ = z;
    }
    void ReverseAxis();  // reverse quaternion's axis : x = -x, y = -y, z = -z
    void ReverseAxisX(); // reverse quaternion's Y axis : y = -y
    void ReverseAxisY(); // reverse quaternion's Y axis : y = -y
    void ReverseAxisZ(); // reverse quaternion's Y axis : y = -y
    //
    void MinimizeRotationAngle();
    // mathematical functions
    CQuaternion Exp() const;
    CQuaternion Log() const;
    // interpolation
    // Spherical Linear intERPolation from 'p' to 'q' with coeff 'factor'
    void Slerp(const float factor, const CQuaternion &p, const CQuaternion &q);
    //  // setup for spherical quadratic interpolation
    //  void Intermediate( const CQuaternion& q0, const CQuaternion& q1, const CQuaternion& q2,
    //  CQuaternion& a, CQuaternion& b );
    //  // spherical quadratic interpolation
    //  void Squad( double t, const CQuaternion& p, const CQuaternion& a, const CQuaternion& b,
    //  const CQuaternion& q );
    // composition/decomposition
    void FromAxisAngle(const CPlot3D &axis, float angle);
    void DecompAxisAngle(CPlot3D &axis, float &angle) const;
    void FromEulerMatrix(const float R[3][3]);
    void DecompEulerMatrix(float R[3][3]) const;
    void DecompEulerMatrix(D3DMATRIX &matrix) const;
    void DecompEulerMatrix(SHMatrix &matrix) const;
    void DecompEulerMatrix(SMatrix &matrix) const;
    void DecompReversedEulerMatrix(D3DMATRIX &matrix) const;
    void DecompReversedEulerMatrix(SHMatrix &matrix) const;
    void DecompReversedEulerMatrix(SMatrix &matrix) const;
    // rotate vector via quaternion itself
    CPlot3D Rotate(const CPlot3D &r) const;
    void Rotate(const CPlot3D &vec, CPlot3D &result) const;
    CPlot3D RotateX() const;
    CPlot3D RotateY() const;
    CPlot3D RotateZ() const;
    void RotateX(CPlot3D &result) const;
    void RotateY(CPlot3D &result) const;
    void RotateZ(CPlot3D &result) const;
#if !defined(__NOMFC__)
    // save/load service
    void Save(CFile &file) const;
    void Load(CFile &file);
    void Load(LPBYTE &lpData);
#endif // !defined(__NOMFC__)
};
/////////////////////////////////////////////////////////////////////////////////////
#include "Quaternion.inl"
/////////////////////////////////////////////////////////////////////////////////////
const CQuaternion QNULL = CQuaternion(1, 0, 0, 0); //-V1043 who cares
typedef CSimplyArray<CQuaternion, CQuaternion> CQuaternionArray;
