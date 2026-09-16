#include "StdAfx.h"
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
    D3D11_TEXTURE2D_DESC tex2d;
    theRenderer.GetD3D()->DefaultSurfaceDesc(&tex2d);

    tex2d.Width = pHdr->dwDimentionX;
    tex2d.Height = pHdr->dwDimentionY;
    tex2d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex2d.MipLevels = pHdr->dwMipMapLevel;
    DWORD line_pitch = tex2d.Width * 4; // 32 bits, 8888
    std::unique_ptr<DWORD[]> data = std::make_unique<DWORD[]>(tex2d.Width * tex2d.Height);
    memcpy(data.get(), pHdr->GetMipLevelData(0), line_pitch * tex2d.Height);
    theWorld.GetTextureManager()->CloseFile((BYTE *)pHdr);

    DWORD *pSrc = data.get();
    DWORD black = 0xFF000000;
    for (int i = 0; i < tex2d.Width; ++i)
    {
        for (int j = 0; j < tex2d.Height; ++j)
        {
            if (*pSrc == black)
            {
                *pSrc = 0;
            }
            pSrc++;
        }
    }
    // load as colorkey
    pSurface = std::make_unique<CDXGISurface>(theRenderer.GetD3D(), &tex2d);

    D3D11_SUBRESOURCE_DATA srd = {};
    srd.pSysMem = data.get();
    srd.SysMemPitch = line_pitch;

    pSurface->UpdateTexture(0, nullptr, &srd);

    dwAnimNumb = tex2d.Width / tex2d.Height;
    sizeCursor.cx = tex2d.Width / dwAnimNumb;
    sizeCursor.cy = tex2d.Height;

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
    if (m_eCursorType == -1 || !m_isInitialized)
        return -1;
    else
    {
        ASSERT(m_aCursorInfo.GetSize() > 0);
        int eOldType = m_eCursorType;
        m_eCursorType = nType;

        m_pCursorInfo = m_aCursorInfo[nType];
        m_pCursorInfo->GetCursorRect(m_rectSrcCur);

        return eOldType;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::Show(BOOL bShow)
{
    if (!m_isInitialized)
        return;

    m_isShow = bShow;
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
            GetCursorPos(&cursorPos);
            m_ptLastInc = cursorPos - m_ptLastCursor;

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
                SetCursorPos(cursorPos.x, cursorPos.y);
            }
            m_ptLastCursor = cursorPos;
        }

        CPoint point;
        if (m_isLock)
            point = m_ptLockedCursor;
        else
        {
            GetCursorPos(&point);
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
void CI3DCursor::Draw()
{
    if (!GetRenderer()->IsActive() || !m_isInitialized)
        return;

    UpdateCursorState();
    if (m_rectDstCur.IsRectEmpty())
        return;

    CDX11 *pDX11 = GetRenderer()->GetD3D();
    if (m_isShow)
    {
        auto m_states = pDX11->GetCommonStates();
        pDX11->GetSpriteBatch()->Begin(DirectX::SpriteSortMode_Deferred,
                                       m_states->NonPremultiplied());
        m_pCursorInfo->pSurface->BltToRenderSurface(m_rectDstCur, m_rectSrcCur, 0, 0);
        pDX11->GetSpriteBatch()->End();
        // pDX11->GetSpriteBatch()->Draw(m_pCursorInfo->pSurface->texture, *m_rectDstCur,
        // m_rectSrcCur);
    };
}
///////////////////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::Init()
{
    sizeBackBuffer = CSize(48, 48);
    m_ptCursor = CPoint(0, 0);
    m_pCursorInfo = NULL;
    m_nScipCounter = 0;

    m_eCursorType = -1;
    m_isShow = TRUE;
    m_isInitialized = true;

    m_hExitEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    m_rectScreen.SetRectEmpty();
    GetRenderer()->GetCurrentMode((DWORD &)m_rectScreen.right, (DWORD &)m_rectScreen.bottom);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::StartProcessing()
{
    m_eCursorType = 0;
    SetMode(0);
    if (!m_isInitialized)
        return;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::Fini()
{
    DWORD dwRes;
    CloseHandle(m_hExitEvent);
    m_hExitEvent = 0;
    for (int i = 0; i < m_aCursorInfo.GetSize(); i++)
    {
        m_aCursorInfo[i]->pSurface = 0;
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
        SetCursorPos(ptPos.x, ptPos.y);
}
/////////////////////////////////////////////////////////////////////////////////////////////////
void CI3DCursor::LockPos(bool bLock)
{
    if (!m_isInitialized)
        return;

    m_isLock = bLock;
}
///////////////////////////////////////////////////////////////////////////////////////////////
