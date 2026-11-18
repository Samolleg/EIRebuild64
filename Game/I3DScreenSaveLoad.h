#pragma once

#include "I3DScreen.h"
#include "Time32.h"
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
extern const unsigned char VERSION_CHAR_1;
extern const unsigned char VERSION_CHAR_4;
extern const unsigned char VERSION_CHAR_7;
extern const unsigned char VERSION_CHAR_10;

extern const unsigned char VERSION_CODE_2;
extern const unsigned char VERSION_CODE_5;
extern const unsigned char VERSION_CODE_8;
extern const unsigned char VERSION_CODE_11;
/////////////////////////////////////////////////////////////////////////////////////
struct SSaveSlotDesc
{
public:
    CString szName;  // name of entry ( localized )
    CString szPath;  // path from saves directory, eg "save25"
    __time32_t time = 0; // time when it was made

    uint64_t generation = 0; // precise write time from the Saves64 envelope
    float fGameTime = 0;
    CString szAllodName;       // current allod ( internal name )
    CString szZoneName;        // current zone ( internal name )
    CStringArray szPersonages; // personages in the party
    CStringArray szQuests;     // available quests

    bool bNoInfo = true; // if old save format or new slot in save screen

public:
    SSaveSlotDesc() {}
    SSaveSlotDesc(const SSaveSlotDesc &a) { *this = a; }
    SSaveSlotDesc &operator=(const SSaveSlotDesc &a);

    void FormName(bool bDate = true);

    CString GetZoneLocalName();
    CString GetAllodLocalName();

    void Load(CSave &file);
    void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScrollBar;
class CI3DScreenSaveLoad : public CI3DScreen
{
private:
    std::vector<SSaveSlotDesc> slots;
    int nCurrentSlot;
    int nPos; // scroll position
    bool bIsSaveScreen;
    bool bIntro;
    float fIntroTime;
    CI3DScrollBar *pScroll;
    LPTEXTURE pCurrentShot;
    CString szEditName;
    bool bEdited;
    int nCursorPos;

private:
    void Save();
    void Load();
    void Delete();
    void FormSlotsList();
    void RedrawSlots();
    void ReloadPicture();
    void MoveScrollToSelection();
    void ConfirmOK();
    void ConfirmDel();
    void BeginEdit(bool bClear = true);
    void ResetEdit();
    bool IsSystemSlot(int nSlot);
    static bool IsEnoughSpace();

public:
    CI3DScreenSaveLoad(CInterface3D *pI)
        : CI3DScreen(pI)
        , bIsSaveScreen(false)
        , bIntro(false)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual void OnExitFromModal(DWORD dwRetValue);
    virtual LPCTSTR GetTutorialScreenName();

    static void QuickSave(LPCTSTR pszSlotName, bool bIsAuto = false);
    static CString LatestSlot();
    static void QuickLoad(LPCTSTR pszSlotName);

    void SetParams(bool bIsSave, bool bIntroAnim)
    {
        bIsSaveScreen = bIsSave;
        bIntro = bIntroAnim;
    }

    virtual BOOL Draw(CRenderer *pRend);
    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonDblClk(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
};
