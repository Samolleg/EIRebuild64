int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    CStringArray szParams;
    CStringArray szSysMsgs;

    // load system messages
    {
        FILE *file = fopen(".\\res\\sysmsg.txt", "rt");
        if (file == 0)
        {
            szSysMsgs.Add("This Game requires DX7 in order to run!");
            szSysMsgs.Add("Not enough free disk space - play on your own risk!");
            szSysMsgs.Add("Please, insert EI disc 2 in the CD-ROM and press OK");
        }
        else
        {
            int nLen = _filelength(_fileno(file));
            char *pBuffer = dbgnew char[nLen];
            memset(pBuffer, 0, nLen);
            fread(pBuffer, 1, nLen, file);
            fclose(file);
            file = 0;

            NStrProc::SplitString(pBuffer, szSysMsgs, 10);
            if (szSysMsgs.GetSize() < 3)
            {
                szSysMsgs.RemoveAll();
                szSysMsgs.Add("This Game requires DX7 in order to run!");
                szSysMsgs.Add("Not enough free disk space - play on your own risk!");
                szSysMsgs.Add("Please, insert EI disc 2 in the CD-ROM and press OK");
            }

            delete[] pBuffer;
        }
    }

    DWORD dwDXVersion = GetDXVersion();
    if (dwDXVersion < 0x700)
    {
        ::MessageBox(0, szSysMsgs[0], "CRITICAL ERROR", MB_OK | MB_ICONEXCLAMATION);
        return 0xDEAD;
    }

    // register classes for save/load
    REGISTER_SAVELOAD_CLASS(1, CShop);
    REGISTER_SAVELOAD_CLASS(2, CSingleParty);

    // split command line
    NStrProc::SplitString(lpCmdLine, szParams, ' ');

    // process command line
    NGame::szCameraFileName = ".\\camera.cam";
    NGame::szScriptFileName.Empty();
    NGame::szObjectsFileName.Empty();
    NGame::szMapFileName.Empty();

    for (int i = 0; i < szParams.GetSize(); i++)
    {
        if (szParams[i].Find("-moviepath_") == 0)
        {
            theWorld.ResetFlag(WC_MAKE_MOVIE);
            NGame::szMovieDir = szParams[i].Right(szParams[i].GetLength() - 11);
            if (NGame::szMovieDir[NGame::szMovieDir.GetLength() - 1] != '\\')
                NGame::szMovieDir += '\\';
        }
        else if (!theWorld.CheckFlag(WC_SAVE_CAMERA) && (szParams[i].CompareNoCase("-nocam") == 0))
            theWorld.ResetFlag(WC_USE_CAMERA_SCRIPT);
        else if (!theWorld.CheckFlag(WC_SAVE_CAMERA) && (szParams[i].Find(".cam") != -1))
            NGame::szCameraFileName = szParams[i];
        else if (szParams[i].Find(".scr") != -1)
            NGame::szScriptFileName = szParams[i];
        else if (szParams[i].Find(".mob") != -1)
            NGame::szObjectsFileName = szParams[i];
        else if (szParams[i].Find(".mpr") != -1)
            NGame::szMapFileName = szParams[i];
        else if (szParams[i].Find("-benchmark_") == 0)
        {
            theSettings.nNumberOfFrames = atoi(szParams[i].Right(szParams[i].GetLength() - 11));
            theSettings.nCreateOutputFile = 1;
        }
        else if (szParams[i].Find("-devicetarget_") == 0)
            theSettings.nDrawingTarget = atoi(szParams[i].Right(szParams[i].GetLength() - 14));
    }

    // Initialize global strings
    LoadString(hInstance, IDS_APP_TITLE, NGame::szAppTitleName, NGame::MAX_LOADSTRING);
    LoadString(hInstance, IDS_WND_CLASS, NGame::szWndClassName, NGame::MAX_LOADSTRING);
    // create and register class style
    NGame::atomWndClassName = NGame::MyRegisterClass(hInstance);
    // check for this app already runed
    if (!NGame::CheckPreviousApp(reinterpret_cast<LPCSTR>(NGame::atomWndClassName),
                                 NGame::szAppTitleName))
        exit(-1);

    // setup registry key
    CWorld::SetRegistryKey("Software\\Nival Interactive\\EvilIslands");
    CWorld::InitCurrentDir();
    // read ai settings
    theAIGeneralSettings.ReadFromFile(CWorld::GetCurrentPath() + "config\\ai.reg");
    theSettings.ReadSettingsFromRegistry(CWorld::GetRegistryKey());

    // set new memory depleted handler
    _PNH pOldMemoryHandler = _set_new_handler(MemoryDepletedHandler);

    // create worlds and establish connection to local server
    theWorld.Create();
    theWorldServer.Create();
    theCamera.Create(&theSettings, theSettings.fpNearClipDistance, theSettings.fpFarClipDistance,
                     static_cast<float>(pi / 3.5));
    theWorld.SetCamera(&theCamera);

    ////////// 456995 //////////
    // CD-2 check
    {
        CString szCDPath = theWorld.GetCDPath();
        CString szWorkPath = theWorld.GetWorkPath();
        // check disk free space
        char drive[128], dir[1024], fname[128], ext[128];
        _splitpath(szWorkPath, drive, dir, fname, ext);
        strcat(drive, "\\.");
        double fDiskFreeSpace = GetFreeDiskSpace(drive);
        if (fDiskFreeSpace != 0)
        {
            if (fDiskFreeSpace < 300.0 * double(1L << 20))
            {
                int nRetCode = ::MessageBox(0, szSysMsgs[1], "Warning",
                                            MB_OKCANCEL | MB_ICONWARNING);
                if (nRetCode == IDCANCEL)
                    return 0xDEAD;
            }
        }
        // check CD2 in CD-ROM for non-full install
        if (szWorkPath != szCDPath) // non-full install
        {
            char name[1024];
            memset(name, 0, sizeof(name));
            // retrieve CD volume information
            _splitpath(szCDPath, drive, dir, fname, ext);
            strcat(drive, "\\.");
            GetVolumeInformation(drive, name, 1024, 0, 0, 0, 0, 0);
            while (theSettings.szCD2Label != name)
            {
                int nRetCode = ::MessageBox(0, szSysMsgs[2], "Warning",
                                            MB_OKCANCEL | MB_ICONWARNING);
                if (nRetCode == IDCANCEL)
                    return 0xDEAD;
                GetVolumeInformation(drive, name, 1024, 0, 0, 0, 0, 0);
            }
        }
    }

    ////////// 456C22 //////////
    NGame::pInputManager = dbgnew CInputManager; // inlined

    ////////// 456C5E //////////
    // create saves\current
    CString szPath;
    szPath.Format("%ssaves\\", theWorld.GetWorkPath());
    CreateDirectory(szPath, 0);
    // create multiplayer dir
    szPath.Format("%smp\\", theWorld.GetWorkPath());
    CreateDirectory(szPath, 0);
    // create textures directory
    szPath.Format("%stextures\\", theWorld.GetWorkPath());
    CreateDirectory(szPath, 0);

    ////////// 456CC7 //////////
    // Perform application initialization
    if (!NGame::InitInstance(hInstance, nCmdShow)) // inlined
        return FALSE;

    ////////// 456D2A //////////
    // setup camera placement
    CPlot3D cameraCP;
    cameraCP.x = theSettings.fpCameraDefaultXDeploy;
    cameraCP.y = theSettings.fpCameraDefaultYDeploy;
    cameraCP.z = theSettings.fpCameraDefaultHeight;
    theCamera.SetCP(cameraCP);

    ////////// 456D62 //////////
    // create CDXDirect3D object and CDXGeometryProcessor object in visualizator
    GetVisualizator()->Create(NGame::hWnd, theSettings);
    {
        DWORD dwTotal = 0, dwFree = 0;
        DDSCAPS2 caps;
        InitCaps(caps);

        caps.dwCaps = DDSCAPS_VIDEOMEMORY;
        theVisualizatorD3D.GetD3D()->GetDD()->GetAvailableVidMem(&caps, &dwTotal, &dwFree);
        DWORD dwVideoFree = dwFree;

        caps.dwCaps = DDSCAPS_TEXTURE;
        theVisualizatorD3D.GetD3D()->GetDD()->GetAvailableVidMem(&caps, &dwTotal, &dwFree);
        DWORD dwTextureFree = dwFree;
        NGame::bLowTexMem = (dwFree <= 8388608);
        if (dwTextureFree < dwVideoFree)
            dwVideoFree -= dwTextureFree;
        NGame::bLowVidMem = (dwVideoFree <= 8388608);

        caps.dwCaps = DDSCAPS_VIDEOMEMORY | DDSCAPS_NONLOCALVIDMEM;
        theVisualizatorD3D.GetD3D()->GetDD()->GetAvailableVidMem(&caps, &dwTotal, &dwFree);
        NGame::bLowAGPMem = (dwFree <= 8388608);

        // voodoo check
        NGame::bVoodoo = theSettings.nDrawingTarget != 0;
    }
    theVisualizatorD3D.SetGammaCorrection(theSettings.fBrightness, theSettings.fContrast,
                                          theSettings.fGamma);

    ////////// 456E4D //////////
    // initialize sound manager, if audio enabled
    if (theSettings.nAudio)
    {
        CString szResourcesPath = theWorld.GetResourcePath();
        CString szWorkPath = theWorld.GetWorkPath();
        CString szCDPath = theWorld.GetCDPath();
        // create all data structures for sound manager
        theWorld.GetSoundManager()->Create(NGame::hWnd, TRUE);
        // select resource for streaming sound
        theWorld.GetSoundManager()->SelectStreamingResource(szWorkPath + "stream\\",
                                                            szResourcesPath + "streamsn.reg");
        // select resource for SFXes
        theWorld.GetSoundManager()->SelectFXResource(szResourcesPath + "sfx.res");
        {
            CString szSpeechRes = szCDPath + "res\\speech.res";
            theWorld.GetSoundManager()->AddFXResource(szCDPath + "res\\speech.res");
        }
        // set sound volumes
        theWorld.GetSoundManager()->SetFXMasterVolume(theSettings.nAudioSFX);
        theWorld.GetSoundManager()->SetSpeechMasterVolume(theSettings.nAudioSpeech);
        theWorld.GetSoundManager()->SetMusicMasterVolume(theSettings.nAudioStreaming);
        theWorld.GetSoundManager()->SetStereoReverse(theSettings.nReverseStereo ? true : false);
    }

    // play start movie sequence
    NGame::PlayMoviesSequence("Start");
    // play static logo
    C2DScreenWait::pszProgressName = "Progres.bik";
    C2DScreenWait::Step(0);
    C2DScreenWait::Clear();

    ////////// 456FE9 //////////
    // load databases
    thePerkDatabase.LoadDatabase(theSettings.szResourcePath, "database.res");
    theQuestDatabase.Load(theSettings.szResourcePath);
    theUnitDatabase.Load(theSettings.szResourcePath, "database.res");
    theSpellDatabase.Load(theSettings.szResourcePath, "database.res");
    theItemDatabase.Load(theSettings.szResourcePath, "database.res");
    theLeverDatabase.Load(theSettings.szResourcePath);
    theAckDatabase.Load(theSettings.szResourcePath);
    theTPDatabase.Load(theSettings.szResourcePath);

    ////////// 457082 //////////
    // final world initialization
    NGame::pInputManager->Create(NGame::hWnd, hInstance);
    theWorld.Init(theVisualizatorD3D.GetD3D());
    TheCursorInit(theSettings.nFPSIndependentCursor != 0, NGame::pInputManager,
                  false /*NGame::bLowVidMem*/, !NGame::bVoodoo);
    theInterface.Init(theVisualizatorD3D.GetD3D());
    theMap.Init();

    ////////// 4570E3 //////////
    // go to game
    LoadGame();

    theVisualizatorD3D.SetGammaCorrection(theSettings.fBrightness, theSettings.fContrast,
                                          theSettings.fGamma);

    // Main message loop:
    MSG msg;
    for (;;)
    {
        bool bActive;
        // Idle processing until no new message come
        while (
            !PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE)) // no messages in the queue => process OnIdle
        {
            // Update render state
            UpdateRenderEnable();
            bActive = CountTime();
            if (bActive)
            {
                theInterface.OnIdle();
                NGame::DrawFrame(bEnableFlip);
            }
        }
        if (!GetMessage(&msg, 0, 0, 0))
            break;
        TranslateMessage(&msg);
        if (bActive)
            NGame::WndProc(msg.message, msg.wParam, msg.lParam);
        else
            NGame::ShortWndProc(NGame::hWnd, msg.message, msg.wParam, msg.lParam);
    }

    ////////// 457668 //////////
    theWorld.GetTextureManager()->DeleteUnitLayerTextures();

    theInterface.Fini();
    theCursor.Fini();
    NGame::pInputManager = 0;

    theVisualizatorD3D.SetGammaCorrection(0, 0, 0);
    GetVisualizator()->Destroy();

    theSettings.WriteSettingsToRegistry(theWorld.GetRegistryKey());

    theWorld.SetCamera(0);
    theWorldServer.Destroy();
    theWorld.Destroy();

    // restore memory handler
    _set_new_handler(pOldMemoryHandler);
    return msg.wParam;
}
