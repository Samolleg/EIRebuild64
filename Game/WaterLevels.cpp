#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "WaterLevels.h"
#include "Terrain.h"
/////////////////////////////////////////////////////////////////////////////////////
void CWaterLevels::Clear()
{
    levels.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWaterLevels::SetWaterLevel(int nObjectID, float fpFinalH, int nTime)
{
    if (nTime < 1)
        nTime = 1;
    int i = 0;
    for (; i < levels.GetSize(); i++)
    {
        if (levels[i].nObjectID == nObjectID)
        {
            levels[i].fpDH = (fpFinalH - levels[i].fpH) / nTime;
            levels[i].nSegments = nTime;
            return;
        }
    }
    levels.Add(SLevel());
    SLevel &l = levels[levels.GetSize() - 1];
    l.nObjectID = nObjectID;
    l.fpH = 0;
    l.fpDH = (fpFinalH - levels[i].fpH) / nTime;
    l.nSegments = nTime;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWaterLevels::Segment()
{
    bool bRv = false;
    for (int i = 0; i < levels.GetSize(); i++)
    {
        if (levels[i].nSegments > 0)
        {
            levels[i].nSegments--;
            levels[i].fpH += levels[i].fpDH;
            bRv = true;
        }
        else if (levels[i].fpH == 0)
        {
            levels.RemoveAt(i);
            i--;
        }
    }
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWaterLevels::UpdateTo(CSave &pkt)
{
    StoreCArray(levels, pkt);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWaterLevels::UpdateFrom(CSave &pkt, CTerrain *pTerrain)
{
    LoadCArray(levels, pkt);
    for (int i = 0; i < levels.GetSize(); i++)
        pTerrain->SetObjectHieght(levels[i].nObjectID, levels[i].fpH);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWaterLevels::Load(CSave &file)
{
    LoadCArray(levels, file);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWaterLevels::Store(CSave &file)
{
    StoreCArray(levels, file);
}
/////////////////////////////////////////////////////////////////////////////////////
