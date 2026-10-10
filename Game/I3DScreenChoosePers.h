#pragma once

#include "I3DScreenConstructor.h"
/////////////////////////////////////////////////////////////////////////////////////
class CUnit;
class CI3DScrollBar;
class CI3DEditBox;
class CI3DScreenChoosePers : public CI3DItemScreen
{
private:
    EItemScreenModes eScreenMode;
    bool bPrimaryCreationActive;
    bool bSpellAddonPrimaryCreation;
    int nPrimaryCreationWeight;

    int nCurrentParty;

    CI3DScrollBar *pScroll;
    int nPos;

    CUnit *pCurUnit;
    CItem *pSelWeapon, *pSelQItem, *pSelSpell;

    CI3DObjectNameStats *pNameStats;
    CI3DObjectQuickSlots *pSlots;
    CI3DObjectSpellSlots *pSpells;
    CI3DObjectInventoryFaces *pMaleFaces;
    CI3DObjectInventoryFaces *pFemaleFaces;
    CI3DObjectPerks *pPerks;

    std::vector<CString> males;
    std::vector<CString> females;

    float fPersAngle;
    int nLock;

    CI3DEditBox *pClanName, *pPersName;

    CI3DScrollBar *pScrollVoices;
    CString szPrototype;
    std::vector<std::pair<CString, CString>> maleVoices, femaleVoices, *pVoices;
    int nVoicePos;
    int nCurrentVoice;
    HSOUND hVoiceSound;

private:
    void SetMode(EItemScreenModes eNewMode);
    void FormPersList();
    void RedrawPersList();
    void ChooseParty(int nParty);
    void UpdateParty();
    void NewParty();
    void DeleteParty();
    void RenameParty();
    void FinishCreateParty();
    void Redress();
    void DeployUnit(bool bShowHelm = true);
    void MoveScrollToSelection();
    void Connect();
    void BeginPrimaryCreation();
    void EndPrimaryCreation();
    bool GetCreationPrimaryValues(int values[3]);
    bool HasCompletePrimaryDistribution();
    bool CanProceedToStats();
    void CheckProceedToStats();
    void ReturnToSkinSelect();
    void RedrawVoiceList();
    void MoveScrollVoiceToSelection();
    void PlayVoiceSample();

public:
    CI3DScreenChoosePers(CInterface3D *pI)
        : CI3DItemScreen(pI)
        , bPrimaryCreationActive(false)
        , bSpellAddonPrimaryCreation(false)
        , nPrimaryCreationWeight(0)
        , pNameStats(0)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual void OnExitFromModal(DWORD dwRetValue);

    void UpdateAttributes();
    void RefreshProgression() override;
    CPersonage *GetCurrentPers();
    void SetPrototype(LPCTSTR pszPrototype);
    bool GetCreationPrimaryPoints(int &points) override;
    bool CanEditCreationPrimary() override;
    bool CanAdjustCreationPrimary(int primary, int delta) override;
    bool AdjustCreationPrimary(int primary, int delta) override;
    float GetItemsWeight();

    virtual bool CanClickOnItem(CItem *pItem, EItemScreenParts eFrom);
    virtual void ClickOnItem(CItem *pItem, EItemScreenParts eFrom);

    virtual void OnIdle();
    virtual BOOL Draw(CRenderer *pRend);
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};
