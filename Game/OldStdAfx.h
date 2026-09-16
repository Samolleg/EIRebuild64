#pragma once
// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

#pragma warning(disable : 4056)
#pragma warning(disable : 4786) // ...debug string truncated to 255 characters...

#define VC_EXTRALEAN // Exclude rarely-used stuff from Windows headers

#ifndef _A4
//	#define _NOTHREADS
//	#include <stl_config.h>
#include "stl_user_config.h"
#endif

#ifdef _A4
#define _REENTRANT
#include "stl_user_config.h"
#include <stl/_config.h>

#define NOMINMAX

#pragma warning(disable : 4018) // signed/unsigned mismatch
#pragma warning(disable : 4244) // conversion from 'type1' to 'type2', possible loss of data
#pragma warning(disable : 4355) // 'this' : used in base member initializer list

#define STDCALL __stdcall // to use with interface function calls
#endif

#include <afx.h>
#include <AfxWin.h> // MFC core and standard components
#include <AfxExt.h> // MFC extensions
#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <AfxCmn.h> // MFC support for Windows Common Controls
#endif              // _AFX_NO_AFXCMN_SUPPORT
// #include <AfxSock.h>		                // MFC socket extensions
#include <mmsystem.h>
// DirectX headers
#define D3D_OVERLOADS // for D3DVECTOR overloads
#include <D3DTypes.h> // Direct3D types
#include <DDraw.h>    // DirectDraw
#include <D3D.h>      // Direct3D
#include <DInput.h>   // DirectInput
#include <DSound.h>   // DirectSound
//////////////////////////////////////////////////////////////////////////////////////
typedef unsigned int uint;
#include "DebugAlloc.h"
//////////////////////////////////////////////////////////////////////////////////////
// my own headers
#include "ClassIDs.h" // class identifications
#include "TypeIDs.h"  // type identifications
// #include "D3DUtils.h" // D3D utils
#include "DXDirectX.h"
#include "Notification.h"  // ReportError and ReportWarning functions
#include "CMMIOResource.h" // multimedia resource file
#ifdef _A4
#include <AfxTempl.h>
#include "SimplyArray.h"
#include "TypeDefs.h"
#import "msxml.dll"
#endif
#include "Tools.h"

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous
// line.
