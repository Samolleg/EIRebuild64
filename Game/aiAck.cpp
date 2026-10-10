#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "aiAck.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "UnitServer.h"
#include "WorldServer.h"
#include "Console.h"

static ostream &unitAcks = NConsole::unitAcks;
/*Acknowledgement is a sound
Unit produce ack when
  a) he is doing something - moment is chosen randomly
  b) when he changes state from one to another
 Priorities of acks
-additional
-from susp
-from aggr
-to susp
-to aggr
-act on alarm ( besides alarm0 )
-make alarm himself

 When ack is to be used monster randomly chooses a .wav to be said
 When unit tell surrounding his friendly to him units should not talk - they are
kinda listening to boss if they have a less priority sound to tell
*/
/////////////////////////////////////////////////////////////////////////////////////
EAIAcks CAIAcknowledgements::GetCurrentAckLevel()
{
    return curAck;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIAcknowledgements::Clear()
{
    curSegmentNeighborAck = AI_ACK_NONE;
    wantAck = AI_ACK_NONE;
    curAck = AI_ACK_NONE;
    nCurAckTimeLeft = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CAIAcknowledgements::CAIAcknowledgements()
{
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIAcknowledgements::SayAck(EAIAcks ack)
{
    wantAck = Max(ack, wantAck);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIAcknowledgements::Segment(CUnitServer *pWho)
{
    CWorldServer *pWS = (CWorldServer *)pWho->GetWorld();
    if (wantAck != AI_ACK_NONE)
    {
        if (wantAck > curSegmentNeighborAck)
        {
            curAck = wantAck;
            nCurAckTimeLeft = 20;
            unitAcks << "Unit " << pWho->GetUnit()->GetID() << " acknowledges with ";
            switch (curAck)
            {
            case AI_ACK_DURING_IDLE:
                pWS->UnitSayAck(pWho->GetUnit(), ACK_UNIT_WHILE_IN_REST);
                unitAcks << "idleSound = AAn4XyU" << std::endl;
                break;
            case AI_ACK_DURING_ATTACK:
                pWS->UnitSayAck(pWho->GetUnit(), ACK_UNIT_WHILE_IN_AGGRESSIVE);
                unitAcks << "idleSound = AAn4XyU" << std::endl;
                break;
            case AI_ACK_FROM_SUSPECTION:
                unitAcks << "back from Suspection" << std::endl;
                break;
            case AI_ACK_FROM_AGGRESSION:
                unitAcks << "back from Aggression" << std::endl;
                break;
            case AI_ACK_TO_SUSPECTION:
                pWS->UnitSayAck(pWho->GetUnit(), ACK_UNIT_BECOME_SUSPECTIOUS);
                unitAcks << "to Suspection" << std::endl;
                break;
            case AI_ACK_TO_AGGRESSION:
            {
                const CModPackage *mod = ModRuntime::Current();
                if (mod && mod->SpellAddon().Feature06() == 6 &&
                    mod->SpellAddon().Int(ModSpellAddon::IntKey::gameplay_improve))
                {
                    pWho->dwSpellAddonLostSight = 0;
                    // ASI passes the logging stream/buffer as its stale ECX unit.
                    // Visibility rejects it: preserve the absent ACK46 safely.
                }
                else
                    pWS->UnitSayAck(pWho->GetUnit(), ACK_UNIT_BECOME_AGGRESSIVE);
            }
                unitAcks << "to Aggression" << std::endl;
                break;
            case AI_ACK_ALARM:
                unitAcks << "to Alarm!" << std::endl;
                break;
            case AI_ACK_SELF_ALARM:
                unitAcks << "ALARM ALARM ALARM" << std::endl;
                break;
            case AI_ACK_KILLED:
                pWS->UnitSayAck(pWho->GetUnit(), ACK_UNIT_BECOME_KILLER);
                unitAcks << "poluchi gadina" << std::endl;
                break;
            default:
                unitAcks << "error... :)" << std::endl;
                break;
            }
        }
        wantAck = AI_ACK_NONE;
    }
    curSegmentNeighborAck = AI_ACK_NONE;
    if (curAck == AI_ACK_NONE)
        return;
    nCurAckTimeLeft--;
    if (nCurAckTimeLeft <= 0)
    {
        curAck = AI_ACK_NONE;
        return;
    }
    // inform neighbours about our ack level
    CMapObjectPtrList &nearUnits = *pWho->logic.GetUnitsList();
    int nOurPlayer = pWho->GetUnit()->GetPlayerNum();
    EAIAcks ourAckLevel = GetCurrentAckLevel();
    for (POSITION pos = nearUnits.GetHeadPosition(); pos;)
    {
        CUnit *pUnit = nearUnits.GetNext(pos);
        if (!pWS->fof.IsPlayerAlly(nOurPlayer, pUnit->GetPlayerNum()))
            continue;
        EAIAcks &hisSegmentAckLevel = ((CUnitServer *)pUnit->GetUnitSpecific())
                                          ->acknowledgements.curSegmentNeighborAck;
        hisSegmentAckLevel = Max(hisSegmentAckLevel, ourAckLevel);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIAcknowledgements::Load(CSave &file)
{
    file.Read(&curSegmentNeighborAck, sizeof(curSegmentNeighborAck));
    file.Read(&wantAck, sizeof(curSegmentNeighborAck));
    file.Read(&curAck, sizeof(curSegmentNeighborAck));
    file.Read(nCurAckTimeLeft);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIAcknowledgements::Store(CSave &file)
{
    file.Write(&curSegmentNeighborAck, sizeof(curSegmentNeighborAck));
    file.Write(&wantAck, sizeof(curSegmentNeighborAck));
    file.Write(&curAck, sizeof(curSegmentNeighborAck));
    file.Write(nCurAckTimeLeft);
}
