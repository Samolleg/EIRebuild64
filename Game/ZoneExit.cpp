#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "ZoneExit.h"

/////////////////////////////////////////////////////////////////////////////////////
void SZoneExit::Load(CSave &file)
{
    file.Read(fPassTime);
    file.Read(x1);
    file.Read(y1);
    file.Read(x2);
    file.Read(y2);
    file.Read(ex1);
    file.Read(ey1);
    file.Read(ex2);
    file.Read(ey2);
    file.Read(szZone);
    file.Read(nZoneEntrance);
    file.Read(fCameraRotate);

    bPersRotate = false;
    fPersRotate = 0;
    eangle = 0;
    if (file.GetVersion() < 104)
        return;
    file.Read(bPersRotate);
    file.Read(fPersRotate);
    file.Read(eangle);
}
/////////////////////////////////////////////////////////////////////////////////////
void SZoneExit::Store(CSave &file)
{
    file.Write(fPassTime);
    file.Write(x1);
    file.Write(y1);
    file.Write(x2);
    file.Write(y2);
    file.Write(ex1);
    file.Write(ey1);
    file.Write(ex2);
    file.Write(ey2);
    file.Write(szZone);
    file.Write(nZoneEntrance);
    file.Write(fCameraRotate);
    file.Write(bPersRotate);
    file.Write(fPersRotate);
    file.Write(eangle);
}
/////////////////////////////////////////////////////////////////////////////////////
bool SZoneExit::IsIn(float fpX, float fpY)
{
    if (eangle == 0)
        return (ex1 <= fpX && fpX <= ex2 && ey1 <= fpY && fpY <= ey2);
    float cx = (ex1 + ex2) * 0.5f, cy = (ey1 + ey2) * 0.5f;
    float tx, ty, kx = cos(eangle), ky = sin(eangle);
    tx = cx + (fpX - cx) * kx + (fpY - cy) * ky;
    ty = cy - (fpX - cx) * ky + (fpY - cy) * kx;
    return (ex1 <= tx && tx <= ex2 && ey1 <= ty && ty <= ey2);
}
/////////////////////////////////////////////////////////////////////////////////////
void SZoneExit::GetExitVertices(float *fpRes)
{
    float cx = (ex1 + ex2) * 0.5f, cy = (ey1 + ey2) * 0.5f;
    float ux = (ex2 - ex1) * 0.5f, uy = (ey1 - ey2) * 0.5f;
    float tx, ty, kx = cos(eangle), ky = sin(eangle);
    tx = cx + ux * kx - uy * ky;
    ty = cy + ux * ky + uy * kx;
    fpRes[0] = tx;
    fpRes[1] = ty;
    tx = cx - ux * kx - uy * ky;
    ty = cy - ux * ky + uy * kx;
    fpRes[2] = tx;
    fpRes[3] = ty;
    tx = cx - ux * kx + uy * ky;
    ty = cy - ux * ky - uy * kx;
    fpRes[4] = tx;
    fpRes[5] = ty;
    tx = cx + ux * kx + uy * ky;
    ty = cy + ux * ky - uy * kx;
    fpRes[6] = tx;
    fpRes[7] = ty;
}
/////////////////////////////////////////////////////////////////////////////////////
