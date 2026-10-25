#pragma once

// Don't include: afxv_w32.h: #error WINDOWS.H already included.  MFC apps must not #include
// <Windows.h> #include <windows.h>

// Native time_t is 64-bit; asset timestamps retain their explicit __time32_t.
#include <cstddef>
#include <cstdint>

#include <afx.h>      // CFile, ...
#include <afxtempl.h> // CList, ...

#include <mmsystem.h>
#include "Game/GameJournal.h"

#define IfFailedThrowHR(expr) \
    {                         \
        HRESULT hr = (expr);  \
        if (hr == E_OUTOFMEMORY) GameJournal::AllocationFailure("HRESULT"); \
        if (FAILED(hr))       \
            throw hr;         \
    }
#define IfFailedThrowHR(expr, msg) \
    {                              \
        HRESULT hr = (expr);       \
        if (hr == E_OUTOFMEMORY) GameJournal::AllocationFailure("HRESULT"); \
        if (FAILED(hr))            \
            throw hr;              \
    }

#define D3D_OVERLOADS
#include <d3dtypes.h>
#include <ddraw.h>
#include <d3d.h>
#include <dinput.h>
#include <dsound.h>
#define D3DCOLORVALUE_DEFINED

#include <afxwin.h> // AfxBeginThread

#include <timeapi.h> // timeGetTime


#include <algorithm>

#include <ostream>
#include <iterator>
#include <list>
#include <map>
#include <string>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <functional>

// #include "json.hpp"

using std::list;
using std::ostream;
using std::string;
using std::unordered_map;

#define dbgnew new

// Common Game's headers
#include "Game/ClassIDs.h"
#include "Game/TypeIDs.h"
#include "Game/BasicChunk.h"
#include "Game/BasicFactory.h"
#include "Game/SmartPtr.h"
#include "Game/Tools.h"
// #include "Game/Object.h"
#include "Game/GeometryInterfaces.h"
#include "Game/AnimationSystem.h"
#include "Game/AnimationSystem2.h"
#include "Game/Figure.h"
#include "Game/Notification.h"
