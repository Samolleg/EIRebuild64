#pragma once

#include "I3DScreen.h"
#include "KeyMap.h"
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
extern const unsigned char VERSION_CHAR_2;
extern const unsigned char VERSION_CHAR_5;
extern const unsigned char VERSION_CHAR_8;
extern const unsigned char VERSION_CHAR_11;

extern const unsigned char VERSION_CODE_0;
extern const unsigned char VERSION_CODE_3;
extern const unsigned char VERSION_CODE_6;
extern const unsigned char VERSION_CODE_9;
/////////////////////////////////////////////////////////////////////////////////////
class CI3DHScrollBar;
class CI3DSwitcher;
const int NUM_GROUP_LINES = 14;
/////////////////////////////////////////////////////////////////////////////////////
struct SKeyToActionRemap
{
    SKbdActionDesc *pActionDesc;
    std::vector<SKbdKeyDesc *> pKeysDesc;

    CString szActionLocalName;
    CString szKeysOutput;

    void FormKeysOutput();
};
/////////////////////////////////////////////////////////////////////////////////////
struct SOptionControl
{
    SGameOptionDesc *pOptionDesc;
    int nValue;

    CI3DHScrollBar *pSlider;
    CI3DSwitcher *pSwitcher;

    CString szOptionLocalName;

    void ApplyValueToControl();
};
/////////////////////////////////////////////////////////////////////////////////////
struct SLineDesc
{
    SKeyToActionRemap *pAction;
    SOptionControl *pControl;
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScreenOptions : public CI3DScreen
{
private:
    // for key remapping
    char keyMap[TOTAL_NUM_SCANCODES]; // current remap

    int nCurrentLine;
    int nCurrentMode;

    std::vector<SKeyToActionRemap> actions;
    std::vector<SOptionControl> controls;

    SOptionControl audioDeviceControl{};
    CStringArray audioDeviceNames, audioDeviceIds;
    void FormAudioDevices();
    SLineDesc lines[NUM_GROUP_LINES];

    bool bKeyInput;

    // for intro playing
    bool bIntro;
    float fIntroTime;

    CStringArray szModes;

    bool bShadowSettingsChanged;
    CI3DSwitcher *helmetSwitcher = nullptr;
    CI3DSwitcher *upperArmorSwitcher = nullptr;
    SKbdKeyDesc *pKeyDesc;

private:
    void FormActionsList();
    void FormControlsList();
    void Redraw();
    void UpdatePartyVisualOptionsVisibility();
    void CommitPartyVisualOptions();

    void NextLine();
    void PrevLine();

    void SetScreenMode(int nMode);

    void GetParameters();
    void ApplyParameters();

    void DoRemap();

public:
    CI3DScreenOptions(CInterface3D *pI)
        : CI3DScreen(pI)
        , bIntro(false)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual void OnExitFromModal(DWORD dwRetValue);
    virtual LPCTSTR GetTutorialScreenName();

    void SetParams(bool bIntroAnim) { bIntro = bIntroAnim; }

    virtual BOOL Draw(CRenderer *pRend);
    virtual void Draw2D();
    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonDblClk(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};
