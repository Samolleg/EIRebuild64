#pragma once
#include "SaveSchema.h"
/////////////////////////////////////////////////////////////////////////////////////
#include "StdAfx.h"
#include "SaveLoader.h"
#include "Party.h"
#include "GlobalMap.h"
#include "AssortmentGenerator.h"
#include "ServerConsts.h"
/////////////////////////////////////////////////////////////////////////////////////
enum
{
    SCN_ZONE_UNKNOWN,
    SCN_ZONE_UNEXPLORED,
    SCN_ZONE_EXPLORED
};
enum
{
    SCN_OBJECTIVE_UNKNOWN,
    SCN_OBJECTIVE_AVAILABLE,
    SCN_OBJECTIVE_COMPLETED,
    SCN_OBJECTIVE_FAILED
};
enum
{
    SCN_BRIEFING_UNKNOWN,
    SCN_BRIEFING_AVAILABLE,
    SCN_BRIEFING_PASSED
};
const int NUM_SHOP_KOEFFS = 10; // currently 9, 1 for case
/////////////////////////////////////////////////////////////////////////////////////
// благодаря усилиям наших сценаристов у нас появилась уникальная возможность играть
// за несколько наборов персонажей поочередно - один набор на одну миссию
// этот класс представляет один набор персонажей
class CSingleParty : public CFundament
{
    FUNDAMENT_BASIC_METHODS(CSingleParty)
public:
    CParty heroes;
    CString szName;

    CSingleParty() { heroes.Create(); }
    ~CSingleParty() { heroes.Destroy(); }
    void Serialize(CStructureSaver &file);
};
/////////////////////////////////////////////////////////////////////////////////////
struct SPartySet
{
    // SPartyID partyID;
    std::vector<CPtr<CSingleParty>> parties;
    CString szCurrentParty;
    CString szPartyOnServer;

    void Clear();
    CParty *GetParty(const char *pszName);
    void CreateParty(const char *pszName);
    void RemoveParty(const char *pszName);
    void Serialize(CStructureSaver &file);
    void NewGame();
    void SetPartyFromServer(CParty &res);
    void GetCurrentParty(CParty &res);
};
/////////////////////////////////////////////////////////////////////////////////////
class CShop : public CFundament
{
    FUNDAMENT_BASIC_METHODS(CShop)
public:
    // constant
    int nID;
    bool bItem;                     // is item shop + constr valid
    bool bSpell;                    // is spell shop + constr valid
    float fKoeffs[NUM_SHOP_KOEFFS]; // koeffs for buy, sell, construct...
    bool bNeedUpdate;

    // non-constant
    CParty itemAssort;                  // assortment for items
    CParty spellAssort;                 // assortment for items
    CAssortmentGenerator itemGenerator; // generators
    CAssortmentGenerator spellGenerator;

    CShop();
    ~CShop();

    void FillStartAssortment();
    void SetAssortmentParameters(float *fMinLevels, float *fMaxLevels);
    void GenerateItems();
    void GenerateSpells();

    void Serialize(CStructureSaver &file);
};
/////////////////////////////////////////////////////////////////////////////////////
struct SShopSet
{
    std::vector<CPtr<CShop>> shops;

    void Clear();
    CShop *GetShop(int nID);
    void Serialize(CStructureSaver &file);
    void NewGame(bool bLMP);
};
/////////////////////////////////////////////////////////////////////////////////////
struct SHatUser
{
    CString szName, szPassword;
    std::vector<SPartyID> parties;

    void Serialize(CStructureSaver &file);
    bool HasParty(const SPartyID &test) const
    {
        return std::find(parties.begin(), parties.end(), test) != parties.end();
    }
};
/////////////////////////////////////////////////////////////////////////////////////
class CGlobalHat
{
    friend class SaveSchema::Slot;
    struct SPartyHash
    {
        int operator()(const SPartyID &p) const { return p.nSomeNumber; }
    };

    typedef std::unordered_map<SPartyID, CParty, SPartyHash> CPartyHash;
    CPartyHash parties;
    std::vector<SHatUser> users;
    int nLastPartyID;

    SPartySet singleParty;

public:
    SShopSet shops;

public:
    void Clear();
    void Serialize(CStructureSaver &file);
    void Load(CSave &file);
    void Store(CSave &file);

    // called from QuestComplete
    void UpdateAssortment();

    // SHatUser* GetUser( const char *pszUserName, const char *pszPassword );
    // create new party with specified id
    // bool NewGame( SPartyID &partyID );
    // Get party from hat if partyID is not initialized then use first party
    bool GetParty(const char *pszUserName, const char *pszPassword, const SPartyID &partyID,
                  CParty &res);
    // Set modified party to hat from game
    bool SetParty(const SPartyID &partyID, CParty &res);
    SPartySet &GetSinglePlayerPartySet() { return singleParty; }
    void GetSinglePlayerParty(CParty &res);
    void SetSinglePlayerParty(CParty &res);
    void NewSingleGame();
    void NewLMPGame();
    void GetUnusedPartyID(SPartyID &res);
};
/////////////////////////////////////////////////////////////////////////////////////
CGlobalHat &ActiveGlobalHat();
void PublishGlobalHat(CGlobalHat *) noexcept;
#define theHat (ActiveGlobalHat())