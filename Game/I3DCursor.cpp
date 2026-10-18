#include "StdAfx.h"
#include "OwnedRender/E2Host.h"
#include "OwnedRender/E3Cursor.h"
static const char LOCAL_FILE[] = __FILE__;
///////////////////////////////////////////////////////////////////////////////////
#include "I3DCursor.h"
#include "TextureManager.h"
#include "WorldClient.h"
#include "dx11\RendererDX11.h"
#include "dx11\Surface.h"

#ifdef _A4
const int N_STREAMING_CURSOR_UPDATE = 1000 / 36;
#else
const int N_STREAMING_CURSOR_UPDATE = 1000 / 24; // Курсор перерисовывается 36 раз в секунду
#endif                                       //_A4
const int N_ANIMATION_TIME_PHASE = 1000 / 8; // Время между сменами фаз курсора
CSize sizeBackBuffer(48, 48);
///////////////////////////////////////////////////////////////////////////////////
CI3DCursor theCursor;
///////////////////////////////////////////////////////////////////////////////////
SCursor2DInfo::SCursor2DInfo(const char *szImageFileName, int x, int y)
{
    dwCurAnim = 0;
    SMMPFileHeader *pHdr = (SMMPFileHeader *)theWorld.GetTextureManager()->GetFile(szImageFileName);
    static std::uint64_t nextImage = 0;
    auto strip = std::make_shared<OwnedRender::E3CursorStrip>();
    strip->id = ++nextImage; strip->epoch = 1;
    strip->width = pHdr->dwDimentionX; strip->height = pHdr->dwDimentionY;
    strip->frames = strip->height ? strip->width / strip->height : 0;
    strip->frame_width = strip->frames ? strip->width / strip->frames : 0;
    strip->pixels.resize(size_t(strip->width) * strip->height);
    memcpy(strip->pixels.data(), pHdr->GetMipLevelData(0), strip->pixels.size() * 4);
    theWorld.GetTextureManager()->CloseFile((BYTE*)pHdr);
    dwAnimNumb = strip->frames;
    sizeCursor = CSize(strip->frame_width, strip->height);
    image = std::move(strip);
    ptHotSpot.x = x * sizeBackBuffer.cx / 48;
    ptHotSpot.y = y * sizeBackBuffer.cy / 48;
}

