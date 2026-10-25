#include "StdAfx.h"
#include "SaveSchema.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "MapObjectServer.h"
#include "script.h"
#include "UnitServer.h"
#include "WorldServer.h"
#include "SpellEffect.h"
#include "ModRuntime.h"
#include "ModPackage.h"

/////////////////////////////////////////////////////////////////////////////////////
//---------------------------- Create / destroy
//------------------------------------------------------------
CMapObjectServer::CMapObjectServer()
{
    Create();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CMapObjectServer::~CMapObjectServer()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObjectServer::Create()
{
    if (CMapObjectSpecific::Create() == FALSE)
        return FALSE;
    rpgCommonStats.Clear();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// destroy all internal data structures
void CMapObjectServer::Destroy()
{
    rpgCommonStats.Clear();
    szNames.RemoveAll();

    CMapObjectSpecific::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObjectServer::LeaveMap()
{
    /*	CUnitList list;
            theWorldServer.aiMap.GetUnitsNear( ((CMapObject*)lpMapObject)->GetCP(), list, 1e30f );
            POSITION pos;
            pos = list.GetHeadPosition();
            while ( pos )
            {
                    CUnit *pUnit = list.GetNext( pos );
                    pUnit->SetFlag( MAPOBJ_MOVED );        // to rescan objects list for pUnit
                    ((CUnitServer*)pUnit->GetUnitSpecific())->logic.ForceRescan();
            }*/
}

/////////////////////////////////////////////////////////////////////////////////////
int CMapObjectServer::Segment()
{
    // segment all spell effects
    for (int i = 0; i < lpMapObject->spellEffects.GetSize(); i++)
    {
        if (lpMapObject->spellEffects[i].Segment() <= 0)
        {
            // remove effect
            int nEffectType = lpMapObject->spellEffects[i].nSpellID;
            lpMapObject->spellEffects.RemoveAt(i); // remove effect
            auto *mod = ModRuntime::Current();
            const int mode = mod ? mod->SpellAddon().Feature24() : 0;
            bool bStrengthChanged = (nEffectType == SPELL_ID_STRENGTHEN) ||
                                    (nEffectType == SPELL_ID_WEAKEN) ||
                                    ((mode != 1 && mode != 2) &&
                                     ((nEffectType == SPELL_ID_ENLARGE) ||
                                      (nEffectType == SPELL_ID_SHRINK)));
            // ASI f24 NOPs only the expiry call, not the RPG recalculation.
            if (bStrengthChanged && mode != 1 && mode != 2)
                lpMapObject->ChangeComplectionWithSpell();
            lpMapObject->SetUpdateMask(CMapObject::VLD_MASK_SPELLEFFECTS);
            lpMapObject->RecalcRPGStats(bStrengthChanged);
            i--;
            lpMapObject->ChangeSpeedWithSpell();
            if (!lpMapObject->HasSpellEffect(SPELL_ID_STUN) &&
                !lpMapObject->HasSpellEffect(SPELL_ID_TELEPORT))
                lpMapObject->SetStunFlag(FALSE);
        }
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObjectServer::SetRPGStats(SRPGCommonStats *lpRPGNewCommonStats)
{
    rpgCommonStats = *lpRPGNewCommonStats;
    lpMapObject->SetUpdateMask(CMapObject::VLD_MASK_STATS);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObjectServer::ProcessAttackPortion(SRPGAttackPortion *lpAttackPortion)
{
    if (rpgCommonStats.ProcessAttackPortion(lpAttackPortion))
    {
        lpMapObject->SetUpdateMask(CMapObject::VLD_MASK_STATS);

        if (IsDead())
        {
            lpMapObject->RemoveAllSpellEffects();
            lpMapObject->Invalidate();
        }
        return TRUE;
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
