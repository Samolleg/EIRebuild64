#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "ClassIDs.h"
#include "GameBase.h"
#include "Plot.h"
#include "Quaternion.h"
/////////////////////////////////////////////////////////////////////////////////////
const DWORD B3D_LAST_BIT = MAKE_LAST_BIT(CGB_LAST_BIT, -1);
/////////////////////////////////////////////////////////////////////////////////////
class CBase3D : public CGameBase
{
protected:
    CPlot3D cp, respectCP;           // position (general and respect)
    CQuaternion rotation, rotation1; // rotation (general and respect)
    SHMatrix placement;              // matrix, which position and rotation will be recalculated to
public:
    CBase3D();
    CBase3D(const CBase3D &base);
    virtual ~CBase3D();
    // assignment operator
    virtual const CBase3D &operator=(const CBase3D &base);
    // central point and quaternion operations
    void SetCP(const CPlot3D &cp);
    void SetRespectCP(const CPlot3D &respectCP);
    void SetOrientation(const CQuaternion &q);
    void SetOrientation(const CPlot3D &axisZ);
    void SetOrientationZ(const CPlot3D &axisZ);
    void SetRespectOrientation(const CQuaternion &q) { rotation1 = q; }
    void OrientInYDirection(const CPlot3D &direction);
    void OrientInYDirection(const CPlot3D &direction, BOOL bSlow);
    void SetOrientationXY(const CPlot3D &direction);
    void SetPlacement(const CPlot3D &cp, const CQuaternion &q);
    const CPlot3D &GetCP() const;
    float GetCPX() const { return cp.x; }
    float GetCPY() const { return cp.y; }
    float GetCPZ() const { return cp.z; }
    const CPlot3D &GetRespectCP() const;
    const CQuaternion &GetOrientation() const;
    const CQuaternion &GetRespectOrientation() const;
    // placement preparation and work with it
    void CreateCurrentPlacement();
    void CreateCurrentRespectPlacement();
    void CreateRespectPlacement(const CQuaternion &rot, const CPlot3D &pos);
    void CreateRespectPlacement(const CQuaternion &rot);
    void ChangeCurrentPlacementPosition();
    void ChangeCurrentRespectPlacementPosition();
    const SHMatrix &GetPlacement() const { return placement; }
    void GetPlacement(SHMatrix &matrix) const { matrix = placement; }
    CPlot3D RotateVector(const CPlot3D &pt);
    CPlot3D RotateHVector(const CPlot3D &pt);
    //
    void PrepareQuaternion(const CQuaternion &A);
    //
    void PrepareD3DRotateMatrix(D3DMATRIX &matrix);
    // get axis functions
    CPlot3D GetXAxis() const { return rotation.RotateX(); }
    CPlot3D GetYAxis() const { return rotation.RotateY(); }
    CPlot3D GetZAxis() const { return rotation.RotateZ(); }
    // moving, shifting, scaling, rotating
    void Move(const CPlot3D &velocity); // moving : cp += velocity
    void Rotate(const CPlot3D &axis, const D3DVALUE angle);
    void Rotate(const D3DVALUE angle, const CPlot3D &axis);
    void Rotate(const CQuaternion &q);
    // drawing
    virtual void PrepareForDraw(const CQuaternion &q, const CPlot3D &sp);
    virtual void PrepareForDraw(const CQuaternion &q);
#if !defined(__NOMFC__)
    // service : save/load
    virtual void Save(CFile &file) const;
    virtual void Load(CFile &file);
#endif // !defined(__NOMFC__)
};
/////////////////////////////////////////////////////////////////////////////////////
typedef CBase3D *LPBASE3D;
/////////////////////////////////////////////////////////////////////////////////////
inline CBase3D::CBase3D()
    : CGameBase()
{
}
inline CBase3D::CBase3D(const CBase3D &base)
    : CGameBase(base)
{
    (*this) = base;
}
inline CBase3D::~CBase3D()
{
}
/////////////////////////////////////////////////////////////////////////////////////
inline void CBase3D::SetCP(const CPlot3D &cp)
{
    this->cp = cp;
}
inline void CBase3D::SetRespectCP(const CPlot3D &respectCP)
{
    this->respectCP = respectCP;
}
inline void CBase3D::SetOrientation(const CQuaternion &q)
{
    rotation = q;
}
inline void CBase3D::SetPlacement(const CPlot3D &cp, const CQuaternion &q)
{
    SetCP(cp);
    SetOrientation(q);
}
inline const CPlot3D &CBase3D::GetCP() const
{
    return cp;
}
inline const CPlot3D &CBase3D::GetRespectCP() const
{
    return respectCP;
}
inline const CQuaternion &CBase3D::GetOrientation() const
{
    return rotation;
}
inline const CQuaternion &CBase3D::GetRespectOrientation() const
{
    return rotation1;
}
/////////////////////////////////////////////////////////////////////////////////////
inline void CBase3D::Move(const CPlot3D &velocity)
{
    cp += velocity;
}
inline void CBase3D::Rotate(const CPlot3D &axis, const D3DVALUE angle)
{
    rotation *= CQuaternion(angle, axis);
}
inline void CBase3D::Rotate(const D3DVALUE angle, const CPlot3D &axis)
{
    rotation *= CQuaternion(angle, axis);
}
inline void CBase3D::Rotate(const CQuaternion &q)
{
    rotation *= q;
}
/////////////////////////////////////////////////////////////////////////////////////
inline void CBase3D::PrepareForDraw(const CQuaternion &q)
{
    rotation1 = q * rotation;
}
inline void CBase3D::PrepareQuaternion(const CQuaternion &q)
{
    PrepareForDraw(q);
}
inline void CBase3D::PrepareForDraw(const CQuaternion &q, const CPlot3D &sp)
{
    PrepareQuaternion(q);
    respectCP = q.Rotate(cp) + sp;
}
/////////////////////////////////////////////////////////////////////////////////////
inline void CBase3D::CreateCurrentPlacement()
{
    rotation.DecompEulerMatrix(placement);
    placement._14 = cp.x;
    placement._24 = cp.y;
    placement._34 = cp.z;
    placement._41 = placement._42 = placement._43 = 0.0f;
    placement._44 = 1.0f;
}
/////////////////////////////////////////////////////////////////////////////////////
inline void CBase3D::CreateCurrentRespectPlacement()
{
    rotation1.DecompEulerMatrix(placement);
    placement._14 = respectCP.x;
    placement._24 = respectCP.y;
    placement._34 = respectCP.z;
    placement._41 = placement._42 = placement._43 = 0.0f;
    placement._44 = 1.0f;
}
/////////////////////////////////////////////////////////////////////////////////////
inline void CBase3D::CreateRespectPlacement(const CQuaternion &rot, const CPlot3D &pos)
{
    rotation1 = rot * rotation;
    respectCP = rotation1.Rotate(cp) + pos;
}
/////////////////////////////////////////////////////////////////////////////////////
inline void CBase3D::CreateRespectPlacement(const CQuaternion &rot)
{
    rotation1 = rot * rotation;
}
/////////////////////////////////////////////////////////////////////////////////////
inline void CBase3D::ChangeCurrentPlacementPosition()
{
    placement._14 = cp.x;
    placement._24 = cp.y;
    placement._34 = cp.z;
}
inline void CBase3D::ChangeCurrentRespectPlacementPosition()
{
    placement._14 = respectCP.x;
    placement._24 = respectCP.y;
    placement._34 = respectCP.z;
}
/////////////////////////////////////////////////////////////////////////////////////
inline CPlot3D CBase3D::RotateVector(const CPlot3D &pt)
{
    return placement.RotateVector(pt);
}
inline CPlot3D CBase3D::RotateHVector(const CPlot3D &pt)
{
    return placement.RotateHVector(pt);
}
/////////////////////////////////////////////////////////////////////////////////////
