#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenEndGame.h"
#include "I3DScreenMessageBox.h"
#include "I3DScreenSaveLoad.h"
#include "WorldInterface.h"
#include "RTFText.h"
#include "Camera.h"
#include "I3DEffects.h"
#include "WorldClient.h"
#include "EString.h"
#include "TextManager.h"
#include "GameMain.h"

#include "A3Cursor.h"
#include "MusicSystem.h"
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenEndGame::OnActivate()
{
    if (!bFromMainMenu)
    {
        theWorld.GetSoundManager()->StopAllSounds(true);
        theWorld.GetMusicSystem()->SetMuteMode();
    }
    /*
    if ( bFromMainMenu )
    {
            CI3DSurface* pSurface;
            pSurface = dbgnew CI3DSurface( pI3D, "logo" );
            pSurface->SetRect( CRect( 540, 10, 790, 90 ) );
            pSurface->SetFlag( BS_STRETCH, TRUE );
            pSurface->Show( TRUE );
            AddSurface( pSurface );

            pSurface = dbgnew CI3DSurface( pI3D, 100, 550, 780, 580 );
            pSurface->Clear();
            pSurface->AcquireDC();
            CString szVersion;
            theWorld.GetTextManager()->GetSystemString( "version_name", szVersion );
            pSurface->DrawText( szVersion, CRect( 0, 0, 675, 30 ), 2, RGB( 228, 215, 167 ), DT_RIGHT
    ); pSurface->ReleaseDC(); AddSurface( pSurface );
    }
    else
    */
    theCursor.Show(FALSE);
    NGame::PlayMoviesSequence(mszBinkName + "fin", true, false, false, false);

    m_fWaitTime = 0;

    SystemParametersInfo(SPI_GETFONTSMOOTHING, UINT(&m_isFontSmooth), 0, 0);
    SystemParametersInfo(SPI_SETFONTSMOOTHING, FALSE, 0, 0);

    VERIFY(m_resText.OpenResource(theSettings.szResourcePath + "outro.res"));
    DWORD dwLen;
    BYTE *pData = m_resText.GetFile("credits.scr", &dwLen);
    if (pData)
    {
        EString szScript;
        memmove(szScript.GetBufferSetLength(dwLen), pData, dwLen);
        szScript.ReleaseBuffer();
        szScript.DeleteSlashR();
        EStringIterator it(szScript, "\n");
        for (; it; it.NextLexem())
        {
            CString szSub = &*it;
            EStringIterator subIt(szSub, " ");
            SShowText st;
            st.isScroll = CString(subIt).CompareNoCase("#scroll") == 0;
            subIt.NextLexem();
            st.fValue = subIt.Value();
            subIt.NextLexem();
            st.szFile = subIt;
            subIt.NextLexem();
            st.szTitle = subIt;
            m_aTexts.push_back(st);
        }
        m_itCurText = m_aTexts.begin();
        //
        m_resText.CloseFile(pData);
    }
    else
        m_itCurText = m_aTexts.end();

    m_pText = static_cast<CI3DTetxRTF *>(
        AddSurface(dbgnew CI3DTetxRTF(pI3D, CRect(10, 150, 790, 450))));
    m_pText->Show(false);
    m_pTitle = AddSurface(dbgnew CI3DSurface(pI3D, CRect(10, 110, 790, 140)));
    m_pTitle->Show(false);

    if (bFromMainMenu)
        IWorld.CombatPause(false);

    dwOldBGColor = GetRenderer()->GetBGColor();
    GetRenderer()->SetBGColor(0);

    //	if ( !bFromMainMenu )
    {
        bink.BeginPlay(IWorld.GetCDPath() + "Movies\\" + mszBinkName + ".bik", false);
    }
    bCreditsDone = false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenEndGame::OnDeactivate()
{
    m_aTexts.clear();
    SystemParametersInfo(SPI_SETFONTSMOOTHING, m_isFontSmooth, 0, 0);

    GetRenderer()->SetBGColor(dwOldBGColor);

    NGame::bNeedRender = true;
    theCursor.Show(TRUE);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreenEndGame::Draw(CRenderer *pRend)
{
    if (IWorldClient.GetPreciseTime() > m_fWaitTime && m_pText->GetScrollSpeed() == 0)
    {
        m_fWaitTime = 0;
        m_pText->Show(false);
        m_pTitle->Show(false);
        if (m_itCurText != m_aTexts.end())
        {
            m_pText->LoadFromRes(&m_resText, m_itCurText->szFile);
            m_pText->SetScrollSpeed(0, false);
            if (m_itCurText->isScroll)
            {
                m_pText->SetScrollSpeed(m_itCurText->fValue);
#ifndef NOGETDC
                CDC *pDC = m_pTitle->GetDC();
                m_pTitle->Clear();
                m_pTitle->DrawText(m_itCurText->szTitle, CRect(0, 0, 0, 0), 2, RGB(254, 254, 254),
                                   DT_CENTER);
                m_pTitle->ReleaseDC();
#endif // NOGETDC
                m_pTitle->Show(true);
            }
            else
            {
                m_fWaitTime = IWorldClient.GetPreciseTime() + m_itCurText->fValue;
            }
            m_pText->Show(true);
            ++m_itCurText;
        }
        else
        {
            //			if ( bFromMainMenu )
            //				pI3D->ExitFromModal( 0xCCCCCCCC );
            //			else
            bCreditsDone = true;
        }
    }
    //	if ( bFromMainMenu )
    //		IWorldClient.Draw();
    //	else
    {
        if (!bink.PlayNextFrame(NGame::IsFlipEnable() != true) && bCreditsDone)
        {
            bink.StopPlaying();
            NGame::PlayMoviesSequence(mszBinkName + "fout", false, false, false, false);
            NGame::bNeedRender = false;
            Exit();
            return TRUE;
        }
    }
    return CI3DScreen::Draw(pRend);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenEndGame::OnExitFromModal(DWORD dwRetValue)
{
    bink.StopPlaying();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenEndGame::OnLButtonDown(UINT nFlags, CPoint point)
{
    Exit();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenEndGame::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (nChar)
    {
    case VK_ESCAPE:
    case VK_SPACE:
    case 13:
        Exit();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenEndGame::Exit()
{
    if (bFromMainMenu)
        pI3D->ExitFromModal(0xCCCCCCCC);
    else
        IWorldConnector.ReqGoMainMenu();
}
/////////////////////////////////////////////////////////////////////////////////////
