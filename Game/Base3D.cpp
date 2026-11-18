#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Base3D.h"
/////////////////////////////////////////////////////////////////////////////////////
const CBase3D &CBase3D::operator=(const CBase3D &base)
{
    ++placementRevision;
    cp = base.cp;
    respectCP = base.respectCP;
    rotation = base.rotation;
    rotation1 = base.rotation1;

    return *this;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CBase3D::Save( CFile& file ) const
// just save
void CBase3D::Save(CFile &file) const
{
    file.Write(&cp, sizeof(cp));
    rotation.Save(file);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CBase3D::Load( CFile& file )
// just load
void CBase3D::Load(CFile &file)
{
    ++placementRevision;
    file.Read(&cp, sizeof(cp));
    rotation.Load(file);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CBase3D::PrepareD3DRotateMatrix( D3DMATRIX& matrix )
// fills D3DMATRIX structure with data from 'rotation1' and 'respectCP'
void CBase3D::PrepareD3DRotateMatrix(D3DMATRIX &matrix)
{
    Identity(*reinterpret_cast<SHMatrix *>(&matrix));
    rotation1.DecompEulerMatrix(matrix);
    matrix._41 = respectCP.GetX();
    matrix._42 = respectCP.GetY();
    matrix._43 = respectCP.GetZ();
}
/////////////////////////////////////////////////////////////////////////////////////
void CBase3D::SetOrientation(const CPlot3D &axisZ)
{
    CPlot3D z = -rotation.RotateY();
    CPlot3D dir = axisZ;
    CPlot3D axis;
    float angle;

    dir *= 1.0f / fabs(dir);
    axis = z ^ dir;
    if (fabs(axis) < 1e-5)
    {
        axis = CPlot3D(1, 0, 0);
        angle = 0.0f;
    }
    else
    {
        angle = z * dir;
        // correction to fit in [-1...1] range
        if (angle > 1)
            angle = 1;
        else if (angle < -1)
            angle = -1;
        //
        angle = static_cast<float>(acos(CorrectCosAngle(angle)));
    }

    angle *= 0.1f;

    Rotate(axis, angle);
}
/////////////////////////////////////////////////////////////////////////////////////
void CBase3D::SetOrientationZ(const CPlot3D &axisZ)
{
    CPlot3D z = rotation.RotateZ();
    CPlot3D dir = axisZ;
    CPlot3D axis;
    float angle;

    dir *= 1.0f / fabs(dir);
    axis = z ^ dir;
    if (fabs(axis) < 1e-5)
    {
        axis = CPlot3D(1, 0, 0);
        angle = 0.0f;
    }
    else
    {
        angle = z * dir;
        //
        axis *= 1.0f / fabs(axis);
        angle = static_cast<float>(acos(CorrectCosAngle(angle)));
    }

    Rotate(axis, angle);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CBase3D::OrientInYDirection( const CPlot3D& direction )
// orient 3d object in y direction with desired z normale
void CBase3D::OrientInYDirection(const CPlot3D &direction)
{
    CPlot3D axisX = rotation.RotateX();
    CPlot3D axisY = -rotation.RotateY();
    CPlot3D axisZ = rotation.RotateZ();
    CPlot3D dir1 = (axisZ ^ direction) ^ axisZ;

    dir1 *= 1.0f / fabs(dir1);

    CPlot3D axis = axisY ^ dir1;
    float angle;

    if (fabs(axis) < 1e-5)
    {
        axis = CPlot3D(1, 0, 0);
        angle = 0.0f;
    }
    else
    {
        angle = axisY * dir1;
        angle = static_cast<float>(acos(CorrectCosAngle(angle)));
    }

    Rotate(axis, angle);
}
/////////////////////////////////////////////////////////////////////////////////////
void CBase3D::OrientInYDirection(const CPlot3D &direction, BOOL bSlow)
{
    CPlot3D axisX = rotation.RotateX();
    CPlot3D axisY = -rotation.RotateY();
    CPlot3D axisZ = rotation.RotateZ();
    CPlot3D dir1 = (axisZ ^ direction) ^ axisZ;

    dir1 *= 1.0f / fabs(dir1);

    CPlot3D axis = axisY ^ dir1;
    float angle = 0;
    // calculate angle
    if (fabs(axis) < 1e-5)
        axis = CPlot3D(1, 0, 0);
    else
    {
        angle = axisY * dir1;
        angle = static_cast<float>(acos(CorrectCosAngle(angle)));
    }
    // minimize rotation angle
    if (angle > pi)
    {
        angle = static_cast<float>(2.0f * pi) - angle;
        axis = -axis;
    }
    // add inertia to the orientation
    if (bSlow)
        angle *= 0.4f;
    // rotate
    Rotate(axis, angle);
}
/////////////////////////////////////////////////////////////////////////////////////
void CBase3D::SetOrientationXY(const CPlot3D &direction)
{
    SetOrientation(CQuaternion(CPlot3D(0, 0, 1), atan2(direction.y, direction.x) + FP_PI2));
    /*
    CPlot3D ptYAxis( 0, -1, 0 );
    CPlot3D ptAxis = ptYAxis ^ direction;
    if ( fabs2(ptAxis) < 0.001 )
            SetOrientation( QNULL );
    else
    {
            ptAxis *= 1.0f / fabs( ptAxis );
            float fAngle = acos( CorrectCosAngle(ptYAxis * direction) );
            SetOrientation( CQuaternion(ptAxis, fAngle) );
    }
    */
}
/////////////////////////////////////////////////////////////////////////////////////
