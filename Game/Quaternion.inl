/////////////////////////////////////////////////////////////////////////////////////
// constructors and operator=
inline CQuaternion::CQuaternion()
    : w(1)
    , x(0)
    , y(0)
    , z(0) {

    };
inline CQuaternion::CQuaternion(const float fpW, const float fpX, const float fpY, const float fpZ)
{
    w = fpW;
    x = fpX;
    y = fpY;
    z = fpZ;
}
/////////////////////////////////////////////////////////////////////////////////////
// axis reversions
inline void CQuaternion::ReverseAxis()
{
    x = -x;
    y = -y;
    z = -z;
}
inline void CQuaternion::ReverseAxisX()
{
    x = -x;
}
inline void CQuaternion::ReverseAxisY()
{
    y = -y;
}
inline void CQuaternion::ReverseAxisZ()
{
    z = -z;
}
/////////////////////////////////////////////////////////////////////////////////////
// rotations
inline CPlot3D CQuaternion::RotateX() const
{
    return CPlot3D(w * w - (x * x + y * y + z * z) + 2.0f * x * x, (z * w + x * y) * 2.0f,
                   (-y * w + x * z) * 2.0f);
}
inline CPlot3D CQuaternion::RotateY() const
{
    return CPlot3D((-z * w + y * x) * 2.0f, w * w - (x * x + y * y + z * z) + 2.0f * y * y,
                   (x * w + y * z) * 2.0f);
}
inline CPlot3D CQuaternion::RotateZ() const
{
    return CPlot3D((y * w + z * x) * 2.0f, (-x * w + z * y) * 2.0f,
                   w * w - (x * x + y * y + z * z) + 2.0f * z * z);
}
inline void CQuaternion::RotateX(CPlot3D &res) const
{
    res.x = w * w - (x * x + y * y + z * z) + 2.0f * x * x;
    res.y = (z * w + x * y) * 2.0f;
    res.z = (-y * w + x * z) * 2.0f;
}
inline void CQuaternion::RotateY(CPlot3D &res) const
{
    res.x = (-z * w + y * x) * 2.0f;
    res.y = w * w - (x * x + y * y + z * z) + 2.0f * y * y;
    res.z = (x * w + y * z) * 2.0f;
}
inline void CQuaternion::RotateZ(CPlot3D &res) const
{
    res.x = (y * w + z * x) * 2.0f;
    res.y = (-x * w + z * y) * 2.0f;
    res.z = w * w - (x * x + y * y + z * z) + 2.0f * z * z;
}
inline CPlot3D CQuaternion::Rotate(const CPlot3D &r) const
{
    const CPlot3D L(x, y, z);
    return (r * (w * w - L * L) + (2.0f * w) * (L ^ r) + (2.0f * (L * r)) * L);
}
inline void CQuaternion::Rotate(const CPlot3D &vec, CPlot3D &res) const
{
    const CPlot3D L(x, y, z);
    res = (vec * (w * w - L * L) + (2.0f * w) * (L ^ vec) + (2.0f * (L * vec)) * L);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CQuaternion::operator*=( const CQuaternion& A )
// quaternion's multiplication with assignment (this = A*this)
inline void CQuaternion::operator*=(const CQuaternion &A)
{
    float wtmp = A.w * w - (A.x * x + A.y * y + A.z * z);
    float xtmp = A.w * x + w * A.x + (A.y * z - A.z * y);
    float ytmp = A.w * y + w * A.y + (A.z * x - A.x * z);
    float ztmp = A.w * z + w * A.z + (A.x * y - A.y * x);
    w = wtmp;
    x = xtmp;
    y = ytmp;
    z = ztmp;
}
/////////////////////////////////////////////////////////////////////////////////////
// CQuaternion operator*( const CQuaternion& A, const CQuaternion& B )
// quaternion's multiplication
inline CQuaternion operator*(const CQuaternion &A, const CQuaternion &B)
{
    return CQuaternion(A.w * B.w - (A.x * B.x + A.y * B.y + A.z * B.z),
                       A.w * B.x + B.w * A.x + (A.y * B.z - A.z * B.y),
                       A.w * B.y + B.w * A.y + (A.z * B.x - A.x * B.z),
                       A.w * B.z + B.w * A.z + (A.x * B.y - A.y * B.x));
}
/////////////////////////////////////////////////////////////////////////////////////
// CQuaternion CQuaternion::operator*( float c ) const
// multiply quaternion by fp value (const function)
inline CQuaternion CQuaternion::operator*(const float c) const
{
    return CQuaternion(c * w, c * x, c * y, c * z);
}
/////////////////////////////////////////////////////////////////////////////////////
// CQuaternion operator*( float c, const CQuaternion& q )
// multiply quaternion by fp value
inline CQuaternion operator*(const float c, const CQuaternion &q)
{
    return CQuaternion(c * q.w, c * q.x, c * q.y, c * q.z);
}
/////////////////////////////////////////////////////////////////////////////////////
// CQuaternion CQuaternion::operator+( const CQuaternion& q ) const
// 'dot' addition
inline CQuaternion CQuaternion::operator+(const CQuaternion &q) const
{
    return CQuaternion(w + q.w, x + q.x, y + q.y, z + q.z);
}
/////////////////////////////////////////////////////////////////////////////////////
// CQuaternion CQuaternion::operator-() const
// signum inversion (unary minus)
inline CQuaternion CQuaternion::operator-() const
{
    return CQuaternion(-w, -x, -y, -z);
}
/////////////////////////////////////////////////////////////////////////////////////
// float CQuaternion::Dot( const CQuaternion& q ) const
// dot product (treat quaternion as 4-vector)
inline float CQuaternion::Dot(const CQuaternion &q) const
{
    return w * q.w + x * q.x + y * q.y + z * q.z;
}
/////////////////////////////////////////////////////////////////////////////////////
// float CQuaternion::Norm() const
// quaternion norm (length of 4D vector, represented by quaternion)
inline float CQuaternion::Norm() const
{
    return w * w + x * x + y * y + z * z;
}
/////////////////////////////////////////////////////////////////////////////////////
// inline CQuaternion CQuaternion::Inverse() const
// inverse quaternion
inline CQuaternion CQuaternion::Inverse() const
{
    float norm = Norm();

    if (norm > 0)
    {
        norm = 1.0f / norm;
        return CQuaternion(w * norm, -x * norm, -y * norm, -z * norm);
    }
    else
        return CQuaternion(1, 0, 0, 0);
}
/////////////////////////////////////////////////////////////////////////////////////
// inline CQuaternion CQuaternion::UnitInverse() const
// unit quaternion inverse
inline CQuaternion CQuaternion::UnitInverse() const
{
    return CQuaternion(w, -x, -y, -z);
}
/////////////////////////////////////////////////////////////////////////////////////
// inline void CQuaternion::DecompAxisAngle( CPlot3D& axis, float& angle ) const
// decomposing quaternion to axis and angle
inline void CQuaternion::DecompAxisAngle(CPlot3D &axis, float &angle) const
{
    // The quaternion representing the rotation is
    //   q = cos(A/2)+sin(A/2)*(x*i+y*j+z*k)
    float length2 = x * x + y * y + z * z;
    if (length2 > 1e-8f)
    {
        angle = 2.0f * float(acos(w));
        float invlen = 1.0f / float(sqrt(length2));
        axis.x = x * invlen;
        axis.y = y * invlen;
        axis.z = z * invlen;
    }
    else
    {
        // angle is 0 (mod 2*pi), so any axis will do
        angle = 0.0f;
        axis.x = 1.0f;
        axis.y = 0.0f;
        axis.z = 0.0f;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CQuaternion::DecompEulerMatrix( float R[3][3] ) const
// decomposing quaternion to Euler matrix
inline void CQuaternion::DecompEulerMatrix(float R[3][3]) const
{
    float tx = 2.0f * x;
    float ty = 2.0f * y;
    float tz = 2.0f * z;
    float twx = tx * w;
    float twy = ty * w;
    float twz = tz * w;
    float txx = tx * x;
    float txy = ty * x;
    float txz = tz * x;
    float tyy = ty * y;
    float tyz = tz * y;
    float tzz = tz * z;

    R[0][0] = 1.0f - (tyy + tzz);
    R[1][0] = txy - twz;
    R[2][0] = txz + twy;

    R[0][1] = txy + twz;
    R[1][1] = 1.0f - (txx + tzz);
    R[2][1] = tyz - twx;

    R[0][2] = txz - twy;
    R[1][2] = tyz + twx;
    R[2][2] = 1.0f - (txx + tyy);
}
/////////////////////////////////////////////////////////////////////////////////////
// inline void CQuaternion::DecompEulerMatrix( D3DMATRIX& matrix ) const
// decompose quaternion to Euler matrix
inline void CQuaternion::DecompEulerMatrix(D3DMATRIX &matrix) const
{
    float tx = 2.0f * x;
    float ty = 2.0f * y;
    float tz = 2.0f * z;
    float twx = tx * w;
    float twy = ty * w;
    float twz = tz * w;
    float txx = tx * x;
    float txy = ty * x;
    float txz = tz * x;
    float tyy = ty * y;
    float tyz = tz * y;
    float tzz = tz * z;

    matrix._11 = 1.0f - (tyy + tzz);
    matrix._21 = txy - twz;
    matrix._31 = txz + twy;

    matrix._12 = txy + twz;
    matrix._22 = 1.0f - (txx + tzz);
    matrix._32 = tyz - twx;

    matrix._13 = txz - twy;
    matrix._23 = tyz + twx;
    matrix._33 = 1.0f - (txx + tyy);
}
/////////////////////////////////////////////////////////////////////////////////////
// inline void CQuaternion::DecompEulerMatrix( SHMatrix& matrix ) const
// decompose quaternion to Euler matrix
inline void CQuaternion::DecompEulerMatrix(SHMatrix &matrix) const
{
    float tx = 2.0f * x;
    float ty = 2.0f * y;
    float tz = 2.0f * z;
    float twx = tx * w;
    float twy = ty * w;
    float twz = tz * w;
    float txx = tx * x;
    float txy = ty * x;
    float txz = tz * x;
    float tyy = ty * y;
    float tyz = tz * y;
    float tzz = tz * z;

    matrix._11 = 1.0f - (tyy + tzz);
    matrix._12 = txy - twz;
    matrix._13 = txz + twy;

    matrix._21 = txy + twz;
    matrix._22 = 1.0f - (txx + tzz);
    matrix._23 = tyz - twx;

    matrix._31 = txz - twy;
    matrix._32 = tyz + twx;
    matrix._33 = 1.0f - (txx + tyy);
}
/////////////////////////////////////////////////////////////////////////////////////
// inline void CQuaternion::DecompEulerMatrix( SMatrix& matrix ) const
// decompose quaternion to Euler matrix
inline void CQuaternion::DecompEulerMatrix(SMatrix &matrix) const
{
    float tx = 2.0f * x;
    float ty = 2.0f * y;
    float tz = 2.0f * z;
    float twx = tx * w;
    float twy = ty * w;
    float twz = tz * w;
    float txx = tx * x;
    float txy = ty * x;
    float txz = tz * x;
    float tyy = ty * y;
    float tyz = tz * y;
    float tzz = tz * z;

    matrix._11 = 1.0f - (tyy + tzz);
    matrix._12 = txy - twz;
    matrix._13 = txz + twy;

    matrix._21 = txy + twz;
    matrix._22 = 1.0f - (txx + tzz);
    matrix._23 = tyz - twx;

    matrix._31 = txz - twy;
    matrix._32 = tyz + twx;
    matrix._33 = 1.0f - (txx + tyy);
}
/////////////////////////////////////////////////////////////////////////////////////
// inline void CQuaternion::DecompReversedEulerMatrix( D3DMATRIX& matrix ) const
// decompose quaternion to reversed Euler matrix (reverse transform)
inline void CQuaternion::DecompReversedEulerMatrix(D3DMATRIX &matrix) const
{
    float tx = -2.0f * x;
    float ty = -2.0f * y;
    float tz = -2.0f * z;
    float twx = tx * w;
    float twy = ty * w;
    float twz = tz * w;
    float txx = -tx * x;
    float txy = -ty * x;
    float txz = -tz * x;
    float tyy = -ty * y;
    float tyz = -tz * y;
    float tzz = -tz * z;

    matrix._11 = 1.0f - (tyy + tzz);
    matrix._21 = txy - twz;
    matrix._31 = txz + twy;

    matrix._12 = txy + twz;
    matrix._22 = 1.0f - (txx + tzz);
    matrix._32 = tyz - twx;

    matrix._13 = txz - twy;
    matrix._23 = tyz + twx;
    matrix._33 = 1.0f - (txx + tyy);
}
/////////////////////////////////////////////////////////////////////////////////////
// inline void CQuaternion::DecompReversedEulerMatrix( SHMatrix& matrix ) const
// decompose quaternion to reversed Euler matrix (reverse transform)
inline void CQuaternion::DecompReversedEulerMatrix(SHMatrix &matrix) const
{
    float tx = -2.0f * x;
    float ty = -2.0f * y;
    float tz = -2.0f * z;
    float twx = tx * w;
    float twy = ty * w;
    float twz = tz * w;
    float txx = -tx * x;
    float txy = -ty * x;
    float txz = -tz * x;
    float tyy = -ty * y;
    float tyz = -tz * y;
    float tzz = -tz * z;

    matrix._11 = 1.0f - (tyy + tzz);
    matrix._12 = txy - twz;
    matrix._13 = txz + twy;

    matrix._21 = txy + twz;
    matrix._22 = 1.0f - (txx + tzz);
    matrix._23 = tyz - twx;

    matrix._31 = txz - twy;
    matrix._32 = tyz + twx;
    matrix._33 = 1.0f - (txx + tyy);
}
/////////////////////////////////////////////////////////////////////////////////////
// inline void CQuaternion::DecompReversedEulerMatrix( SMatrix& matrix ) const
// decompose quaternion to reversed Euler matrix (reverse transform)
inline void CQuaternion::DecompReversedEulerMatrix(SMatrix &matrix) const
{
    float tx = -2.0f * x;
    float ty = -2.0f * y;
    float tz = -2.0f * z;
    float twx = tx * w;
    float twy = ty * w;
    float twz = tz * w;
    float txx = -tx * x;
    float txy = -ty * x;
    float txz = -tz * x;
    float tyy = -ty * y;
    float tyz = -tz * y;
    float tzz = -tz * z;

    matrix._11 = 1.0f - (tyy + tzz);
    matrix._12 = txy - twz;
    matrix._13 = txz + twy;

    matrix._21 = txy + twz;
    matrix._22 = 1.0f - (txx + tzz);
    matrix._23 = tyz - twx;

    matrix._31 = txz - twy;
    matrix._32 = tyz + twx;
    matrix._33 = 1.0f - (txx + tyy);
}
/////////////////////////////////////////////////////////////////////////////////////
#if !defined(__NOMFC__)
// void CQuaternion::Save( CFile& file ) const
// save quaternion's data: w, x,  y, z
inline void CQuaternion::Save(CFile &file) const
{
    file.Write(this, 4 * sizeof(float));
}
/////////////////////////////////////////////////////////////////////////////////////
// void CQuaternion::Load( CFile& file )
// load quaternion's data: w, x,  y, z
inline void CQuaternion::Load(CFile &file)
{
    file.Read(this, 4 * sizeof(float));
}
/////////////////////////////////////////////////////////////////////////////////////
// void CQuaternion::Load( LPBYTE& lpData )
// load quaternion data (w, x, y, z) from memory
inline void CQuaternion::Load(LPBYTE &lpData)
{
    Copy16Bytes(this, lpData);
    lpData += 4 * sizeof(float);
}
#endif // !defined(__NOMFC__)
/////////////////////////////////////////////////////////////////////////////////////
// void CQuaternion::MinimizeRotationAngle()
// minimize rotatioon angle to fit in [0...pi]
inline void CQuaternion::MinimizeRotationAngle()
{
    if (w < 0)
    {
        w = -w;
        x = -x;
        y = -y;
        z = -z;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
