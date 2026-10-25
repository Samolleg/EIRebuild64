#pragma once
#include "SaveSchema.h"
/**********************************************************************
 *<
        FILE: aiMap.H

        DESCRIPTION:  Unit`s acknowledgements

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

#include "SaveLoader.h"

/////////////////////////////////////////////////////////////////////////////////////
class CUnitServer;
/////////////////////////////////////////////////////////////////////////////////////
enum EAIAcks
{
    AI_ACK_NONE,
    AI_ACK_DURING_IDLE,
    AI_ACK_DURING_ATTACK,
    AI_ACK_FROM_SUSPECTION,
    AI_ACK_FROM_AGGRESSION,
    AI_ACK_TO_SUSPECTION,
    AI_ACK_TO_AGGRESSION,
    AI_ACK_ALARM,
    AI_ACK_SELF_ALARM,
    AI_ACK_KILLED,
};
/////////////////////////////////////////////////////////////////////////////////////
class CAIAcknowledgements
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
private:
    // уровень ack соседних дружественных юнитов
    EAIAcks curSegmentNeighborAck;
    // ack, который хочет юнит (будет приведен в действие через сегмент, когда окажется,
    // что никто кроме этого юнита не хочет произнести что-либо
    EAIAcks wantAck;
    // текущий уровень ack юнита
    EAIAcks curAck;
    // количество сегментов, оставшихся до конца текущего ack
    int nCurAckTimeLeft;

    EAIAcks GetCurrentAckLevel();

public:
    CAIAcknowledgements();
    void Clear();
    // эта функция должна вызываться после Segment() того же юнита, причем между вызовом
    // Segment() юнита и вызовом этой функции не должно быть вызовов Segment() других юнитов
    // лучше вызывать эту функцию во время работы unitserver::segment()
    void SayAck(EAIAcks ack);
    void Segment(CUnitServer *pWho);
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};
/////////////////////////////////////////////////////////////////////////////////////
