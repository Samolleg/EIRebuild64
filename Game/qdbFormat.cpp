#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "qdbFormat.h"

#include "BasicChunk.h"
/////////////////////////////////////////////////////////////////////////////////////
void SQDBQuest::Serialize(CStructureSaver &file)
{
    file.Add(0, szName);
    file.Add(1, fExp);
    file.Add(2, fExpTotal);
    file.Add(3, dwZone);
    file.Add(4, szDescription);
    file.Add(5, fMoney);
    file.Add(6, dwNumber);

    file.AddT(7, places);
}
/////////////////////////////////////////////////////////////////////////////////////
void SQDBBriefing::Serialize(CStructureSaver &file)
{
    file.Add(0, szName);
    file.Add(1, fExp);
    file.Add(2, fMoney);
    file.AddT(3, szItems);
    file.Add(4, szDescription);
    file.AddT(5, szRemoveItems);
    file.AddT(6, szGiveQuests);
    file.AddT(7, szCompleteQuests);
    file.AddT(8, szAvailableZones);
    file.AddT(9, szCompleteZones);
    file.Add(10, nSpecial);
}
/////////////////////////////////////////////////////////////////////////////////////
