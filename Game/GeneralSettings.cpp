#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "GeneralSettings.h"

#include "Notification.h"

#include "WinRegistry.h"
#include "IniFile.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include <filesystem>
/////////////////////////////////////////////////////////////////////////////////////
SGeneralSettings theSettings;
namespace {
float ImportedShortcutPitch(float value) {
    // Legacy defaults/profiles persisted 45 degrees in a slot consumed as radians.
    // Preserve normal radian shortcuts; convert only unambiguous degree-range values.
    return value > FP_2PI && value <= 90.0f ? ToRadian(value) : value;
}
std::filesystem::path InstalledGameRoot()
{
    char modulePath[1024] = {};
    DWORD length = GetModuleFileNameA(nullptr, modulePath, sizeof(modulePath));
    if (!length || length >= sizeof(modulePath)) return {};
    const auto engine = std::filesystem::path(modulePath).parent_path();
    const auto portable = engine.parent_path() / "Base";
    if (std::filesystem::is_regular_file(portable / "Res" / "database.res") &&
        std::filesystem::is_directory(portable / "Maps")) return portable;
    const auto root = engine.parent_path().parent_path();
    std::error_code error;
    if (!std::filesystem::is_regular_file(root / "Res" / "database.res", error) || error)
        return {};
    if (!std::filesystem::is_directory(root / "Maps", error) || error)
        return {};
    return root;
}
CString DirectoryWithSlash(const std::filesystem::path &path)
{
    return CString((path.string() + "\\").c_str());
}
}
/////////////////////////////////////////////////////////////////////////////////////
SGeneralSettings::SGeneralSettings()
{
    // set section names
    szStatisticsOutputSettings = "Statistics Output Settings";
    szGameSpecificSettings = "Game Specific Settings";
    szTerrainSettings = "Terrain Settings";
    szGeneralSettings = "General Settings";
    szCameraSettings = "Camera Settings";
    szShadowSettings = "Shadow Settings";
    szPathSettings = "Path Settings";
    // common general settings (visualization settings)
    nDrawingTarget = 0;
    dwWidth = 800;
    dwHeight = 600;
    dwBPP = 16;
    dwZBufferBPP = 16;
    dwRefresh = 0;
    nFullScreen = 1;
    nJoystick = 0;
    nVideo = 0;
    nAudio = 1;
    nAudioStreaming = 100;
    nAudioSFX = 100;
    nAudioSpeech = 100;
    nAntialiasing = 0;
    nDithering = 0;
    nSpecular = 0;
    nTextureQuality = 1;
    nMipmapping = 0;
    nFiltering = 1;
    nPerspectiveCorrection = 1;
    nLightingFreq = 10000;
    nFloraSpecialLight = 0;
    nShowParticles = 1;
    nFPSIndependentCursor = 1;
    nAutomaticQuantization = 0;
    nMovieCapturingFPS = 15;
    fBrightness = 0;
    fContrast = 0;
    fGamma = 0;
    nReverseStereo = 0;
    // shadow settings
    nShadowQuality = 0;
    nHeroShadow = 1;
    nUnitShadow = 1;
    nBuildingShadow = 0;
    nFloraShadow = 0;
    nOtherShadow = 0;
    // terrain LOD distances
    fpTerrainLODs[0] = 40.0f;
    fpTerrainLODs[1] = 75.0f;
    fpTerrainBorderFogDistance = 32.0f;
    nTerrainEnableSelfShadowing = 0;
    nTerrainEnableWaterWaves = 1;
    nTerrainEnableDynamicLights = 1;
    nTerrainLightingFreq = 1000;
    nTerrainShadowingFreq = 6000;
    nTerrainEnableFootprints = 1;
    nTerrainEnableBloodprints = 1;
    nTerrainEnableFireprints = 1;
    nTerrainEnableBloodprintsRelight = 0;
    // pathes settings
    szWorkPath = ".\\";
    szResourcePath = ".\\res\\";
    szCDPath = ".\\";
    //
    nCreateOutputFile = 0;
    nNumberOfFrames = -1;
    // game specific settings
    // camera settings
    fCameraXAxisDir = 1;
    fCameraYAxisDir = 1;
    fpCameraMinHeight = 4;
    fpCameraMaxHeight = 100;
    fpCameraDefaultHeight = 30;
    fpCameraDefaultXDeploy = 0;
    fpCameraDefaultYDeploy = 0;
    fpCameraMinDistanceToCarrier = 4;
    fpCameraMaxDistanceToCarrier = 100;
    fpCameraMinLimitDistanceToCarrier = 3;
    fpCameraMaxLimitDistanceToCarrier = 80;
    fpCameraDefaultDistanceToCarrier = 30;
    fpCameraDefaultMinPitch = 20.0f;
    fpCameraDefaultMaxPitch = 80.0f;

    fpCameraBriefingMinDistanceToCarrier = 4;
    fpCameraBriefingMaxDistanceToCarrier = 100;
    fpCameraBriefingMinLimitDistanceToCarrier = 3;
    fpCameraBriefingMaxLimitDistanceToCarrier = 80;
    fpCameraBriefingMinPitch = 30;
    fpCameraBriefingMaxPitch = 60;
    //
    nCameraBorderScrollArea = 5;
    nCameraFrameSelectionSensetiveArea = 20;
    fCameraTerrainSensetiveArea = 10;
    // camera scroll values
    cctCameraScroll.fpValue = 1.0f;
    cctCameraScroll.fpMousePower = 1.0f;
    cctCameraScroll.fpKeyboardPower = 0.5f;
    // camera rotate values
    cctCameraRotate.fpValue = 1.0f;
    cctCameraRotate.fpMousePower = 1.0f;
    cctCameraRotate.fpKeyboardPower = 0.5f;
    // camera pitch values
    cctCameraPitch.fpValue = 1.0f;
    cctCameraPitch.fpMousePower = 1.0f;
    cctCameraPitch.fpKeyboardPower = 0.5f;
    // camera zoom values
    cctCameraZoom.fpValue = 1.0f;
    cctCameraZoom.fpMousePower = 1.0f;
    cctCameraZoom.fpKeyboardPower = 0.5f;
    //
    memset(camerashortcuts, 0, sizeof(camerashortcuts));
    for (int i = 0; i < SETTING_NUM_CAMERA_SHORTCUTS; ++i)
    {
        camerashortcuts[i].fAxisZ = 1;
        camerashortcuts[i].fDistance = fpCameraDefaultDistanceToCarrier;
        camerashortcuts[i].fPitch = ToRadian(45.0f);
    }
    //
    fpNearClipDistance = 2;
    fpFarClipDistance = 100;
    // interface and world settings
    fpDefaultWindForce = 0.4f;
    nDefaultTimeScale = 60;
    nUseSWCursors = 1;
    nSelectionType = 0;
    fpCornerSize = 0.5f;
    nDrawVisibleOnly = 1;
    nDrawPath = 1;
    nDrawFlyingHP = 1;
    nShowTutorial = 1;
    nDoAutosave = 1;
    fToolTipTime = 0.5f;
    nSwitchFilters = 1;
    szCD1Label = "EICD1";
    szCD2Label = "EICD2";
    szCD3Label = "EICD3";
    szCharSet = "english";
    dwKeyboardLayout = 0x00000409;

    fObjectsLightingCoeff = 1.0f;
    //
    fHeroLight[0] = 255.0f / 255.0f;
    fHeroLight[1] = 236.0f / 255.0f;
    fHeroLight[2] = 170.0f / 255.0f;
    fHeroLightRadius = 10;
    fQuestLight[0] = 200.0f / 255.0f;
    fQuestLight[1] = 255.0f / 255.0f;
    fQuestLight[2] = 255.0f / 255.0f;
    fQuestLightRadius = 10;
    //
    nDifficultyLevel = 0;
    nDifficultyLevel2 = 0;
    //
    szDefaultMapName = "none";
    szDefaultStreamingSound = "none";
    fpFogNightStartDistance = 50;
    fpFogDayStartDistance = 90;
}
/////////////////////////////////////////////////////////////////////////////////////
// read setting from .ini file
// requires FULL INI-FILE NAME (with drive too)
void SGeneralSettings::ReadSettingsFromFile(LPCSTR szSettingsFileName)
{
    CStringArray szStrings;
    CIniFile reg;
    //
    reg.Open(szSettingsFileName);
    ////////////////////////////////////////////////////////////////////////////////////
    // COMMON SETTINGS (independent from user)
    // pathes settings
    szWorkPath = reg.GetString(szPathSettings, "Work path", szWorkPath);
    szResourcePath = reg.GetString(szPathSettings, "Resources path", szResourcePath);
    szCDPath = reg.GetString(szPathSettings, "CD-ROM Path", szCDPath);
    ////////////////////////////////////////////////////////////////////////////////////
    // GENERAL SETTINGS
    // video settings
    nDrawingTarget = reg.GetInt(szGeneralSettings, "DrawingTarget", nDrawingTarget);
    reg.GetStringArray(szGeneralSettings, "VideoResolution", szStrings, 'x');
    if (szStrings.GetSize() == 2)
    {
        dwWidth = atoi(szStrings[0]);
        dwHeight = atoi(szStrings[1]);
    }
    dwBPP = reg.GetDWord(szGeneralSettings, "VideoColorDepthInBits", dwBPP);
    dwZBufferBPP = reg.GetDWord(szGeneralSettings, "VideoZBufferDepthInBits", dwZBufferBPP);
    dwRefresh = reg.GetDWord(szGeneralSettings, "VideoRefreshRate", dwRefresh);
    nFullScreen = reg.GetInt(szGeneralSettings, "FullScreen", nFullScreen);
    // joystick
    nJoystick = reg.GetInt(szGeneralSettings, "Joystick", 0);
    // Audio settings
    nAudio = reg.GetInt(szGeneralSettings, "Audio", nAudio);
    nVideo = reg.GetInt(szGeneralSettings, "Video", nVideo);
    nAudioStreaming = reg.GetInt(szGeneralSettings, "AudioStreaming", nAudioStreaming);
    nAudioSFX = reg.GetInt(szGeneralSettings, "AudioSFX", nAudioSFX);
    nAudioSpeech = reg.GetInt(szGeneralSettings, "AudioSpeech", nAudioSpeech);
    // quality settings
    nAntialiasing = reg.GetInt(szGeneralSettings, "Antialiasing", nAntialiasing);
    nDithering = reg.GetInt(szGeneralSettings, "Dithering", nDithering);
    nSpecular = reg.GetInt(szGeneralSettings, "Specular", nSpecular);
    nTextureQuality = reg.GetInt(szGeneralSettings, "TextureQuality", nTextureQuality);
    nMipmapping = reg.GetInt(szGeneralSettings, "Mipmapping", nMipmapping);
    nFiltering = reg.GetInt(szGeneralSettings, "Filtering", nFiltering);
    nPerspectiveCorrection = reg.GetInt(szGeneralSettings, "PerspectiveCorrection",
                                        nPerspectiveCorrection);
    nShowParticles = reg.GetInt(szGeneralSettings, "ShowParticles", nShowParticles);
    nFPSIndependentCursor = reg.GetInt(szGeneralSettings, "FPSIndependentCursor",
                                       nFPSIndependentCursor);
    nAutomaticQuantization = reg.GetInt(szGeneralSettings, "AutomaticQuantization",
                                        nAutomaticQuantization);
    nMovieCapturingFPS = reg.GetInt(szGeneralSettings, "MovieCapturingFPS", nMovieCapturingFPS);
    // gamma ramp settings
    fBrightness = reg.GetFloat(szGeneralSettings, "Brightness", fBrightness);
    fContrast = reg.GetFloat(szGeneralSettings, "Contrast", fContrast);
    fGamma = reg.GetFloat(szGeneralSettings, "Gamma", fGamma);
    // sound
    nReverseStereo = reg.GetInt(szGeneralSettings, "Reverse Stereo", nReverseStereo);

    // shadow settings
    nShadowQuality = reg.GetInt(szShadowSettings, "ShadowQuality", nShadowQuality);
    nHeroShadow = reg.GetInt(szShadowSettings, "HeroShadow", nHeroShadow);
    nUnitShadow = reg.GetInt(szShadowSettings, "UnitShadow", nUnitShadow);
    nBuildingShadow = reg.GetInt(szShadowSettings, "BuildingShadow", nBuildingShadow);
    nFloraShadow = reg.GetInt(szShadowSettings, "FloraShadow", nFloraShadow);
    nOtherShadow = reg.GetInt(szShadowSettings, "OtherShadow", nOtherShadow);
    nLightingFreq = reg.GetInt(szShadowSettings, "LightingFreq", nLightingFreq);
    nFloraSpecialLight = reg.GetInt(szShadowSettings, "FloraSpecialLight", nFloraSpecialLight);
    // terrain LOD distances
    fpTerrainLODs[0] = reg.GetFloat(szTerrainSettings, "LOD1", fpTerrainLODs[0]);
    fpTerrainLODs[1] = reg.GetFloat(szTerrainSettings, "LOD2", fpTerrainLODs[1]);
    fpTerrainBorderFogDistance = reg.GetFloat(szTerrainSettings, "BorderFogDistance",
                                              fpTerrainBorderFogDistance);
    nTerrainEnableWaterWaves = reg.GetInt(szTerrainSettings, "EnableWaterWaves",
                                          nTerrainEnableWaterWaves);
    nTerrainEnableDynamicLights = reg.GetInt(szTerrainSettings, "EnableDynamicLights",
                                             nTerrainEnableDynamicLights);
    nTerrainEnableSelfShadowing = reg.GetInt(szTerrainSettings, "EnableSelfShadowing",
                                             nTerrainEnableSelfShadowing);
    nTerrainLightingFreq = reg.GetInt(szTerrainSettings, "LightingFreq", nTerrainLightingFreq);
    nTerrainShadowingFreq = reg.GetInt(szTerrainSettings, "ShadowingFreq", nTerrainShadowingFreq);
    nTerrainEnableFootprints = reg.GetInt(szTerrainSettings, "EnableFootprints",
                                          nTerrainEnableFootprints);
    nTerrainEnableBloodprints = reg.GetInt(szTerrainSettings, "EnableBloodprints",
                                           nTerrainEnableBloodprints);
    nTerrainEnableFireprints = reg.GetInt(szTerrainSettings, "EnableFireprints",
                                          nTerrainEnableFireprints);
    nTerrainEnableBloodprintsRelight = reg.GetInt(szTerrainSettings, "EnableBloodprintsRelight",
                                                  nTerrainEnableBloodprintsRelight);
    ////////////////////////////////////////////////////////////////////////////////////
    // NON-GENERAL (GAME SPECIFIC) SETTINGS
    // output info
    nCreateOutputFile = reg.GetInt(szStatisticsOutputSettings, "CreateOutputInfo",
                                   nCreateOutputFile);
    nNumberOfFrames = reg.GetInt(szStatisticsOutputSettings, "NumberOfFrames", nNumberOfFrames);
    // camera settings
    fCameraXAxisDir = reg.GetInt(szCameraSettings, "CameraFlipXAxis", fCameraXAxisDir == -1) ? -1
                                                                                             : 1;
    fCameraYAxisDir = reg.GetInt(szCameraSettings, "CameraFlipYAxis", fCameraYAxisDir == -1) ? -1
                                                                                             : 1;
    fpCameraMinHeight = reg.GetFloat(szCameraSettings, "CameraMinHeight", fpCameraMinHeight);
    fpCameraMaxHeight = reg.GetFloat(szCameraSettings, "CameraMaxHeight", fpCameraMaxHeight);
    fpCameraDefaultHeight = reg.GetFloat(szCameraSettings, "CameraDefaultHeight",
                                         fpCameraDefaultHeight);
    fpCameraDefaultXDeploy = reg.GetFloat(szCameraSettings, "CameraDefaultXDeploy",
                                          fpCameraDefaultXDeploy);
    fpCameraDefaultYDeploy = reg.GetFloat(szCameraSettings, "CameraDefaultYDeploy",
                                          fpCameraDefaultYDeploy);
    fpCameraMinDistanceToCarrier = reg.GetFloat(szCameraSettings, "CameraMinDistanceToCarrier",
                                                fpCameraMinDistanceToCarrier);
    fpCameraMaxDistanceToCarrier = reg.GetFloat(szCameraSettings, "CameraMaxDistanceToCarrier",
                                                fpCameraMaxDistanceToCarrier);
    fpCameraMinLimitDistanceToCarrier = reg.GetFloat(szCameraSettings,
                                                     "CameraMinLimitDistanceToCarrier",
                                                     fpCameraMinLimitDistanceToCarrier);
    fpCameraMaxLimitDistanceToCarrier = reg.GetFloat(szCameraSettings,
                                                     "CameraMaxLimitDistanceToCarrier",
                                                     fpCameraMaxLimitDistanceToCarrier);
    fpCameraDefaultDistanceToCarrier = reg.GetFloat(szCameraSettings,
                                                    "CameraDefaultDistanceToCarrier",
                                                    fpCameraDefaultDistanceToCarrier);
    fpCameraDefaultMinPitch = reg.GetFloat(szCameraSettings, "CameraDefaultMinPitch",
                                           fpCameraDefaultMinPitch);
    fpCameraDefaultMaxPitch = reg.GetFloat(szCameraSettings, "CameraDefaultMaxPitch",
                                           fpCameraDefaultMaxPitch);

    fpCameraBriefingMinDistanceToCarrier = reg.GetFloat(szCameraSettings,
                                                        "CameraBriefingMinDistanceToCarrier",
                                                        fpCameraBriefingMinDistanceToCarrier);
    fpCameraBriefingMaxDistanceToCarrier = reg.GetFloat(szCameraSettings,
                                                        "CameraBriefingMaxDistanceToCarrier",
                                                        fpCameraBriefingMaxDistanceToCarrier);
    fpCameraBriefingMinLimitDistanceToCarrier = reg.GetFloat(
        szCameraSettings, "CameraBriefingMinLimitDistanceToCarrier",
        fpCameraBriefingMinLimitDistanceToCarrier);
    fpCameraBriefingMaxLimitDistanceToCarrier = reg.GetFloat(
        szCameraSettings, "CameraBriefingMaxLimitDistanceToCarrier",
        fpCameraBriefingMaxLimitDistanceToCarrier);
    fpCameraBriefingMinPitch = reg.GetFloat(szCameraSettings, "CameraBriefingMinPitch",
                                            fpCameraBriefingMinPitch);
    fpCameraBriefingMaxPitch = reg.GetFloat(szCameraSettings, "CameraBriefingMaxPitch",
                                            fpCameraBriefingMaxPitch);

    nCameraBorderScrollArea = reg.GetInt(szCameraSettings, "CameraBorderScrollArea",
                                         nCameraBorderScrollArea);
    nCameraFrameSelectionSensetiveArea = reg.GetInt(szCameraSettings,
                                                    "CameraFrameSelectionSensetiveArea",
                                                    nCameraFrameSelectionSensetiveArea);
    fCameraTerrainSensetiveArea = reg.GetFloat(szCameraSettings, "CameraTerrainSensetiveArea",
                                               fCameraTerrainSensetiveArea);

    cctCameraScroll.fpValue = reg.GetFloat(szCameraSettings, "CameraDefaultScrollSpeed",
                                           cctCameraScroll.fpValue);
    cctCameraScroll.fpMousePower = reg.GetFloat(szCameraSettings, "CameraDefaultScrollPowerMouse",
                                                cctCameraScroll.fpMousePower);
    cctCameraScroll.fpKeyboardPower = reg.GetFloat(szCameraSettings,
                                                   "CameraDefaultScrollPowerKeyboard",
                                                   cctCameraScroll.fpKeyboardPower);

    cctCameraRotate.fpValue = reg.GetFloat(szCameraSettings, "CameraDefaultRotateSpeed",
                                           cctCameraRotate.fpValue);
    cctCameraRotate.fpMousePower = reg.GetFloat(szCameraSettings, "CameraDefaultRotatePowerMouse",
                                                cctCameraRotate.fpMousePower);
    cctCameraRotate.fpKeyboardPower = reg.GetFloat(szCameraSettings,
                                                   "CameraDefaultRotatePowerKeyboard",
                                                   cctCameraRotate.fpKeyboardPower);

    cctCameraPitch.fpValue = reg.GetFloat(szCameraSettings, "CameraDefaultPitchSpeed",
                                          cctCameraPitch.fpValue);
    cctCameraPitch.fpMousePower = reg.GetFloat(szCameraSettings, "CameraDefaultPitchPowerMouse",
                                               cctCameraPitch.fpMousePower);
    cctCameraPitch.fpKeyboardPower = reg.GetFloat(szCameraSettings,
                                                  "CameraDefaultPitchPowerKeyboard",
                                                  cctCameraPitch.fpKeyboardPower);

    cctCameraZoom.fpValue = reg.GetFloat(szCameraSettings, "CameraDefaultZoomSpeed",
                                         cctCameraZoom.fpValue);
    cctCameraZoom.fpMousePower = reg.GetFloat(szCameraSettings, "CameraDefaultZoomPowerMouse",
                                              cctCameraZoom.fpMousePower);
    cctCameraZoom.fpKeyboardPower = reg.GetFloat(szCameraSettings, "CameraDefaultZoomPowerKeyboard",
                                                 cctCameraZoom.fpKeyboardPower);
    //
    for (int i = 0; i < SETTING_NUM_CAMERA_SHORTCUTS; ++i)
    {
        char buff[1024];
        sprintf(buff, "CameraShortcut %d AxisX", i);
        camerashortcuts[i].fAxisX = reg.GetFloat(szCameraSettings, buff, camerashortcuts[i].fAxisX);
        sprintf(buff, "CameraShortcut %d AxisY", i);
        camerashortcuts[i].fAxisY = reg.GetFloat(szCameraSettings, buff, camerashortcuts[i].fAxisY);
        sprintf(buff, "CameraShortcut %d AxisZ", i);
        camerashortcuts[i].fAxisZ = reg.GetFloat(szCameraSettings, buff, camerashortcuts[i].fAxisZ);
        sprintf(buff, "CameraShortcut %d Angle", i);
        camerashortcuts[i].fAngle = reg.GetFloat(szCameraSettings, buff, camerashortcuts[i].fAngle);
        sprintf(buff, "CameraShortcut %d Distance", i);
        camerashortcuts[i].fDistance = reg.GetFloat(szCameraSettings, buff,
                                                    camerashortcuts[i].fDistance);
        sprintf(buff, "CameraShortcut %d Pitch", i);
        camerashortcuts[i].fPitch = ImportedShortcutPitch(reg.GetFloat(szCameraSettings, buff, camerashortcuts[i].fPitch));
    }

    fpNearClipDistance = reg.GetFloat(szCameraSettings, "NearClipDistance", fpNearClipDistance);
    fpFarClipDistance = reg.GetFloat(szCameraSettings, "FarClipDistance", fpFarClipDistance);
    // interface and world settings
    fpDefaultWindForce = reg.GetFloat(szGameSpecificSettings, "DefaultWindForce",
                                      fpDefaultWindForce);
    nDefaultTimeScale = reg.GetInt(szGameSpecificSettings, "DefaultTimeScale", nDefaultTimeScale);
    nUseSWCursors = reg.GetInt(szGameSpecificSettings, "UseSWCursors", nUseSWCursors);
    nSelectionType = reg.GetInt(szGameSpecificSettings, "SelectionType", nSelectionType);
    fpCornerSize = reg.GetFloat(szGameSpecificSettings, "CornerSize", fpCornerSize);
    nDrawVisibleOnly = reg.GetInt(szGameSpecificSettings, "DrawVisibleOnly", nDrawVisibleOnly);
    nDrawPath = reg.GetInt(szGameSpecificSettings, "DrawPath", nDrawPath);
    nDrawFlyingHP = reg.GetInt(szGameSpecificSettings, "DrawFlyingHP", nDrawFlyingHP);
    if(!ModRuntime::Resources()||!ModRuntime::Resources()->ForceTutorial())
        nShowTutorial = reg.GetInt(szGameSpecificSettings, "ShowTutorial", nShowTutorial);
    nDoAutosave = reg.GetInt(szGameSpecificSettings, "Autosave", nDoAutosave);
    fToolTipTime = reg.GetFloat(szGameSpecificSettings, "ToolTip Time", fToolTipTime);
    nSwitchFilters = reg.GetInt(szGameSpecificSettings, "SwitchFilters", nSwitchFilters);
    szCD1Label = reg.GetString(szGameSpecificSettings, "CD1 Label", szCD1Label);
    szCD2Label = reg.GetString(szGameSpecificSettings, "CD2 Label", szCD2Label);
    szCD3Label = reg.GetString(szGameSpecificSettings, "CD3 Label", szCD3Label);
    szCharSet = reg.GetString(szGameSpecificSettings, "Font CharSet", szCharSet);
    dwKeyboardLayout = reg.GetDWord(szGameSpecificSettings, "Keyboard Layout", dwKeyboardLayout);
    fObjectsLightingCoeff = reg.GetFloat(szGameSpecificSettings, "ObjectsLightingCoeff",
                                         fObjectsLightingCoeff);

    fHeroLight[0] = reg.GetDWord(szGameSpecificSettings, "Hero Light R", fHeroLight[0] * 255.0f) /
                    255.0f;
    fHeroLight[1] = reg.GetDWord(szGameSpecificSettings, "Hero Light G", fHeroLight[1] * 255.0f) /
                    255.0f;
    fHeroLight[2] = reg.GetDWord(szGameSpecificSettings, "Hero Light B", fHeroLight[2] * 255.0f) /
                    255.0f;
    fHeroLightRadius = reg.GetDWord(szGameSpecificSettings, "Hero Light Radius", fHeroLightRadius);
    fQuestLight[0] = reg.GetDWord(szGameSpecificSettings, "Quest Light R",
                                  fQuestLight[0] * 255.0f) /
                     255.0f;
    fQuestLight[1] = reg.GetDWord(szGameSpecificSettings, "Quest Light G",
                                  fQuestLight[1] * 255.0f) /
                     255.0f;
    fQuestLight[2] = reg.GetDWord(szGameSpecificSettings, "Quest Light B",
                                  fQuestLight[2] * 255.0f) /
                     255.0f;
    fQuestLightRadius = reg.GetDWord(szGameSpecificSettings, "Quest Light Radius",
                                     fQuestLightRadius);
    nDifficultyLevel = nDifficultyLevel2 = reg.GetDWord(szGameSpecificSettings, "Difficulty Level",
                                                        nDifficultyLevel2);
    // some ambiguid settings, which will be removed
    szDefaultMapName = reg.GetString(szGameSpecificSettings, "DefaultMapName", szDefaultMapName);
    szDefaultStreamingSound = reg.GetString(szGameSpecificSettings, "DefaultStreamingSound",
                                            szDefaultStreamingSound);
    fpFogNightStartDistance = reg.GetFloat(szGameSpecificSettings, "FogNightStartDistance",
                                           fpFogNightStartDistance);
    fpFogDayStartDistance = reg.GetFloat(szGameSpecificSettings, "FogDayStartDistance",
                                         fpFogDayStartDistance);
    //
    reg.Close();
}
/////////////////////////////////////////////////////////////////////////////////////
// void SGeneralSettings::WriteSettingsToFile( LPCSTR szSettingsFileName )
// write current setting to .ini file
// requires FULL INI-FILE NAME (with drive too)
void SGeneralSettings::WriteSettingsToFile(LPCSTR szSettingsFileName)
{
    CString szString;
    CIniFile reg;
    //
    reg.Open(szSettingsFileName);
    ////////////////////////////////////////////////////////////////////////////////////
    // COMMON SETTINGS (independent from user)
    // pathes settings
    reg.SetString(szPathSettings, "Work path", szWorkPath);
    reg.SetString(szPathSettings, "Resources path", szResourcePath);
    reg.SetString(szPathSettings, "CD-ROM Path", szCDPath);
    ////////////////////////////////////////////////////////////////////////////////////
    // GENERAL SETTINGS
    // video settings
    reg.SetInt(szGeneralSettings, "DrawingTarget", nDrawingTarget);
    szString.Format("%dx%d", dwWidth, dwHeight);
    reg.SetString(szGeneralSettings, "VideoResolution", szString);
    reg.SetDWord(szGeneralSettings, "VideoColorDepthInBits", dwBPP);
    reg.SetDWord(szGeneralSettings, "VideoZBufferDepthInBits", dwZBufferBPP);
    reg.SetDWord(szGeneralSettings, "VideoRefreshRate", dwRefresh);
    reg.SetInt(szGeneralSettings, "FullScreen", nFullScreen);
    // joystick
    reg.SetInt(szGeneralSettings, "Joystick", 0);
    // Audio settings
    reg.SetInt(szGeneralSettings, "Video", nVideo);
    reg.SetInt(szGeneralSettings, "Audio", nAudio);
    reg.SetInt(szGeneralSettings, "AudioStreaming", nAudioStreaming);
    reg.SetInt(szGeneralSettings, "AudioSFX", nAudioSFX);
    reg.SetInt(szGeneralSettings, "AudioSpeech", nAudioSpeech);
    // quality settings
    reg.SetInt(szGeneralSettings, "Antialiasing", nAntialiasing);
    reg.SetInt(szGeneralSettings, "Dithering", nDithering);
    reg.SetInt(szGeneralSettings, "Specular", nSpecular);
    reg.SetInt(szGeneralSettings, "TextureQuality", nTextureQuality);
    reg.SetInt(szGeneralSettings, "Mipmapping", nMipmapping);
    reg.SetInt(szGeneralSettings, "Filtering", nFiltering);
    reg.SetInt(szGeneralSettings, "PerspectiveCorrection", nPerspectiveCorrection);
    reg.SetInt(szGeneralSettings, "ShowParticles", nShowParticles);
    reg.SetInt(szGeneralSettings, "FPSIndependentCursor", nFPSIndependentCursor);
    reg.SetInt(szGeneralSettings, "AutomaticQuantization", nAutomaticQuantization);
    reg.SetInt(szGeneralSettings, "MovieCapturingFPS", nMovieCapturingFPS);
    // gamma ramp settings
    reg.SetFloat(szGeneralSettings, "Brightness", fBrightness);
    reg.SetFloat(szGeneralSettings, "Contrast", fContrast);
    reg.SetFloat(szGeneralSettings, "Gamma", fGamma);
    // sound
    reg.SetInt(szGeneralSettings, "Reverse Stereo", nReverseStereo);
    // shadow settings
    reg.SetInt(szShadowSettings, "ShadowQuality", nShadowQuality);
    reg.SetInt(szShadowSettings, "HeroShadow", nHeroShadow);
    reg.SetInt(szShadowSettings, "UnitShadow", nUnitShadow);
    reg.SetInt(szShadowSettings, "BuildingShadow", nBuildingShadow);
    reg.SetInt(szShadowSettings, "FloraShadow", nFloraShadow);
    reg.SetInt(szShadowSettings, "OtherShadow", nOtherShadow);
    reg.SetInt(szShadowSettings, "LightingFreq", nLightingFreq);
    reg.SetInt(szShadowSettings, "FloraSpecialLight", nFloraSpecialLight);
    // terrain LOD distances
    reg.SetFloat(szTerrainSettings, "LOD1", fpTerrainLODs[0]);
    reg.SetFloat(szTerrainSettings, "LOD2", fpTerrainLODs[1]);
    reg.SetFloat(szTerrainSettings, "BorderFogDistance", fpTerrainBorderFogDistance);
    reg.SetInt(szTerrainSettings, "EnableWaterWaves", nTerrainEnableWaterWaves);
    reg.SetInt(szTerrainSettings, "EnableDynamicLights", nTerrainEnableDynamicLights);
    reg.SetInt(szTerrainSettings, "EnableSelfShadowing", nTerrainEnableSelfShadowing);
    reg.SetInt(szTerrainSettings, "LightingFreq", nTerrainLightingFreq);
    reg.SetInt(szTerrainSettings, "ShadowingFreq", nTerrainShadowingFreq);
    reg.SetInt(szTerrainSettings, "EnableFootprints", nTerrainEnableFootprints);
    reg.SetInt(szTerrainSettings, "EnableBloodprints", nTerrainEnableBloodprints);
    reg.SetInt(szTerrainSettings, "EnableFireprints", nTerrainEnableFireprints);
    reg.SetInt(szTerrainSettings, "EnableBloodprintsRelight", nTerrainEnableBloodprintsRelight);
    ////////////////////////////////////////////////////////////////////////////////////
    // NON-GENERAL (GAME SPECIFIC) SETTINGS
    // output info
    reg.SetInt(szStatisticsOutputSettings, "CreateOutputInfo", nCreateOutputFile);
    reg.SetInt(szStatisticsOutputSettings, "NumberOfFrames", nNumberOfFrames);
    // camera settings
    reg.SetInt(szCameraSettings, "CameraFlipXAxis", fCameraXAxisDir == -1);
    reg.SetInt(szCameraSettings, "CameraFlipYAxis", fCameraYAxisDir == -1);
    reg.SetFloat(szCameraSettings, "CameraMinHeight", fpCameraMinHeight);
    reg.SetFloat(szCameraSettings, "CameraMaxHeight", fpCameraMaxHeight);
    reg.SetFloat(szCameraSettings, "CameraDefaultHeight", fpCameraDefaultHeight);
    reg.SetFloat(szCameraSettings, "CameraDefaultXDeploy", fpCameraDefaultXDeploy);
    reg.SetFloat(szCameraSettings, "CameraDefaultYDeploy", fpCameraDefaultYDeploy);
    reg.SetFloat(szCameraSettings, "CameraMinDistanceToCarrier", fpCameraMinDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraMaxDistanceToCarrier", fpCameraMaxDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraMinLimitDistanceToCarrier",
                 fpCameraMinLimitDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraMaxLimitDistanceToCarrier",
                 fpCameraMaxLimitDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraDefaultDistanceToCarrier",
                 fpCameraDefaultDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "NearClipDistance", fpNearClipDistance);
    reg.SetFloat(szCameraSettings, "FarClipDistance", fpFarClipDistance);
    reg.SetFloat(szCameraSettings, "CameraDefaultMinPitch", fpCameraDefaultMinPitch);
    reg.SetFloat(szCameraSettings, "CameraDefaultMaxPitch", fpCameraDefaultMaxPitch);

    reg.SetFloat(szCameraSettings, "CameraBriefingMinDistanceToCarrier",
                 fpCameraBriefingMinDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraBriefingMaxDistanceToCarrier",
                 fpCameraBriefingMaxDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraBriefingMinLimitDistanceToCarrier",
                 fpCameraBriefingMinLimitDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraBriefingMaxLimitDistanceToCarrier",
                 fpCameraBriefingMaxLimitDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraBriefingMinPitch", fpCameraBriefingMinPitch);
    reg.SetFloat(szCameraSettings, "CameraBriefingMaxPitch", fpCameraBriefingMaxPitch);

    reg.SetInt(szCameraSettings, "CameraBorderScrollArea", nCameraBorderScrollArea);
    reg.SetInt(szCameraSettings, "CameraFrameSelectionSensetiveArea",
               nCameraFrameSelectionSensetiveArea);
    reg.SetFloat(szCameraSettings, "CameraTerrainSensetiveArea", fCameraTerrainSensetiveArea);

    reg.SetFloat(szCameraSettings, "CameraDefaultScrollSpeed", cctCameraScroll.fpValue);
    reg.SetFloat(szCameraSettings, "CameraDefaultScrollPowerMouse", cctCameraScroll.fpMousePower);
    reg.SetFloat(szCameraSettings, "CameraDefaultScrollPowerKeyboard",
                 cctCameraScroll.fpKeyboardPower);

    reg.SetFloat(szCameraSettings, "CameraDefaultRotateSpeed", cctCameraRotate.fpValue);
    reg.SetFloat(szCameraSettings, "CameraDefaultRotatePowerMouse", cctCameraRotate.fpMousePower);
    reg.SetFloat(szCameraSettings, "CameraDefaultRotatePowerKeyboard",
                 cctCameraRotate.fpKeyboardPower);

    reg.SetFloat(szCameraSettings, "CameraDefaultPitchSpeed", cctCameraPitch.fpValue);
    reg.SetFloat(szCameraSettings, "CameraDefaultPitchPowerMouse", cctCameraPitch.fpMousePower);
    reg.SetFloat(szCameraSettings, "CameraDefaultPitchPowerKeyboard",
                 cctCameraPitch.fpKeyboardPower);

    reg.SetFloat(szCameraSettings, "CameraDefaultZoomSpeed", cctCameraZoom.fpValue);
    reg.SetFloat(szCameraSettings, "CameraDefaultZoomPowerMouse", cctCameraZoom.fpMousePower);
    reg.SetFloat(szCameraSettings, "CameraDefaultZoomPowerKeyboard", cctCameraZoom.fpKeyboardPower);

    for (int i = 0; i < SETTING_NUM_CAMERA_SHORTCUTS; ++i)
    {
        char buff[1024];
        sprintf(buff, "CameraShortcut %d AxisX", i);
        reg.SetFloat(szCameraSettings, buff, camerashortcuts[i].fAxisX);
        sprintf(buff, "CameraShortcut %d AxisY", i);
        reg.SetFloat(szCameraSettings, buff, camerashortcuts[i].fAxisY);
        sprintf(buff, "CameraShortcut %d AxisZ", i);
        reg.SetFloat(szCameraSettings, buff, camerashortcuts[i].fAxisZ);
        sprintf(buff, "CameraShortcut %d Angle", i);
        reg.SetFloat(szCameraSettings, buff, camerashortcuts[i].fAngle);
        sprintf(buff, "CameraShortcut %d Distance", i);
        reg.SetFloat(szCameraSettings, buff, camerashortcuts[i].fDistance);
        sprintf(buff, "CameraShortcut %d Pitch", i);
        reg.SetFloat(szCameraSettings, buff, camerashortcuts[i].fPitch);
    }
    // interface and world settings
    reg.SetFloat(szGameSpecificSettings, "DefaultWindForce", fpDefaultWindForce);
    reg.SetInt(szGameSpecificSettings, "DefaultTimeScale", nDefaultTimeScale);
    reg.SetInt(szGameSpecificSettings, "UseSWCursors", nUseSWCursors);
    reg.SetInt(szGameSpecificSettings, "SelectionType", nSelectionType);
    reg.SetFloat(szGameSpecificSettings, "CornerSize", fpCornerSize);
    reg.SetInt(szGameSpecificSettings, "DrawVisibleOnly", nDrawVisibleOnly);
    reg.SetInt(szGameSpecificSettings, "DrawPath", nDrawPath);
    reg.SetInt(szGameSpecificSettings, "DrawFlyingHP", nDrawFlyingHP);
    reg.SetInt(szGameSpecificSettings, "ShowTutorial", nShowTutorial);
    reg.SetInt(szGameSpecificSettings, "Autosave", nDoAutosave);
    reg.SetFloat(szGameSpecificSettings, "ToolTip Time", fToolTipTime);
    reg.SetInt(szGameSpecificSettings, "SwitchFilters", nSwitchFilters);
    reg.SetString(szGameSpecificSettings, "CD1 Label", szCD1Label);
    reg.SetString(szGameSpecificSettings, "CD2 Label", szCD2Label);
    reg.SetString(szGameSpecificSettings, "CD3 Label", szCD3Label);
    reg.SetString(szGameSpecificSettings, "Font CharSet", szCharSet);
    reg.SetDWord(szGameSpecificSettings, "Keyboard Layout", dwKeyboardLayout);
    reg.SetFloat(szGameSpecificSettings, "ObjectsLightingCoeff", fObjectsLightingCoeff);

    reg.SetDWord(szGameSpecificSettings, "Hero Light R", fHeroLight[0] * 255.0f);
    reg.SetDWord(szGameSpecificSettings, "Hero Light G", fHeroLight[1] * 255.0f);
    reg.SetDWord(szGameSpecificSettings, "Hero Light B", fHeroLight[2] * 255.0f);
    reg.SetDWord(szGameSpecificSettings, "Hero Light Radius", fHeroLightRadius);
    reg.SetDWord(szGameSpecificSettings, "Quest Light R", fQuestLight[0] * 255.0f);
    reg.SetDWord(szGameSpecificSettings, "Quest Light G", fQuestLight[1] * 255.0f);
    reg.SetDWord(szGameSpecificSettings, "Quest Light B", fQuestLight[2] * 255.0f);
    reg.SetDWord(szGameSpecificSettings, "Quest Light Radius", fQuestLightRadius);
    reg.SetDWord(szGameSpecificSettings, "Difficulty Level", nDifficultyLevel2);
    // some ambiguid settings, which will be removed
    reg.SetString(szGameSpecificSettings, "DefaultMapName", szDefaultMapName);
    reg.SetString(szGameSpecificSettings, "DefaultStreamingSound", szDefaultStreamingSound);
    reg.SetFloat(szGameSpecificSettings, "FogNightStartDistance", fpFogNightStartDistance);
    reg.SetFloat(szGameSpecificSettings, "FogDayStartDistance", fpFogDayStartDistance);
    //
    reg.Close();
}
/////////////////////////////////////////////////////////////////////////////////////
// void SGeneralSettings::ReadSettingsFromRegistry( LPCSTR pszRegistryKey )
// read settings from windows registry
void SGeneralSettings::ReadSettingsFromRegistry(LPCSTR pszRegistryKey)
{
    CString szString;
    CWinRegistry reg;
    int pos;
    //
    reg.Open(pszRegistryKey, HKEY_CURRENT_USER);
    ////////////////////////////////////////////////////////////////////////////////////
    // COMMON SETTINGS (independent from user).
    // pathes settings
    // EIStarter starts in Starter/Engine, while loose game data lives in the
    // installation root. Keep every explicitly configured path unchanged.
    const auto gameRoot = InstalledGameRoot();
    const bool portableLayout = gameRoot.filename() == "Base";
    CString configuredWork = reg.GetString(szPathSettings, "Work path", "");
    CString configuredResources = reg.GetString(szPathSettings, "Resources path", "");
    CString configuredCD = reg.GetString(szPathSettings, "CD-ROM Path", "");
    szWorkPath = portableLayout ? DirectoryWithSlash(gameRoot) : !configuredWork.IsEmpty() ? configuredWork
        : !gameRoot.empty() ? DirectoryWithSlash(gameRoot) : szWorkPath;
    szResourcePath = portableLayout ? DirectoryWithSlash(gameRoot / "Res") : !configuredResources.IsEmpty() ? configuredResources
        : !gameRoot.empty() ? DirectoryWithSlash(gameRoot / "Res") : szResourcePath;
    szCDPath = portableLayout ? DirectoryWithSlash(gameRoot) : !configuredCD.IsEmpty() ? configuredCD
        : !gameRoot.empty() ? DirectoryWithSlash(gameRoot) : szCDPath;
    //
    ////////////////////////////////////////////////////////////////////////////////////
    // GENERAL SETTINGS
    // video settings
    nDrawingTarget = reg.GetInt(szGeneralSettings, "DrawingTarget", nDrawingTarget);
    szString = reg.GetString(szGeneralSettings, "VideoResolution", szString);
    pos = szString.Find('x');
    if (pos != -1)
    {
        dwWidth = atoi(szString.Left(pos));
        dwHeight = atoi(szString.Right(szString.GetLength() - pos - 1));
    }
    dwBPP = reg.GetDWord(szGeneralSettings, "VideoColorDepthInBits", dwBPP);
    dwZBufferBPP = reg.GetDWord(szGeneralSettings, "VideoZBufferDepthInBits", dwZBufferBPP);
    dwRefresh = reg.GetDWord(szGeneralSettings, "VideoRefreshRate", dwRefresh);
    nFullScreen = reg.GetInt(szGeneralSettings, "FullScreen", nFullScreen);
    // joystick
    nJoystick = reg.GetInt(szGeneralSettings, "Joystick", 0);
    // Audio settings
    nVideo = reg.GetInt(szGeneralSettings, "Video", nVideo);
    nAudio = reg.GetInt(szGeneralSettings, "Audio", nAudio);
    nAudioStreaming = reg.GetInt(szGeneralSettings, "AudioStreaming", nAudioStreaming);
    nAudioSFX = reg.GetInt(szGeneralSettings, "AudioSFX", nAudioSFX);
    nAudioSpeech = reg.GetInt(szGeneralSettings, "AudioSpeech", nAudioSpeech);
    // quality settings
    nAntialiasing = reg.GetInt(szGeneralSettings, "Antialiasing", nAntialiasing);
    nDithering = reg.GetInt(szGeneralSettings, "Dithering", nDithering);
    nSpecular = reg.GetInt(szGeneralSettings, "Specular", nSpecular);
    nTextureQuality = reg.GetInt(szGeneralSettings, "TextureQuality", nTextureQuality);
    nMipmapping = reg.GetInt(szGeneralSettings, "Mipmapping", nMipmapping);
    nFiltering = reg.GetInt(szGeneralSettings, "Filtering", nFiltering);
    nPerspectiveCorrection = reg.GetInt(szGeneralSettings, "PerspectiveCorrection",
                                        nPerspectiveCorrection);
    nShowParticles = reg.GetInt(szGeneralSettings, "ShowParticles", nShowParticles);
    nFPSIndependentCursor = reg.GetInt(szGeneralSettings, "FPSIndependentCursor",
                                       nFPSIndependentCursor);
    nAutomaticQuantization = reg.GetInt(szGeneralSettings, "AutomaticQuantization",
                                        nAutomaticQuantization);
    nMovieCapturingFPS = reg.GetInt(szGeneralSettings, "MovieCapturingFPS", nMovieCapturingFPS);
    // gamma ramp settings
    fBrightness = reg.GetFloat(szGeneralSettings, "Brightness", fBrightness);
    fContrast = reg.GetFloat(szGeneralSettings, "Contrast", fContrast);
    fGamma = reg.GetFloat(szGeneralSettings, "Gamma", fGamma);
    // sound
    nReverseStereo = reg.GetInt(szGeneralSettings, "Reverse Stereo", nReverseStereo);
    // shadow settings
    nShadowQuality = reg.GetInt(szShadowSettings, "ShadowQuality", nShadowQuality);
    nHeroShadow = reg.GetInt(szShadowSettings, "HeroShadow", nHeroShadow);
    nUnitShadow = reg.GetInt(szShadowSettings, "UnitShadow", nUnitShadow);
    nBuildingShadow = reg.GetInt(szShadowSettings, "BuildingShadow", nBuildingShadow);
    nFloraShadow = reg.GetInt(szShadowSettings, "FloraShadow", nFloraShadow);
    nOtherShadow = reg.GetInt(szShadowSettings, "OtherShadow", nOtherShadow);
    nLightingFreq = reg.GetInt(szShadowSettings, "LightingFreq", nLightingFreq);
    nFloraSpecialLight = reg.GetInt(szShadowSettings, "FloraSpecialLight", nFloraSpecialLight);
    // terrain LOD distances
    fpTerrainLODs[0] = reg.GetFloat(szTerrainSettings, "LOD1", fpTerrainLODs[0]);
    fpTerrainLODs[1] = reg.GetFloat(szTerrainSettings, "LOD2", fpTerrainLODs[1]);
    fpTerrainBorderFogDistance = reg.GetFloat(szTerrainSettings, "BorderFogDistance",
                                              fpTerrainBorderFogDistance);
    nTerrainEnableWaterWaves = reg.GetInt(szTerrainSettings, "EnableWaterWaves",
                                          nTerrainEnableWaterWaves);
    nTerrainEnableDynamicLights = reg.GetInt(szTerrainSettings, "EnableDynamicLights",
                                             nTerrainEnableDynamicLights);
    nTerrainEnableSelfShadowing = reg.GetInt(szTerrainSettings, "EnableSelfShadowing",
                                             nTerrainEnableSelfShadowing);
    nTerrainLightingFreq = reg.GetInt(szTerrainSettings, "LightingFreq", nTerrainLightingFreq);
    nTerrainShadowingFreq = reg.GetInt(szTerrainSettings, "ShadowingFreq", nTerrainShadowingFreq);
    nTerrainEnableFootprints = reg.GetInt(szTerrainSettings, "EnableFootprints",
                                          nTerrainEnableFootprints);
    nTerrainEnableBloodprints = reg.GetInt(szTerrainSettings, "EnableBloodprints",
                                           nTerrainEnableBloodprints);
    nTerrainEnableFireprints = reg.GetInt(szTerrainSettings, "EnableFireprints",
                                          nTerrainEnableFireprints);
    nTerrainEnableBloodprintsRelight = reg.GetInt(szTerrainSettings, "EnableBloodprintsRelight",
                                                  nTerrainEnableBloodprintsRelight);
    ////////////////////////////////////////////////////////////////////////////////////
    // NON-GENERAL (GAME SPECIFIC) SETTINGS
    // output info
    nCreateOutputFile = reg.GetInt(szStatisticsOutputSettings, "CreateOutputInfo",
                                   nCreateOutputFile);
    nNumberOfFrames = reg.GetInt(szStatisticsOutputSettings, "NumberOfFrames", nNumberOfFrames);
    // camera settings
    fCameraXAxisDir = reg.GetInt(szCameraSettings, "CameraFlipXAxis", fCameraXAxisDir == -1) ? -1
                                                                                             : 1;
    fCameraYAxisDir = reg.GetInt(szCameraSettings, "CameraFlipYAxis", fCameraYAxisDir == -1) ? -1
                                                                                             : 1;
    fpCameraMinHeight = reg.GetFloat(szCameraSettings, "CameraMinHeight", fpCameraMinHeight);
    fpCameraMaxHeight = reg.GetFloat(szCameraSettings, "CameraMaxHeight", fpCameraMaxHeight);
    fpCameraDefaultHeight = reg.GetFloat(szCameraSettings, "CameraDefaultHeight",
                                         fpCameraDefaultHeight);
    fpCameraDefaultXDeploy = reg.GetFloat(szCameraSettings, "CameraDefaultXDeploy",
                                          fpCameraDefaultXDeploy);
    fpCameraDefaultYDeploy = reg.GetFloat(szCameraSettings, "CameraDefaultYDeploy",
                                          fpCameraDefaultYDeploy);
    fpCameraMinDistanceToCarrier = reg.GetFloat(szCameraSettings, "CameraMinDistanceToCarrier",
                                                fpCameraMinDistanceToCarrier);
    fpCameraMaxDistanceToCarrier = reg.GetFloat(szCameraSettings, "CameraMaxDistanceToCarrier",
                                                fpCameraMaxDistanceToCarrier);
    fpCameraMinLimitDistanceToCarrier = reg.GetFloat(szCameraSettings,
                                                     "CameraMinLimitDistanceToCarrier",
                                                     fpCameraMinLimitDistanceToCarrier);
    fpCameraMaxLimitDistanceToCarrier = reg.GetFloat(szCameraSettings,
                                                     "CameraMaxLimitDistanceToCarrier",
                                                     fpCameraMaxLimitDistanceToCarrier);
    fpCameraDefaultDistanceToCarrier = reg.GetFloat(szCameraSettings,
                                                    "CameraDefaultDistanceToCarrier",
                                                    fpCameraDefaultDistanceToCarrier);
    fpNearClipDistance = reg.GetFloat(szCameraSettings, "NearClipDistance", fpNearClipDistance);
    fpFarClipDistance = reg.GetFloat(szCameraSettings, "FarClipDistance", fpFarClipDistance);
    fpCameraDefaultMinPitch = reg.GetFloat(szCameraSettings, "CameraDefaultMinPitch",
                                           fpCameraDefaultMinPitch);
    fpCameraDefaultMaxPitch = reg.GetFloat(szCameraSettings, "CameraDefaultMaxPitch",
                                           fpCameraDefaultMaxPitch);

    fpCameraBriefingMinDistanceToCarrier = reg.GetFloat(szCameraSettings,
                                                        "CameraBriefingMinDistanceToCarrier",
                                                        fpCameraBriefingMinDistanceToCarrier);
    fpCameraBriefingMaxDistanceToCarrier = reg.GetFloat(szCameraSettings,
                                                        "CameraBriefingMaxDistanceToCarrier",
                                                        fpCameraBriefingMaxDistanceToCarrier);
    fpCameraBriefingMinLimitDistanceToCarrier = reg.GetFloat(
        szCameraSettings, "CameraBriefingMinLimitDistanceToCarrier",
        fpCameraBriefingMinLimitDistanceToCarrier);
    fpCameraBriefingMaxLimitDistanceToCarrier = reg.GetFloat(
        szCameraSettings, "CameraBriefingMaxLimitDistanceToCarrier",
        fpCameraBriefingMaxLimitDistanceToCarrier);
    fpCameraBriefingMinPitch = reg.GetFloat(szCameraSettings, "CameraBriefingMinPitch",
                                            fpCameraBriefingMinPitch);
    fpCameraBriefingMaxPitch = reg.GetFloat(szCameraSettings, "CameraBriefingMaxPitch",
                                            fpCameraBriefingMaxPitch);

    nCameraBorderScrollArea = reg.GetInt(szCameraSettings, "CameraBorderScrollArea",
                                         nCameraBorderScrollArea);
    nCameraFrameSelectionSensetiveArea = reg.GetInt(szCameraSettings,
                                                    "CameraFrameSelectionSensetiveArea",
                                                    nCameraFrameSelectionSensetiveArea);
    fCameraTerrainSensetiveArea = reg.GetFloat(szCameraSettings, "CameraTerrainSensetiveArea",
                                               fCameraTerrainSensetiveArea);

    cctCameraScroll.fpValue = reg.GetFloat(szCameraSettings, "CameraDefaultScrollSpeed",
                                           cctCameraScroll.fpValue);
    cctCameraScroll.fpMousePower = reg.GetFloat(szCameraSettings, "CameraDefaultScrollPowerMouse",
                                                cctCameraScroll.fpMousePower);
    cctCameraScroll.fpKeyboardPower = reg.GetFloat(szCameraSettings,
                                                   "CameraDefaultScrollPowerKeyboard",
                                                   cctCameraScroll.fpKeyboardPower);

    cctCameraRotate.fpValue = reg.GetFloat(szCameraSettings, "CameraDefaultRotateSpeed",
                                           cctCameraRotate.fpValue);
    cctCameraRotate.fpMousePower = reg.GetFloat(szCameraSettings, "CameraDefaultRotatePowerMouse",
                                                cctCameraRotate.fpMousePower);
    cctCameraRotate.fpKeyboardPower = reg.GetFloat(szCameraSettings,
                                                   "CameraDefaultRotatePowerKeyboard",
                                                   cctCameraRotate.fpKeyboardPower);

    cctCameraPitch.fpValue = reg.GetFloat(szCameraSettings, "CameraDefaultPitchSpeed",
                                          cctCameraPitch.fpValue);
    cctCameraPitch.fpMousePower = reg.GetFloat(szCameraSettings, "CameraDefaultPitchPowerMouse",
                                               cctCameraPitch.fpMousePower);
    cctCameraPitch.fpKeyboardPower = reg.GetFloat(szCameraSettings,
                                                  "CameraDefaultPitchPowerKeyboard",
                                                  cctCameraPitch.fpKeyboardPower);

    cctCameraZoom.fpValue = reg.GetFloat(szCameraSettings, "CameraDefaultZoomSpeed",
                                         cctCameraZoom.fpValue);
    cctCameraZoom.fpMousePower = reg.GetFloat(szCameraSettings, "CameraDefaultZoomPowerMouse",
                                              cctCameraZoom.fpMousePower);
    cctCameraZoom.fpKeyboardPower = reg.GetFloat(szCameraSettings, "CameraDefaultZoomPowerKeyboard",
                                                 cctCameraZoom.fpKeyboardPower);

    for (int i = 0; i < SETTING_NUM_CAMERA_SHORTCUTS; ++i)
    {
        char buff[1024];
        sprintf(buff, "CameraShortcut %d AxisX", i);
        camerashortcuts[i].fAxisX = reg.GetFloat(szCameraSettings, buff, camerashortcuts[i].fAxisX);
        sprintf(buff, "CameraShortcut %d AxisY", i);
        camerashortcuts[i].fAxisY = reg.GetFloat(szCameraSettings, buff, camerashortcuts[i].fAxisY);
        sprintf(buff, "CameraShortcut %d AxisZ", i);
        camerashortcuts[i].fAxisZ = reg.GetFloat(szCameraSettings, buff, camerashortcuts[i].fAxisZ);
        sprintf(buff, "CameraShortcut %d Angle", i);
        camerashortcuts[i].fAngle = reg.GetFloat(szCameraSettings, buff, camerashortcuts[i].fAngle);
        sprintf(buff, "CameraShortcut %d Distance", i);
        camerashortcuts[i].fDistance = reg.GetFloat(szCameraSettings, buff,
                                                    camerashortcuts[i].fDistance);
        sprintf(buff, "CameraShortcut %d Pitch", i);
        camerashortcuts[i].fPitch = ImportedShortcutPitch(reg.GetFloat(szCameraSettings, buff, camerashortcuts[i].fPitch));
    }
    // interface and world settings
    fpDefaultWindForce = reg.GetFloat(szGameSpecificSettings, "DefaultWindForce",
                                      fpDefaultWindForce);
    nDefaultTimeScale = reg.GetInt(szGameSpecificSettings, "DefaultTimeScale", nDefaultTimeScale);
    nUseSWCursors = reg.GetInt(szGameSpecificSettings, "UseSWCursors", nUseSWCursors);
    nSelectionType = reg.GetInt(szGameSpecificSettings, "SelectionType", nSelectionType);
    fpCornerSize = reg.GetFloat(szGameSpecificSettings, "CornerSize", fpCornerSize);
    nDrawVisibleOnly = reg.GetInt(szGameSpecificSettings, "DrawVisibleOnly", nDrawVisibleOnly);
    nDrawPath = reg.GetInt(szGameSpecificSettings, "DrawPath", nDrawPath);
    nDrawFlyingHP = reg.GetInt(szGameSpecificSettings, "DrawFlyingHP", nDrawFlyingHP);
    nShowTutorial = reg.GetInt(szGameSpecificSettings, "ShowTutorial", nShowTutorial);
    nDoAutosave = reg.GetInt(szGameSpecificSettings, "Autosave", nDoAutosave);
    fToolTipTime = reg.GetFloat(szGameSpecificSettings, "ToolTip Time", fToolTipTime);
    nSwitchFilters = reg.GetInt(szGameSpecificSettings, "SwitchFilters", nSwitchFilters);
    szCD1Label = reg.GetString(szGameSpecificSettings, "CD1 Label", szCD1Label);
    szCD2Label = reg.GetString(szGameSpecificSettings, "CD2 Label", szCD2Label);
    szCD3Label = reg.GetString(szGameSpecificSettings, "CD3 Label", szCD3Label);
    szCharSet = reg.GetString(szGameSpecificSettings, "Font CharSet", szCharSet);
    dwKeyboardLayout = reg.GetDWord(szGameSpecificSettings, "Keyboard Layout", dwKeyboardLayout);
    fObjectsLightingCoeff = reg.GetFloat(szGameSpecificSettings, "ObjectsLightingCoeff",
                                         fObjectsLightingCoeff);

    fHeroLight[0] = reg.GetDWord(szGameSpecificSettings, "Hero Light R", fHeroLight[0] * 255.0f) /
                    255.0f;
    fHeroLight[1] = reg.GetDWord(szGameSpecificSettings, "Hero Light G", fHeroLight[1] * 255.0f) /
                    255.0f;
    fHeroLight[2] = reg.GetDWord(szGameSpecificSettings, "Hero Light B", fHeroLight[2] * 255.0f) /
                    255.0f;
    fHeroLightRadius = reg.GetDWord(szGameSpecificSettings, "Hero Light Radius", fHeroLightRadius);
    fQuestLight[0] = reg.GetDWord(szGameSpecificSettings, "Quest Light R",
                                  fQuestLight[0] * 255.0f) /
                     255.0f;
    fQuestLight[1] = reg.GetDWord(szGameSpecificSettings, "Quest Light G",
                                  fQuestLight[1] * 255.0f) /
                     255.0f;
    fQuestLight[2] = reg.GetDWord(szGameSpecificSettings, "Quest Light B",
                                  fQuestLight[2] * 255.0f) /
                     255.0f;
    fQuestLightRadius = reg.GetDWord(szGameSpecificSettings, "Quest Light Radius",
                                     fQuestLightRadius);
    nDifficultyLevel = nDifficultyLevel2 = reg.GetDWord(szGameSpecificSettings, "Difficulty Level",
                                                        nDifficultyLevel2);
    // some ambiguid settings, which will be removed
    szDefaultMapName = reg.GetString(szGameSpecificSettings, "DefaultMapName", szDefaultMapName);
    szDefaultStreamingSound = reg.GetString(szGameSpecificSettings, "DefaultStreamingSound",
                                            szDefaultStreamingSound);
    fpFogNightStartDistance = reg.GetFloat(szGameSpecificSettings, "FogNightStartDistance",
                                           fpFogNightStartDistance);
    fpFogDayStartDistance = reg.GetFloat(szGameSpecificSettings, "FogDayStartDistance",
                                         fpFogDayStartDistance);
    //
    reg.Close();
}
/////////////////////////////////////////////////////////////////////////////////////
// void SGeneralSettings::WriteSettingsToRegistry( LPCSTR pszRegistryKey )
// write settings to windows registry
void SGeneralSettings::WriteSettingsToRegistry(LPCSTR pszRegistryKey)
{
    CString szString;
    CWinRegistry reg;
    //
    reg.Open(pszRegistryKey, HKEY_CURRENT_USER);
    ////////////////////////////////////////////////////////////////////////////////////
    // COMMON SETTINGS (independent from user)
    // pathes settings
    reg.SetString(szPathSettings, "Work path", szWorkPath);
    reg.SetString(szPathSettings, "Resources path", szResourcePath);
    reg.SetString(szPathSettings, "CD-ROM Path", szCDPath);
    //
    ////////////////////////////////////////////////////////////////////////////////////
    // GENERAL SETTINGS
    // video settings
    reg.SetInt(szGeneralSettings, "DrawingTarget", nDrawingTarget);
    szString.Format("%dx%d", dwWidth, dwHeight);
    reg.SetString(szGeneralSettings, "VideoResolution", szString);
    reg.SetDWord(szGeneralSettings, "VideoColorDepthInBits", dwBPP);
    reg.SetDWord(szGeneralSettings, "VideoZBufferDepthInBits", dwZBufferBPP);
    reg.SetDWord(szGeneralSettings, "VideoRefreshRate", dwRefresh);
    reg.SetInt(szGeneralSettings, "FullScreen", nFullScreen);
    // joystick
    reg.SetInt(szGeneralSettings, "Joystick", 0);
    // Audio settings
    reg.SetInt(szGeneralSettings, "Video", nVideo);
    reg.SetInt(szGeneralSettings, "Audio", nAudio);
    reg.SetInt(szGeneralSettings, "AudioStreaming", nAudioStreaming);
    reg.SetInt(szGeneralSettings, "AudioSFX", nAudioSFX);
    reg.SetInt(szGeneralSettings, "AudioSpeech", nAudioSpeech);
    // quality settings
    reg.SetInt(szGeneralSettings, "Antialiasing", nAntialiasing);
    reg.SetInt(szGeneralSettings, "Dithering", nDithering);
    reg.SetInt(szGeneralSettings, "Specular", nSpecular);
    reg.SetInt(szGeneralSettings, "TextureQuality", nTextureQuality);
    reg.SetInt(szGeneralSettings, "Mipmapping", nMipmapping);
    reg.SetInt(szGeneralSettings, "Filtering", nFiltering);
    reg.SetInt(szGeneralSettings, "PerspectiveCorrection", nPerspectiveCorrection);
    reg.SetInt(szGeneralSettings, "ShowParticles", nShowParticles);
    reg.SetInt(szGeneralSettings, "FPSIndependentCursor", nFPSIndependentCursor);
    reg.SetInt(szGeneralSettings, "AutomaticQuantization", nAutomaticQuantization);
    reg.SetInt(szGeneralSettings, "MovieCapturingFPS", nMovieCapturingFPS);
    // gamma ramp settings
    reg.SetFloat(szGeneralSettings, "Brightness", fBrightness);
    reg.SetFloat(szGeneralSettings, "Contrast", fContrast);
    reg.SetFloat(szGeneralSettings, "Gamma", fGamma);
    // sound
    reg.SetInt(szGeneralSettings, "Reverse Stereo", nReverseStereo);
    // shadow settings
    reg.SetInt(szShadowSettings, "ShadowQuality", nShadowQuality);
    reg.SetInt(szShadowSettings, "HeroShadow", nHeroShadow);
    reg.SetInt(szShadowSettings, "UnitShadow", nUnitShadow);
    reg.SetInt(szShadowSettings, "BuildingShadow", nBuildingShadow);
    reg.SetInt(szShadowSettings, "FloraShadow", nFloraShadow);
    reg.SetInt(szShadowSettings, "OtherShadow", nOtherShadow);
    reg.SetInt(szShadowSettings, "LightingFreq", nLightingFreq);
    reg.SetInt(szShadowSettings, "FloraSpecialLight", nFloraSpecialLight);
    // terrain LOD distances
    reg.SetFloat(szTerrainSettings, "LOD1", fpTerrainLODs[0]);
    reg.SetFloat(szTerrainSettings, "LOD2", fpTerrainLODs[1]);
    reg.SetFloat(szTerrainSettings, "BorderFogDistance", fpTerrainBorderFogDistance);
    reg.SetInt(szTerrainSettings, "EnableWaterWaves", nTerrainEnableWaterWaves);
    reg.SetInt(szTerrainSettings, "EnableDynamicLights", nTerrainEnableDynamicLights);
    reg.SetInt(szTerrainSettings, "EnableSelfShadowing", nTerrainEnableSelfShadowing);
    reg.SetInt(szTerrainSettings, "LightingFreq", nTerrainLightingFreq);
    reg.SetInt(szTerrainSettings, "ShadowingFreq", nTerrainShadowingFreq);
    reg.SetInt(szTerrainSettings, "EnableFootprints", nTerrainEnableFootprints);
    reg.SetInt(szTerrainSettings, "EnableBloodprints", nTerrainEnableBloodprints);
    reg.SetInt(szTerrainSettings, "EnableFireprints", nTerrainEnableFireprints);
    reg.SetInt(szTerrainSettings, "EnableBloodprintsRelight", nTerrainEnableBloodprintsRelight);
    ////////////////////////////////////////////////////////////////////////////////////
    // NON-GENERAL (GAME SPECIFIC) SETTINGS
    // output info
    reg.SetInt(szStatisticsOutputSettings, "CreateOutputInfo", nCreateOutputFile);
    reg.SetInt(szStatisticsOutputSettings, "NumberOfFrames", nNumberOfFrames);
    // camera settings
    reg.SetInt(szCameraSettings, "CameraFlipXAxis", fCameraXAxisDir == -1);
    reg.SetInt(szCameraSettings, "CameraFlipYAxis", fCameraYAxisDir == -1);
    reg.SetFloat(szCameraSettings, "CameraMinHeight", fpCameraMinHeight);
    reg.SetFloat(szCameraSettings, "CameraMaxHeight", fpCameraMaxHeight);
    reg.SetFloat(szCameraSettings, "CameraDefaultHeight", fpCameraDefaultHeight);
    reg.SetFloat(szCameraSettings, "CameraDefaultXDeploy", fpCameraDefaultXDeploy);
    reg.SetFloat(szCameraSettings, "CameraDefaultYDeploy", fpCameraDefaultYDeploy);
    reg.SetFloat(szCameraSettings, "CameraMinDistanceToCarrier", fpCameraMinDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraMaxDistanceToCarrier", fpCameraMaxDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraMinLimitDistanceToCarrier",
                 fpCameraMinLimitDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraMaxLimitDistanceToCarrier",
                 fpCameraMaxLimitDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraDefaultDistanceToCarrier",
                 fpCameraDefaultDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "NearClipDistance", fpNearClipDistance);
    reg.SetFloat(szCameraSettings, "FarClipDistance", fpFarClipDistance);
    reg.SetFloat(szCameraSettings, "CameraDefaultMinPitch", fpCameraDefaultMinPitch);
    reg.SetFloat(szCameraSettings, "CameraDefaultMaxPitch", fpCameraDefaultMaxPitch);

    reg.SetFloat(szCameraSettings, "CameraBriefingMinDistanceToCarrier",
                 fpCameraBriefingMinDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraBriefingMaxDistanceToCarrier",
                 fpCameraBriefingMaxDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraBriefingMinLimitDistanceToCarrier",
                 fpCameraBriefingMinLimitDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraBriefingMaxLimitDistanceToCarrier",
                 fpCameraBriefingMaxLimitDistanceToCarrier);
    reg.SetFloat(szCameraSettings, "CameraBriefingMinPitch", fpCameraBriefingMinPitch);
    reg.SetFloat(szCameraSettings, "CameraBriefingMaxPitch", fpCameraBriefingMaxPitch);

    reg.SetInt(szCameraSettings, "CameraBorderScrollArea", nCameraBorderScrollArea);
    reg.SetInt(szCameraSettings, "CameraFrameSelectionSensetiveArea",
               nCameraFrameSelectionSensetiveArea);
    reg.SetFloat(szCameraSettings, "CameraTerrainSensetiveArea", fCameraTerrainSensetiveArea);

    reg.SetFloat(szCameraSettings, "CameraDefaultScrollSpeed", cctCameraScroll.fpValue);
    reg.SetFloat(szCameraSettings, "CameraDefaultScrollPowerMouse", cctCameraScroll.fpMousePower);
    reg.SetFloat(szCameraSettings, "CameraDefaultScrollPowerKeyboard",
                 cctCameraScroll.fpKeyboardPower);

    reg.SetFloat(szCameraSettings, "CameraDefaultRotateSpeed", cctCameraRotate.fpValue);
    reg.SetFloat(szCameraSettings, "CameraDefaultRotatePowerMouse", cctCameraRotate.fpMousePower);
    reg.SetFloat(szCameraSettings, "CameraDefaultRotatePowerKeyboard",
                 cctCameraRotate.fpKeyboardPower);

    reg.SetFloat(szCameraSettings, "CameraDefaultPitchSpeed", cctCameraPitch.fpValue);
    reg.SetFloat(szCameraSettings, "CameraDefaultPitchPowerMouse", cctCameraPitch.fpMousePower);
    reg.SetFloat(szCameraSettings, "CameraDefaultPitchPowerKeyboard",
                 cctCameraPitch.fpKeyboardPower);

    reg.SetFloat(szCameraSettings, "CameraDefaultZoomSpeed", cctCameraZoom.fpValue);
    reg.SetFloat(szCameraSettings, "CameraDefaultZoomPowerMouse", cctCameraZoom.fpMousePower);
    reg.SetFloat(szCameraSettings, "CameraDefaultZoomPowerKeyboard", cctCameraZoom.fpKeyboardPower);

    for (int i = 0; i < SETTING_NUM_CAMERA_SHORTCUTS; ++i)
    {
        char buff[1024];
        sprintf(buff, "CameraShortcut %d AxisX", i);
        reg.SetFloat(szCameraSettings, buff, camerashortcuts[i].fAxisX);
        sprintf(buff, "CameraShortcut %d AxisY", i);
        reg.SetFloat(szCameraSettings, buff, camerashortcuts[i].fAxisY);
        sprintf(buff, "CameraShortcut %d AxisZ", i);
        reg.SetFloat(szCameraSettings, buff, camerashortcuts[i].fAxisZ);
        sprintf(buff, "CameraShortcut %d Angle", i);
        reg.SetFloat(szCameraSettings, buff, camerashortcuts[i].fAngle);
        sprintf(buff, "CameraShortcut %d Distance", i);
        reg.SetFloat(szCameraSettings, buff, camerashortcuts[i].fDistance);
        sprintf(buff, "CameraShortcut %d Pitch", i);
        reg.SetFloat(szCameraSettings, buff, camerashortcuts[i].fPitch);
    }

    // interface and world settings
    reg.SetFloat(szGameSpecificSettings, "DefaultWindForce", fpDefaultWindForce);
    reg.SetInt(szGameSpecificSettings, "DefaultTimeScale", nDefaultTimeScale);
    reg.SetInt(szGameSpecificSettings, "UseSWCursors", nUseSWCursors);
    reg.SetInt(szGameSpecificSettings, "SelectionType", nSelectionType);
    reg.SetFloat(szGameSpecificSettings, "CornerSize", fpCornerSize);
    reg.SetInt(szGameSpecificSettings, "DrawVisibleOnly", nDrawVisibleOnly);
    reg.SetInt(szGameSpecificSettings, "DrawPath", nDrawPath);
    reg.SetInt(szGameSpecificSettings, "DrawFlyingHP", nDrawFlyingHP);
    reg.SetInt(szGameSpecificSettings, "ShowTutorial", nShowTutorial);
    reg.SetInt(szGameSpecificSettings, "Autosave", nDoAutosave);
    reg.SetFloat(szGameSpecificSettings, "ToolTip Time", fToolTipTime);
    reg.SetInt(szGameSpecificSettings, "SwitchFilters", nSwitchFilters);
    reg.SetString(szGameSpecificSettings, "CD1 Label", szCD1Label);
    reg.SetString(szGameSpecificSettings, "CD2 Label", szCD2Label);
    reg.SetString(szGameSpecificSettings, "CD3 Label", szCD3Label);
    reg.SetString(szGameSpecificSettings, "Font CharSet", szCharSet);
    reg.SetDWord(szGameSpecificSettings, "Keyboard Layout", dwKeyboardLayout);
    reg.SetFloat(szGameSpecificSettings, "ObjectsLightingCoeff", fObjectsLightingCoeff);

    reg.SetDWord(szGameSpecificSettings, "Hero Light R", fHeroLight[0] * 255.0f);
    reg.SetDWord(szGameSpecificSettings, "Hero Light G", fHeroLight[1] * 255.0f);
    reg.SetDWord(szGameSpecificSettings, "Hero Light B", fHeroLight[2] * 255.0f);
    reg.SetDWord(szGameSpecificSettings, "Hero Light Radius", fHeroLightRadius);
    reg.SetDWord(szGameSpecificSettings, "Quest Light R", fQuestLight[0] * 255.0f);
    reg.SetDWord(szGameSpecificSettings, "Quest Light G", fQuestLight[1] * 255.0f);
    reg.SetDWord(szGameSpecificSettings, "Quest Light B", fQuestLight[2] * 255.0f);
    reg.SetDWord(szGameSpecificSettings, "Quest Light Radius", fQuestLightRadius);
    reg.SetDWord(szGameSpecificSettings, "Difficulty Level", nDifficultyLevel2);
    // some ambiguid settings, which will be removed
    reg.SetString(szGameSpecificSettings, "DefaultMapName", szDefaultMapName);
    reg.SetString(szGameSpecificSettings, "DefaultStreamingSound", szDefaultStreamingSound);
    reg.SetFloat(szGameSpecificSettings, "FogNightStartDistance", fpFogNightStartDistance);
    reg.SetFloat(szGameSpecificSettings, "FogDayStartDistance", fpFogDayStartDistance);
    //
    reg.Close();
}
/////////////////////////////////////////////////////////////////////////////////////
