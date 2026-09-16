#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Object3DClientSpecific.h"
#include "MapObjectClientSpecific.h"
#include "MapObjectManager.h"
#include "LightManager.h"
#include "Unit.h"
#include "Terrain.h"
#include "Particle.h"
#include "ParticleManager.h"
#include "RandomGen.h"
#include "WorldClient.h"
#include "SoundManager.h"
#include "GSVars.h"
/////////////////////////////////////////////////////////////////////////////////////
// расстояния, на которых слышны звуки от спелов
const float FP_SPELL_SOUND_MIN_R = 10;
const float FP_SPELL_SOUND_MAX_R = 50;
const int N_PROTECTION_SOUND_TIME = 25;
const int N_STENCH_SOUND_TIME = 16;

static const CPlot3D LIT_UNIT_ADD = CPlot3D(0, -0.5f, 1.0f);
static const float LIT_MO_ADD_HEIGHT = 2.0f;
/////////////////////////////////////////////////////////////////////////////////////
CMapObjectClientSpecific::CMapObjectClientSpecific()
{
    fpTime = 0.0f;
    nStepTimeCount = 0;

#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CMapObjectClientSpecific::~CMapObjectClientSpecific()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObjectClientSpecific::Create()
{
    fpTime = 0.0f;
    nStepTimeCount = 0;
    effectsShow.RemoveAll();
    hLastSpellEffectTick = -1;
    return CMapObjectSpecific::Create();
    ;
}
/////////////////////////////////////////////////////////////////////////////////////
// destroy all internal data structures
void CMapObjectClientSpecific::Destroy()
{
    fpTime = 0.0f;
    nStepTimeCount = 0;
    effectsShow.RemoveAll();
    if (pLight)
    {
        pLight->ResetFlag(LIT_QUEST);
        LightOff();
        static_cast<CLightManager *>(theWorld.GetLightManager())
            ->RemoveQuestLightID(pLight->GetID());
        pLight = 0;
    }
    CMapObjectSpecific::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
int CMapObjectClientSpecific::Segment()
{
    int i;
    // segment all spell effects
    for (i = 0; i < lpMapObject->spellEffects.GetSize(); i++)
    {
        CSpellEffect &eff = lpMapObject->spellEffects[i];
        if (eff.nDuration > 0)
        {
            eff.nDuration--;
            if ((eff.nDuration < 2 * 15) && !eff.bQuiet && IsIDofPersonage(lpMapObject->GetID()))
            {
                // если предыдущий щелчёк уже доигрался
                if (!theWorld.GetSoundManager()->IsFXValid(hLastSpellEffectTick))
                    hLastSpellEffectTick = theWorld.GetSoundManager()->BeginFX("magic\\end1.wav", 0,
                                                                               lpMapObject->GetCP(),
                                                                               8, 20);
                //					hLastSpellEffectTick =
                // theWorld.GetSoundManager()->BeginFX( "magic\\end1.wav", 0, 100 );
            }
        }
    }
    // processing sound of some spell effects
    for (i = 0; i < effectsSound.GetSize(); i++)
    {
        SMOSpellEffectSound &eff = effectsSound[i];
        switch (eff.nSpellID)
        {
        case SPELL_ID_STENCH:
            if (--eff.nSoundTimeLeft < 0)
            {
                CString szSound;
                int nSound = random.Get(1, 3);
                switch (eff.nSpellID)
                {
                /*
                case SPELL_ID_PROT_FIRE:
                        szSound.Format( "magic\\Fireshield\\%i.wav", nSound );
                        eff.nSoundTimeLeft = N_PROTECTION_SOUND_TIME;
                        break;
                case SPELL_ID_PROT_ELECTRIC:
                        szSound.Format( "magic\\Lightshield\\%i.wav", nSound );
                        eff.nSoundTimeLeft = N_PROTECTION_SOUND_TIME;
                        break;
                case SPELL_ID_ANTIMAGIC:
                        nSound = random.Get( 1, 5 );
                        szSound.Format( "magic\\Antimagic\\%i.wav", nSound );
                        eff.nSoundTimeLeft = N_PROTECTION_SOUND_TIME;
                        break;*/
                case SPELL_ID_STENCH:
                    szSound.Format("magic\\Stench\\%i.wav", nSound);
                    eff.nSoundTimeLeft = N_STENCH_SOUND_TIME;
                    break;
                }
                theWorld.GetSoundManager()->BeginFX(szSound, 0, lpMapObject->GetCP(),
                                                    FP_SPELL_SOUND_MIN_R, FP_SPELL_SOUND_MAX_R,
                                                    FALSE);
            }
            break;
        // case SPELL_ID_PROT_FIRE:
        // case SPELL_ID_PROT_ELECTRIC:
        // case SPELL_ID_ANTIMAGIC:
        // case SPELL_ID_PROT_ACID:
        //	if ( eff.hSnd != SND_EMPTY )
        //		theWorld.GetSoundManager()->UpdateFX( eff.hSnd, lpMapObject->GetCP(),
        // FP_SPELL_SOUND_MIN_R, FP_SPELL_SOUND_MAX_R ); 	break;
        case SPELL_ID_FEEBLEMIND:
            if (eff.hSndList != SND_EMPTY)
                theWorld.GetSoundManager()->UpdateFXList(eff.hSndList, lpMapObject->GetCP());
            break;
        }
    }
    // move attached light during object moving...
    if (pLight && !pLight->CheckFlag(LIT_INACTIVE))
    {
        CPlot3D plot = pLight->GetCP();
        CPlot3D ptShift = (lpMapObject->bv.ptCenter + 2.0f * lpMapObject->bv.ptMax) * (2.0f / 3.0f);
        ptShift.y = -ptShift.y;
        /*
        if ( lpMapObject->WhoAmI() == HPC_UNIT )
                plot -= lpMapObject->GetYAxis() * ptShift.y;
        */
        plot.z -= LIT_MO_ADD_HEIGHT;
        if (fabs2(plot - lpMapObject->GetCP()) != 0)
        {
            plot = lpMapObject->GetCP() - plot;
            theWorld.GetTerrain()->MoveLight(pLight, &plot);
            plot = lpMapObject->GetCP();
            /*
            if ( lpMapObject->WhoAmI() == HPC_UNIT )
                    plot += lpMapObject->GetYAxis() * ptShift.y;
            */
            plot.z += LIT_MO_ADD_HEIGHT;
            pLight->SetCP(plot);
        }
    }
    //
    return 1;
}
/////////////////////////////////////////////////////////////////////////////////////
static ETypeIDs GetParticleForSpell(int nSpellID)
{
    switch (nSpellID)
    {
    case SPELL_ID_PROT_FIRE:
        return PTID_SPHEREFIRE;
    case SPELL_ID_PROT_ELECTRIC:
        return PTID_SPHEREELECTRICITY;
    case SPELL_ID_PROT_ACID:
        return PTID_SPHEREACID;
    case SPELL_ID_STENCH:
        return PTID_BLUEGAS;
    case SPELL_ID_ANTIMAGIC:
        return PTID_ANTIMAGIC;
    case SPELL_ID_EAGLE_SIGHT:
        return PTID_VISIONSTAR1;
    case SPELL_ID_INFRAVISION:
        return PTID_VISIONSTAR2;
    case SPELL_ID_DETECT_LIFE:
        return PTID_VISIONSTAR3;
    case SPELL_ID_REGENERATION:
        return PTID_SILENCE;
    case SPELL_ID_SILENCE:
        return PTID_VISIONSTAR4;
    case SPELL_ID_FEEBLEMIND:
        return PTID_FEEBLEMIND;
    case SPELL_ID_SPEED:
        return PTID_FEETCLOUD2;
    case SPELL_ID_SLOW:
        return PTID_FEETCLOUD1;
    }
    ReportWarning("ERROR: Spell effect on map object with unknown particle effect");
    return PTID_BLUEGAS;
}
/////////////////////////////////////////////////////////////////////////////////////
static ETypeIDs GetParticleForModifier(int nSpellID)
{
    switch (nSpellID)
    {
    case SPELL_ID_STRENGTHEN:
        return PTID_MODIFIER1;
    case SPELL_ID_WEAKEN:
        return PTID_MODIFIER1;
    }
    return PTID_CLAYRING;
}
/////////////////////////////////////////////////////////////////////////////////////
static bool HasContinousEffect(int nSpellID)
{
    switch (nSpellID)
    {
    case SPELL_ID_PROT_FIRE:
    case SPELL_ID_PROT_ELECTRIC:
    case SPELL_ID_PROT_ACID:
    case SPELL_ID_ANTIMAGIC:
    case SPELL_ID_STUN:
    case SPELL_ID_STENCH:
    case SPELL_ID_EAGLE_SIGHT:
    case SPELL_ID_INFRAVISION:
    case SPELL_ID_DETECT_LIFE:
    case SPELL_ID_REGENERATION:
    case SPELL_ID_SILENCE:
    case SPELL_ID_FEEBLEMIND:
    case SPELL_ID_SPEED:
    case SPELL_ID_SLOW:
    case SPELL_ID_STRENGTHEN:
    case SPELL_ID_WEAKEN:
        return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObjectClientSpecific::UpdateEffectParticles(bool bStart)
{
    int i, k;
    CParticle *pParticle;
    CSpellEffectArray spellEffects;
    for (k = 0; k < lpMapObject->spellEffects.GetSize(); k++)
    {
        bool bFound = false;
        CSpellEffect &eff = lpMapObject->spellEffects[k];
        if (HasContinousEffect(eff.nSpellID))
        {
            for (i = 0; i < spellEffects.GetSize(); i++)
            {
                if (spellEffects[i].nSpellID == eff.nSpellID)
                {
                    spellEffects[i].nVisStrength = Max(spellEffects[i].nVisStrength,
                                                       eff.nVisStrength);
                    bFound = true;
                    break;
                }
            }
        }
        if (!bFound)
            spellEffects.Add(eff);
    }

    for (k = 0; k < effectsShow.GetSize(); k++)
        effectsShow[k].bCorrect = false;
    for (k = 0; k < spellEffects.GetSize(); k++)
    {
        bool bFoundEffect = false;
        CSpellEffect &eff = spellEffects[k];
        // is there a perfect match ?
        for (i = 0; i < effectsShow.GetSize(); i++)
        {
            if (!effectsShow[i].bCorrect)
            {
                if (effectsShow[i].nSpellID == eff.nSpellID &&
                    effectsShow[i].nVisStrength == eff.nVisStrength)
                {
                    effectsShow[i].bCorrect = true;
                    bFoundEffect = true;
                    break;
                }
            }
        }
        if (bFoundEffect)
            continue;
        // may be not so perfect but also a match ?
        for (i = 0; i < effectsShow.GetSize(); i++)
        {
            if (!effectsShow[i].bCorrect)
            {
                if (effectsShow[i].nSpellID == eff.nSpellID && !effectsShow[i].pParticle.IsEmpty())
                {
                    effectsShow[i].nVisStrength = eff.nVisStrength;
                    effectsShow[i].pParticle->SetScale(((float)eff.nVisStrength) / 0x10);
                    effectsShow[i].bCorrect = true;
                    bFoundEffect = true;
                    break;
                }
            }
        }
        if (bFoundEffect)
            continue;
        // ok ok we got to create a new one
        SMOSpellEffectShow newEff;
        newEff.bCorrect = true;
        newEff.nVisStrength = eff.nVisStrength;
        newEff.nSpellID = eff.nSpellID;
        switch (eff.nSpellID)
        {
        case SPELL_ID_INVISIBILITY:
            lpMapObject->SetFlag(MAPOBJ_TRANSPARENT);
            break;
        case SPELL_ID_STUN:
            lpMapObject->SetFlag(MAPOBJ_STUNED);
        case SPELL_ID_TELEPORT:
        case SPELL_ID_ENLARGE:
        case SPELL_ID_SHRINK:
            break;
        case SPELL_ID_LICHDOM:
        case SPELL_ID_STRENGTHEN:
        case SPELL_ID_WEAKEN:
            if (!bStart)
            {
                float fpStrength = ((float)eff.nVisStrength) / 0x10;
                if (eff.nSpellID == SPELL_ID_WEAKEN)
                    fpStrength = -fpStrength;
                pParticle = ((CParticleManager *)theWorld.GetParticleManager())
                                ->CreateParticleSystem(GetParticleForModifier(eff.nSpellID));
                pParticle->AttachCarrier(lpMapObject);
                pParticle->SetScale(fpStrength);
            }
            break;
        default:
            newEff.pParticle = ((CParticleManager *)theWorld.GetParticleManager())
                                   ->CreateParticleSystem(GetParticleForSpell(eff.nSpellID));
            newEff.pParticle->AttachCarrier(lpMapObject);
            newEff.pParticle->SetScale(((float)eff.nVisStrength) / 0x10);
            if (bStart)
            {
                for (int zz = 0; zz < 30; zz++)
                    newEff.pParticle->Segment();
            }
            break;
        }
        effectsShow.Add(newEff);
    }
    // remove not needed effects
    for (k = 0; k < effectsShow.GetSize(); k++)
    {
        bool bFlag;
        SMOSpellEffectShow &eff = effectsShow[k];
        if (!eff.bCorrect)
        {
            switch (eff.nSpellID)
            {
            case SPELL_ID_STUN:
                // проверим, может на юните есть еще stun
                bFlag = true;
                for (i = 0; i < effectsShow.GetSize(); i++)
                {
                    if (i != k && effectsShow[i].nSpellID == SPELL_ID_STUN)
                        bFlag = false;
                }
                if (bFlag)
                {
                    lpMapObject->ResetFlag(MAPOBJ_STUNED);
                    ((CObject3DClientSpecific *)lpMapObject->GetObjectSpecific())->SetUnstun();
                }
                break;
            case SPELL_ID_INVISIBILITY:
                // проверим, может на юните есть еще invis
                bFlag = true;
                for (i = 0; i < effectsShow.GetSize(); i++)
                {
                    if (i != k && effectsShow[i].nSpellID == SPELL_ID_INVISIBILITY)
                        bFlag = false;
                }
                if (bFlag)
                {
                    lpMapObject->ResetFlag(MAPOBJ_TRANSPARENT);
                    ((CObject3DClientSpecific *)lpMapObject->GetObjectSpecific())->SetAsVisible();
                }
                break;
            case SPELL_ID_LICHDOM:
            case SPELL_ID_STRENGTHEN:
            case SPELL_ID_WEAKEN:
                if (!lpMapObject->IsDead())
                {
                    float fpStrength = ((float)eff.nVisStrength) / 0x10;
                    if (eff.nSpellID == SPELL_ID_WEAKEN)
                        fpStrength = -fpStrength;
                    pParticle = ((CParticleManager *)theWorld.GetParticleManager())
                                    ->CreateParticleSystem(GetParticleForModifier(eff.nSpellID));
                    pParticle->AttachCarrier(lpMapObject);
                    pParticle->SetScale(-fpStrength);
                }
                break;
            }
            if (eff.pParticle)
                eff.pParticle->FinishEffect();
            effectsShow.RemoveAt(k--);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
static void PlayCycleProtection(SMOSpellEffectSound &eff, const CPlot3D &cp, const char *pszFName)
{
    if (eff.hSnd == SND_EMPTY)
        eff.hSnd = theWorld.GetSoundManager()->BeginFX(CString(pszFName) + "\\circle.wav", 0, cp,
                                                       FP_SPELL_SOUND_MIN_R, FP_SPELL_SOUND_MAX_R,
                                                       TRUE);
    else
    {
        theWorld.GetSoundManager()->CancelFX(eff.hSnd);
        eff.hSnd = SND_EMPTY;
        theWorld.GetSoundManager()->BeginFX(CString(pszFName) + "\\end.wav", 0, cp,
                                            FP_SPELL_SOUND_MIN_R, FP_SPELL_SOUND_MAX_R, FALSE);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
static int FXListS1(int nPrevIndex, int nPlayingNumber)
{
    return nPlayingNumber ? 1 | SNDLIST_CYCLE_BIT : 0;
}
static void PlaySpellSound(bool bPlaySingle, SMOSpellEffectSound &eff, const CPlot3D &cp,
                           int nSpellID, const char *pszFinish)
{
    CString szSound;
    switch (nSpellID)
    {
    case SPELL_ID_STENCH:
        eff.nSoundTimeLeft = 0;
        return;
    /*case SPELL_ID_PROT_ELECTRIC:
            PlayCycleProtection( eff, cp, "magic\\Lightsheild" );
            return;*/
    case SPELL_ID_FEEBLEMIND:
        if (eff.hSndList == SND_EMPTY)
        {
            CStringArray szSoundNames;
            szSoundNames.Add("magic\\FeebleMind\\start.wav");
            szSoundNames.Add("magic\\FeebleMind\\circle.wav");
            eff.hSndList = theWorld.GetSoundManager()->BeginFXList(szSoundNames, 0, cp,
                                                                   FP_SPELL_SOUND_MIN_R,
                                                                   FP_SPELL_SOUND_MAX_R, FXListS1);
            theWorld.GetSoundManager()->BeginFX(szSound + pszFinish, 0, cp, FP_SPELL_SOUND_MIN_R,
                                                FP_SPELL_SOUND_MAX_R, FALSE);
        }
        else
        {
            theWorld.GetSoundManager()->CancelFXList(eff.hSndList);
            eff.hSndList = SND_EMPTY;
            theWorld.GetSoundManager()->BeginFX("magic\\FeebleMind\\end.wav", 0, cp,
                                                FP_SPELL_SOUND_MIN_R, FP_SPELL_SOUND_MAX_R, FALSE);
        }
        return;
    case SPELL_ID_PROT_ELECTRIC:
        szSound = "magic\\Lightsheild";
        break;
    case SPELL_ID_PROT_ACID:
        szSound = "magic\\PoisonShield";
        break;
    case SPELL_ID_PROT_FIRE:
        szSound = "magic\\Fireshield";
        break;
    case SPELL_ID_ANTIMAGIC:
        szSound = "magic\\Antimagic";
        break;
    case SPELL_ID_EAGLE_SIGHT:
        szSound = "magic\\Eaglesight";
        break;
    case SPELL_ID_INFRAVISION:
        szSound = "magic\\Infravision";
        break;
    case SPELL_ID_DETECT_LIFE:
        szSound = "magic\\Detectlife";
        break;
    case SPELL_ID_INVISIBILITY:
        szSound = "magic\\Invisibility";
        break;
    case SPELL_ID_SILENCE:
        szSound = "magic\\Silence";
        break;
    case SPELL_ID_LICHDOM:
        szSound = "magic\\Lichdom";
        break;
    case SPELL_ID_STRENGTHEN:
        szSound = "magic\\Strengthen";
        break;
    case SPELL_ID_WEAKEN:
        szSound = "magic\\Weaken";
        break;
    case SPELL_ID_REGENERATION:
        szSound = "magic\\Regeneration";
        break;
    case SPELL_ID_SPEED:
        szSound = "magic\\Speed";
        break;
    case SPELL_ID_SLOW:
        szSound = "magic\\Slow";
        break;
    case SPELL_ID_STUN:
        szSound = "magic\\Stun";
        break;
    case SPELL_ID_ENLARGE:
        szSound = "magic\\Enlarge";
        break;
    case SPELL_ID_SHRINK:
        szSound = "magic\\Shrink";
        break;
    }
    if (szSound != "" && bPlaySingle)
        theWorld.GetSoundManager()->BeginFX(szSound + pszFinish, 0, cp, FP_SPELL_SOUND_MIN_R,
                                            FP_SPELL_SOUND_MAX_R, FALSE);
}
/////////////////////////////////////////////////////////////////////////////////////
static bool HasContinousSound(int nSpellID)
{
    return true;
    /*
    switch ( nSpellID )
    {
            case SPELL_ID_PROT_FIRE:
            case SPELL_ID_PROT_ELECTRIC:
            case SPELL_ID_PROT_ACID:
            case SPELL_ID_STENCH:
            case SPELL_ID_ANTIMAGIC:
            case SPELL_ID_FEEBLEMIND:
                    return true;
    }
    return false;*/
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObjectClientSpecific::UpdateEffectSounds(bool bStart)
{
    int i, k;
    CSpellEffectArray spellEffects;
    for (k = 0; k < lpMapObject->spellEffects.GetSize(); k++)
    {
        bool bFound = false;
        CSpellEffect &eff = lpMapObject->spellEffects[k];
        // if ( eff.bQuiet )
        //	continue;
        if (HasContinousSound(eff.nSpellID))
        {
            for (i = 0; i < spellEffects.GetSize(); i++)
            {
                if (spellEffects[i].nSpellID == eff.nSpellID)
                {
                    bFound = true;
                    break;
                }
            }
        }
        if (!bFound)
            spellEffects.Add(eff);
    }

    for (k = 0; k < effectsSound.GetSize(); k++)
        effectsSound[k].bCorrect = false;
    for (k = 0; k < spellEffects.GetSize(); k++)
    {
        bool bFoundEffect = false;
        CSpellEffect &eff = spellEffects[k];
        // is there a perfect match ?
        for (i = 0; i < effectsSound.GetSize(); i++)
        {
            if (!effectsSound[i].bCorrect)
            {
                if (effectsSound[i].nSpellID == eff.nSpellID)
                {
                    effectsSound[i].bCorrect = true;
                    bFoundEffect = true;
                    break;
                }
            }
        }
        if (bFoundEffect)
            continue;
        // ok ok we got to create a new one
        SMOSpellEffectSound newEff;
        newEff.bCorrect = true;
        newEff.nSpellID = eff.nSpellID;
        PlaySpellSound(!bStart, newEff, lpMapObject->GetCP(), newEff.nSpellID, "\\start.wav");
        effectsSound.Add(newEff);
    }
    // remove not needed effects
    for (k = 0; k < effectsSound.GetSize(); k++)
    {
        SMOSpellEffectSound &eff = effectsSound[k];
        if (!eff.bCorrect)
        {
            PlaySpellSound(!lpMapObject->IsDead(), eff, lpMapObject->GetCP(), eff.nSpellID,
                           "\\end.wav");
            effectsSound.RemoveAt(k--);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObjectClientSpecific::UpdateSpellEffectVisualization(bool bStart)
{
    UpdateEffectParticles(bStart);
    UpdateEffectSounds(bStart);
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObjectClientSpecific::ReceiveDamage(int nAmount, DWORD dwFlags)
{
    if (!lpMapObject->IsDead() && lpMapObject->CheckFlag(UNIT_VISIBLE))
        static_cast<CWorldClient *>(GetWorld())
            ->ReceiveDamage(lpMapObject->GetID(), nAmount, dwFlags);
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObjectClientSpecific::SetLight(const D3DCOLORVALUE &dcvColor, const float fpRange,
                                        bool bSpecular)
{
    LightOff();
    pLight = (CLight *)theWorld.GetLightManager()->CreateData(LTID_POINTLIGHT);
    D3DVECTOR color = D3DVECTOR(dcvColor.r, dcvColor.g, dcvColor.b);
    pLight->SetPointLight(color, color);
    CPlot3D plot = lpMapObject->GetCP();
    CPlot3D ptShift = (lpMapObject->bv.ptCenter + 2.0f * lpMapObject->bv.ptMax) * (2.0f / 3.0f);
    ptShift.y = -ptShift.y;
    /*
    if ( lpMapObject->WhoAmI() == HPC_UNIT )
            plot += lpMapObject->GetYAxis() * ptShift.y;
    */
    plot.z += LIT_MO_ADD_HEIGHT;
    pLight->SetCP(plot);
    pLight->SetRange(fpRange);
    pLight->SetFlag(LIT_DYNAMIC | LIT_INACTIVE | LIT_UNSHADOW);
    if (bSpecular)
        pLight->SetFlag(LIT_SPECULAR);
    LightOn();
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObjectClientSpecific::LightOn()
{
    if (pLight && pLight->CheckFlag(LIT_INACTIVE) && !pLight->CheckFlag(LIT_QUEST))
    {
        pLight->ResetFlag(LIT_INACTIVE);
        theWorld.GetTerrain()->AddLight(pLight);
        if (lpMapObject && (lpMapObject->WhoAmI() == HPC_UNIT) && lpMapObject->GetObjectSpecific())
            lpMapObject->GetObjectSpecific()->Preselect(TRUE);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObjectClientSpecific::LightOff()
{
    if (pLight && !pLight->CheckFlag(LIT_INACTIVE) && !pLight->CheckFlag(LIT_QUEST))
    {
        pLight->SetFlag(LIT_INACTIVE);
        theWorld.GetTerrain()->RemoveLight(pLight);
        if (lpMapObject && lpMapObject->GetObjectSpecific())
            lpMapObject->GetObjectSpecific()->Preselect(FALSE);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObjectClientSpecific::SetQuestState(EQuestState qs)
{
    CLightManager *pLightManager = static_cast<CLightManager *>(theWorld.GetLightManager());
    if (qs == QS_ACTIVE)
    {
        if (pLight == 0)
        {
            //			D3DCOLORVALUE dcvColor = { 255.0f/255.0f, 236.0f/255.0f,
            // 170.0f/255.0f, 1.0f
            //}; 			D3DCOLORVALUE dcvColor = { 216.0f/255.0f, 0.0f/255.0f,
            // 255.0f/255.0f, 1.0f };
            const D3DCOLORVALUE dcvColor = { theSettings.fQuestLight[0], theSettings.fQuestLight[1],
                                             theSettings.fQuestLight[2], 1.0f };
            SetLight(dcvColor, theSettings.fQuestLightRadius, true);
        }
        else
        {
            pLight->ResetFlag(LIT_QUEST);
            LightOn();
        }
        pLight->SetFlag(LIT_QUEST);
        pLightManager->AddQuestLightID(pLight->GetID());
    }
    else if (qs == QS_COMPLETED)
    {
        if (pLight)
        {
            pLight->ResetFlag(LIT_QUEST);
            LightOff();
            pLight->SetFlag(LIT_QUEST);
            pLightManager->RemoveQuestLightID(pLight->GetID());
            pLight = 0; // we don't need this light anymore
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
