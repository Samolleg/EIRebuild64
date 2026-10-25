#pragma once

struct SCameraControlTag
{
public:
    float fpValue;
    float fpMousePower;
    float fpKeyboardPower;

public:
    float GetMouseValue() const { return (fpValue * fpMousePower); }
    float GetKeyboardValue() const { return (fpValue * fpKeyboardPower); }
};
/////////////////////////////////////////////////////////////////////////////////////
static const DWORD SETTING_NUM_CAMERA_SHORTCUTS = 4;
struct SCameraShortcutSettings
{
    float fDistance; // rod in polar coords
    float fAxisX, fAxisY, fAxisZ;
    float fAngle;
    float fPitch; // pitch angle
};
/////////////////////////////////////////////////////////////////////////////////////
struct SGeneralSettings
{
private:
    CString szStatisticsOutputSettings; // statistics section name
    CString szGameSpecificSettings;     // game specific section name
    CString szTerrainSettings;          // terrain LOD distances
    CString szGeneralSettings;          // general section name
    CString szCameraSettings;           // camera section name
    CString szShadowSettings;           // shadow section name
    CString szPathSettings;             // path section name
public:
    // USER INDEPENDENT SETTINGS
    // path settings
    CString szWorkPath;     // working path
    CString szResourcePath; // resources path
    CString szCDPath;       // path to CD-ROM
    // common general settings (visualization settings)
    int nDrawingTarget;  // drawing target index: 0 - primary driver, 1 - secondary driver, etc.
    DWORD dwWidth;       // resolution width
    DWORD dwHeight;      // resolution height
    DWORD dwBPP;         // resolution bits per pixel
    DWORD dwZBufferBPP;  // z-buffer depth
    DWORD dwRefresh;     // refresh rate (not currently used)
    int nFullScreen;     // is going fullscreen ? (1 = yes, 0 = no)
    int nJoystick;       // is using joystick ?
    int nVideo;          // video playing type
    CString szAudioDevice; // WASAPI endpoint ID; empty selects the default at startup.
    int nAudio;          // enable audio in general
    int nAudioStreaming; // streaming audio volume
    int nAudioSFX;       // SFX audio volume
    int nAudioSpeech;    // speech audio volume
    int nDithering;      // enable dithering
    int nSpecular;       // enable specular
    int nAntialiasing;   // enable antialiasing
    int nTextureQuality; // texture quality settings: currently not used
    int nMipmapping;     // enable mip-mapping
    int nFiltering;      // point = 0, bilinear = 1, trilinear = 2
    int nPerspectiveCorrection; // enable perspective correction
    int nShowParticles;         // enable particles
    int nFPSIndependentCursor;  // enable FPS independent cursor
    int nAutomaticQuantization; // enable automatic quantization of all objects
    int nMovieCapturingFPS;     // FPS during movie capturing
    float fBrightness;
    float fContrast;
    float fGamma;
    int nReverseStereo;
    //
    // lighting/shadow settings
    int nShadowQuality; // shadow quality: 0 - no shadow, 1 - SW shadow w/o blur, 2 - SW shadow with
                        // blur
    int nHeroShadow;    // show my heroes shadow
    int nUnitShadow;    // show shadows from all units
    int nBuildingShadow;    // show shadows from buildings
    int nFloraShadow;       // show shadow from trees/bushes
    int nOtherShadow;       // show shadow from other objects
    int nLightingFreq;      // average time to relight static objects
    int nFloraSpecialLight; // enable after-lighting colors averaging
    // terrain LOD distances
    float fpTerrainLODs[2];           // LOD1 and LOD2 start distances
    float fpTerrainBorderFogDistance; // border fog distance
    int nTerrainEnableWaterWaves;
    int nTerrainEnableSelfShadowing;
    int nTerrainEnableDynamicLights;
    int nTerrainLightingFreq;
    int nTerrainShadowingFreq;
    int nTerrainEnableFootprints;
    int nTerrainEnableBloodprints;
    int nTerrainEnableFireprints;
    int nTerrainEnableBloodprintsRelight;
    // statistics output (for Intel Benchmark purposes)
    int nCreateOutputFile;        // enable create output statistics info file on exit
    int nNumberOfFrames;          // number of frames to play and exit
                                  // game specific settings
                                  // camera settings
    float fCameraXAxisDir;        // 1 or -1
    float fCameraYAxisDir;        // 1 or -1
    float fpCameraMinHeight;      // camera minimal height over terrain
    float fpCameraMaxHeight;      // camera maximal height over terrain
    float fpCameraDefaultHeight;  // camera default height over tarrain (sets only in the beginning)
    float fpCameraDefaultXDeploy; // camera default deploy point x-coord
    float fpCameraDefaultYDeploy; // camera default deploy point y-coord
    float fpCameraMinDistanceToCarrier;      // camera minimal distance to carrier
    float fpCameraMaxDistanceToCarrier;      // camera maximal distance to carrier
    float fpCameraMinLimitDistanceToCarrier; // minimal limit distance to carrier - can't zoom to
                                             // this distance
    float fpCameraMaxLimitDistanceToCarrier; // maximal limit distance to carrier - can't zoom to
                                             // this distance
    float fpCameraDefaultDistanceToCarrier;  // camera default distance to carrier (will be used for
                                             // camera centering on unit)
    // alternative restrictions for the briefing zone
    float fpCameraBriefingMinDistanceToCarrier;      // camera minimal distance to carrier
    float fpCameraBriefingMaxDistanceToCarrier;      // camera maximal distance to carrier
    float fpCameraBriefingMinLimitDistanceToCarrier; // minimal limit distance to carrier - can't
                                                     // zoom to this distance
    float fpCameraBriefingMaxLimitDistanceToCarrier; // maximal limit distance to carrier - can't
                                                     // zoom to this distance
    float fpCameraBriefingMinPitch;                  // минимальный наклон камеры к горизонтали
    float fpCameraBriefingMaxPitch;                  // максимальный наклон камеры к горизонтали
    //
    int nCameraBorderScrollArea; // border area (in pixels) to scroll camera
    int nCameraFrameSelectionSensetiveArea;
    float fCameraTerrainSensetiveArea;
    // camera control
    SCameraControlTag cctCameraScroll; // camera scrolling values
    SCameraControlTag cctCameraRotate; // camera rotation values
    SCameraControlTag cctCameraPitch;  // camera pitch values
    SCameraControlTag cctCameraZoom;   // camera zoom values
    SCameraShortcutSettings camerashortcuts[SETTING_NUM_CAMERA_SHORTCUTS];

