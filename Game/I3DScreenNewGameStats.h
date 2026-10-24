#pragma once

#include "I3DScreen.h"
#include "I3DMain.h"
#include "Party.h"
#include "WorldInterface.h"
#include "ScenarioTracker.h"
#include "GameJournal.h"

// Ordinary interface screen: the connected menu world keeps ticking while the
// player edits a private copy of the fresh party. No nested message/game loop.
class CI3DScreenNewGameStats : public CI3DScreen
{
    CParty party;
    CI3DSurface *surface = nullptr;
    int points = 0;
    int heldControl = -1;
    DWORD heldSince = 0;
    bool leaving = false;
    static constexpr COLORREF CAMP_COLOR_SKILLS = RGB(228, 215, 167);
    static constexpr COLORREF CAMP_COLOR_SKILLS_DIS = RGB(130, 130, 130);

    static int Price(int upper)
    {
        return upper == 26 || upper == 27 || upper == 29 ? 2 : 1;
    }

    SRPGPrimaryStat &Stat(int index)
    {
        auto &stats = party.people[0].stats;
        return index == 0 ? stats.psStrength :
            (index == 1 ? stats.psDexterity : stats.psIntelligence);
    }

    bool Change(int control)
    {
        const int index = control < 3 ? control : control - 3;
        if (index < 0 || index > 2 || leaving) return false;
        auto &stat = Stat(index);
        const int value = static_cast<int>(stat.fpBaseValue);
        const bool increase = control >= 3;
        const int price = Price(increase ? value + 1 : value);
        if (increase ? (value >= 32 || points < price) : value <= 15)
            return false;
        const int step = increase ? 1 : -1;
        points += increase ? -price : price;
        stat.fpBaseValue = static_cast<float>(value + step);
        stat.fpEffectiveValue = stat.fpBaseValue;
        if (index == 0) party.people[0].complection.strength += step * .015f;
        if (index == 1) party.people[0].complection.grace -= step * .015f;
        return true;
    }

    void DrawRows(CI3DSurface* surface, CPersonage* person, const char* points,
                  int x, int y, int rowHeight);
    void Redraw();

    void Finish(bool accept)
    {
        if (leaving || (accept && points != 0)) return;
        leaving = true;
        heldControl = -1;
        if (accept)
        {
            auto &hero = party.people[0];
            hero.stats.RecalcInternalHeroStats(hero.GetItemsWeight());
            theHat.SetSinglePlayerParty(party);
        }
        // Esc leaves the original party in the hat entirely untouched. Only
        // enqueue: DoGo owns teardown after this input callback has returned.
        GameJournal::Event("NEWGAME", "attributes_close", "accepted=%d strength=%g dexterity=%g intelligence=%g",
            accept, Stat(0).fpBaseValue, Stat(1).fpBaseValue, Stat(2).fpBaseValue);
        IWorldConnector.ReqStartPreparedNewGame();
    }

public:
    CI3DScreenNewGameStats(CInterface3D *interface3D, const CParty &freshParty)
        : CI3DScreen(interface3D), party(freshParty) {}

    void OnActivate() override
    {
        SetRect(0, 0, 800, 600);
        auto *button = AddFigure();
        button->MakeBar(2.9f, 100, 335, 450, 465, 480);
        button->SetTexture(nullptr);
        button->UseDefaultColors(TRUE);
        button->SetDefaultColor(RGBA_MAKE(55, 48, 32, 255));
        CRect bounds(0, 0, pI3D->GetWidth(), pI3D->GetHeight());
        pI3D->ScreenToVirtual(bounds);
        auto *back = AddFigure();
        back->MakeBar(3, 100, float(bounds.left - 1), float(bounds.top - 1),
                      float(bounds.right + 1), float(bounds.bottom + 1));
        back->SetTexture(nullptr);
        back->UseDefaultColors(TRUE);
        back->SetDefaultColor(RGBA_MAKE(12, 12, 16, 255));
        AddSurface(surface = dbgnew CI3DSurface(pI3D, 120, 80, 680, 520));
        for (int i = 0; i < 3; ++i)
            AddArea()->MakeRect(440, 255 + 28 * i, 460, 283 + 28 * i);
        for (int i = 0; i < 3; ++i)
            AddArea()->MakeRect(460, 255 + 28 * i, 480, 283 + 28 * i);
        AddArea()->MakeRect(335, 450, 465, 480);
        Redraw();
    }

