#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "GlobalMap.h"
#include "TextManager.h"
#include "WorldClient.h"
/////////////////////////////////////////////////////////////////////////////////////
CGlobalMap theMap;
/////////////////////////////////////////////////////////////////////////////////////
SScenarioZone *CGlobalMap::GetZoneByName(LPCTSTR pszName)
{
    for (int i = 0; i < allZones.GetSize(); i++)
        if (allZones[i].szName.CompareNoCase(pszName) == 0)
            return &allZones[i];
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
SScenarioZone *CGlobalMap::GetZoneByMPRName(LPCTSTR pszName)
{
    for (int i = 0; i < allZones.GetSize(); i++)
        if (allZones[i].szTerrainName.CompareNoCase(pszName) == 0)
            return &allZones[i];
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
SScenarioAllod *CGlobalMap::GetAllodByName(LPCTSTR pszName)
{
    for (int i = 0; i < allAllods.GetSize(); i++)
        if (allAllods[i].szName.CompareNoCase(pszName) == 0)
            return &allAllods[i];
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
SScenarioAllod *CGlobalMap::GetAllodByMobName(LPCTSTR pszName)
{
    for (int i = 0; i < allZones.GetSize(); i++)
        if (allZones[i].szMobName.CompareNoCase(pszName) == 0)
            return GetAllodByName(allZones[i].szAllodName);
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CGlobalMap::Init()
{
    LoadZones("map.txt");
}
/////////////////////////////////////////////////////////////////////////////////////
bool CGlobalMap::LoadZones(const char *pszFileName)
{
    CStringArray szTmpArray;

    CTextManager *pTxtMng = theWorld.GetTextManager();
    if (!pTxtMng->GetText(pszFileName))
        return false;

    int i = allZones.GetSize() - 1, j = -1;
    while (pTxtMng->FindNextCommand())
    {
        pTxtMng->GetStringArray(szTmpArray);

        if (szTmpArray[0] == "#allod" && szTmpArray.GetSize() > 1)
        {
            int k;
            allAllods.SetSize((k = allAllods.GetSize()) + 1);
            allAllods[k].szName = szTmpArray[1];
            pTxtMng->GetStringArray(szTmpArray);
            allAllods[k].szArrowFigure1 = szTmpArray[0];
            pTxtMng->GetStringArray(szTmpArray);
            allAllods[k].szArrowFigure2 = szTmpArray[0];
            pTxtMng->GetStringArray(szTmpArray);
            allAllods[k].szObjFigure = szTmpArray[0];
            pTxtMng->GetStringArray(szTmpArray);
            allAllods[k].szBottomFigure = szTmpArray[0];
            pTxtMng->GetStringArray(szTmpArray);
            allAllods[k].szCampFigure = szTmpArray[0];
            continue;
        }

        if (szTmpArray[0] == "#zone" && szTmpArray.GetSize() > 3)
        {
            allZones.SetSize(allZones.GetSize() + 1);
            i++;
            j = -1;
            allZones[i].szName = szTmpArray[1];
            allZones[i].szAllodName = szTmpArray[2];
            if (SScenarioAllod *pA = GetAllodByName(szTmpArray[2]))
                pA->zones.Add(i);
            if (szTmpArray[3] == "game")
                allZones[i].eType = GM_REALZONE;
            else if (szTmpArray[3] == "brief")
                allZones[i].eType = GM_BASE;
            else if (szTmpArray[3] == "edge")
                allZones[i].eType = GM_BETWEENZONES;

            allZones[i].eWeatherType = GMW_RAIN;
            allZones[i].eSkyType = GMS_NORMAL;
            allZones[i].bRestrictCamera = false;
            allZones[i].bCageBriefings = false;
            continue;
        }
        if (i < 0)
            continue;
        if (szTmpArray[0] == "#res")
        {
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() > 1)
            {
                allZones[i].szTerrainName = szTmpArray[0];
                allZones[i].szMobName = szTmpArray[1];
            }
            continue;
        }
        if (szTmpArray[0] == "#camera")
        {
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() > 0)
                allZones[i].szCameraName = szTmpArray[0];
            continue;
        }
        if (szTmpArray[0] == "#maps")
        {
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() > 1)
            {
                allZones[i].nSizeX = atoi(szTmpArray[0]);
                allZones[i].nSizeY = atoi(szTmpArray[1]);
            }
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() > 1)
            {
                allZones[i].szMinimapTexture = szTmpArray[0];
                allZones[i].szMapTexture = szTmpArray[1];
            }
            continue;
        }
        if (szTmpArray[0] == "#figure")
        {
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() > 0)
                allZones[i].szFigureName = szTmpArray[0];
            continue;
        }
        if (szTmpArray[0] == "#weather")
        {
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() > 0)
            {
                if (szTmpArray[0] == "none")
                    allZones[i].eWeatherType = GMW_NONE;
                else if (szTmpArray[0] == "snow")
                    allZones[i].eWeatherType = GMW_SNOW;
                else if (szTmpArray[0] == "tornado")
                    allZones[i].eWeatherType = GMW_TORNADO;
                else
                    allZones[i].eWeatherType = GMW_RAIN;
            }
            continue;
        }
        if (szTmpArray[0] == "#cage")
        {
            allZones[i].bCageBriefings = true;
            continue;
        }
        if (szTmpArray[0] == "#restrict")
        {
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() > 2)
            {
                allZones[i].bRestrictCamera = true;
                allZones[i].fRestrictX = (float)atof(szTmpArray[0]);
                allZones[i].fRestrictY = (float)atof(szTmpArray[1]);
                allZones[i].fRadius = (float)atof(szTmpArray[2]);
            }
            continue;
        }
        if (szTmpArray[0] == "#sky")
        {
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() > 0)
            {
                if (szTmpArray[0] == "cave")
                    allZones[i].eSkyType = GMS_CAVE;
                else
                    allZones[i].eSkyType = GMS_NORMAL;
            }
            continue;
        }
        if (szTmpArray[0] == "#exit")
        {
            allZones[i].exits.SetSize(allZones[i].exits.GetSize() + 1);
            j++;
            allZones[i].exits[j].fCameraRotate = 0;
            allZones[i].exits[j].bPersRotate = false;
            allZones[i].exits[j].fPersRotate = 0;
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() > 1)
            {
                allZones[i].exits[j].szZone = szTmpArray[0];
                allZones[i].exits[j].nZoneEntrance = atoi(szTmpArray[1]) - 1;
            }
            continue;
        }
        if (j < 0)
            continue;
        if (szTmpArray[0] == "#deploy")
        {
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() > 3)
            {
                allZones[i].exits[j].x1 = allZones[i].exits[j].ex1 = (float)atof(szTmpArray[0]);
                allZones[i].exits[j].y1 = allZones[i].exits[j].ey1 = (float)atof(szTmpArray[1]);
                allZones[i].exits[j].x2 = allZones[i].exits[j].ex2 = (float)atof(szTmpArray[2]);
                allZones[i].exits[j].y2 = allZones[i].exits[j].ey2 = (float)atof(szTmpArray[3]);
                allZones[i].exits[j].eangle = 0;
            }
            if (szTmpArray.GetSize() > 4)
                allZones[i].exits[j].eangle = ToRadian((float)atof(szTmpArray[4]));
            continue;
        }
        if (szTmpArray[0] == "#remove")
        {
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() > 3)
            {
                allZones[i].exits[j].ex1 = (float)atof(szTmpArray[0]);
                allZones[i].exits[j].ey1 = (float)atof(szTmpArray[1]);
                allZones[i].exits[j].ex2 = (float)atof(szTmpArray[2]);
                allZones[i].exits[j].ey2 = (float)atof(szTmpArray[3]);
                allZones[i].exits[j].eangle = 0;
            }
            if (szTmpArray.GetSize() > 4)
                allZones[i].exits[j].eangle = ToRadian((float)atof(szTmpArray[4]));
            continue;
        }
        if (szTmpArray[0] == "#passtime")
        {
            pTxtMng->GetStringArray(szTmpArray);
            if (szTmpArray.GetSize() > 0)
                allZones[i].exits[j].fPassTime = (float)atof(szTmpArray[0]);
            continue;
        }
        if (szTmpArray[0] == "#view")
        {
            pTxtMng->GetStringArray(szTmpArray);
            szTmpArray.Add("0");
            allZones[i].exits[j].fCameraRotate = ToRadian((float)atof(szTmpArray[0]));
            continue;
        }
        if (szTmpArray[0] == "#deployangle")
        {
            allZones[i].exits[j].bPersRotate = true;
            pTxtMng->GetStringArray(szTmpArray);
            szTmpArray.Add("0");
            allZones[i].exits[j].fPersRotate = ToRadian((float)atof(szTmpArray[0]));
            continue;
        }
    }
    pTxtMng->CloseText();
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
