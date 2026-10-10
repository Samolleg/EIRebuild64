#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "KeyMap.h"
#include "ModPackage.h"
#include "ModRuntime.h"

#include "StringProcessing.h"
#include "WorldInterface.h"

/////////////////////////////////////////////////////////////////////////////////////
SKbdKeyDesc NKeyMap::allKeys[TOTAL_NUM_KBD_KEYS] = {
    //	{ 1,   "ESCAPE" },
    { 2, "1" },
    { 3, "2" },
    { 4, "3" },
    { 5, "4" },
    { 6, "5" },
    { 7, "6" },
    { 8, "7" },
    { 9, "8" },
    { 10, "9" },
    { 11, "0" },
    { 12, "-" },
    { 13, "+" },
    //	{ 14,  "BACKSPACE" },
    { 15, "TAB" },
    { 16, "Q" },
    { 17, "W" },
    { 18, "E" },
    { 19, "R" },
    { 20, "T" },
    { 21, "Y" },
    { 22, "U" },
    { 23, "I" },
    { 24, "O" },
    { 25, "P" },
    { 26, "[" },
    { 27, "]" },
    //	{ 28,  "ENTER" },
    //	{ 29,  "LCTRL" },
    { 30, "A" },
    { 31, "S" },
    { 32, "D" },
    { 33, "F" },
    { 34, "G" },
    { 35, "H" },
    { 36, "J" },
    { 37, "K" },
    { 38, "L" },
    { 39, ";" },
    { 40, "\'" },
    //	{ 41,  "`" },
    //	{ 42,  "LSHIFT" },
    { 43, "\\" },
    { 44, "Z" },
    { 45, "X" },
    { 46, "C" },
    { 47, "V" },
    { 48, "B" },
    { 49, "N" },
    { 50, "M" },
    { 51, "," },
    { 52, "." },
    { 53, "/" },
    //	{ 54,  "RSHIFT" },
    { 55, "KP_STAR" },
    //	{ 56,  "LALT" },
    { 57, "SPACE" },
    { 58, "CAPSLOCK" },
    { 59, "F1" },
    { 60, "F2" },
    { 61, "F3" },
    { 62, "F4" },
    { 63, "F5" },
    { 64, "F6" },
    { 65, "F7" },
    { 66, "F8" },
    { 67, "F9" },
    { 68, "F10" },
    { 69, "PAUSE" },
    //	{ 70,  "SCROLL" },
    { 71, "KP_HOME" },
    { 72, "KP_UP" },
    { 73, "KP_PGUP" },
    { 74, "KP_MINUS" },
    { 75, "KP_LEFT" },
    { 76, "KP_CENTER" },
    { 77, "KP_RIGHT" },
    { 78, "KP_PLUS" },
    { 79, "KP_END" },
    { 80, "KP_DOWN" },
    { 81, "KP_PGDN" },
    { 82, "KP_INS" },
    { 83, "KP_DEL" },
    { 87, "F11" },
    { 88, "F12" },
    //	{ 284, "KP_ENTER" },
    //	{ 285, "RCTRL" },
    { 309, "KP_SLASH" },
    //	{ 312, "RALT" },
    { 325, "KP_NUMLOCK" },
    { 327, "HOME" },
    { 328, "UP" },
    { 329, "PGUP" },
    { 331, "LEFT" },
    { 333, "RIGHT" },
    { 335, "END" },
    { 336, "DOWN" },
    { 337, "PGDN" },
    { 338, "INS" },
    { 339, "DEL" }
};
/////////////////////////////////////////////////////////////////////////////////////
SKbdActionDesc NKeyMap::allActions[TOTAL_NUM_KBD_ACTIONS] = {
    { KA_UNKNOWN, "", 0, 0 },
    { KA_PAUSE, "pause", OG_GAME, 4 },
    { KA_DECELERATE, "decel", OG_GAME, 3 },
    { KA_ACCELERATE, "accel", OG_GAME, 2 },
    { KA_OBJECTIVES, "obj", OG_GAME, 6 },
    { KA_CAMERA_UP, "camera_up", OG_CAMERA, 1 },
    { KA_CAMERA_DOWN, "camera_down", OG_CAMERA, 2 },
    { KA_CAMERA_LEFT, "camera_left", OG_CAMERA, 3 },
    { KA_CAMERA_RIGHT, "camera_right", OG_CAMERA, 4 },
    { KA_CAMERA_ZOOM_IN, "camera_zoom_in", OG_CAMERA, 5 },
    { KA_CAMERA_ZOOM_OUT, "camera_zoom_out", OG_CAMERA, 6 },
    { KA_CAMERA_TRACK, "camera_track", OG_CAMERA, 0 },
    //	{ KA_CAMERA_CENTER, "camera_center", OG_CAMERA, 1 },
    { KA_CAMERA_NORM, "camera_norm", OG_CAMERA, 8 },
    { KA_CAMERA1, "camera1", OG_CAMERA, 9 },
    { KA_CAMERA2, "camera2", OG_CAMERA, 10 },
    { KA_CAMERA3, "camera3", OG_CAMERA, 11 },
    { KA_CAMERA4, "camera4", OG_CAMERA, 12 },
    { KA_CSHOT_HEAD, "cs_head", OG_CSHOTS, 0 },
    { KA_CSHOT_BODY, "cs_body", OG_CSHOTS, 1 },
    { KA_CSHOT_RLEG, "cs_rleg", OG_CSHOTS, 2 },
    { KA_CSHOT_LLEG, "cs_lleg", OG_CSHOTS, 3 },
    { KA_CSHOT_RHAND, "cs_rhand", OG_CSHOTS, 4 },
    { KA_CSHOT_LHAND, "cs_lhand", OG_CSHOTS, 5 },
    { KA_POSE_LAY, "crawl", OG_ACTIONS, 7 },
    { KA_POSE_CRAWL, "sneak", OG_ACTIONS, 6 },
    { KA_POSE_WALK, "walk", OG_ACTIONS, 5 },
    { KA_POSE_RUN, "run", OG_ACTIONS, 4 },
    { KA_SWARM, "swarm", OG_ACTIONS, 2 },
    { KA_FOLLOW, "follow", OG_ACTIONS, 0 },
    //	{ KA_SKILL_STEAL, "use_steal" },
    { KA_SKILL_SCIENCE, "use_science", OG_ACTIONS, 1 },
    //	{ KA_SKILL_TAME, "use_tame" },
    { KA_SPELL1, "spell1", OG_SPELLS, 0 },
    { KA_SPELL2, "spell2", OG_SPELLS, 1 },
    { KA_SPELL3, "spell3", OG_SPELLS, 2 },
    { KA_SPELL4, "spell4", OG_SPELLS, 3 },
    { KA_SPELL5, "spell5", OG_SPELLS, 4 },
    { KA_SPELL6, "spell6", OG_SPELLS, 5 },
    { KA_SPELL7, "spell7", OG_SPELLS, 6 },
    { KA_SPELL8, "spell8", OG_SPELLS, 7 },
    { KA_WEAPON1, "weapon1", OG_ITEMS, 0 },
    { KA_WEAPON2, "weapon2", OG_ITEMS, 1 },
    { KA_WEAPON3, "weapon3", OG_ITEMS, 2 },
    { KA_WEAPON4, "weapon4", OG_ITEMS, 3 },
    { KA_QUICK1, "item1", OG_ITEMS, 5 },
    { KA_QUICK2, "item2", OG_ITEMS, 6 },
    { KA_QUICK3, "item3", OG_ITEMS, 7 },
    { KA_QUICK4, "item4", OG_ITEMS, 8 },
    { KA_INFO1, "w_info1", OG_WINDOWS, 3 },
    { KA_INFO2, "w_info2", OG_WINDOWS, 4 },
    { KA_INFO3, "w_info3", OG_WINDOWS, 5 },
    { KA_INFO4, "w_info4", OG_WINDOWS, 6 },
    { KA_TEXT1, "w_text1", OG_WINDOWS, 0 },
    { KA_TEXT2, "w_text2", OG_WINDOWS, 1 },
    { KA_MINIMAP, "w_minimap", OG_WINDOWS, 8 },
    { KA_QSAVE, "quicksave", OG_GAME, 8 },
    { KA_QLOAD, "quickload", OG_GAME, 9 },
    { KA_SELECT1, "select1", OG_SELECT, 0 },
    { KA_SELECT2, "select2", OG_SELECT, 1 },
    { KA_SELECT3, "select3", OG_SELECT, 2 },
    //	{ KA_SELECT4, "select4", OG_SELECT, 3 },
    //	{ KA_SELECT5, "select5", OG_SELECT, 4 },
    //	{ KA_SELECT6, "select6", OG_SELECT, 5 },
    { KA_SELECTALL, "select_all", OG_SELECT, 4 },
    //	{ KA_TUTORIAL_INTERFACE, "tutorial_interface", OG_GAME, 9 },
    { KA_TUTORIAL_SCRIPT, "tutorial_script", OG_WINDOWS, 10 },
#ifndef NODEBUGINFO
    // DEBUG{
    { KA_DEBUG_SHOT, "debug_shot", OG_DEBUG1, 0 },
    { KA_DEBUG_SHOT_SEQ, "debug_shot_sequence", OG_DEBUG1, 1 },
    { KA_DEBUG_CAM_SAVE, "debug_cam_save", OG_DEBUG1, 2 },
    { KA_DEBUG_CAM_LOAD, "debug_cam_load", OG_DEBUG1, 3 },
    { KA_DEBUG_CAM_CNTR, "debug_cam_cntr", OG_DEBUG1, 4 },
    { KA_DEBUG_RND_TERRAIN, "debug_toggle_terrain", OG_DEBUG1, 5 },
    { KA_DEBUG_RND_SHADOWS, "debug_toggle_shadows", OG_DEBUG1, 6 },
    { KA_DEBUG_RND_UNITS, "debug_toggle_units", OG_DEBUG1, 7 },
    { KA_DEBUG_RND_IFACE, "debug_toggle_interface", OG_DEBUG1, 8 },
    { KA_DEBUG_RND_OBJECTS, "debug_toggle_objects", OG_DEBUG1, 9 },
    { KA_DEBUG_RND_EFFECTS, "debug_toggle_effects", OG_DEBUG1, 10 },
    { KA_DEBUG_RND_SKY, "debug_toggle_sky", OG_DEBUG1, 11 },
    { KA_DEBUG_WIREFRAME, "debug_wireframe", OG_DEBUG1, 12 },
    { KA_DEBUG_BBOXES, "debug_bboxes", OG_DEBUG1, 13 },
    { KA_DEBUG_VISIBLE_ONLY, "debug_visible", OG_DEBUG2, 0 },
    { KA_DEBUG_ADD_TIME, "debug_add_time", OG_DEBUG2, 1 },
    { KA_DEBUG_SUB_TIME, "debug_sub_time", OG_DEBUG2, 2 },
    { KA_DEBUG_ADD_WIND, "debug_add_wind", OG_DEBUG2, 3 },
    { KA_DEBUG_SUB_WIND, "debug_sub_wind", OG_DEBUG2, 4 },
    { KA_DEBUG_WEATHER_CLEAR, "debug_weather_clear", OG_DEBUG2, 5 },
    { KA_DEBUG_WEATHER_RAIN, "debug_weather_rain", OG_DEBUG2, 6 },
    { KA_DEBUG_WEATHER_SNOW, "debug_weather_snow", OG_DEBUG2, 7 },
    { KA_DEBUG_WEATHER_LGTN, "debug_weather_lightning", OG_DEBUG2, 8 },
// DEBUG}
#endif // NODEBUGINFO
};

