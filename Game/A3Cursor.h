// A3Cursor.h: interface for the CA3Cursor class.
//
//////////////////////////////////////////////////////////////////////
#if !defined(AFX_A3CURSOR_H__08114FE1_82FF_11D3_B158_006008641A0D__INCLUDED_)
#define AFX_A3CURSOR_H__08114FE1_82FF_11D3_B158_006008641A0D__INCLUDED_
///////////////////////////////////////////////////////////////////////////////////////////////////
#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
///////////////////////////////////////////////////////////////////////////////////////////////////
#include "I3DCursor.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
enum E2DCursorType
{
    CT_BLOCKING = -1, // Use to block current cursor mode
    CT_DEFAULT = 0,
    CT_ATTACK,
    CT_SPELL,
    CT_WAIT,
    CT_USE,
    CT_SCROLL_U,
    CT_SCROLL_UR,
    CT_SCROLL_R,
    CT_SCROLL_DR,
    CT_SCROLL_D,
    CT_SCROLL_DL,
    CT_SCROLL_L,
    CT_SCROLL_UL,
    CT_CANCEL,
    CT_TALK,
    CT_MOVE,
    CT_ATTACK_HD,
    CT_ATTACK_BD,
    CT_ATTACK_LH,
    CT_ATTACK_RH,
    CT_ATTACK_LL,
    CT_ATTACK_RL,
    CT_CAMERA,
    CT_SPELL_CANCEL,
    CT_STEAL
};
///////////////////////////////////////////////////////////////////////////////////////////////////
void TheCursorInit();
void TheCursorReload();
///////////////////////////////////////////////////////////////////////////////////////////////////
#endif // !defined(AFX_A3CURSOR_H__08114FE1_82FF_11D3_B158_006008641A0D__INCLUDED_)
