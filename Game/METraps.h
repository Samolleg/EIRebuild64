#pragma once
#include "SaveSchema.h"

#include "RPGSystem.h"

// область, при попадании на который срабатывает MagicTrap
struct SMEArea
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    CPlot2D ptCenter;
    float fRadius;

    void GetRound(float &fpCenterX, float &fpCenterY, float &fpRadius)
    {
        fpCenterX = ptCenter.x;
        fpCenterY = ptCenter.y;
        fpRadius = fRadius;
    }
    BOOL IsInside(CPlot3D pos)
    {
        return sqr(pos.x - ptCenter.x) + sqr(pos.y - ptCenter.y) < sqr(fRadius);
    }
    void Load(CSave &) = delete;
    void Store(CSave &) = delete;
};

struct SMEMagicTrap
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    SMEMagicTrap()
        : nCastingInterval(15)
        , dwDiplomacy(0xffffffff)
    {
    }
    DWORD dwDiplomacy;
    CString szSpell;
    CPlot3D ptPosition;
    CAArray<SMEArea, SMEArea &> aAreas;
    CAArray<CPlot2D, CPlot2D &> aTargets;
    int nCastingInterval; // сколько сегментов между кастованиями
    bool bCastOnce;       // кастовать только один спелл за раз
};

struct SMELever
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    SMELever()
        : isDoor(false)
        , needGraphRecalc(false)
        , nCurrentState(0)
        , nTotalStates(2)
        , isCycled(false)
    {
    }
    SRPGScienceStats science;        // для определения того, какие научные воздействия корректны
    int nCurrentState, nTotalStates; // текущее состояние и общее количество состояний
    bool isDoor, needGraphRecalc;
    bool isCycled;
};