SGameOptionDesc NKeyMap::allOptions[TOTAL_NUM_GAME_OPTIONS] = {
    { OPTION_VOLUME_SFX, "volume_sfx", OPTION_TYPE_SCROLL, 100, OG_SOUND, 0 },
    { OPTION_VOLUME_STREAM, "volume_stream", OPTION_TYPE_SCROLL, 100, OG_SOUND, 1 },
    { OPTION_VOLUME_VOICE, "volume_voice", OPTION_TYPE_SCROLL, 100, OG_SOUND, 2 },
    { OPTION_POWER_KBD, "power_kbd", OPTION_TYPE_SCROLL, 100, OG_SENS, 0 },
    { OPTION_POWER_MOUSE, "power_mouse", OPTION_TYPE_SCROLL, 100, OG_SENS, 1 },
    { OPTION_SCROLL_BORDER, "scroll_border", OPTION_TYPE_SCROLL, 10, OG_SENS, 2 },
    { OPTION_RUBBER_SELECT, "rubber_select", OPTION_TYPE_SCROLL, 10, OG_SENS, 3 },
    { OPTION_SHADOW_UNITS, "shadow_units", OPTION_TYPE_SWITCH, 2, OG_VIDEO, 0 },
    { OPTION_SHADOW_BUILDINGS, "shadow_buildings", OPTION_TYPE_SWITCH, 2, OG_VIDEO, 1 },
    { OPTION_SHADOW_FLORA, "shadow_flora", OPTION_TYPE_SWITCH, 2, OG_VIDEO, 2 },
    { OPTION_MARKS_ON, "marks", OPTION_TYPE_SWITCH, 2, OG_VIDEO, 4 },
    { OPTION_FOOTPRINTS, "footprints", OPTION_TYPE_SWITCH, 2, OG_VIDEO, 5 },
    { OPTION_SELECTION_TYPE, "select_type", OPTION_TYPE_SWITCH, 2, OG_VIDEO, 6 },
    { OPTION_SHOW_PATH, "show_path", OPTION_TYPE_SWITCH, 2, OG_VIDEO, 7 },
    { OPTION_BRIGHTNESS, "brightness", OPTION_TYPE_SCROLL, 100, OG_VIDEO, 10 },
    { OPTION_CONTRAST, "contrast", OPTION_TYPE_SCROLL, 100, OG_VIDEO, 11 },
    { OPTION_GAMMA, "gamma", OPTION_TYPE_SCROLL, 100, OG_VIDEO, 12 },
    { OPTION_SHOW_FLYINGHP, "show_flying_hp", OPTION_TYPE_SWITCH, 2, OG_VIDEO, 8 },
    { OPTION_SHOW_TUTORIAL, "show_tutorial", OPTION_TYPE_SWITCH, 2, OG_WINDOWS, 11 },
    { OPTION_AUTOSAVE, "autosave", OPTION_TYPE_SWITCH, 2, OG_GAME, 11 },
    { OPTION_TOOLTIP_TIME, "tooltip_time", OPTION_TYPE_SCROLL, 20, OG_SENS, 4 },
    { OPTION_SWITCH_FILTERS, "switch_filters", OPTION_TYPE_SWITCH, 2, OG_GAME, 12 },
    { OPTION_REVERSE_CAMERA_X, "camera_reverse_x", OPTION_TYPE_SWITCH, 2, OG_SENS, 6 },
    { OPTION_REVERSE_CAMERA_Y, "camera_reverse_y", OPTION_TYPE_SWITCH, 2, OG_SENS, 7 },
    { OPTION_STEREO_REVERSE, "reverse_stereo", OPTION_TYPE_SWITCH, 2, OG_SOUND, 4 },
    { OPTION_DIFFICULTY, "difficulty", OPTION_TYPE_SWITCH, 2, OG_GAME, 0 },
};
/////////////////////////////////////////////////////////////////////////////////////
char NKeyMap::keyMap[TOTAL_NUM_SCANCODES];

