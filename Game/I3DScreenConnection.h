#pragma once

#include "I3DScreen.h"
#include "ChatServer.h"
#include "I3DScreenMessageBox.h"
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////

#ifndef MASTER_SERVER

/////////////////////////////////////////////////////////////////////////////////////
struct SServerDesc
{
public:
    CString szName;
    CString szZone;
    int nPlayers;
    int nMaxPlayers;
    int nPing;
    CCSNodeAddress addr;
    ELMPAllod allod;
    bool bWrong;
    bool bIsInBook;
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScrollBar;
class CI3DHScrollBar;
class CI3DEditBox;
class CI3DSwitcher;
class CI3DScreenConnection : public CI3DScreen
{
private:
    float fTime;
    std::vector<SServerDesc> slots;
    int nCurrentSlot;
    CCSNodeAddress curAddress;
    int nPos; // scroll position

    CI3DScrollBar *pScroll;
    CI3DHScrollBar *pPlayersSlider;
    CI3DEditBox *pServerName;
    CI3DEditBox *pIPAddress;
    int nNewMaxPlayers;
    int nAllod;

    CStringArray szAllodNames;
    bool bEnterIP;
    bool bOldConnectState;
    enum
    {
        SERVER,
        PLAYERS,
        MAX_PLAYERS,
        ALLOD,
        QUEST,
        IP,
        PING,
        NEW_SERVER,
        CONNECTING,
        TOTAL
    };
    CStringArray szStrings;

private:
    void EnterGame();
    void AddressBookAdd();
    void AddressBookDelete();
    void FinishEnterIP();
    void FormSlotsList();
    void RedrawSlots();
    void MoveScrollToSelection();
    void SaveAddressBook();

public:
    CI3DScreenConnection(CInterface3D *pI)
        : CI3DScreen(pI)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual void OnExitFromModal(DWORD dwRetValue);
    virtual LPCTSTR GetTutorialScreenName();

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonDblClk(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};
/////////////////////////////////////////////////////////////////////////////////////

#else // MASTER_SERVER

/////////////////////////////////////////////////////////////////////////////////////
struct SServerDesc
{
public:
    CString szName;
    CString szZone;
    int nPlayers;
    int nMaxPlayers;
    int nPing;
    CCSNodeAddress addr;
    ELMPAllod allod;
    bool bWrong;
    CCSServerInfo::EType type;
    bool bPassword;
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScrollBar;
class CI3DHScrollBar;
class CI3DEditBox;
class CI3DSwitcher;
class CI3DScreenConnection : public CI3DScreen
{
private:
    float fTime;
    std::vector<SServerDesc> slots;
    int nCurrentSlot;
    CCSNodeAddress curAddress;
    int nPos; // scroll position

    CI3DScrollBar *pScroll;
    CI3DHScrollBar *pPlayersSlider;
    CI3DEditBox *pServerName;
    int nNewMaxPlayers;
    int nAllod;
    char pszBuffer[128];
    bool bPrivate;
    CString szPassword;

    CStringArray szAllodNames;
    bool bOldConnectState;
    enum
    {
        SERVER,
        PLAYERS,
        MAX_PLAYERS,
        ALLOD,
        QUEST,
        IP,
        PING,
        NEW_SERVER,
        CONNECTING,
        PK,

        REFRESH,
        FAVORITES,
        HD_NAME,
        HD_PLAYERS,
        HD_ALLOD,
        HD_PING,
        PRIVATE,
        PASSWORD,
        ENTER_PASSWORD,
        ENTER_IP,
        RETRIEVING,
        IS_PASSWORD,

        TOTAL
    };
    CStringArray szStrings;
    CTCPRequest *pMasterRequest;

    int nSortMode;
    bool bNewServer;

private:
    void EnterGame();
    void AddressBookAdd();
    void Refresh();
    void FormSlotsList();
    void RedrawSlots();
    void MoveScrollToSelection();
    void SaveAddressBook();

public:
    CI3DScreenConnection(CInterface3D *pI)
        : CI3DScreen(pI)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual void OnExitFromModal(DWORD dwRetValue);
    virtual LPCTSTR GetTutorialScreenName();

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonDblClk(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DTCPMessageBox : public CI3DScreenMessageBox
{
    CTCPRequest request;
    CI3DFigure *pProgress;

public:
    CI3DTCPMessageBox(CInterface3D *pI, LPCTSTR pszTitleID, LPTSTR pszTextID, DWORD dwPar,
                      const CCSNodeAddress &addr, unsigned int nClientID);

    virtual void OnIdle();
};
/////////////////////////////////////////////////////////////////////////////////////

#endif // MASTER_SERVER
