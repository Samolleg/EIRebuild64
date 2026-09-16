// A3Cursor.cpp: implementation
//
////////////////////////////////////////////////////////////////////////////////////////////////
#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "A3Cursor.h"
////////////////////////////////////////////////////////////////////////////////////////////////
void TheCursorReload()
{
    theCursor.Fini();
}
////////////////////////////////////////////////////////////////////////////////////////////////
void TheCursorInit()
{
    theCursor.Init();

    theCursor.LoadCursor("cursor_default", 0, 0);
    theCursor.LoadCursor("cursor_attack", 0, 0);
    theCursor.LoadCursor("cursor_spell", 0, 0);
    theCursor.LoadCursor("cursor_wait", 0, 0);
    theCursor.LoadCursor("cursor_use", 0, 0);
    theCursor.LoadCursor("cursor_scroll_u", 16, 0);
    theCursor.LoadCursor("cursor_scroll_ur", 32, 0);
    theCursor.LoadCursor("cursor_scroll_r", 32, 16);
    theCursor.LoadCursor("cursor_scroll_dr", 32, 32);
    theCursor.LoadCursor("cursor_scroll_d", 16, 32);
    theCursor.LoadCursor("cursor_scroll_dl", 0, 32);
    theCursor.LoadCursor("cursor_scroll_l", 0, 16);
    theCursor.LoadCursor("cursor_scroll_ul", 0, 0);
    theCursor.LoadCursor("cursor_cancel", 0, 0);
    theCursor.LoadCursor("cursor_talk", 0, 0);
    theCursor.LoadCursor("cursor_move", 0, 0);
    theCursor.LoadCursor("cursor_attack_hd", 0, 0);
    theCursor.LoadCursor("cursor_attack_bd", 0, 0);
    theCursor.LoadCursor("cursor_attack_lh", 0, 0);
    theCursor.LoadCursor("cursor_attack_rh", 0, 0);
    theCursor.LoadCursor("cursor_attack_ll", 0, 0);
    theCursor.LoadCursor("cursor_attack_rl", 0, 0);
    theCursor.LoadCursor("cursor_camera", 0, 0);
    theCursor.LoadCursor("cursor_spellcancel", 0, 0);
    theCursor.LoadCursor("cursor_steal", 0, 0);

    theCursor.StartProcessing();
}
////////////////////////////////////////////////////////////////////////////////////////////////