    void OnDeactivate() override { ResetInputState(); }
    void ResetInputState() override { heldControl = -1; heldSince = 0; }

    void OnLButtonDown(UINT, CPoint point) override
    {
        SetProcessed();
        ResetInputState();
        if (leaving) return;
        const int hit = HitTest(point);
        if (hit == 6) { Finish(true); return; }
        if (hit >= 0 && hit < 6 && Change(hit))
        {
            heldControl = hit;
            heldSince = GetTickCount();
            pI3D->BeginFX("buttons\\camp\\perk.wav");
            Redraw();
        }
    }

    void OnLButtonUp(UINT, CPoint) override { ResetInputState(); SetProcessed(); }
    void OnMouseMove(UINT, CPoint point) override
    {
        if (heldControl >= 0 && HitTest(point) != heldControl) ResetInputState();
        SetProcessed();
    }
    void OnKeyDown(UINT key, UINT, UINT) override
    {
        SetProcessed();
        if (key == VK_RETURN) Finish(true);
        if (key == VK_ESCAPE) Finish(false);
    }
    void OnIdle() override
    {
        if (leaving || heldControl < 0) return;
        const DWORD now = GetTickCount();
        if (now - heldSince < 250) return;
        heldSince = now;
        if (Change(heldControl)) Redraw();
        else ResetInputState();
    }
};

inline void CI3DScreenNewGameStats::DrawRows(CI3DSurface* surface, CPersonage* person, const char* pointsLabel,
                 int x, int y, int rowHeight)
{
    const int values[] = { static_cast<int>(person->stats.psStrength.fpBaseValue),
        static_cast<int>(person->stats.psDexterity.fpBaseValue),
        static_cast<int>(person->stats.psIntelligence.fpBaseValue), this->points };
    // Same Russian labels as the game; literal CP1251 like the window title.
    const char* labels[] = { "\xD1\xE8\xEB\xE0", "\xCB\xEE\xE2\xEA\xEE\xF1\xF2\xFC", "\xD0\xE0\xE7\xF3\xEC" };
    for (int i = 0; i < 4; ++i)
    {
        CString label, value;
        if (i < 3) label = labels[i];
        else label = pointsLabel;
        value.Format("%d", values[i]);
        const int top = y + i * rowHeight;
        surface->DrawText(label.GetString(), CRect(x + 20, top, x + 110, top + rowHeight),
                              0, CAMP_COLOR_SKILLS, DT_LEFT);
        surface->DrawText(value.GetString(), CRect(x + 110, top, x + 135, top + rowHeight),
                              0, CAMP_COLOR_SKILLS, DT_RIGHT);
        if (i == 3) continue;
        surface->DrawText("-", CRect(x + 140, top, x + 160, top + rowHeight), 0,
            values[i] > 15 ? CAMP_COLOR_SKILLS : CAMP_COLOR_SKILLS_DIS, DT_CENTER);
        surface->DrawText("+", CRect(x + 160, top, x + 180, top + rowHeight), 0,
            values[i] < 32 && this->points >= Price(values[i] + 1) ?
                CAMP_COLOR_SKILLS : CAMP_COLOR_SKILLS_DIS, DT_CENTER);
    }
}