bool NKeyMap::SpellAddonDifficultyLevels()
{
    const CModPackage *package = ModRuntime::Current();
    return package && package->SpellAddon().Feature35() == 1;
}
/////////////////////////////////////////////////////////////////////////////////////
void NKeyMap::Init()
{
    for (int k = 0; k < TOTAL_NUM_KBD_KEYS; ++k)
    {
        if (!GetKeyNameText(allKeys[k].nScanCode << 16, allKeys[k].pszLocalKeyName, 31))
            allKeys[k].pszLocalKeyName[0] = 0;
    }

    CString szStr;
    CStringArray szStrArray;
    CStdioFile file;
    int i, j;
    bool bKeyFound, bActFound;
    for (i = 0; i < TOTAL_NUM_SCANCODES; i++)
        keyMap[i] = KA_UNKNOWN;
    if (file.Open(IWorld.GetWorkPath() + "config\\keyboard.ini",
                  CFile::modeRead | CFile::typeText) == FALSE)
        return;
    while (file.ReadString(szStr))
    {
        NStrProc::SplitStringMulti(szStr, szStrArray, ' ');
        if (szStrArray.GetSize() > 1)
        {
            bKeyFound = false;
            bActFound = false;
            for (i = 0; i < TOTAL_NUM_KBD_KEYS; i++)
                if (stricmp(allKeys[i].pszKeyName, szStrArray[0]) == 0)
                {
                    bKeyFound = true;
                    break;
                }
            for (j = 0; j < TOTAL_NUM_KBD_ACTIONS; j++)
                if (stricmp(allActions[j].pszActionName, szStrArray[1]) == 0)
                {
                    bActFound = true;
                    break;
                }
            if (bActFound && bKeyFound)
                keyMap[allKeys[i].nScanCode] = allActions[j].eAction;
        }
    }
    file.Close();
}
/////////////////////////////////////////////////////////////////////////////////////
void NKeyMap::Fini()
{
    CString szStr;
    CStdioFile file;
    int i, j, k;
    bool bKeyFound, bActFound;
    if (file.Open(IWorld.GetWorkPath() + "config\\keyboard.ini",
                  CFile::modeCreate | CFile::modeWrite) == FALSE)
        return;
    for (k = 0; k < TOTAL_NUM_SCANCODES; k++)
    {
        if (keyMap[k] == KA_UNKNOWN)
            continue;
        bKeyFound = false;
        bActFound = false;
        for (i = 0; i < TOTAL_NUM_KBD_KEYS; i++)
            if (allKeys[i].nScanCode == k)
            {
                bKeyFound = true;
                break;
            }
        for (j = 0; j < TOTAL_NUM_KBD_ACTIONS; j++)
            if (allActions[j].eAction == keyMap[k])
            {
                bActFound = true;
                break;
            }
        if (bActFound && bKeyFound)
        {
            szStr.Format("%s %s\n", allKeys[i].pszKeyName, allActions[j].pszActionName);
            file.WriteString(szStr);
        }
    }
    file.Close();
}
/////////////////////////////////////////////////////////////////////////////////////
const char *NKeyMap::GetKeyNameByAction(EKbdActions eAction)
{
    for (int k = 0; k < TOTAL_NUM_SCANCODES; k++)
        if (keyMap[k] == eAction)
        {
            for (int i = 0; i < TOTAL_NUM_KBD_KEYS; i++)
                if (allKeys[i].nScanCode == k)
                    return allKeys[i].pszLocalKeyName;
        }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
