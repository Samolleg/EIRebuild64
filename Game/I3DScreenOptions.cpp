#include "StdAfx.h"
#include "CorePerf.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenOptions.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include "I3DScrollBar.h"
#include "I3DScreenMessageBox.h"
#include "WorldInterface.h"
#include "WorldClient.h"
#include "TextManager.h"
#include "I3DMapObjTranslation.h"
#include "SoundManager.h"
#include "Renderer.h"
#include "dx11/RendererDX11.h"

/////////////////////////////////////////////////////////////////////////////////////
const unsigned char VERSION_CHAR_2 = 151;
const unsigned char VERSION_CHAR_5 = 43;
const unsigned char VERSION_CHAR_8 = 82;
const unsigned char VERSION_CHAR_11 = 245;

const unsigned char VERSION_CODE_0 = 81;
const unsigned char VERSION_CODE_3 = 199;
const unsigned char VERSION_CODE_6 = 107;
const unsigned char VERSION_CODE_9 = 57;
////////////////////////////////////////////////////////////////////////////////
const float OPTIONS_FLYTIME = .5f;
const float OPTIONS_FLY_BEGIN = 700.f;
const float OPTIONS_FLY_END = 480.f;

const COLORREF OPTIONS_COLOR_TEXT = RGB(228, 215, 167);
const char *PARTY_HELMET_LABEL = "\xD1\xEA\xF0\xFB\xE2\xE0\xF2\xFC\x20\xF8\xEB\xE5\xEC\xFB";
const char *PARTY_ARMOR_LABEL = "\xD1\xEA\xF0\xFB\xE2\xE0\xF2\xFC\x20\xE4\xEE\xF1\xEF\xE5\xF5\xE8";
const char *PARTY_HELMET_TIP = "\xD1\xEA\xF0\xFB\xE2\xE0\xF2\xFC\x20\xF8\xEB\xE5\xEC\xFB\n"
    "\xF2\xEE\xEB\xFC\xEA\xEE\x20\xF3\x20\xF7\xEB\xE5\xED\xEE\xE2\x20\xE3\xF0\xF3\xEF\xEF\xFB.";
const char *PARTY_ARMOR_TIP = "\xD1\xEA\xF0\xFB\xE2\xE0\xF2\xFC\x20\xE4\xEE\xF1\xEF\xE5\xF5\xE8\n"
    "\xC2\xE5\xF0\xF5\xED\xE8\xE5\x20\xE4\xEE\xF1\xEF\xE5\xF5\xE8\x20\xF7\xEB\xE5\xED\xEE\xE2\x20\xE3\xF0\xF3\xEF\xEF\xFB.";

const char* AUDIO_DEVICE_TIP = "\xC2\xFB\xE1\xEE\xF0\x20\xEF\xF0\xE8\xEC\xE5\xED\xFF\xE5\xF2\xF1\xFF\x20\xEF\xEE\xF1\xEB\xE5\x20\xEF\xE5\xF0\xE5\xE7\xE0\xEF\xF3\xF1\xEA\xE0\x20\xE8\xE3\xF0\xFB\x2E";
const int NUM_ALTERN_KEYS = 1;

const char *pszOptionGroups[] = { "video",   "sound",  "sens",  "game",   "select",
                                  "actions", "camera", "items", "spells", "cshots",
                                  "windows", "debug1", "debug2" };