    float fpCameraDefaultMinPitch; // минимальный наклон камеры к горизонтали
    float fpCameraDefaultMaxPitch; // максимальный наклон камеры к горизонтали

    float fpNearClipDistance; // camera near clip distance
    float fpFarClipDistance;  // camera far clip distance
    // interface and world settings
    float fpDefaultWindForce; // 0...1
    int nDefaultTimeScale;    // default time scale (number of IRL seconds per game hour)
    int nUseSWCursors;        // enable Software cursors (depends on framerate)
    int nSelectionType;       // 0 - no selection, 1 - corners, 2 - wireframe
    float fpCornerSize;       // size of corner for selection
    int nDrawVisibleOnly;     // draw only visible units (only units, which are visible by player's
                              // units)
    int nDrawPath;            // draw expected path as a dots
    int nDrawFlyingHP;
    int nShowTutorial;
    int nDoAutosave;
    float fToolTipTime; // time (in seconds) to appear tooltip
    int nSwitchFilters; // switch inventory filters when dropping items
    CString szCD1Label, szCD2Label, szCD3Label;
    CString szCharSet;
    DWORD dwKeyboardLayout;
    float fObjectsLightingCoeff; // коэффициент на освещение объектов в зоне
    float fHeroLight[3];         // RGB components (in the [0..1] range)
    float fHeroLightRadius;
    float fQuestLight[3];
    float fQuestLightRadius;
    //
    int nDifficultyLevel;  // 0 = difficult, 1 = normal, 2 = easy
    int nDifficultyLevel2; // storage for difficulty level
    //
    CString szDefaultMapName;        // default name for the terrain map
    CString szDefaultStreamingSound; // default streaming sound name
    float fpFogNightStartDistance;   // night fog distance
    float fpFogDayStartDistance;     // day fog distance

public:
    SGeneralSettings();
    // read write functions
    void ReadSettingsFromFile(LPCSTR pszSettingsFileName);
    void WriteSettingsToFile(LPCSTR pszSettingsFileName);
    void ReadSettingsFromRegistry(LPCSTR pszRegistryKey);
    void WriteSettingsToRegistry(LPCSTR pszRegistryKey);
};
// --------------------------------------------------------------------------------------------
extern SGeneralSettings theSettings;
