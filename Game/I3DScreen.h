#pragma once

#include "I3DObject.h"
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScreen : public CI3DObject
{
protected:
    CSimplyArray<CI3DObject *, CI3DObject *> pObjects;
    std::vector<CI3DHandler *> pHandlers;

public:
    CI3DScreen(CInterface3D *pI);
    virtual ~CI3DScreen();

    virtual void OnActivate() = 0;
    virtual void OnDeactivate() = 0;

    virtual void OnExitFromModal(DWORD dwRetValue) {}

    virtual void NewHooveredItems(CList<CItem *, CItem *> &newItems, int nArrivedBucks,
                                  bool bCrippled)
    {
    }
    virtual void AddControlUnit(CUnit *pUnit) {}
    virtual void RemoveControlUnit(CUnit *pUnit) {}
    virtual void GameOver(bool bVictory) {}
    virtual void PlayAcknowlegment(DWORD dwUnitID, DWORD dwFailure, const char *pszAckID) {}
    virtual void QuestUpdate(LPCTSTR pszQuestVar, float fState) {}
    virtual void ForceBriefing(LPCTSTR pszBriefing) {}
    virtual bool Autosave() { return false; }
    virtual void GenerateCheatAssortment() {}

    virtual void StoreSpecific() {}

    virtual LPCTSTR GetTutorialScreenName() { return 0; }

    virtual void MemoryBackup(BOOL bShow);

    // handlers
    void AddHandler(CI3DHandler *pHandler);
    void RemoveAllHandlers();

    // objects
    CI3DObject *AddObject(CI3DObject *pObj);
    CI3DObject *GetObject(int num) { return pObjects[num]; }
    void RemoveAllObjects();

    // drawing
    virtual BOOL Draw(CRenderer *pRend);
    virtual void DrawTips(CRenderer *pRend);
    virtual void Draw2D();
    virtual void Draw2DTips();

    virtual void ResetInputState();
    // messages
    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual void OnRButtonDown(UINT nFlags, CPoint point);
    virtual void OnRButtonUp(UINT nFlags, CPoint point);
    virtual void OnLButtonDblClk(UINT nFlags, CPoint point);
    virtual void OnRButtonDblClk(UINT nFlags, CPoint point);
    virtual BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
};
