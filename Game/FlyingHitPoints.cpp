#include "StdAfx.h"
#include "OwnedRender/E3Scale.h"
#include "ModRuntime.h"
#include "ModPackage.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "FlyingHitPoints.h"

#include "WorldInterface.h"
#include "Texture.h"
#include "Unit.h"
#include "PlayerClient.h"
#include "Renderer.h"
#include "FigureClientSpecific.h"
/////////////////////////////////////////////////////////////////////////////////////
const int FHP_BACKSTAB_WIDTH = 64;
const int FHP_BACKSTAB_HEIGHT = 10;
const int FHP_CRITICAL_WIDTH = 64;
const int FHP_CRITICAL_HEIGHT = 10;
const int FHP_EXP_WIDTH = 64;
const int FHP_EXP_HEIGHT = 10;

const int FHP_SHIFTX = 120;
const int FHP_SHIFTY = -100;

const int FHP_WIDTH = 8;
const int FHP_HEIGHT = 12;

const float FHP_NUMBERS_MAP_HEIGHT = 128.f;
const float FHP_DIGIT_WIDTH = .125f;
const float FHP_DIGIT_HEIGHT = 19.f / FHP_NUMBERS_MAP_HEIGHT;
const float FHP_DIGIT_ROW_HEIGHT = 20.f / FHP_NUMBERS_MAP_HEIGHT;

// numbers.mmp texture has digits/phrases in rows
enum EGLYPH_ORDER
{
    EGLYPH_ROW_DIGITS_1 = 0, // 0123 !!!
    EGLYPH_ROW_EXCL = 0,     //  !!!
    EGLYPH_ROW_DIGITS_2 = 1, // 4567
    EGLYPH_ROW_DIGITS_3 = 2, // 89
    EGLYPH_ROW_BACKSTAB = 3, // ÑÎ ÑÏÈÍÛ
    EGLYPH_ROW_CRITICAL = 4, // Â ÃÎËÎÂÓ
    EGLYPH_ROW_EXP = 5       // ÎÏÛÒ
};

// a bit more accurate?
const float FHP_PHRASE_START = 59.f / FHP_NUMBERS_MAP_HEIGHT;
enum EGLYPH_PHRASE_ORDER
{
    EGLYPH_PHRASE_ROW_BACKSTAB = 0, // ÑÎ ÑÏÈÍÛ
    EGLYPH_PHRASE_ROW_CRITICAL = 1, // Â ÃÎËÎÂÓ
    EGLYPH_PHRASE_ROW_EXP = 2       // ÎÏÛÒ
};

struct QuadRect
{
    float left;
    float top;
    float width;
    float height;
};