////////////////////////////////////////////////////////////////////////////////
void SKeyToActionRemap::FormKeysOutput()
{
    szKeysOutput.Empty();
    for (int i = 0; i < pKeysDesc.size(); i++)
    {
        szKeysOutput += pKeysDesc[i]->pszLocalKeyName;
        szKeysOutput += "  |  ";
    }
    szKeysOutput.TrimRight(" |");
}
////////////////////////////////////////////////////////////////////////////////
void SOptionControl::ApplyValueToControl()
{
    if (pSlider)
        pSlider->SetPos(nValue);
    else if (pSwitcher)
        pSwitcher->SetState(nValue);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::OnActivate()
{
    SetRect(0, 0, 800, 600);

    AddTexture("saveload");
    AddTexture(256, 256, TFTID_565);

    CI3DFigure *pFig;

    pFig = AddFigure(); // ok
    pFig->MakeBar(3, 100, 213, 526, 287, 574, 81, 2, 155, 50);
    pFig = AddFigure(); // cancel
    pFig->MakeBar(3, 100, 513, 526, 587, 574, 160, 2, 234, 50);

    CI3DPolyArea *pArea;
    pArea = AddArea();
    pArea->MakeRect(213, 526, 287, 574); // ok
    pArea->SetToolTip(70100);

    pArea = AddArea();
    pArea->MakeRect(513, 526, 587, 574); // cancel
    pArea->SetToolTip(70101);

    pFig = AddFigure(); // selection
    pFig->MakeBar(3, 100, 250, 108, 692, 132);
    pFig->SetTexture(0);
    pFig->SetDefaultColor(RGBA_MAKE(160, 104, 0, 200));
    pFig->UseDefaultColors(TRUE);

    CString szTmp;
    for (int i = 0; i < TOTAL_NUM_OPTION_GROUPS; i++)
    {
        pArea = AddArea();
        pArea->MakeRect(100, 156 + i * 24, 250, 180 + i * 24);

        pFig = AddFigure();
        pFig->MakeBar(3, 100, 110, 158 + i * 24, 240, 177 + i * 24, 122, 108, 252, 127);

        szTmp.Format("tip option_group_%s", pszOptionGroups[i]);
        if (theWorld.GetTextManager()->GetText(szTmp))
            theWorld.GetTextManager()->GetUntilNextCommand(szTmp);
        pArea->SetToolTip(szTmp);

        IWorld.GetTextManager()->GetSystemString(CString("option_group_") + pszOptionGroups[i],
                                                 szTmp);
        szModes.Add(szTmp);
    }

    pFig = AddFigure();
    pFig->MakeBarFrame(2, 100, 95, 95, 705, 505, 5, 2, 254, 180, 249);

    pFig = AddFigure();
    pFig->MakeBar(5, 100, 100, 100, 700, 500);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));

    for (int i = 0; i < NUM_GROUP_LINES; i++)
        AddArea()->MakeRect(250, 156 + i * 24, 520, 180 + i * 24);

    AddSurface(dbgnew CI3DSurface(pI3D, 100, 100, 700, 500));

    CI3DMapObjTranslation *pObj = AddMapObjTranslation();
    pObj->Init("unmoco1", "escMenu00", "escMenu00labels");
    pObj->SetOrientation(CQuaternion(CPlot3D(1, 0, 0), FP_PI2 * 1.00f));
    pObj->SetRelativeToScreen(400, 700, 4.8f);

    pObj->ShowAllBodyParts(false);
    pObj->ShowBodyPart("options", true);
    pObj->ShowBodyPart("optionslabel", true);

    if (bIntro)
    {
        fIntroTime = 0;
    }

    bKeyInput = false;
    pKeyDesc = 0;

    FormControlsList();
    FormAudioDevices();
    GetParameters();
    FormActionsList();

    bShadowSettingsChanged = false;

    helmetSwitcher = nullptr;
    upperArmorSwitcher = nullptr;
    SetScreenMode(0);
    if (auto *mod = ModRuntime::Current())
    {
        if (mod->PartyVisualOptionsEnabled())
        {
            CString offText("OFF"), onText("ON");
            IWorld.GetTextManager()->GetSystemString("option_off", offText);
            IWorld.GetTextManager()->GetSystemString("option_on", onText);
            helmetSwitcher = dbgnew CI3DSwitcher(pI3D, CRect(530, 232, 690, 248), offText);
            helmetSwitcher->AddState(onText);
            helmetSwitcher->SetState(mod->HidePartyHelmets() ? 1 : 0);
            AddObject(helmetSwitcher);
            upperArmorSwitcher = dbgnew CI3DSwitcher(pI3D, CRect(530, 376, 690, 392), offText);
            upperArmorSwitcher->AddState(onText);
            upperArmorSwitcher->SetState(mod->HidePartyUpperArmor() ? 1 : 0);
            AddObject(upperArmorSwitcher);
            UpdatePartyVisualOptionsVisibility();
            Redraw();
        }
    }
    if (!bIntro)
        pI3D->CheckTutorial();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::OnDeactivate()
{
    if (auto* renderer = theRenderer.GetD3D())
        renderer->SetGammaCorrection(theSettings.fBrightness, theSettings.fContrast, theSettings.fGamma);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::OnExitFromModal(DWORD dwRetValue)
{
    switch (dwRetValue)
    {
    case 0x00010001:
        DoRemap();
        break;
    }
    Redraw();
}
/////////////////////////////////////////////////////////////////////////////////////
LPCTSTR CI3DScreenOptions::GetTutorialScreenName()
{
    if (!bKeyInput)
        return "options";
    return 0;
}
////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreenOptions::Draw(CRenderer *pRend)
{
    CorePerf::Scope coreTiming(CorePerf::ScreenOptions);
    float fPart;
    CI3DMapObjTranslation *pObj = 0;
    if (bIntro)
    {
        surfaces[0]->Show(FALSE);
        if (fIntroTime < .5f)
            fPart = 2.f * fIntroTime * fIntroTime;
        else
            fPart = 1.f - 2.f * (1.f - fIntroTime) * (1.f - fIntroTime);

        if (pObj = GetMapObjTranslation(0))
        {
            pObj->SetRelativeToScreen(
                400, OPTIONS_FLY_BEGIN + fPart * (OPTIONS_FLY_END - OPTIONS_FLY_BEGIN), 4.8f);
            pObj->Draw(pRend);
        }
    }
    else
    {
        surfaces[0]->Show(TRUE);
        GetMapObjTranslation(0)->SetRelativeToScreen(400, OPTIONS_FLY_END, 4.8f);
        CI3DScreen::Draw(pRend);
    }
    return TRUE;
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::Draw2D()
{
    if (!bIntro)
        CI3DScreen::Draw2D();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::OnIdle()
{
    CI3DScreen::OnIdle();
    if (bIntro)
    {
        fIntroTime += GetFrameTime() / OPTIONS_FLYTIME;
        if (fIntroTime > 1)
        {
            bIntro = false;
            pI3D->CheckTutorial();
        }
    }

    if (nCurrentMode == OG_SOUND && audioDeviceControl.pSwitcher)
    {
        int selected = 0;
        audioDeviceControl.pSwitcher->GetState(selected);
        if (selected >= 0 && selected < audioDeviceNames.GetSize())
            areas[2 + TOTAL_NUM_OPTION_GROUPS + 6]->SetToolTip(
                audioDeviceNames[selected] + "\n" + AUDIO_DEVICE_TIP);
    }
    for (int i = 0; i < TOTAL_NUM_GAME_OPTIONS; i++)
    {
        int n;
        if (controls[i].pSlider && controls[i].pSlider->GetPos(n))
        {
            controls[i].nValue = n;
            switch (controls[i].pOptionDesc->eOption)
            {
            case OPTION_VOLUME_SFX:
                theWorld.GetSoundManager()->SetFXMasterVolume(n);
                break;
            case OPTION_VOLUME_STREAM:
                theWorld.GetSoundManager()->SetMusicMasterVolume(n);
                break;
            case OPTION_VOLUME_VOICE:
                theWorld.GetSoundManager()->SetSpeechMasterVolume(n);
                break;
            case OPTION_BRIGHTNESS:
            case OPTION_CONTRAST:
            case OPTION_GAMMA: {
                float fBrightness = controls[OPTION_BRIGHTNESS].nValue / 50.f - 1;
                float fContrast = controls[OPTION_CONTRAST].nValue / 50.f - 1;
                float fGamma = controls[OPTION_GAMMA].nValue / 50.f - 1;
                if (auto* renderer = theRenderer.GetD3D())
                    renderer->SetGammaCorrection(fBrightness, fContrast, fGamma);
            }
            break;
            }
        }
        if (controls[i].pSwitcher && controls[i].pSwitcher->GetState(n))
        {
            controls[i].nValue = n;
            switch (controls[i].pOptionDesc->eOption)
            {
            case OPTION_SHADOW_UNITS:
            case OPTION_SHADOW_BUILDINGS:
            case OPTION_SHADOW_FLORA:
                bShadowSettingsChanged = true;
                break;
            case OPTION_STEREO_REVERSE:
                theWorld.GetSoundManager()->SetStereoReverse(n ? true : false);
                break;
            }
        }
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::OnLButtonDown(UINT nFlags, CPoint point)
{
    if (bIntro)
        return;
    int ht = HitTest(point);
    switch (ht)
    {
    case 0: // OK
        ApplyParameters();
        pI3D->BeginFX("buttons\\messbox\\ok.wav");
        pI3D->ExitFromModal(0x00110001);
        break;
    case 1: // cancel
    {
        theWorld.GetSoundManager()->SetFXMasterVolume(theSettings.nAudioSFX);
        theWorld.GetSoundManager()->SetMusicMasterVolume(theSettings.nAudioStreaming);
        theWorld.GetSoundManager()->SetSpeechMasterVolume(theSettings.nAudioSpeech);
        pI3D->BeginFX("buttons\\messbox\\cancel.wav");
        pI3D->ExitFromModal(0x00110001);
    }
    break;
    }
    if (ht >= 2 && ht < 2 + TOTAL_NUM_OPTION_GROUPS)
    {
        pI3D->BeginFX("buttons\\save\\select.wav");
        bKeyInput = false;
        SetScreenMode(ht - 2);
    }
    if (ht >= 2 + TOTAL_NUM_OPTION_GROUPS)
    {
        int n = ht - 2 - TOTAL_NUM_OPTION_GROUPS;
        if (lines[n].pAction || lines[n].pControl)
        {
            pI3D->BeginFX("buttons\\save\\select.wav");
            nCurrentLine = n;
            bKeyInput = false;
            Redraw();
        }
    }
    const bool visualClicked = (helmetSwitcher && helmetSwitcher->IsInside(point))
        || (upperArmorSwitcher && upperArmorSwitcher->IsInside(point));
    CI3DScreen::OnLButtonDown(nFlags, point);
    if (visualClicked)
        CommitPartyVisualOptions();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    if (bIntro)
        return;
    int ht = HitTest(point);
    if (ht >= 2 + TOTAL_NUM_OPTION_GROUPS)
    {
        int n = ht - 2 - TOTAL_NUM_OPTION_GROUPS;
        if (lines[n].pAction)
        {
            pI3D->BeginFX("buttons\\save\\select.wav");
            nCurrentLine = n;
            bKeyInput = true;
            Redraw();
        }
        else if (lines[n].pControl && lines[n].pControl->pSwitcher)
        {
            int i;
            lines[n].pControl->pSwitcher->GetState(i);
            lines[n].pControl->pSwitcher->SetState(i + 1);
            pI3D->BeginFX("buttons\\messbox\\ok.wav");
            Redraw();
        }
    }
    const bool visualClicked = (helmetSwitcher && helmetSwitcher->IsInside(point))
        || (upperArmorSwitcher && upperArmorSwitcher->IsInside(point));
    CI3DScreen::OnLButtonDblClk(nFlags, point);
    if (visualClicked)
        CommitPartyVisualOptions();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if (bIntro)
        return;
    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);

    if (bKeyInput)
    {
        int nCode = nFlags & 0x1ff;
        SetProcessed();
        if (nCode == 1) // escape
        {
            bKeyInput = false;
            Redraw();
            return;
        }
        pKeyDesc = 0;
        for (int i = 0; i < TOTAL_NUM_KBD_KEYS; i++)
            if (NKeyMap::allKeys[i].nScanCode == nCode)
            {
                pKeyDesc = &NKeyMap::allKeys[i];
                break;
            }
        if (!pKeyDesc)
        {
            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
            return;
        }

        bKeyInput = false;

        SKeyToActionRemap *pCurAction = lines[nCurrentLine].pAction;
        if (!pCurAction)
        {
            Redraw();
            return;
        }

        pI3D->BeginFX("buttons\\messbox\\ok.wav");

        EKbdActions eOldAction = (EKbdActions)keyMap[pKeyDesc->nScanCode];
        SKeyToActionRemap *pOldAction = 0;
        std::vector<SKeyToActionRemap>::iterator ita;
        for (ita = actions.begin(); ita != actions.end(); ++ita)
            if (ita->pActionDesc->eAction == eOldAction)
            {
                pOldAction = &*ita;
                break;
            }

        if (pOldAction && pOldAction != pCurAction)
        {
            CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "key_already_mapped",
                                                                     "key_already_mapped_msg",
                                                                     0x00010000);
            CString szTmp, szOut;
            theWorld.GetTextManager()->GetSystemString("key_already_mapped_msg", szTmp);
            szOut.Format(szTmp, pKeyDesc->pszLocalKeyName, pOldAction->szActionLocalName);
            pScr->SetText(szOut);
            pI3D->DoModal(pScr);
            return;
        }

        DoRemap();
        Redraw();
    }
    else
    {
        switch (nChar)
        {
        case VK_ESCAPE: {
            theWorld.GetSoundManager()->SetFXMasterVolume(theSettings.nAudioSFX);
            theWorld.GetSoundManager()->SetMusicMasterVolume(theSettings.nAudioStreaming);
            theWorld.GetSoundManager()->SetSpeechMasterVolume(theSettings.nAudioSpeech);
            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
            pI3D->ExitFromModal(0x00110001);
        }
            SetProcessed();
            break;
        case 13:
            if (lines[nCurrentLine].pAction)
            {
                bKeyInput = true;
                pI3D->BeginFX("buttons\\messbox\\ok.wav");
                Redraw();
            }
            else if (lines[nCurrentLine].pControl && lines[nCurrentLine].pControl->pSwitcher)
            {
                int n;
                lines[nCurrentLine].pControl->pSwitcher->GetState(n);
                lines[nCurrentLine].pControl->pSwitcher->SetState(n + 1);
                pI3D->BeginFX("buttons\\messbox\\ok.wav");
                Redraw();
            }
            SetProcessed();
            break;
        case VK_LEFT:
            if (lines[nCurrentLine].pControl && lines[nCurrentLine].pControl->pSlider)
            {
                int n;
                lines[nCurrentLine].pControl->pSlider->GetPos(n);
                lines[nCurrentLine].pControl->pSlider->SetPos(n - 1);
            }
            break;
        case VK_RIGHT:
            if (lines[nCurrentLine].pControl && lines[nCurrentLine].pControl->pSlider)
            {
                int n;
                lines[nCurrentLine].pControl->pSlider->GetPos(n);
                lines[nCurrentLine].pControl->pSlider->SetPos(n + 1);
            }
            break;
        case VK_UP:
            pI3D->BeginFX("buttons\\save\\select.wav");
            PrevLine();
            SetProcessed();
            break;
        case VK_DOWN:
            pI3D->BeginFX("buttons\\save\\select.wav");
            NextLine();
            SetProcessed();
            break;
        case VK_PRIOR:
            pI3D->BeginFX("buttons\\save\\select.wav");
            SetScreenMode(nCurrentMode - 1);
            SetProcessed();
            break;
        case VK_NEXT:
            pI3D->BeginFX("buttons\\save\\select.wav");
            SetScreenMode(nCurrentMode + 1);
            SetProcessed();
            break;
        }
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::UpdatePartyVisualOptionsVisibility()
{
    const BOOL visible = nCurrentMode == 0;
    if (helmetSwitcher)
        helmetSwitcher->Show(visible);
    if (upperArmorSwitcher)
        upperArmorSwitcher->Show(visible);
    if (visible)
    {
        const int first = 2 + TOTAL_NUM_OPTION_GROUPS;
        if (helmetSwitcher)
            areas[first + 3]->SetToolTip(PARTY_HELMET_TIP);
        if (upperArmorSwitcher)
            areas[first + 9]->SetToolTip(PARTY_ARMOR_TIP);
    }
}
void CI3DScreenOptions::CommitPartyVisualOptions()
{
    if (nCurrentMode != 0 || !helmetSwitcher || !upperArmorSwitcher)
        return;
    CModPackage *mod = ModRuntime::Current();
    if (!mod || !mod->PartyVisualOptionsEnabled())
        return;
    int helmet = 0, upperArmor = 0;
    helmetSwitcher->GetState(helmet);
    upperArmorSwitcher->GetState(upperArmor);
    if ((helmet != 0) != mod->HidePartyHelmets()
        || (upperArmor != 0) != mod->HidePartyUpperArmor())
        mod->SetPartyVisualOptions(helmet != 0, upperArmor != 0);
}
void CI3DScreenOptions::Redraw()
{
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();
    pSurf->Clear();

    int i;
    COLORREF cr;

    figures[2]->Show(FALSE);
    CRect rc(10, 60, 140, 80);
    CPoint shift(0, 24);
    for (i = 0; i < TOTAL_NUM_OPTION_GROUPS; i++)
    {
        if (i == nCurrentMode)
        {
            cr = OPTIONS_COLOR_TEXT;
            figures[3 + i]->SetAmbient(1, 1, 1);
        }
        else
        {
            cr = RGB(160, 160, 160);
            figures[3 + i]->SetAmbient(.5f, .5f, .5f);
        }
        pSurf->DrawText(szModes[i], rc, 1, cr, DT_CENTER);
        rc += shift;
    }
    pSurf->DrawText(szModes[nCurrentMode], CRect(0, 10, 600, 34), 2, OPTIONS_COLOR_TEXT, DT_CENTER);

    rc = CRect(160, 60, 420, 80);
    CRect rc2(430, 60, 590, 80);

    for (i = 0; i < NUM_GROUP_LINES; i++)
    {
        if (i == nCurrentLine)
        {
            figures[2]->Show(TRUE);
            figures[2]->SetRelativeToScreen(471, 168 + i * 24, 3);
        }
        if (lines[i].pAction)
        {
            if (i == nCurrentLine && bKeyInput)
                cr = RGB(255, 0, 0);
            else
                cr = OPTIONS_COLOR_TEXT;
            pSurf->DrawText(lines[i].pAction->szActionLocalName, rc, 1, cr, DT_LEFT);
            pSurf->DrawText(lines[i].pAction->szKeysOutput, rc2, 1, cr, DT_LEFT);
        }
        else if (lines[i].pControl)
        {
            pSurf->DrawText(lines[i].pControl->szOptionLocalName, rc, 1, OPTIONS_COLOR_TEXT,
                            DT_LEFT);
        }
        rc += shift;
        rc2 += shift;
    }

    if (nCurrentMode == 0)
    {
        if (helmetSwitcher)
            pSurf->DrawText(PARTY_HELMET_LABEL, CRect(160, 132, 420, 152), 1, OPTIONS_COLOR_TEXT, DT_LEFT);
        if (upperArmorSwitcher)
            pSurf->DrawText(PARTY_ARMOR_LABEL, CRect(160, 276, 420, 296), 1, OPTIONS_COLOR_TEXT, DT_LEFT);
    }
    pSurf->ReleaseDC();
#endif // NOGETDC
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::FormActionsList()
{
    actions.clear();
    for (int i = 1; i < TOTAL_NUM_KBD_ACTIONS; i++)
    {
        SKeyToActionRemap action;
        actions.push_back(action);
        SKeyToActionRemap *pAction = &actions.back();

        pAction->pActionDesc = &NKeyMap::allActions[i];
        if (!theWorld.GetTextManager()->GetSystemString(CString("action_") +
                                                            NKeyMap::allActions[i].pszActionName,
                                                        pAction->szActionLocalName))
            pAction->szActionLocalName = NKeyMap::allActions[i].pszActionName;

        pAction->pKeysDesc.clear();
        std::map<int, EKbdActions>::const_iterator it;
        for (int k = 0; k < TOTAL_NUM_SCANCODES; k++)
            if (keyMap[k] == pAction->pActionDesc->eAction)
            {
                for (int j = 0; j < TOTAL_NUM_KBD_KEYS; j++)
                {
                    if (NKeyMap::allKeys[j].nScanCode == k &&
                        pAction->pKeysDesc.size() < NUM_ALTERN_KEYS)
                        pAction->pKeysDesc.push_back(&NKeyMap::allKeys[j]);
                }
            }
        pAction->FormKeysOutput();
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::FormAudioDevices()
{
    audioDeviceControl = {};
    CStringArray names, ids;
    MiniaudioOneShotSfx::GetPlaybackDevices(names, ids);
    audioDeviceNames.RemoveAll();
    audioDeviceIds.RemoveAll();
    audioDeviceNames.Add("\xCF\xEE\x20\xF3\xEC\xEE\xEB\xF7\xE0\xED\xE8\xFE");
    audioDeviceIds.Add("");
    int selected = theSettings.szAudioDevice.IsEmpty() ? 0 : -1;
    for (int i = 0; i < names.GetSize(); ++i)
    {
        audioDeviceNames.Add(names[i]);
        audioDeviceIds.Add(ids[i]);
        if (ids[i] == theSettings.szAudioDevice) selected = i + 1;
    }
    if (selected < 0)
    {
        selected = int(audioDeviceIds.GetSize());
        audioDeviceNames.Add("\xCD\xE5\xE4\xEE\xF1\xF2\xF3\xEF\xED\xEE");
        audioDeviceIds.Add(theSettings.szAudioDevice);
    }
    audioDeviceControl.szOptionLocalName = "\xD3\xF1\xF2\xF0\xEE\xE9\xF1\xF2\xE2\xEE\x20\xE7\xE2\xF3\xEA\xE0";
    auto* control = dbgnew CI3DSwitcher(pI3D, CRect(530, 304, 690, 320), audioDeviceNames[0]);
    for (int i = 1; i < audioDeviceNames.GetSize(); ++i) control->AddState(audioDeviceNames[i]);
    control->SetState(selected);
    audioDeviceControl.pSwitcher = control;
    AddObject(control);
}

void CI3DScreenOptions::FormControlsList()
{
    NKeyMap::allOptions[OPTION_DIFFICULTY].eType = NKeyMap::SpellAddonDifficultyLevels()
        ? OPTION_TYPE_SCROLL : OPTION_TYPE_SWITCH;
    controls.clear();
    for (int i = 0; i < TOTAL_NUM_GAME_OPTIONS; i++)
    {
        SOptionControl control;
        controls.push_back(control);
        SOptionControl *pControl = &controls.back();
        pControl->nValue = 0;
        pControl->pOptionDesc = &NKeyMap::allOptions[i];

        if (!theWorld.GetTextManager()->GetSystemString(CString("option_") +
                                                            NKeyMap::allOptions[i].pszOptionName,
                                                        pControl->szOptionLocalName))
            pControl->szOptionLocalName = NKeyMap::allOptions[i].pszOptionName;

        int nLine = pControl->pOptionDesc->nLine;
        switch (pControl->pOptionDesc->eType)
        {
        case OPTION_TYPE_SCROLL: {
            pControl->pSlider = dbgnew CI3DHScrollBar(pI3D, CRect(0, 0, 100, 100), 3,
                                                      pControl->pOptionDesc->nRange);
            pControl->pSlider->SetRect(CRect(530, 163 + nLine * 24, 680, 173 + nLine * 24));
            AddObject(pControl->pSlider);
            pControl->pSwitcher = 0;
        }
        break;
        case OPTION_TYPE_SWITCH: {
            CString szState;
            szState = "OFF";
            IWorld.GetTextManager()->GetSystemString("option_off", szState);
            if (i == OPTION_DIFFICULTY)
                IWorld.GetTextManager()->GetSystemString("difficulty_hard", szState);
            pControl->pSwitcher = dbgnew CI3DSwitcher(
                pI3D, CRect(530, 160 + nLine * 24, 690, 176 + nLine * 24), szState);
            szState = "ON";
            IWorld.GetTextManager()->GetSystemString("option_on", szState);
            if (i == OPTION_DIFFICULTY)
            {
                IWorld.GetTextManager()->GetSystemString("difficulty_norm", szState);
                pControl->pSwitcher->AddState(szState);
                IWorld.GetTextManager()->GetSystemString("difficulty_easy", szState);
            }
            pControl->pSwitcher->AddState(szState);
            AddObject(pControl->pSwitcher);
            pControl->pSlider = 0;
        }
        break;
        }
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::NextLine()
{
    int i = nCurrentLine + 1;
    while (i < NUM_GROUP_LINES && !lines[i].pAction && !lines[i].pControl)
        i++;
    if (i < NUM_GROUP_LINES)
    {
        nCurrentLine = i;
        Redraw();
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::PrevLine()
{
    int i = nCurrentLine - 1;
    while (i >= 0 && !lines[i].pAction && !lines[i].pControl)
        i--;
    if (i >= 0)
    {
        nCurrentLine = i;
        Redraw();
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::SetScreenMode(int nMode)
{
    int i;
    CString szTmp;
    if (nMode < 0)
        nMode = 0;
    if (nMode >= TOTAL_NUM_OPTION_GROUPS)
        nMode = TOTAL_NUM_OPTION_GROUPS - 1;
    nCurrentMode = nMode;
    if (audioDeviceControl.pSwitcher) audioDeviceControl.pSwitcher->Show(nMode == OG_SOUND);

    for (i = 0; i < NUM_GROUP_LINES; i++)
    {
        lines[i].pAction = 0;
        lines[i].pControl = 0;
        areas[2 + TOTAL_NUM_OPTION_GROUPS + i]->SetToolTip(0);
    }
    for (i = 0; i < TOTAL_NUM_GAME_OPTIONS; i++)
        if (NKeyMap::allOptions[i].nGroup == nMode)
        {
            lines[NKeyMap::allOptions[i].nLine].pControl = &controls[i];
            szTmp.Format("tip option_%s", NKeyMap::allOptions[i].pszOptionName);
            if (theWorld.GetTextManager()->GetText(szTmp))
                theWorld.GetTextManager()->GetUntilNextCommand(szTmp);
            areas[2 + TOTAL_NUM_OPTION_GROUPS + NKeyMap::allOptions[i].nLine]->SetToolTip(szTmp);
        }
    for (i = 1; i < TOTAL_NUM_KBD_ACTIONS; i++)
        if (NKeyMap::allActions[i].nGroup == nMode)
        {
            lines[NKeyMap::allActions[i].nLine].pAction = &actions[i - 1];
            szTmp.Format("tip action_%s", NKeyMap::allActions[i].pszActionName);
            if (theWorld.GetTextManager()->GetText(szTmp))
                theWorld.GetTextManager()->GetUntilNextCommand(szTmp);
            areas[2 + TOTAL_NUM_OPTION_GROUPS + NKeyMap::allActions[i].nLine]->SetToolTip(szTmp);
        }

    for (i = 0; i < TOTAL_NUM_GAME_OPTIONS; i++)
        if (controls[i].pSlider)
            controls[i].pSlider->Show(FALSE);
    for (i = 0; i < TOTAL_NUM_GAME_OPTIONS; i++)
        if (controls[i].pSwitcher)
            controls[i].pSwitcher->Show(FALSE);

    for (i = 0; i < NUM_GROUP_LINES; i++)
        if (lines[i].pControl)
        {
            if (lines[i].pControl->pSlider)
                lines[i].pControl->pSlider->Show(TRUE);
            if (lines[i].pControl->pSwitcher)
                lines[i].pControl->pSwitcher->Show(TRUE);
        }
    if (nMode == OG_SOUND && audioDeviceControl.pSwitcher)
    {
        lines[6].pControl = &audioDeviceControl;
        areas[2 + TOTAL_NUM_OPTION_GROUPS + 6]->SetToolTip("\xC2\xFB\xE1\xEE\xF0\x20\xEF\xF0\xE8\xEC\xE5\xED\xFF\xE5\xF2\xF1\xFF\x20\xEF\xEE\xF1\xEB\xE5\x20\xEF\xE5\xF0\xE5\xE7\xE0\xEF\xF3\xF1\xEA\xE0\x20\xE8\xE3\xF0\xFB\x2E");
    }
    nCurrentLine = -1;
    NextLine();
    if (nCurrentLine == -1)
        nCurrentLine = 0;
    bKeyInput = false;
    UpdatePartyVisualOptionsVisibility();
    Redraw();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::DoRemap()
{
    if (!pKeyDesc || !lines[nCurrentLine].pAction)
        return;
    EKbdActions eOldAction = (EKbdActions)keyMap[pKeyDesc->nScanCode];

    SKeyToActionRemap *pOldAction = 0;
    std::vector<SKeyToActionRemap>::iterator ita;
    for (ita = actions.begin(); ita != actions.end(); ++ita)
        if (ita->pActionDesc->eAction == eOldAction)
        {
            pOldAction = &*ita;
            break;
        }

    SKeyToActionRemap *pCurAction = lines[nCurrentLine].pAction;

    if (pCurAction != pOldAction)
    {
        std::vector<SKbdKeyDesc *>::iterator it;
        // this key is not on this action
        if (pCurAction->pKeysDesc.size() >= NUM_ALTERN_KEYS)
        {
            // remove all keys from this action
            for (it = pCurAction->pKeysDesc.begin(); it != pCurAction->pKeysDesc.end(); ++it)
                keyMap[(*it)->nScanCode] = KA_UNKNOWN;
            pCurAction->pKeysDesc.clear();
        }
        pCurAction->pKeysDesc.push_back(pKeyDesc);
        pCurAction->FormKeysOutput();

        // check if we need to remove this key from other action
        if (pOldAction)
        {
            bool bFound = false;
            for (it = pOldAction->pKeysDesc.begin(); it != pOldAction->pKeysDesc.end(); ++it)
                if ((*it) == pKeyDesc)
                {
                    bFound = true;
                    break;
                }
            if (bFound)
            {
                pOldAction->pKeysDesc.erase(it);
                pOldAction->FormKeysOutput();
            }
        }
    }

    keyMap[pKeyDesc->nScanCode] = pCurAction->pActionDesc->eAction;
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::GetParameters()
{
    memcpy(keyMap, NKeyMap::keyMap, TOTAL_NUM_SCANCODES);

    controls[OPTION_VOLUME_SFX].nValue = theSettings.nAudioSFX;
    controls[OPTION_VOLUME_STREAM].nValue = theSettings.nAudioStreaming;
    controls[OPTION_VOLUME_VOICE].nValue = theSettings.nAudioSpeech;
    controls[OPTION_STEREO_REVERSE].nValue = theSettings.nReverseStereo;

    controls[OPTION_POWER_KBD].nValue = int(theSettings.cctCameraScroll.fpKeyboardPower * 50);
    controls[OPTION_POWER_MOUSE].nValue = int(theSettings.cctCameraScroll.fpMousePower * 50);
    controls[OPTION_REVERSE_CAMERA_X].nValue = (theSettings.fCameraXAxisDir == 1 ? 0 : 1);
    controls[OPTION_REVERSE_CAMERA_Y].nValue = (theSettings.fCameraYAxisDir == 1 ? 0 : 1);

    controls[OPTION_SCROLL_BORDER].nValue = theSettings.nCameraBorderScrollArea;
    controls[OPTION_RUBBER_SELECT].nValue = int(theSettings.nCameraFrameSelectionSensetiveArea /
                                                10);
    controls[OPTION_TOOLTIP_TIME].nValue = int(theSettings.fToolTipTime * 4);

    controls[OPTION_SHADOW_UNITS].nValue = theSettings.nUnitShadow;
    controls[OPTION_SHADOW_BUILDINGS].nValue = theSettings.nBuildingShadow;
    controls[OPTION_SHADOW_FLORA].nValue = theSettings.nFloraShadow;
    controls[OPTION_MARKS_ON].nValue = theSettings.nTerrainEnableBloodprints;
    controls[OPTION_FOOTPRINTS].nValue = theSettings.nTerrainEnableFootprints;
    controls[OPTION_SELECTION_TYPE].nValue = theSettings.nSelectionType;
    controls[OPTION_SHOW_PATH].nValue = theSettings.nDrawPath;
    controls[OPTION_SHOW_FLYINGHP].nValue = theSettings.nDrawFlyingHP;
    controls[OPTION_SHOW_TUTORIAL].nValue = theSettings.nShowTutorial;
    controls[OPTION_AUTOSAVE].nValue = theSettings.nDoAutosave;
    controls[OPTION_SWITCH_FILTERS].nValue = theSettings.nSwitchFilters;

    controls[OPTION_BRIGHTNESS].nValue = int((theSettings.fBrightness + 1) * 50);
    controls[OPTION_CONTRAST].nValue = int((theSettings.fContrast + 1) * 50);
    controls[OPTION_GAMMA].nValue = int((theSettings.fGamma + 1) * 50);

    controls[OPTION_DIFFICULTY].nValue = NKeyMap::SpellAddonDifficultyLevels()
        ? 2 - theSettings.nDifficultyLevel : theSettings.nDifficultyLevel;

    for (int i = 0; i < TOTAL_NUM_GAME_OPTIONS; i++)
        controls[i].ApplyValueToControl();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenOptions::ApplyParameters()
{
    memcpy(NKeyMap::keyMap, keyMap, TOTAL_NUM_SCANCODES);

    if (audioDeviceControl.pSwitcher)
    {
        int selected = 0;
        audioDeviceControl.pSwitcher->GetState(selected);
        if (selected >= 0 && selected < audioDeviceIds.GetSize())
            theSettings.szAudioDevice = audioDeviceIds[selected];
    }
    theSettings.nAudioSFX = controls[OPTION_VOLUME_SFX].nValue;
    theSettings.nAudioStreaming = controls[OPTION_VOLUME_STREAM].nValue;
    theSettings.nAudioSpeech = controls[OPTION_VOLUME_VOICE].nValue;
    theSettings.nReverseStereo = controls[OPTION_STEREO_REVERSE].nValue;

    float fPowerKbd = controls[OPTION_POWER_KBD].nValue / 50.f;
    float fPowerMouse = controls[OPTION_POWER_MOUSE].nValue / 50.f;
    theSettings.cctCameraScroll.fpKeyboardPower = fPowerKbd;
    theSettings.cctCameraPitch.fpKeyboardPower = fPowerKbd;
    theSettings.cctCameraZoom.fpKeyboardPower = fPowerKbd;
    theSettings.cctCameraRotate.fpKeyboardPower = fPowerKbd;
    theSettings.cctCameraScroll.fpMousePower = fPowerMouse;
    theSettings.cctCameraPitch.fpMousePower = fPowerMouse;
    theSettings.cctCameraZoom.fpMousePower = fPowerMouse;
    theSettings.cctCameraRotate.fpMousePower = fPowerMouse;
    theSettings.fCameraXAxisDir = (controls[OPTION_REVERSE_CAMERA_X].nValue == 1 ? -1 : 1);
    theSettings.fCameraYAxisDir = (controls[OPTION_REVERSE_CAMERA_Y].nValue == 1 ? -1 : 1);
    theSettings.nCameraBorderScrollArea = controls[OPTION_SCROLL_BORDER].nValue;
    theSettings.nCameraFrameSelectionSensetiveArea = controls[OPTION_RUBBER_SELECT].nValue * 10;
    theSettings.fToolTipTime = controls[OPTION_TOOLTIP_TIME].nValue / 4.f;

    theSettings.nUnitShadow = controls[OPTION_SHADOW_UNITS].nValue;
    theSettings.nBuildingShadow = controls[OPTION_SHADOW_BUILDINGS].nValue;
    theSettings.nFloraShadow = controls[OPTION_SHADOW_FLORA].nValue;
    theSettings.nTerrainEnableBloodprints = controls[OPTION_MARKS_ON].nValue;
    theSettings.nTerrainEnableFootprints = controls[OPTION_FOOTPRINTS].nValue;
    theSettings.nSelectionType = controls[OPTION_SELECTION_TYPE].nValue;
    theSettings.nDrawPath = controls[OPTION_SHOW_PATH].nValue;
    theSettings.nDrawFlyingHP = controls[OPTION_SHOW_FLYINGHP].nValue;
    if(!ModRuntime::Resources()||!ModRuntime::Resources()->ForceTutorial())
        theSettings.nShowTutorial = controls[OPTION_SHOW_TUTORIAL].nValue;
    theSettings.nDoAutosave = controls[OPTION_AUTOSAVE].nValue;
    theSettings.nSwitchFilters = controls[OPTION_SWITCH_FILTERS].nValue;

    theSettings.fBrightness = controls[OPTION_BRIGHTNESS].nValue / 50.f - 1;
    theSettings.fContrast = controls[OPTION_CONTRAST].nValue / 50.f - 1;
    theSettings.fGamma = controls[OPTION_GAMMA].nValue / 50.f - 1;

    {
        const int value = controls[OPTION_DIFFICULTY].nValue;
        theSettings.nDifficultyLevel = theSettings.nDifficultyLevel2 =
            NKeyMap::SpellAddonDifficultyLevels() ? 2 - value : value;
    }

    if (bShadowSettingsChanged)
        theWorld.RemoveShadowInformationFromObjects();
}
////////////////////////////////////////////////////////////////////////////////