void SCursor2DInfo::GetCursorRect(CRect &rect)
{
    rect.left = dwCurAnim * sizeCursor.cx;
    rect.top = 0;
    rect.right = rect.left + sizeCursor.cx;
    rect.bottom = sizeCursor.cy;
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DCursor::CI3DCursor()
{
    m_isInitialized = false;
}
/////////////////////////////////////////////////////////////////////////////////////
int CI3DCursor::SetMode(int nType)
{
    OwnedRender::E2ClearHoveredObject();
    if (m_eCursorType == -1 || !m_isInitialized)
        return -1;
    else
    {
        ASSERT(m_aCursorInfo.GetSize() > 0);
        int eOldType = m_eCursorType;
        m_eCursorType = nType;

        m_pCursorInfo = m_aCursorInfo[nType];
        m_pCursorInfo->GetCursorRect(m_rectSrcCur);
        Publish();

        return eOldType;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::Show(BOOL bShow)
{
    if (!m_isInitialized)
        return;

    m_isShow = bShow;
    Publish();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DCursor::UpdateCursorState()
{
    // I: Update cursor animation
    DWORD dwCurTime = timeGetTime();
    DWORD dwSkipPhase = (dwCurTime - m_dwAnimLastTime) / N_ANIMATION_TIME_PHASE;
    m_pCursorInfo->dwCurAnim += dwSkipPhase;
    m_pCursorInfo->dwCurAnim %= m_pCursorInfo->dwAnimNumb;
    m_dwAnimLastTime += dwSkipPhase * N_ANIMATION_TIME_PHASE;
    bool isUpdated = (dwSkipPhase != 0);

    // II: Update cursor position
    {
        {
            // update increment
            CPoint cursorPos;
            OwnedRender::E3ReadCursor(&cursorPos);
            if (m_isLock) m_ptLastInc += cursorPos - m_ptLastCursor;
            else m_ptLastInc = cursorPos - m_ptLastCursor;

            if (m_isLock)
            {
                DWORD s_width = m_rectScreen.right;
                DWORD s_height = m_rectScreen.bottom;
                if (cursorPos.x >= s_width - 1)
                    cursorPos.x = 1;
                else if (cursorPos.x == 0)
                    cursorPos.x = s_width - 2;

                if (cursorPos.y >= s_height - 1)
                    cursorPos.y = 1;
                else if (cursorPos.y == 0)
                    cursorPos.y = s_height - 2;
                OwnedRender::E3WriteCursor(cursorPos.x, cursorPos.y);
            }
            m_ptLastCursor = cursorPos;
        }

        CPoint point;
        if (m_isLock)
            point = m_ptLockedCursor;
        else
        {
            OwnedRender::E3ReadCursor(&point);
            m_ptLockedCursor = point;
        }
        if (point.x < 0)
            point.x = 0;
        if (point.y < 0)
            point.y = 0;
        if (point.x > m_rectScreen.right)
            point.x = m_rectScreen.right;
        if (point.y > m_rectScreen.bottom)
            point.y = m_rectScreen.bottom;
        if (m_ptCursor != point)
            isUpdated = true;

        m_ptCursor = point;
    }

    // III: Update cursor rect for animation
    m_pCursorInfo->GetCursorRect(m_rectSrcCur);
    CPoint ptDrawCursorPos = GetDrawCursorPos();
    m_rectDstCur = CRect(ptDrawCursorPos, m_rectSrcCur.Size());
    m_rectDstCur.IntersectRect(m_rectDstCur, m_rectScreen);
    CPoint ptTemp = m_rectSrcCur.TopLeft() + m_rectDstCur.TopLeft() - ptDrawCursorPos;
    m_rectSrcCur = CRect(ptTemp, m_rectDstCur.Size());

    return isUpdated;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::Publish()
{
    OwnedRender::E3CursorState state;
    if (m_isInitialized && m_pCursorInfo && m_pCursorInfo->dwAnimNumb) {
        state.strip = m_pCursorInfo->image;
        state.position = m_ptCursor; state.hotspot = m_pCursorInfo->ptHotSpot;
        state.frame = m_pCursorInfo->dwCurAnim; state.phase_time = m_dwAnimLastTime;
        state.visible = m_isShow != FALSE; state.locked = m_isLock;
    }
    OwnedRender::E3CursorPublish(std::move(state));
}
void CI3DCursor::Draw()
{
    if (!GetRenderer()->IsActive() || !m_isInitialized || !m_pCursorInfo)
        return;
    UpdateCursorState();
    Publish();
}
///////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::Init()
{
    sizeBackBuffer = CSize(48, 48);
    m_ptCursor = CPoint(0, 0);
    m_pCursorInfo = NULL;
    m_nScipCounter = 0;

    m_eCursorType = -1;
    m_isShow = TRUE;
    m_isInitialized = true;

    m_dwAnimLastTime = timeGetTime();
    m_isLock = false;
    m_ptLastInc = m_ptLastCursor = m_ptLockedCursor = CPoint(0, 0);
    m_rectScreen.SetRectEmpty();
    GetRenderer()->GetCurrentMode((DWORD &)m_rectScreen.right, (DWORD &)m_rectScreen.bottom);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::StartProcessing()
{
    if (!m_isInitialized) return;
    m_eCursorType = 0;
    SetMode(0);
    OwnedRender::E3CursorStart();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::Fini()
{
    OwnedRender::E3CursorStop();
    for (int i = 0; i < m_aCursorInfo.GetSize(); i++)
    {
        m_aCursorInfo[i]->image.reset();
        delete m_aCursorInfo[i];
    }
    m_aCursorInfo.RemoveAll();

    m_ptCursor = CPoint(0, 0);
    m_pCursorInfo = NULL;
    m_nScipCounter = 0;
    m_eCursorType = -1;
    m_isInitialized = false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::LoadCursor(const char *szName, int x, int y)
{
    SCursor2DInfo *pCursorInfo = dbgnew SCursor2DInfo(CString(szName) + ".mmp", x, y);
    m_aCursorInfo.Add(pCursorInfo);
}
///////////////////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::BeginDraw()
{
    if (!m_isInitialized)
        return;
}
///////////////////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::EndDraw()
{
    if (!m_isInitialized)
        return;
}
/////////////////////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::SetPos(CPoint ptPos)
{
    if (!m_isInitialized)
        return;
    if (m_isLock)
    {
        m_ptLockedCursor = ptPos;
    }
    else
        OwnedRender::E3WriteCursor(ptPos.x, ptPos.y);
    m_ptCursor = ptPos;
    Publish();
}
/////////////////////////////////////////////////////////////////////////////////////////////////
CPoint CI3DCursor::GetIncrement()
{
    if (m_isInitialized && m_pCursorInfo) UpdateCursorState();
    return m_ptLastInc;
}
void CI3DCursor::LockPos(bool bLock)
{
    if (!m_isInitialized)
        return;

    if (m_pCursorInfo) UpdateCursorState();
    m_ptLastInc = CPoint(0, 0);
    m_isLock = bLock;
    Publish();
}
///////////////////////////////////////////////////////////////////////////////////////////////
