#pragma once

/**********************************************************************
 *<
        FILE: Lightning.H

        DESCRIPTION:  Lightning effect

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

#include "plot.h"
#include "gamebase.h"
#include "BaseManager.h"
#include "TextureDescriptor.h"

class CTexture;
// --------------------------------------------------------------------------------------------
// количество узлов молнии, должно быть кратно 4-м
// --------------------------------------------------------------------------------------------
const int N_MAX_LIGHTNING_SEGMENTS = 400;
const int N_LIGHTNING_SMALL_THREADS = 6;

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
struct SLightningFunc
{
    float data[N_MAX_LIGHTNING_SEGMENTS];
    void CreateRandom(int nStartHarm, int nFinishHarm, float fpStartPower, float fpFinishPower);
    void CreateLightning(BOOL bHighFreq);
    float &operator[](int i)
    {
        ASSERT(i >= 0 && i < N_MAX_LIGHTNING_SEGMENTS);
        return data[i];
    }
    void Load(CSave &file);
    void Store(CSave &file);
};

// --------------------------------------------------------------------------------------------
// одна случайная функция, описывается как линейная комбинация из 2-х заранее рассчитанных
// случайных функций с заданными распределениями
// --------------------------------------------------------------------------------------------
struct SLightningLeafFunc
{
    int nIdx[2];     // индексы составляющих функций
    float fpKoef[2]; // коэффициенты с которыми эти фунции входят в результат

    void CreateRandom(float fpAmpl);
};

// --------------------------------------------------------------------------------------------
// одна линия от молнии
// --------------------------------------------------------------------------------------------
struct SLightningLeaf
{
    CPlot3D src, XDir, YDir, mainDir;
    SLightningLeafFunc randX[3], randY[3];
    float fpCreateTime;
    CXYZWArray
        points; // количество точек в линии молнии не должно превосходить N_MAX_LIGHTNING_SEGMENTS
    float fpWidth;

    void CalcXYFunctions(float fpTime, SLightningFunc &funcX, SLightningFunc &funcY, float fpAmpl);
    void SetNewSrcDst(const CPlot3D &ptSrc, const CPlot3D &ptDst);
    void CreateLeaf(const CPlot3D &ptSrc, const CPlot3D &ptDst, float fpTime, float fpAmpl);
    // получить среднюю точку последней ветви длиной nLeng в позиции fpFrac
    CPlot3D GetSrcCoord(float fpFrac);
    CPlot3D GetDst() { return src + mainDir * float(GetLeng()); }
    // рассчитать молнию на момент fpTime
    void CalcLeafPoints(float fpTime, float fpAmpl);
    int GetLeng() { return points.GetSize(); }
    float GetLineLeng() { return GetLeng() * fabs(mainDir); }
};

// --------------------------------------------------------------------------------------------
// Молния
// --------------------------------------------------------------------------------------------
class CLightning : public CGameBase
{
protected:
    CTextureDescriptor pTexture, pTextureBall;
    SBoundingVolume bv;
    SLightningLeaf leaf;
    SLightningLeaf leafSmall[N_LIGHTNING_SMALL_THREADS];
    float fpSmallPlaces[N_LIGHTNING_SMALL_THREADS];
    float fpLWidth;
    float fpFinalWidth;

    float GetAmpl();
    void BuildBoundingVolume();
    void CreateNewLightningPoints();
    void AddLeafToBV(SLightningLeaf &lf);
    void DrawBall(CRenderer *pTarget, const CPlot3D &ptWhere, int nPhase, float fpSize);
    void DrawLeaf(CRenderer *pTarget, SLightningLeaf &leaf, int nClipFlag);

public:
    void Clear();
    CLightning() { Clear(); }
    ~CLightning() { Clear(); }
    virtual BOOL Create();
    virtual void Destroy() { Clear(); }
    BOOL Draw(CRenderer *pTarget);
    void CreateLightning(const CPlot3D &ptNSrc, const CPlot3D &ptNDst, float fpNWidth, BYTE bColor);
    void MoveLightning(const CPlot3D &ptNSrc, const CPlot3D &ptNDst);

    virtual LPCSTR GetGameClassName() const { return "CLightning"; };
    virtual EClassIDs WhoAmI() const { return HPC_LIGHTNING; }
};

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CLightningManager : public CBaseManager
{
public:
    CList<CSmartPtr<CLightning>, CLightning *> lightnings; // to draw lightning at least once
    // Draw
    BOOL Draw(CRenderer *lpTarget);
    virtual BOOL Create(CWorld *lpWorld) { return CBaseManager::Create(lpWorld); }

    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CLightningManager"; };
    virtual EClassIDs WhoAmI() const { return HPC_LIGHTNINGMANAGER; }

protected:
    virtual BOOL Create();
    LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);
};
