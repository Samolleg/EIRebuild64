#include "StdAfx.h"
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
#include "WorldClient.h"
#include "dx11/RendererDX11.h"
/////////////////////////////////////////////////////////////////////////////////////
namespace owned { void bridge_note_combat_hit() noexcept; }
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
            hp.dwTime = owned::embedded_combat_text_clock_ms(theWorld.GetRenderTimeSegments()) + 500;
        else
            hp.dwTime = owned::embedded_combat_text_clock_ms(theWorld.GetRenderTimeSegments());
        hp.nAmount = nAmount;
        hp.nUnitID = nUnitID;
        hp.dwDamageFlags = dwDamageFlags;
        damages.push_back(hp);
        owned::bridge_note_combat_hit();
    }
}

// The renderer owns anchor selection, layout, glyph geometry and scale.
BOOL CFlyingHitPoints::Draw(CRenderer *pRend)
{
    if (!pTexture) return FALSE;
    if (!pRend->BindTexture(0, pTexture))
        return ReportError("CFlyingHitPoints::Draw.\n SetTexture failed.\n");
    const double gameTime = theWorld.GetRenderTimeSegments();
    const DWORD now = owned::embedded_combat_text_clock_ms(gameTime);
    damages.remove_if([now](const SFlyingHPDesc& event) {
        return owned::embedded_combat_text_expired(now, event.dwTime);
    });
    if (!theSettings.nDrawFlyingHP) return TRUE;
    std::vector<owned::EmbeddedCombatTextEvent> events;
    events.reserve(damages.size());
    for (const auto& source : damages)
    {
        owned::EmbeddedCombatTextEvent event;
        event.unit_id = source.nUnitID; event.amount = source.nAmount;
        event.flags = source.dwDamageFlags; event.time_ms = source.dwTime;
        if (CUnit* unit = IWorldClient.GetUnit(source.nUnitID)) {
            auto& value = event.unit;
            value.present = true;
            value.visible = unit->CheckFlag(OBJ_VISIBLE) != 0;
            value.hidden = unit->CheckFlag(OBJ_HIDDEN) != 0;
            value.unit_visible = unit->CheckFlag(UNIT_VISIBLE) != 0;
            value.owned_unit = unit->GetOwner() != 0;
            CFigure* figure = unit->MatchFigure("hd");
            if (!figure) figure = unit->MatchFigure("bd");
            if (!figure) figure = unit->MatchFigure("hp");
            value.has_figure = figure != nullptr;
            const auto& rect = unit->GetScreenRect();
            value.unit_rect = {rect.left, rect.top, rect.right, rect.bottom};
            if (figure && figure->CheckFlag(FIG_VISIBLE)) {
                auto* specific = static_cast<CFigureClientSpecific*>(figure->GetFigureSpecific());
                if (specific) {
                    value.figure_visible = true;
                    const auto& bounds = specific->Get2DClipRect();
                    value.figure_rect = {bounds.left, bounds.top, bounds.right, bounds.bottom};
                } else value.has_figure = false;
            }
        }
        events.push_back(event);
    }
    DWORD width, height;
    pRend->GetCurrentMode(width, height);
    owned::EmbeddedCombatTextInput input;
    input.events = events.data(); input.count = static_cast<std::uint32_t>(events.size());
    input.game_time_segments = gameTime; input.screen_width = float(width); input.screen_height = float(height);
    input.addon_layout = ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVGraphics2");
    pRend->GetD3D()->SubmitC74CombatText(input);
    if (!input.addon_layout)
        damages.remove_if([](const SFlyingHPDesc& event) { return (event.dwDamageFlags & BRIEFING_TO_SAY) != 0; });
    return TRUE;
}
