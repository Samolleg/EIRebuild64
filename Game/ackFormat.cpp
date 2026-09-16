#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "ackFormat.h"

#include "BasicChunk.h"
/////////////////////////////////////////////////////////////////////////////////////
void SAckEntry::Serialize(CStructureSaver &file)
{
    file.Add(1, szFileName);
    file.Add(2, nProbability);
    file.Add(3, szDescription);
    file.Add(4, szKey);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAckEntryBored::Serialize(CStructureSaver &file)
{
    SAckEntry::Serialize(file);
    file.Add(10, dwUsageFlags);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAckBase::Serialize(CStructureSaver &file)
{
    file.Add(1, szName);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAckHeroAck::Serialize(CStructureSaver &file)
{
    SAckBase::Serialize(file);
    // acks
    file.AddT(10, ackSelection);
    file.AddT(11, ackMove);
    file.AddT(12, ackAttack);
    file.AddT(13, ackCast);
    file.AddT(14, ackLoot);
    file.AddT(15, ackUseObject);
    file.AddT(16, ackStealFromUnit);
    file.AddT(17, ackFollow);
    file.AddT(18, ackUsePotion);
    file.AddT(19, ackChangePosition);
    // refuses
    file.AddT(20, ackNoWayToAttack);
    file.AddT(21, ackCantChangePosition);
    file.AddT(22, ackPathNotFound);
    file.AddT(23, ackCantCast);
    file.AddT(24, ackCantTeleport);
    file.AddT(25, ackScienceFailed);
    file.AddT(26, ackCantSeeTarget);
    file.AddT(27, ackTooComplexSpellToCast);
    // notifies
    file.AddT(28, ackDeciedToAttackUnit);
    file.AddT(29, ackOutOfStamina);
    file.AddT(30, ackArmCrippled);
    file.AddT(31, ackLegCrippled);
    // bored
    file.AddT(32, ackBored);
    // additional...
    file.AddT(33, ackTargetTooHigh); // attack refuse due to target too high
    //
    file.AddT(34, ackCantRunOverloaded);
    file.AddT(35, ackCantRunInjured);
    //
    file.AddT(36, ackAttackTooBig);
    file.AddT(37, ackArmorCrippled);
    file.AddT(38, ackWeaponCrippled);
    file.AddT(39, ackAttackedInDefence);
    file.AddT(40, ackWaitingFollowers);
    //
    file.AddT(41, ackScenario);
    //
    file.AddT(42, ackStealEmpty);
    // whisper
    file.AddT(43, ackWhisperYes);
    file.AddT(44, ackWhisperNo);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAckNPC::Serialize(CStructureSaver &file)
{
    SAckBase::Serialize(file);

    file.AddT(10, ackSelectedForTalking);
    file.AddT(11, ackWhileInRestState);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAckUnitGeneral::Serialize(CStructureSaver &file)
{
    SAckBase::Serialize(file);

    file.AddT(10, ackBecomeAggressive);
    file.AddT(11, ackBecameSuspectious);
    file.AddT(12, ackBecomeKiller);
    file.AddT(13, ackWhileInRestState);
    file.AddT(14, ackWhileInAggressiveState);
}
/////////////////////////////////////////////////////////////////////////////////////