/////////////////////////////////////////////////////////////////////////////////////
CFlyingHitPoints::CFlyingHitPoints()
{
    pTexture = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CFlyingHitPoints::~CFlyingHitPoints()
{
    if (pTexture)
        pTexture->Release();
}
/////////////////////////////////////////////////////////////////////////////////////
void CFlyingHitPoints::ReceiveDamage(int nUnitID, int nAmount, DWORD dwDamageFlags)
{
    SFlyingHPDesc hp;
    if (nUnitID != 0 && nAmount >= 0)
    {
        if (dwDamageFlags & HLDAMAGE_EXPERIENCE)
            hp.dwTime = timeGetTime() + 500;
        else
            hp.dwTime = timeGetTime();
        hp.nAmount = nAmount;
        hp.nUnitID = nUnitID;
        hp.dwDamageFlags = dwDamageFlags;
        damages.push_back(hp);
    }
}

struct QuadBatch
{
    DWORD capacity;
    STLVertex1 *pCurVertex, vertices[64];
    WORD *pCurIndex, indices[96];

    int nVertices, nIndices;
    int quadIdx;

    void Reset()
    {
        capacity = 16;
        quadIdx = 0;
        nVertices = 0;
        nIndices = 0;
        pCurVertex = vertices;
        pCurIndex = indices;
    };
    void AddQuad(QuadRect rect, QuadRect txcs, DWORD color, DWORD specular)
    {
        if (quadIdx == capacity)
            throw std::exception("Logic error");
        SetupQuad(pCurVertex, pCurIndex, quadIdx, rect, txcs, color, specular);
        quadIdx++;
        pCurVertex += 4;
        nVertices += 4;
        pCurIndex += 6;
        nIndices += 6;
    };

    void SetupQuad(STLVertex1 *pVertices, PWORD pIndices, DWORD quadIdx, QuadRect rect,
                   QuadRect txcs, DWORD color, DWORD specular)
    {
        // xyz
        CPlot3D shifts[4] = {
            { 0, 0, 0 }, // LT
            { 1, 0, 0 }, // RT
            { 0, 1, 0 }, // LB
            { 1, 1, 0 }, // RB
        };
        for (int i = 0; i < 4; ++i)
        {
            pVertices[i].sx = rect.left + shifts[i].x * rect.width;
            pVertices[i].sy = rect.top + shifts[i].y * rect.height;

            pVertices[i].tu = txcs.left + shifts[i].x * txcs.width;
            pVertices[i].tv = txcs.top + shifts[i].y * txcs.height;
            // common
            pVertices[i].sz = 1.0;
            pVertices[i].rhw = 1.0;
            pVertices[i].color = color;
            pVertices[i].specular = specular;
        }

        // counter-clockwise (same in OG)
        pIndices[0] = quadIdx * 4 + 0; // LT  |1st   3rd/
        pIndices[1] = quadIdx * 4 + 2; // LB  |    /
        pIndices[2] = quadIdx * 4 + 1; // RT  |2nd
        pIndices[3] = quadIdx * 4 + 1; // RT
        pIndices[4] = quadIdx * 4 + 2; // LB
        pIndices[5] = quadIdx * 4 + 3; // RB
    }
    void GetData(DWORD &nVerticesOut, DWORD &nIndicesOut, STLVertex1 *&verticesOut,
                 PWORD &indicesOut)
    {
        nVerticesOut = nVertices;
        nIndicesOut = nIndices;
        indicesOut = indices;
        verticesOut = vertices;
    }
};

/////////////////////////////////////////////////////////////////////////////////////
BOOL CFlyingHitPoints::Draw(CRenderer *pRend)
{
    const bool addonGraphics = ModRuntime::Current() &&
        ModRuntime::Current()->CombatSetting("JMVGraphics2");
    const int digitWidth = addonGraphics ? 15 : FHP_WIDTH;
    const int digitHeight = addonGraphics ? 23 : FHP_HEIGHT;
    CString szTmp;
    DWORD dwCurrentTime = timeGetTime();
    DWORD screenWidth, screenHeight;
    pRend->GetCurrentMode(screenWidth, screenHeight);

    if (!pTexture)
        return FALSE;

    if (!pRend->BindTexture(0, pTexture))
        return ReportError("CFlyingHitPoints::Draw.\n SetTexture failed.\n");

    // remove excess
    damages.remove_if(CNeedDeleteFlyingHPFunctional(dwCurrentTime));

    if (!theSettings.nDrawFlyingHP)
        return TRUE;

    SHMatrix world_to_screen = pRend->GetGPU()->GetWorldToScreenMatrix();
    pRend->SetProjectionMatrixOne();

    std::list<SFlyingHPDesc>::iterator it;
    for (it = damages.begin(); it != damages.end(); ++it)
    {
        float fpPos = float((int)dwCurrentTime - (int)it->dwTime) / FHP_FLYTIME;
        if (fpPos > 1 || fpPos < 0)
            continue;

        CUnit *pUnit = IWorldClient.GetUnit(it->nUnitID);
        if (!pUnit)
            continue;
        if (!pUnit->CheckFlag(OBJ_VISIBLE) || pUnit->CheckFlag(OBJ_HIDDEN) ||
            !pUnit->CheckFlag(UNIT_VISIBLE))
            continue;
        CFigure *pFig = pUnit->MatchFigure("hd");
        if (!pFig)
        {
            pFig = pUnit->MatchFigure("bd");
            if (!pFig)
            {
                pFig = pUnit->MatchFigure("hp");
                if (!pFig)
                    continue;
            }
        }
        CPoint pt;
        if (addonGraphics) {
            if (pFig->CheckFlag(FIG_VISIBLE))
                pt = static_cast<CFigureClientSpecific *>(pFig->GetFigureSpecific())
                         ->Get2DClipRect().CenterPoint();
            else
                pt = pUnit->GetScreenRect().CenterPoint();
        } else {
            CPlot3D unit_pos = pUnit->GetCP();
            auto proj_pos = world_to_screen.Rotate4Vector(unit_pos);
            pt = CPoint(int(proj_pos.x / proj_pos.w), int(proj_pos.y / proj_pos.w));
        }

        // DEBUG - show all flags
        /*it->dwDamageFlags |= HLDAMAGE_BACKSTAB;
        it->dwDamageFlags |= HLDAMAGE_CRITICAL;
        it->dwDamageFlags |= HLDAMAGE_EXPERIENCE;*/

        pt.x += int(fpPos * FHP_SHIFTX);
        pt.y += int(fpPos * FHP_SHIFTY);

        szTmp.Format("%d", it->nAmount);
        int nLength = szTmp.GetLength();
        const DWORD iconFlags[] = { HLDAMAGE_CRIT, HLDAMAGE_PARRY, HLDAMAGE_BLOCK,
                                    HLDAMAGE_CHEST, HLDAMAGE_POTION, HLDAMAGE_AVOID };
        bool showIcons[6] = {};
        int iconLength = 0;
        for (int i = 0; i < 6; ++i) {
            showIcons[i] = addonGraphics && (it->dwDamageFlags & iconFlags[i]);
            iconLength += int(showIcons[i]);
            if (addonGraphics && (((it->dwDamageFlags & HLDAMAGE_CRIT) && it->nAmount < 1) ||
                ((it->dwDamageFlags & HLDAMAGE_PARRY) && it->nAmount > 0)))
                iconLength = 0;
        }
        int nInfoWidth = (nLength + iconLength) * digitWidth;
        int nInfoHeight = digitHeight;
        if (addonGraphics && it->nAmount < 1) {
            nLength = 0;
            nInfoWidth = nInfoHeight = 0;
        }
        if (it->dwDamageFlags & HLDAMAGE_BACKSTAB) // backstab
        {
            nInfoWidth = Max(nInfoWidth, FHP_BACKSTAB_WIDTH);
            nInfoHeight += FHP_BACKSTAB_HEIGHT;
        }
        if (it->dwDamageFlags & HLDAMAGE_CRITICAL) // critical hit
        {
            nInfoWidth = Max(nInfoWidth, FHP_CRITICAL_WIDTH);
            nInfoHeight += FHP_CRITICAL_HEIGHT;
        }
        if (it->dwDamageFlags & HLDAMAGE_EXPERIENCE) // exp points
        {
            nInfoWidth = Max(nInfoWidth, FHP_EXP_WIDTH);
            nInfoHeight += FHP_EXP_HEIGHT;
        }
        if (it->dwDamageFlags & BRIEFING_TO_SAY) // exp points
        {
            nInfoWidth = Max(nInfoWidth, FHP_EXP_WIDTH);
            nInfoHeight += FHP_EXP_HEIGHT;
        }

        float xx = pt.x - nInfoWidth / 2.f;
        float y = pt.y - nInfoHeight / 2.f;
        float x;

        if (xx < 0 || (xx + nInfoWidth) > (float)screenWidth || y < 0 ||
            (y + nInfoHeight) > (float)screenHeight)
            continue;

        DWORD color = 0;
        BYTE alpha = BYTE((1 - fpPos) * 255);
        if (it->dwDamageFlags & HLDAMAGE_EXPERIENCE)
            color = RGBA_MAKE(64, 255, 255, alpha);
        else
        {
            if (pUnit->GetOwner())
                color = RGBA_MAKE(255, 64, 64, alpha);
            else
                color = RGBA_MAKE(64, 255, 64, alpha);
        }

        if (addonGraphics && !(it->dwDamageFlags & HLDAMAGE_EXPERIENCE)) {
            if (it->dwDamageFlags & (HLDAMAGE_CRIT | HLDAMAGE_PARRY | HLDAMAGE_AVOID | HLDAMAGE_BLOCK))
                color = pUnit->GetOwner() ? RGBA_MAKE(255, 74, 74, alpha) : RGBA_MAKE(74, 255, 74, alpha);
            else if (it->dwDamageFlags & (HLDAMAGE_CHEST | HLDAMAGE_POTION))
                color = RGBA_MAKE(255, 255, 255, alpha);
        }
        DWORD specular = RGBA_MAKE(0, 0, 0, 255);

        QuadBatch quads = {};
        quads.Reset();

        x = xx + (nInfoWidth - (nLength + iconLength) * digitWidth) / 2.f;
        const QuadRect iconTexcoords[6] = {
            {263.f/512.f,433.f/512.f,71.f/512.f,-FHP_DIGIT_HEIGHT},
            {336.f/512.f,431.f/512.f,71.f/512.f,-FHP_DIGIT_HEIGHT},
            {412.f/512.f,436.f/512.f,84.f/512.f,-FHP_DIGIT_HEIGHT},
            {263.f/512.f,357.f/512.f,81.f/512.f,-FHP_DIGIT_HEIGHT},
            {345.f/512.f,356.f/512.f,81.f/512.f,-FHP_DIGIT_HEIGHT},
            {336.f/512.f,431.f/512.f,71.f/512.f,-FHP_DIGIT_HEIGHT}
        };
        for (int i = 0; i < 6; ++i) {
            if (showIcons[i]) {
                QuadRect coords = {x - 2, y, digitWidth * 1.5f, float(digitHeight)};
                quads.AddQuad(coords, iconTexcoords[i], color, specular);
                x += digitWidth * 1.5f;
            }
        }
        for (int i = 0; i < nLength; i++)
        {
            // Add damage / exp digits, quad for each digit
            QuadRect coords = { x, y, float(digitWidth), float(digitHeight) };

            int nDigit = szTmp[i] - '0';
            int digitCol = (nDigit & 3);
            int digitRow = (nDigit >> 2);
            float tu = digitCol * FHP_DIGIT_WIDTH;
            float tv = digitRow * FHP_DIGIT_ROW_HEIGHT;
            QuadRect texCoords = { tu, 1 - tv, FHP_DIGIT_WIDTH, -FHP_DIGIT_HEIGHT };
            quads.AddQuad(coords, texCoords, color, specular);
            x += digitWidth;
        }
        y += digitHeight;                           // draw next below
        if (it->dwDamageFlags & HLDAMAGE_BACKSTAB) // ôðàçà "ñî ñïèíû"
        {
            x = xx + (nInfoWidth - FHP_BACKSTAB_WIDTH) / 2.f; // center
            QuadRect coords = { x, y, FHP_BACKSTAB_WIDTH, FHP_BACKSTAB_HEIGHT };

            // float tv = FHP_PHRASE_START + (int)(EGLYPH_PHRASE_ROW_BACKSTAB) * FHP_DIGIT_HEIGHT;
            float tv = (int)(EGLYPH_ROW_BACKSTAB)*FHP_DIGIT_ROW_HEIGHT;
            QuadRect texCoords = { 0, 1 - tv, 1, -FHP_DIGIT_HEIGHT };
            quads.AddQuad(coords, texCoords, color, specular);
            y += FHP_BACKSTAB_HEIGHT; // draw next below
        }
        if (it->dwDamageFlags & HLDAMAGE_CRITICAL) // ôðàçà "â ãîëîâó"
        {
            x = xx + (nInfoWidth - FHP_CRITICAL_WIDTH) / 2.f;
            QuadRect coords = { x, y, FHP_CRITICAL_WIDTH, FHP_CRITICAL_HEIGHT };

            // float tv = FHP_PHRASE_START + (int)(EGLYPH_PHRASE_ROW_CRITICAL) * FHP_DIGIT_HEIGHT;
            float tv = (int)(EGLYPH_ROW_CRITICAL)*FHP_DIGIT_ROW_HEIGHT;
            QuadRect texCoords = { 0, 1 - tv, 1, -FHP_DIGIT_HEIGHT };
            quads.AddQuad(coords, texCoords, color, specular);
            y += FHP_CRITICAL_HEIGHT; // draw next below
        }
        if (it->dwDamageFlags & HLDAMAGE_EXPERIENCE) // ôðàçà "îïûò"
        {
            x = xx + (nInfoWidth - FHP_EXP_WIDTH) / 2.f;

            // float tv = FHP_PHRASE_START + (int)(EGLYPH_PHRASE_ROW_EXP) * FHP_DIGIT_HEIGHT;
            float tv = (int)(EGLYPH_ROW_EXP)*FHP_DIGIT_ROW_HEIGHT;
            QuadRect texCoords = { 0, 1 - tv, 1, -FHP_DIGIT_HEIGHT };
            QuadRect coords = { x, y, FHP_EXP_WIDTH, FHP_EXP_HEIGHT };
            quads.AddQuad(coords, texCoords, color, specular);
            y += FHP_EXP_HEIGHT; // draw next below
        }

        {
            DWORD nVertices, nIndices;
            STLVertex1 *vertices;
            PWORD indices;
            quads.GetData(nVertices, nIndices, vertices, indices);
            OwnedRender::E3CombatVertices(vertices, nVertices, sizeof(*vertices));
            for (int j = 0; j < nVertices; j++)
            {
                vertices[j].tu += 1 / 512.f;
                vertices[j].tv -= 1 / 512.f;

                // convert to NDC space -1..1
                CPlot3D screen = { (2 * vertices[j].sx / screenWidth) - 1.f,
                                   -(2 * vertices[j].sy / screenHeight) + 1.f, 0 };
                vertices[j].sx = screen.x;
                vertices[j].sy = screen.y;
            }

            pRend->RenderTriangleSet(vertices, nVertices, indices, nIndices);
        }
    }
    // remove excess
    if (!addonGraphics) damages.remove_if(CNeedDeleteFlyingHPFunctional2());
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