inline void CI3DScreenNewGameStats::Redraw()
{
    CPersonage* hero = &party.people[0];
    surface->GetDC();
    surface->Clear();
    // Literal labels use the game's CP1251, independent of compiler locale.
    surface->DrawText("\xD1\xF2\xE0\xF0\xF2\xEE\xE2\xFB\xE5 \xF5\xE0\xF0\xE0\xEA\xF2\xE5\xF0\xE8\xF1\xF2\xE8\xEA\xE8",
        CRect(10, 15, 550, 45), 2, RGB(255, 179, 49), DT_CENTER);
    surface->DrawText("\xCF\xE5\xF0\xE5\xE4 \xED\xE0\xF7\xE0\xEB\xEE\xEC \xE8\xE3\xF0\xFB \xEC\xEE\xE6\xED\xEE \xEF\xE5\xF0\xE5\xF0\xE0\xF1\xEF\xF0\xE5\xE4\xE5\xEB\xE8\xF2\xFC",
        CRect(10, 65, 550, 87), 0, CAMP_COLOR_SKILLS, DT_CENTER);
    surface->DrawText("\xEE\xF1\xED\xEE\xE2\xED\xFB\xE5 \xF5\xE0\xF0\xE0\xEA\xF2\xE5\xF0\xE8\xF1\xF2\xE8\xEA\xE8 \xE3\xE5\xF0\xEE\xFF. \xCA\xE0\xE6\xE4\xE0\xFF - \xEE\xF2 15 \xE4\xEE 32.",
        CRect(10, 87, 550, 109), 0, CAMP_COLOR_SKILLS, DT_CENTER);
    surface->DrawText("\xD7\xF2\xEE\xE1\xFB \xEF\xEE\xE4\xED\xFF\xF2\xFC \xEE\xE4\xED\xF3, \xF1\xED\xE0\xF7\xE0\xEB\xE0 \xF3\xEC\xE5\xED\xFC\xF8\xE8\xF2\xE5 \xE4\xF0\xF3\xE3\xF3\xFE;",
        CRect(10, 109, 550, 131), 0, CAMP_COLOR_SKILLS, DT_CENTER);
    surface->DrawText("\xEF\xE5\xF0\xE5\xF5\xEE\xE4\xFB 25 -> 26, 26 -> 27 \xE8 28 -> 29 \xF1\xF2\xEE\xFF\xF2 2 \xEE\xF7\xEA\xE0.",
        CRect(10, 131, 550, 153), 0, CAMP_COLOR_SKILLS, DT_CENTER);
    DrawRows(surface, hero, "\xCE\xF1\xF2\xE0\xEB\xEE\xF1\xFC \xEE\xF7\xEA\xEE\xE2", 180, 175, 28);
    surface->DrawText("\xD0\xE5\xEA\xEE\xEC\xE5\xED\xE4\xF3\xE5\xEC \xF3\xED\xE8\xE2\xE5\xF0\xF1\xE0\xEB\xFC\xED\xEE\xE3\xEE \xE3\xE5\xF0\xEE\xFF: 25 / 25 / 25",
        CRect(10, 305, 550, 329), 1, RGB(255, 179, 49), DT_CENTER);
    surface->DrawText("(\xF1\xF2\xE0\xED\xE4\xE0\xF0\xF2\xED\xEE\xE5 \xF0\xE0\xF1\xEF\xF0\xE5\xE4\xE5\xEB\xE5\xED\xE8\xE5).",
        CRect(10, 329, 550, 353), 1, RGB(255, 179, 49), DT_CENTER);
    surface->DrawText("\xC3\xEE\xF2\xEE\xE2\xEE",
        CRect(215, 370, 345, 400), 1,
        points == 0 ? CAMP_COLOR_SKILLS : CAMP_COLOR_SKILLS_DIS, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    surface->DrawText("Esc: \xED\xE0\xF7\xE0\xF2\xFC \xE1\xE5\xE7 \xE8\xE7\xEC\xE5\xED\xE5\xED\xE8\xE9",
        CRect(10, 410, 550, 435), 0, CAMP_COLOR_SKILLS, DT_CENTER);
    surface->ReleaseDC();
}
