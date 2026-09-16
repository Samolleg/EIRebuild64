#pragma once

#include <string>
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
class CStructureSaver;
/////////////////////////////////////////////////////////////////////////////////////
struct SQDBQuest
{
public:
    std::string szName;        // quest name
    float fExp;                // exp for this quest
    float fExpTotal;           // total exp for all previous quests
    float fMoney;              // money for this quest
    DWORD dwZone;              // zone, this quest work in
    DWORD dwNumber;            // предполагаемый порядок выдачи и выполнения
    std::vector<float> places; // quest places coords (each 2 floats = (x,y))

    std::string szDescription; // string description
public:
    void Serialize(CStructureSaver &file);

    const std::string &GetCode() const { return szName; }
};
/////////////////////////////////////////////////////////////////////////////////////
struct SQDBBriefing
{
public:
    std::string szName;                    // briefing name
    float fExp;                            // exp to give in this briefing
    float fMoney;                          // money to give in this briefing
    std::vector<CString> szItems;          // items to give in this briefing
    std::vector<CString> szRemoveItems;    // items to remove after this briefing
    std::vector<CString> szGiveQuests;     // quest to give after this briefing
    std::vector<CString> szCompleteQuests; // quest to complete after this briefing
    std::vector<CString> szAvailableZones; // zones, which will be available after this briefing
    std::vector<CString> szCompleteZones;  // zones, which will be completed after this briefing
    std::string szDescription;             // string description
    int nSpecial;

public:
    void Serialize(CStructureSaver &file);

    const std::string &GetCode() const { return szName; }
};
/////////////////////////////////////////////////////////////////////////////////////
