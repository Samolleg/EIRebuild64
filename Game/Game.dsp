# Microsoft Developer Studio Project File - Name="Game" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=Game - Win32 Debug Interface with BC
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "Game.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "Game.mak" CFG="Game - Win32 Debug Interface with BC"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "Game - Win32 Release with Debug" (based on "Win32 (x86) Application")
!MESSAGE "Game - Win32 Debug with Optimization" (based on "Win32 (x86) Application")
!MESSAGE "Game - Win32 Release Interface" (based on "Win32 (x86) Application")
!MESSAGE "Game - Win32 Debug Interface" (based on "Win32 (x86) Application")
!MESSAGE "Game - Win32 Debug Interface with BC" (based on "Win32 (x86) Application")
!MESSAGE "Game - Win32 Release Interface PCD" (based on "Win32 (x86) Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""$/Allods II/Game", RYAAAAAA"
# PROP Scc_LocalPath "."
CPP=xicl6.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

# PROP BASE Use_MFC 6
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Game___Win32_Release_with_Debug"
# PROP BASE Intermediate_Dir "Game___Win32_Release_with_Debug"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 6
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "c:\home\Allods3\Game\ReleaseDebug"
# PROP Intermediate_Dir "c:\home\Allods3\Game\ReleaseDebug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "__FPS_COUNT__" /D "__READ_AMF__" /D "__TESTING__" /D "_FORDEMO" /D "__GAME__" /D "_AFXDLL" /Yu"StdAfx.h" /FD /c
# ADD CPP /nologo /Gr /MD /W3 /GX /Zd /O2 /Ob2 /D "NDEBUG" /D "__INTERFACE3D__" /D "__VIDEO__" /D "WIN32" /D "_WINDOWS" /D "__FPS_COUNT__" /D "__GAME__" /D "_MBCS" /D "FINALRELEASE" /D "_DO_SEH" /D "FORCE_ALLOC_CHECK" /D "_AFXDLL" /Yu"StdAfx.h" /FD /Zm200 /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL"
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL"
# ADD BASE RSC /l 0x419 /d "NDEBUG" /d "_AFXDLL"
# ADD RSC /l 0x419 /d "NDEBUG" /d "_AFXDLL"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=xilink6.exe
# ADD BASE LINK32 wsock32.lib dplayx.lib dinput.lib dsound.lib ddraw.lib d3drm.lib dxguid.lib winmm.lib msacm32.lib /nologo /subsystem:windows /pdb:none /machine:I386
# SUBTRACT BASE LINK32 /profile /map /debug /nodefaultlib
# ADD LINK32 imagehlp.lib binkw32.lib mss32.lib dplayx.lib wsock32.lib dsound.lib ddraw.lib d3drm.lib dxguid.lib dinput.lib winmm.lib /nologo /subsystem:windows /pdb:none /map /debug /machine:I386 /nodefaultlib:"libcpmt.lib" /fixed:no
# SUBTRACT LINK32 /verbose

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

# PROP BASE Use_MFC 5
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Game___Win32_Debug_with_Optimization"
# PROP BASE Intermediate_Dir "Game___Win32_Debug_with_Optimization"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 6
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "c:\home\Allods3\Game\DebugOpt"
# PROP Intermediate_Dir "c:\home\Allods3\Game\DebugOpt"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "__FPS_COUNT__" /D "__GAME__" /D "_MBCS" /D "__INTERFACE2D__" /Yu"StdAfx.h" /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /D "_DEBUG" /D "__INTERFACE2D__" /D "_AFXDLL" /D "WIN32" /D "_WINDOWS" /D "__FPS_COUNT__" /D "__GAME__" /D "_MBCS" /D "__SINGLEONLY__" /Yu"StdAfx.h" /FD /Zm200 /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL"
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL"
# ADD BASE RSC /l 0x419 /d "NDEBUG"
# ADD RSC /l 0x419 /d "NDEBUG" /d "_AFXDLL"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=xilink6.exe
# ADD BASE LINK32 wsock32.lib dplayx.lib dinput.lib dsound.lib ddraw.lib d3drm.lib dxguid.lib winmm.lib msacm32.lib /nologo /subsystem:windows /pdb:none /machine:I386
# SUBTRACT BASE LINK32 /profile /map /debug /nodefaultlib
# ADD LINK32 dinput.lib vtuneapi.lib msacm32.lib mss32.lib dplayx.lib wsock32.lib dsound.lib ddraw.lib d3drm.lib dxguid.lib winmm.lib /nologo /subsystem:windows /pdb:none /machine:I386
# SUBTRACT LINK32 /profile /map /debug /nodefaultlib

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

# PROP BASE Use_MFC 5
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Game___Win32_Release_Interface"
# PROP BASE Intermediate_Dir "Game___Win32_Release_Interface"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 5
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "c:\home\Allods3\Game\ReleaseInterface"
# PROP Intermediate_Dir "c:\home\Allods3\Game\ReleaseInterface"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /G6 /Gr /MT /W3 /GX /O2 /Ob2 /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "__FPS_COUNT__" /D "__GAME__" /D "_MBCS" /D "AI_SAVE_LOG_FILE" /Yu"StdAfx.h" /FD /c
# ADD CPP /nologo /G6 /Gr /MT /W3 /GX /O2 /Ob2 /D "NDEBUG" /D "__INTERFACE3D__" /D "__VIDEO__" /D "FINALRELEASE" /D "WIN32" /D "_WINDOWS" /D "__FPS_COUNT__" /D "__GAME__" /D "_MBCS" /D "NODEBUGINFO" /D "DONT_DISPLAY_DEBUG_IDS" /D "__DOUBLE_RESOURCES__" /Yu"StdAfx.h" /FD /Zm200 /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL"
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL"
# ADD BASE RSC /l 0x419 /d "NDEBUG"
# ADD RSC /l 0x419 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=xilink6.exe
# ADD BASE LINK32 wsock32.lib dplayx.lib dinput.lib dsound.lib ddraw.lib d3drm.lib dxguid.lib winmm.lib msacm32.lib /nologo /subsystem:windows /pdb:none /machine:I386
# SUBTRACT BASE LINK32 /profile /map /debug /nodefaultlib
# ADD LINK32 imagehlp.lib binkw32.lib mss32.lib dplayx.lib wsock32.lib dsound.lib ddraw.lib d3drm.lib dxguid.lib dinput.lib winmm.lib /nologo /subsystem:windows /pdb:none /machine:I386 /nodefaultlib:"libcpmt.lib"
# SUBTRACT LINK32 /profile /map /debug /nodefaultlib

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# PROP BASE Use_MFC 5
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Game___Win32_Debug_Interface"
# PROP BASE Intermediate_Dir "Game___Win32_Debug_Interface"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 6
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "c:\home\Allods3\Game\DebugInterface"
# PROP Intermediate_Dir "c:\home\Allods3\Game\DebugInterface"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /GX /ZI /Od /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "__FPS_COUNT__" /D "__GAME__" /D "_MBCS" /Yu"StdAfx.h" /FD /c
# SUBTRACT BASE CPP /Fr
# ADD CPP /nologo /MDd /W3 /GX /ZI /Od /D "_DEBUG" /D "__INTERFACE3D__" /D "__VIDEO__" /D "WIN32" /D "_WINDOWS" /D "__FPS_COUNT__" /D "__GAME__" /D "_MBCS" /D "FORCE_ALLOC_CHECK" /D "_AFXDLL" /Yu"StdAfx.h" /FD /Zm200 /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /o /win32 "NUL"
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /o /win32 "NUL"
# ADD BASE RSC /l 0x419 /d "_DEBUG"
# ADD RSC /l 0x419 /d "_DEBUG" /d "_AFXDLL"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=xilink6.exe
# ADD BASE LINK32 dplayx.lib wsock32.lib dinput.lib dsound.lib ddraw.lib d3drm.lib dxguid.lib winmm.lib msacm32.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# SUBTRACT BASE LINK32 /map
# ADD LINK32 binkw32.lib msacm32.lib dinput.lib mss32.lib dplayx.lib wsock32.lib dsound.lib ddraw.lib d3drm.lib dxguid.lib winmm.lib /nologo /subsystem:windows /debug /machine:I386 /nodefaultlib:"libcpmtd.lib" /pdbtype:sept
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# PROP BASE Use_MFC 5
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Game___Win32_Debug_Interface_with_BC"
# PROP BASE Intermediate_Dir "Game___Win32_Debug_Interface_with_BC"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 5
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "c:\home\Allods3\Game\DebugInterfaceBC"
# PROP Intermediate_Dir "c:\home\Allods3\Game\DebugInterfaceBC"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /GX /ZI /Od /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "__FPS_COUNT__" /D "__GAME__" /D "_MBCS" /D "__INTERFACE3D__" /D "__VIDEO__" /Yu"StdAfx.h" /FD /c
# SUBTRACT BASE CPP /Fr
# ADD CPP /nologo /MTd /W3 /GX /ZI /Od /D "_DEBUG" /D "__INTERFACE3D__" /D "__VIDEO__" /D "WIN32" /D "_WINDOWS" /D "__FPS_COUNT__" /D "__GAME__" /D "_MBCS" /D "__SINGLEONLY__" /Yu"StdAfx.h" /FD /Zm200 /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /o /win32 "NUL"
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /o /win32 "NUL"
# ADD BASE RSC /l 0x419 /d "_DEBUG"
# ADD RSC /l 0x419 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=xilink6.exe
# ADD BASE LINK32 dplayx.lib wsock32.lib dinput.lib dsound.lib ddraw.lib d3drm.lib dxguid.lib winmm.lib msacm32.lib binkw32.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# SUBTRACT BASE LINK32 /map
# ADD LINK32 binkw32.lib dinput.lib vtuneapi.lib msacm32.lib mss32.lib dplayx.lib wsock32.lib dsound.lib ddraw.lib d3drm.lib dxguid.lib winmm.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# SUBTRACT LINK32 /map

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

# PROP BASE Use_MFC 5
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Game___Win32_Release_Interface_PCD"
# PROP BASE Intermediate_Dir "Game___Win32_Release_Interface_PCD"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 5
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "c:\home\Allods3\Game\ReleaseInterfacePCD"
# PROP Intermediate_Dir "c:\home\Allods3\Game\ReleaseInterfacePCD"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /G6 /Gr /MT /W3 /GX /O2 /Ob2 /D "NDEBUG" /D "__INTERFACE3D__" /D "__VIDEO__" /D "FINALRELEASE" /D "WIN32" /D "_WINDOWS" /D "__FPS_COUNT__" /D "__GAME__" /D "_MBCS" /D "NODEBUGINFO" /D "DONT_DISPLAY_DEBUG_IDS" /Yu"StdAfx.h" /FD /Zm200 /c
# ADD CPP /nologo /G6 /Gr /MT /W3 /GX /O2 /Ob2 /D "NDEBUG" /D "__INTERFACE3D__" /D "__VIDEO__" /D "FINALRELEASE" /D "WIN32" /D "_WINDOWS" /D "__FPS_COUNT__" /D "__GAME__" /D "_MBCS" /D "NODEBUGINFO" /D "DONT_DISPLAY_DEBUG_IDS" /D "USE_PCD" /Yu"StdAfx.h" /FD /Zm200 /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL"
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL"
# ADD BASE RSC /l 0x419 /d "NDEBUG"
# ADD RSC /l 0x419 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=xilink6.exe
# ADD BASE LINK32 sdklib.lib imagehlp.lib binkw32.lib mss32.lib dplayx.lib wsock32.lib dsound.lib ddraw.lib d3drm.lib dxguid.lib dinput.lib winmm.lib /nologo /subsystem:windows /pdb:none /machine:I386 /nodefaultlib:"libcpmt.lib"
# SUBTRACT BASE LINK32 /profile /map /debug /nodefaultlib
# ADD LINK32 sdklib.lib imagehlp.lib binkw32.lib mss32.lib dplayx.lib wsock32.lib dsound.lib ddraw.lib d3drm.lib dxguid.lib dinput.lib winmm.lib /nologo /subsystem:windows /pdb:none /machine:I386 /nodefaultlib:"libcpmt.lib"
# SUBTRACT LINK32 /profile /map /debug /nodefaultlib

!ENDIF 

# Begin Target

# Name "Game - Win32 Release with Debug"
# Name "Game - Win32 Debug with Optimization"
# Name "Game - Win32 Release Interface"
# Name "Game - Win32 Debug Interface"
# Name "Game - Win32 Debug Interface with BC"
# Name "Game - Win32 Release Interface PCD"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\Base2D.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Base3D.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\BaseManager.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Basic.cpp
# End Source File
# Begin Source File

SOURCE=.\BasicChunk.cpp
# End Source File
# Begin Source File

SOURCE=.\CallStack.cpp
# End Source File
# Begin Source File

SOURCE=.\Camera.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\ChunkFile.cpp
# End Source File
# Begin Source File

SOURCE=.\ClientSpecificManager.cpp
# End Source File
# Begin Source File

SOURCE=.\clip.cpp
# End Source File
# Begin Source File

SOURCE=.\Collision.cpp
# End Source File
# Begin Source File

SOURCE=.\Console.cpp
# End Source File
# Begin Source File

SOURCE=.\Cruncher.cpp
# End Source File
# Begin Source File

SOURCE=.\DebugAlloc.cpp
# End Source File
# Begin Source File

SOURCE=.\DebugInfoSystem.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# SUBTRACT CPP /YX /Yc /Yu

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# SUBTRACT BASE CPP /YX /Yc /Yu
# SUBTRACT CPP /YX /Yc /Yu

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\EString.cpp
# End Source File
# Begin Source File

SOURCE=.\FakeUnit.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\FOFSystem.cpp
# End Source File
# Begin Source File

SOURCE=.\GameBase.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\GameMain.cpp
# End Source File
# Begin Source File

SOURCE=.\GeneralSettings.cpp
# End Source File
# Begin Source File

SOURCE=.\GeneralUnitLogic.cpp
# End Source File
# Begin Source File

SOURCE=.\Getdxver.cpp
# SUBTRACT CPP /YX /Yc /Yu
# End Source File
# Begin Source File

SOURCE=.\GlobalMap.cpp
# End Source File
# Begin Source File

SOURCE=.\GSVars.cpp
# End Source File
# Begin Source File

SOURCE=.\Hoover.cpp
# End Source File
# Begin Source File

SOURCE=.\Inventory.cpp
# End Source File
# Begin Source File

SOURCE=.\KeyMap.cpp
# End Source File
# Begin Source File

SOURCE=.\ldbFormat.cpp
# End Source File
# Begin Source File

SOURCE=.\LeverDatabase.cpp
# End Source File
# Begin Source File

SOURCE=.\Lightning.cpp
# End Source File
# Begin Source File

SOURCE=.\MapEditObject.cpp
# End Source File
# Begin Source File

SOURCE=.\MaxReader.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\MEUnitStats.cpp
# End Source File
# Begin Source File

SOURCE=.\Net.cpp
# End Source File
# Begin Source File

SOURCE=.\NetProtocol.cpp
# End Source File
# Begin Source File

SOURCE=.\NetUpdate.cpp
# End Source File
# Begin Source File

SOURCE=.\Notification.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\ObjectDBReader.cpp
# End Source File
# Begin Source File

SOURCE=.\Particle.cpp
# End Source File
# Begin Source File

SOURCE=.\ParticleManager.cpp
# End Source File
# Begin Source File

SOURCE=.\Perlin.cpp
# End Source File
# Begin Source File

SOURCE=.\Plot.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Polygon.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\PolyLine.cpp
# End Source File
# Begin Source File

SOURCE=.\Quantizator.cpp
# End Source File
# Begin Source File

SOURCE=.\Quaternion.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\RandomGen.cpp
# End Source File
# Begin Source File

SOURCE=.\Reader.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\ReportExceptionDlg.cpp
# End Source File
# Begin Source File

SOURCE=.\SaveLoader.cpp
# End Source File
# Begin Source File

SOURCE=.\ScenarioTracker.cpp
# End Source File
# Begin Source File

SOURCE=.\Script.cpp
# End Source File
# Begin Source File

SOURCE=.\ScriptParser.cpp
# End Source File
# Begin Source File

SOURCE=.\ServerSpecificManager.cpp
# End Source File
# Begin Source File

SOURCE=.\Shadow.cpp
# End Source File
# Begin Source File

SOURCE=.\SimpleCryptographer.cpp
# End Source File
# Begin Source File

SOURCE=.\sky.cpp
# End Source File
# Begin Source File

SOURCE=.\Spline.cpp
# End Source File
# Begin Source File

SOURCE=.\StdAfx.cpp
# ADD CPP /Yc"StdAfx.h"
# End Source File
# Begin Source File

SOURCE=.\Streams.cpp
# End Source File
# Begin Source File

SOURCE=.\StringProcessing.cpp
# End Source File
# Begin Source File

SOURCE=.\SysInfo.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

# ADD BASE CPP /Od
# SUBTRACT BASE CPP /YX /Yc /Yu
# ADD CPP /Od
# SUBTRACT CPP /YX /Yc /Yu

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

# ADD BASE CPP /Od
# SUBTRACT BASE CPP /YX /Yc /Yu
# ADD CPP /Od
# SUBTRACT CPP /YX /Yc /Yu

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

# ADD BASE CPP /Od
# SUBTRACT BASE CPP /YX /Yc /Yu
# ADD CPP /Od
# SUBTRACT CPP /YX /Yc /Yu

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

# ADD BASE CPP /Od
# SUBTRACT BASE CPP /YX /Yc /Yu
# ADD CPP /Od
# SUBTRACT CPP /YX /Yc /Yu

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\TextManager.cpp
# End Source File
# Begin Source File

SOURCE=.\VisFog.cpp
# End Source File
# Begin Source File

SOURCE=.\weather.cpp
# End Source File
# Begin Source File

SOURCE=.\ZoneExit.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=.\AreaMap.h
# End Source File
# Begin Source File

SOURCE=.\AreaMap.inl
# End Source File
# Begin Source File

SOURCE=.\Base2D.h
# End Source File
# Begin Source File

SOURCE=.\Base3D.h
# End Source File
# Begin Source File

SOURCE=.\BaseManager.h
# End Source File
# Begin Source File

SOURCE=.\Basic.h
# End Source File
# Begin Source File

SOURCE=.\BasicChunk.h
# End Source File
# Begin Source File

SOURCE=.\BasicFactory.h
# End Source File
# Begin Source File

SOURCE=.\BasicM.h
# End Source File
# Begin Source File

SOURCE=.\BoundCube.h
# End Source File
# Begin Source File

SOURCE=.\CallStack.h
# End Source File
# Begin Source File

SOURCE=.\Camera.h
# End Source File
# Begin Source File

SOURCE=.\ChunkFile.h
# End Source File
# Begin Source File

SOURCE=.\ClassIDs.h
# End Source File
# Begin Source File

SOURCE=.\ClientSpecificManager.h
# End Source File
# Begin Source File

SOURCE=.\clip.h
# End Source File
# Begin Source File

SOURCE=.\Collision.h
# End Source File
# Begin Source File

SOURCE=.\Console.h
# End Source File
# Begin Source File

SOURCE=.\copy_if.h
# End Source File
# Begin Source File

SOURCE=.\Cruncher.h
# End Source File
# Begin Source File

SOURCE=.\Database.h
# End Source File
# Begin Source File

SOURCE=.\DebugAlloc.h
# End Source File
# Begin Source File

SOURCE=.\DebugInfoSystem.h
# End Source File
# Begin Source File

SOURCE=.\DirElement.h
# End Source File
# Begin Source File

SOURCE=.\EString.h
# End Source File
# Begin Source File

SOURCE=.\Events.h
# End Source File
# Begin Source File

SOURCE=.\FakeUnit.h
# End Source File
# Begin Source File

SOURCE=.\FofSystem.h
# End Source File
# Begin Source File

SOURCE=.\GameBase.h
# End Source File
# Begin Source File

SOURCE=.\GameMain.h
# End Source File
# Begin Source File

SOURCE=.\GeneralSettings.h
# End Source File
# Begin Source File

SOURCE=.\GeneralUnitLogic.h
# End Source File
# Begin Source File

SOURCE=.\GlobalMap.h
# End Source File
# Begin Source File

SOURCE=.\GSVars.h
# End Source File
# Begin Source File

SOURCE=.\HashMap.h
# End Source File
# Begin Source File

SOURCE=.\HashMap.inl
# End Source File
# Begin Source File

SOURCE=.\Hoover.h
# End Source File
# Begin Source File

SOURCE=.\IDebugInfoSystem.h
# End Source File
# Begin Source File

SOURCE=.\Inventory.h
# End Source File
# Begin Source File

SOURCE=.\KeyMap.h
# End Source File
# Begin Source File

SOURCE=.\ldbFormat.h
# End Source File
# Begin Source File

SOURCE=.\LeverDatabase.h
# End Source File
# Begin Source File

SOURCE=.\Lightning.h
# End Source File
# Begin Source File

SOURCE=.\LRUStorage.h
# End Source File
# Begin Source File

SOURCE=.\MapEditObject.h
# End Source File
# Begin Source File

SOURCE=.\MapEditorChunkIDs.h
# End Source File
# Begin Source File

SOURCE=.\MaxReader.h
# End Source File
# Begin Source File

SOURCE=.\meshFormat.h
# End Source File
# Begin Source File

SOURCE=.\METraps.h
# End Source File
# Begin Source File

SOURCE=.\MEUnitStats.h
# End Source File
# Begin Source File

SOURCE=.\mm_malloc.h
# End Source File
# Begin Source File

SOURCE=.\Net.h
# End Source File
# Begin Source File

SOURCE=.\NetProtocol.h
# End Source File
# Begin Source File

SOURCE=.\NetUpdate.h
# End Source File
# Begin Source File

SOURCE=.\Notification.h
# End Source File
# Begin Source File

SOURCE=.\ObjectDBReader.h
# End Source File
# Begin Source File

SOURCE=.\PacketIDs.h
# End Source File
# Begin Source File

SOURCE=.\Particle.h
# End Source File
# Begin Source File

SOURCE=.\ParticleManager.h
# End Source File
# Begin Source File

SOURCE=.\Perlin.h
# End Source File
# Begin Source File

SOURCE=.\Ping.h
# End Source File
# Begin Source File

SOURCE=.\Plot.h
# End Source File
# Begin Source File

SOURCE=.\Polygon.h
# End Source File
# Begin Source File

SOURCE=.\PolyLine.h
# End Source File
# Begin Source File

SOURCE=.\PtrList.h
# End Source File
# Begin Source File

SOURCE=.\Quantizator.h
# End Source File
# Begin Source File

SOURCE=.\Quaternion.h
# End Source File
# Begin Source File

SOURCE=.\Quaternion.inl
# End Source File
# Begin Source File

SOURCE=.\RandomGen.h
# End Source File
# Begin Source File

SOURCE=.\Reader.h
# End Source File
# Begin Source File

SOURCE=.\ReportExceptionDlg.h
# End Source File
# Begin Source File

SOURCE=.\Resource.h
# End Source File
# Begin Source File

SOURCE=.\sArray.h
# End Source File
# Begin Source File

SOURCE=.\SaveLoader.h
# End Source File
# Begin Source File

SOURCE=.\SavePrimitives.h
# End Source File
# Begin Source File

SOURCE=.\ScenarioTracker.h
# End Source File
# Begin Source File

SOURCE=.\Script.h
# End Source File
# Begin Source File

SOURCE=.\ScriptErrors.h
# End Source File
# Begin Source File

SOURCE=.\ScriptParser.h
# End Source File
# Begin Source File

SOURCE=.\ServerSpecificManager.h
# End Source File
# Begin Source File

SOURCE=.\Shadow.h
# End Source File
# Begin Source File

SOURCE=.\Shared.h
# End Source File
# Begin Source File

SOURCE=.\SimpleCryptographer.h
# End Source File
# Begin Source File

SOURCE=.\SimplyArray.h
# End Source File
# Begin Source File

SOURCE=.\SimplyArray.inl
# End Source File
# Begin Source File

SOURCE=.\sky.h
# End Source File
# Begin Source File

SOURCE=.\SmartPtr.h
# End Source File
# Begin Source File

SOURCE=.\Spline.h
# End Source File
# Begin Source File

SOURCE=.\StdAfx.h
# End Source File
# Begin Source File

SOURCE=.\stl_user_config.h
# End Source File
# Begin Source File

SOURCE=.\Streams.h
# End Source File
# Begin Source File

SOURCE=.\StringProcessing.h
# End Source File
# Begin Source File

SOURCE=.\SysInfo.h
# End Source File
# Begin Source File

SOURCE=.\TextManager.h
# End Source File
# Begin Source File

SOURCE=.\Tools.h
# End Source File
# Begin Source File

SOURCE=.\typedefs.h
# End Source File
# Begin Source File

SOURCE=.\TypeIDs.h
# End Source File
# Begin Source File

SOURCE=.\VisFog.h
# End Source File
# Begin Source File

SOURCE=.\weather.h
# End Source File
# Begin Source File

SOURCE=.\ZoneExit.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;cnt;rtf;gif;jpg;jpeg;jpe"
# Begin Source File

SOURCE=.\Game.rc
# End Source File
# Begin Source File

SOURCE=.\res\Game.rc2
# End Source File
# Begin Source File

SOURCE=.\res\Icon32x16.ico
# End Source File
# Begin Source File

SOURCE=.\res\main.ico
# End Source File
# Begin Source File

SOURCE=.\res\NivalLogo.bmp
# End Source File
# End Group
# Begin Group "CDX"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\DXDirect3D.cpp
# End Source File
# Begin Source File

SOURCE=.\DXDirect3D.h
# End Source File
# Begin Source File

SOURCE=.\DXDirect3D.inl
# End Source File
# Begin Source File

SOURCE=.\DXDirectDraw.cpp
# End Source File
# Begin Source File

SOURCE=.\DXDirectDraw.h
# End Source File
# Begin Source File

SOURCE=.\DXDirectDraw.inl
# End Source File
# Begin Source File

SOURCE=.\DXDirectX.cpp
# End Source File
# Begin Source File

SOURCE=.\DXDirectX.h
# End Source File
# Begin Source File

SOURCE=.\DXGeometryProcessor.cpp
# End Source File
# Begin Source File

SOURCE=.\DXGeometryProcessor.h
# End Source File
# Begin Source File

SOURCE=.\DXMaterialLight.cpp
# End Source File
# Begin Source File

SOURCE=.\DXMaterialLight.h
# End Source File
# Begin Source File

SOURCE=.\GeometryContainer.cpp
# End Source File
# Begin Source File

SOURCE=.\GeometryContainer.h
# End Source File
# Begin Source File

SOURCE=.\GeometryInterfaces.h
# End Source File
# Begin Source File

SOURCE=.\GeometryManager.cpp
# End Source File
# Begin Source File

SOURCE=.\GeometryManager.h
# End Source File
# Begin Source File

SOURCE=.\GraphicsProcessor.h
# End Source File
# Begin Source File

SOURCE=.\Shader.h
# End Source File
# Begin Source File

SOURCE=.\ShadingManager.cpp
# End Source File
# Begin Source File

SOURCE=.\ShadingManager.h
# End Source File
# Begin Source File

SOURCE=.\TLPipeline.h
# End Source File
# Begin Source File

SOURCE=.\Visualizator.cpp
# End Source File
# Begin Source File

SOURCE=.\Visualizator.h
# End Source File
# Begin Source File

SOURCE=.\VisualizatorD3D.cpp
# End Source File
# Begin Source File

SOURCE=.\VisualizatorD3D.h
# End Source File
# End Group
# Begin Group "MapObject"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\adFormat.h
# End Source File
# Begin Source File

SOURCE=.\AnimationSystem.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\AnimationSystem.h
# End Source File
# Begin Source File

SOURCE=.\AnimationSystem2.cpp
# End Source File
# Begin Source File

SOURCE=.\AnimationSystem2.h
# End Source File
# Begin Source File

SOURCE=.\MapObject.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\MapObject.h
# End Source File
# Begin Source File

SOURCE=.\MapObjectClientSpecific.cpp
# End Source File
# Begin Source File

SOURCE=.\MapObjectClientSpecific.h
# End Source File
# Begin Source File

SOURCE=.\MapObjectManager.cpp
# End Source File
# Begin Source File

SOURCE=.\MapObjectManager.h
# End Source File
# Begin Source File

SOURCE=.\MapObjectServer.cpp
# End Source File
# Begin Source File

SOURCE=.\MapObjectServer.h
# End Source File
# Begin Source File

SOURCE=.\Object.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Object.h
# End Source File
# Begin Source File

SOURCE=.\Object3DClientSpecific.cpp
# End Source File
# Begin Source File

SOURCE=.\Object3DClientSpecific.h
# End Source File
# Begin Source File

SOURCE=.\Object3DServer.cpp
# End Source File
# Begin Source File

SOURCE=.\Object3DServer.h
# End Source File
# Begin Source File

SOURCE=.\pdbFormat.cpp
# End Source File
# Begin Source File

SOURCE=.\pdbFormat.h
# End Source File
# Begin Source File

SOURCE=.\PerkDatabase.cpp
# End Source File
# Begin Source File

SOURCE=.\PerkDatabase.h
# End Source File
# Begin Source File

SOURCE=.\RPGSystem.cpp
# End Source File
# Begin Source File

SOURCE=.\RPGSystem.h
# End Source File
# Begin Source File

SOURCE=.\RPGSystemConsts.h
# End Source File
# Begin Source File

SOURCE=.\StuffMapObjects.cpp
# End Source File
# Begin Source File

SOURCE=.\StuffMapObjects.h
# End Source File
# Begin Source File

SOURCE=.\StuffMOClientSpec.cpp
# End Source File
# Begin Source File

SOURCE=.\StuffMOClientSpec.h
# End Source File
# Begin Source File

SOURCE=.\udbFormat.cpp
# End Source File
# Begin Source File

SOURCE=.\udbFormat.h
# End Source File
# Begin Source File

SOURCE=.\Unit.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Unit.h
# End Source File
# Begin Source File

SOURCE=.\UnitClientSpecific.cpp
# End Source File
# Begin Source File

SOURCE=.\UnitClientSpecific.h
# End Source File
# Begin Source File

SOURCE=.\UnitDatabase.cpp
# End Source File
# Begin Source File

SOURCE=.\UnitDatabase.h
# End Source File
# Begin Source File

SOURCE=.\UnitServer.cpp
# End Source File
# Begin Source File

SOURCE=.\UnitServer.h
# End Source File
# Begin Source File

SOURCE=.\UnitStatisticsDriver.cpp
# End Source File
# Begin Source File

SOURCE=.\UnitStatisticsDriver.h
# End Source File
# End Group
# Begin Group "World"

# PROP Default_Filter "World*.*"
# Begin Source File

SOURCE=.\ServerConsts.h
# End Source File
# Begin Source File

SOURCE=.\World.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\World.h
# End Source File
# Begin Source File

SOURCE=.\WorldClient.cpp
# End Source File
# Begin Source File

SOURCE=.\WorldClient.h
# End Source File
# Begin Source File

SOURCE=.\WorldInterface.cpp
# End Source File
# Begin Source File

SOURCE=.\WorldInterface.h
# End Source File
# Begin Source File

SOURCE=.\WorldServer.cpp
# End Source File
# Begin Source File

SOURCE=.\WorldServer.h
# End Source File
# End Group
# Begin Group "Figure"

# PROP Default_Filter "Figure*.*"
# Begin Source File

SOURCE=.\figFormat.h
# End Source File
# Begin Source File

SOURCE=.\Figure.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Figure.h
# End Source File
# Begin Source File

SOURCE=.\FigureClientSpecific.cpp
# End Source File
# Begin Source File

SOURCE=.\FigureClientSpecific.h
# End Source File
# Begin Source File

SOURCE=.\FigureManager.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\FigureManager.h
# End Source File
# End Group
# Begin Group "AI"

# PROP Default_Filter "ai*.*"
# Begin Source File

SOURCE=.\aiAck.cpp
# End Source File
# Begin Source File

SOURCE=.\aiAck.h
# End Source File
# Begin Source File

SOURCE=.\aiGraph.cpp
# End Source File
# Begin Source File

SOURCE=.\AIGraph.h
# End Source File
# Begin Source File

SOURCE=.\aiGraph.inl
# End Source File
# Begin Source File

SOURCE=.\aiMap.cpp
# End Source File
# Begin Source File

SOURCE=.\aiMap.h
# End Source File
# Begin Source File

SOURCE=.\aiObjects.cpp
# End Source File
# Begin Source File

SOURCE=.\aiObjects.h
# End Source File
# Begin Source File

SOURCE=.\aiPathFinder.cpp
# End Source File
# Begin Source File

SOURCE=.\aiPathFinder.h
# End Source File
# Begin Source File

SOURCE=.\aiRoute.cpp
# End Source File
# Begin Source File

SOURCE=.\aiRoute.h
# End Source File
# Begin Source File

SOURCE=.\aiScanUnits.cpp
# End Source File
# Begin Source File

SOURCE=.\aiScanUnits.h
# End Source File
# Begin Source File

SOURCE=.\aiSensesConst.h
# End Source File
# Begin Source File

SOURCE=.\aiSettings.cpp
# End Source File
# Begin Source File

SOURCE=.\aiSettings.h
# End Source File
# Begin Source File

SOURCE=.\aiUnitLogic.cpp
# End Source File
# Begin Source File

SOURCE=.\aiUnitLogic.h
# End Source File
# End Group
# Begin Group "Interface"

# PROP Default_Filter "I3D*.*"
# Begin Source File

SOURCE=.\2DScreenWait.cpp
# End Source File
# Begin Source File

SOURCE=.\2DScreenWait.h
# End Source File
# Begin Source File

SOURCE=.\A3Cursor.cpp
# End Source File
# Begin Source File

SOURCE=.\A3Cursor.h
# End Source File
# Begin Source File

SOURCE=.\BinkVideo.cpp
# End Source File
# Begin Source File

SOURCE=.\BinkVideo.h
# End Source File
# Begin Source File

SOURCE=.\FlyingHitPoints.cpp
# End Source File
# Begin Source File

SOURCE=.\FlyingHitPoints.h
# End Source File
# Begin Source File

SOURCE=.\I3DConsole.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DConsole.h
# End Source File
# Begin Source File

SOURCE=.\I3DCursor.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DCursor.h
# End Source File
# Begin Source File

SOURCE=.\I3DEffects.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DEffects.h
# End Source File
# Begin Source File

SOURCE=.\I3DFigure.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DFigure.h
# End Source File
# Begin Source File

SOURCE=.\I3DHandler.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DHandler.h
# End Source File
# Begin Source File

SOURCE=.\I3DInventory.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DInventory.h
# End Source File
# Begin Source File

SOURCE=.\I3DMain.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DMain.h
# End Source File
# Begin Source File

SOURCE=.\I3DMapObjTranslation.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DMapObjTranslation.h
# End Source File
# Begin Source File

SOURCE=.\I3DMenuRects.h
# End Source File
# Begin Source File

SOURCE=.\I3DObject.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DObject.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreen.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreen.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenAutoPlay.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenAutoPlay.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenBase.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenBase.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenCamp.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenCamp.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenConstructor.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenConstructor.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenDebug.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenDebug.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenEcsapeMenu.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenEcsapeMenu.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenEndGame.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenEndGame.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenGlobalMap.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenGlobalMap.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenMainMenu.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenMainMenu.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenMessageBox.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenMessageBox.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenMission.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenMission.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenObjectives.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenObjectives.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenOptions.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenOptions.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenSaveLoad.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenSaveLoad.h
# End Source File
# Begin Source File

SOURCE=.\I3DScreenTutorial.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScreenTutorial.h
# End Source File
# Begin Source File

SOURCE=.\I3DScrollBar.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScrollBar.h
# End Source File
# Begin Source File

SOURCE=.\I3DScrollText.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DScrollText.h
# End Source File
# Begin Source File

SOURCE=.\I3DToolTip.cpp
# End Source File
# Begin Source File

SOURCE=.\I3DToolTip.h
# End Source File
# Begin Source File

SOURCE=.\InputManager.cpp
# End Source File
# Begin Source File

SOURCE=.\InputManager.h
# End Source File
# Begin Source File

SOURCE=.\RTFText.cpp
# End Source File
# Begin Source File

SOURCE=.\RTFText.h
# End Source File
# End Group
# Begin Group "Player"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\Party.cpp
# End Source File
# Begin Source File

SOURCE=.\Party.h
# End Source File
# Begin Source File

SOURCE=.\Player.cpp
# End Source File
# Begin Source File

SOURCE=.\Player.h
# End Source File
# Begin Source File

SOURCE=.\PlayerClient.cpp
# End Source File
# Begin Source File

SOURCE=.\PlayerClient.h
# End Source File
# Begin Source File

SOURCE=.\PlayerManager.cpp
# End Source File
# Begin Source File

SOURCE=.\PlayerManager.h
# End Source File
# Begin Source File

SOURCE=.\PlayerServer.cpp
# End Source File
# Begin Source File

SOURCE=.\PlayerServer.h
# End Source File
# End Group
# Begin Group "Resources"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\CMappedResource.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\CMappedResource.h
# End Source File
# Begin Source File

SOURCE=.\CMMIOResource.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\CMMIOResource.h
# End Source File
# Begin Source File

SOURCE=.\CResourceBase.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\CResourceBase.h
# End Source File
# Begin Source File

SOURCE=.\resFormat.h
# End Source File
# Begin Source File

SOURCE=.\ResourceManager.cpp
# End Source File
# Begin Source File

SOURCE=.\ResourceManager.h
# End Source File
# Begin Source File

SOURCE=.\Win32Classes.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Win32Classes.h
# End Source File
# End Group
# Begin Group "Spell System"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\Effect.cpp
# End Source File
# Begin Source File

SOURCE=.\Effect.h
# End Source File
# Begin Source File

SOURCE=.\EffectManager.cpp
# End Source File
# Begin Source File

SOURCE=.\EffectManager.h
# End Source File
# Begin Source File

SOURCE=.\EffectSpell.cpp
# End Source File
# Begin Source File

SOURCE=.\EffectSpell.h
# End Source File
# Begin Source File

SOURCE=.\sdbFormat.cpp
# End Source File
# Begin Source File

SOURCE=.\sdbFormat.h
# End Source File
# Begin Source File

SOURCE=.\SpellConstructor.cpp
# End Source File
# Begin Source File

SOURCE=.\SpellConstructor.h
# End Source File
# Begin Source File

SOURCE=.\SpellDatabase.cpp
# End Source File
# Begin Source File

SOURCE=.\SpellDatabase.h
# End Source File
# Begin Source File

SOURCE=.\SpellEffect.cpp
# End Source File
# Begin Source File

SOURCE=.\SpellEffect.h
# End Source File
# Begin Source File

SOURCE=.\SpellManager.cpp
# End Source File
# Begin Source File

SOURCE=.\SpellManager.h
# End Source File
# Begin Source File

SOURCE=.\SpellSystem.cpp
# End Source File
# Begin Source File

SOURCE=.\SpellSystem.h
# End Source File
# End Group
# Begin Group "Light, Material & Texture"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\Light.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Light.h
# End Source File
# Begin Source File

SOURCE=.\LightManager.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\LightManager.h
# End Source File
# Begin Source File

SOURCE=.\Material.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Material.h
# End Source File
# Begin Source File

SOURCE=.\MaterialManager.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\MaterialManager.h
# End Source File
# Begin Source File

SOURCE=.\mmpFormat.h
# End Source File
# Begin Source File

SOURCE=.\Texture.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Texture.h
# End Source File
# Begin Source File

SOURCE=.\TextureManager.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\TextureManager.h
# End Source File
# End Group
# Begin Group "Registry"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\FileRegistry.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\FileRegistry.h
# End Source File
# Begin Source File

SOURCE=.\IniFile.cpp
# End Source File
# Begin Source File

SOURCE=.\IniFile.h
# End Source File
# Begin Source File

SOURCE=.\regFormat.h
# End Source File
# Begin Source File

SOURCE=.\WinRegistry.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\WinRegistry.h
# End Source File
# End Group
# Begin Group "Item"

# PROP Default_Filter "Item*.*"
# Begin Source File

SOURCE=.\AssortmentGenerator.cpp
# End Source File
# Begin Source File

SOURCE=.\AssortmentGenerator.h
# End Source File
# Begin Source File

SOURCE=.\idbFormat.cpp
# End Source File
# Begin Source File

SOURCE=.\idbFormat.h
# End Source File
# Begin Source File

SOURCE=.\Item.cpp
# End Source File
# Begin Source File

SOURCE=.\Item.h
# End Source File
# Begin Source File

SOURCE=.\ItemConstructor.cpp
# End Source File
# Begin Source File

SOURCE=.\ItemConstructor.h
# End Source File
# Begin Source File

SOURCE=.\ItemDatabase.cpp
# End Source File
# Begin Source File

SOURCE=.\ItemDatabase.h
# End Source File
# Begin Source File

SOURCE=.\ItemManager.cpp
# End Source File
# Begin Source File

SOURCE=.\ItemManager.h
# End Source File
# End Group
# Begin Group "Quest"

# PROP Default_Filter "quest*.*"
# Begin Source File

SOURCE=.\qdbFormat.cpp
# End Source File
# Begin Source File

SOURCE=.\qdbFormat.h
# End Source File
# Begin Source File

SOURCE=.\QuestDatabase.cpp
# End Source File
# Begin Source File

SOURCE=.\QuestDatabase.h
# End Source File
# End Group
# Begin Group "Sound System"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\MusicSystem.cpp
# End Source File
# Begin Source File

SOURCE=.\MusicSystem.h
# End Source File
# Begin Source File

SOURCE=.\SoundManager.cpp
# End Source File
# Begin Source File

SOURCE=.\SoundManager.h
# End Source File
# Begin Source File

SOURCE=.\SoundTypes.h
# End Source File
# End Group
# Begin Group "Terrain"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\Geometry.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Geometry.h
# End Source File
# Begin Source File

SOURCE=.\mapFormat.h
# End Source File
# Begin Source File

SOURCE=.\Sector.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Sector.h
# End Source File
# Begin Source File

SOURCE=.\Terrain.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Terrain.h
# End Source File
# Begin Source File

SOURCE=.\TerrainPrints.cpp
# End Source File
# Begin Source File

SOURCE=.\TerrainPrints.h
# End Source File
# Begin Source File

SOURCE=.\TerrainPrintsDatabase.cpp
# End Source File
# Begin Source File

SOURCE=.\TerrainPrintsDatabase.h
# End Source File
# Begin Source File

SOURCE=.\tpFormat.cpp
# End Source File
# Begin Source File

SOURCE=.\tpFormat.h
# End Source File
# Begin Source File

SOURCE=.\WaterLevels.cpp
# End Source File
# Begin Source File

SOURCE=.\WaterLevels.h
# End Source File
# End Group
# Begin Group "Shoter"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\DIB.cpp

!IF  "$(CFG)" == "Game - Win32 Release with Debug"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug with Optimization"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Debug Interface with BC"

# ADD BASE CPP /Yu"StdAfx.h"
# ADD CPP /Yu"StdAfx.h"

!ELSEIF  "$(CFG)" == "Game - Win32 Release Interface PCD"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\DIB.h
# End Source File
# Begin Source File

SOURCE=.\Shoter.cpp
# End Source File
# Begin Source File

SOURCE=.\Shoter.h
# End Source File
# End Group
# Begin Group "Acknowledgements"

# PROP Default_Filter "ack*.*"
# Begin Source File

SOURCE=.\AckDatabase.cpp
# End Source File
# Begin Source File

SOURCE=.\AckDatabase.h
# End Source File
# Begin Source File

SOURCE=.\ackFormat.cpp
# End Source File
# Begin Source File

SOURCE=.\ackFormat.h
# End Source File
# End Group
# Begin Source File

SOURCE=.\todo.txt
# End Source File
# End Target
# End Project
