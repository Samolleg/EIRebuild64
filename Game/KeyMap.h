#pragma once

#include <map>

const int TOTAL_NUM_KBD_KEYS = 88;

const int TOTAL_NUM_SCANCODES = 512;

enum EKbdActions
{
    KA_UNKNOWN = 0,

    KA_PAUSE,
    KA_DECELERATE,
    KA_ACCELERATE,
    KA_OBJECTIVES,

    KA_CAMERA_UP,
    KA_CAMERA_DOWN,
    KA_CAMERA_LEFT,
    KA_CAMERA_RIGHT,
    KA_CAMERA_ZOOM_IN,
    KA_CAMERA_ZOOM_OUT,
    KA_CAMERA_TRACK,
    //	KA_CAMERA_CENTER,
    KA_CAMERA_NORM,
    KA_CAMERA1,
    KA_CAMERA2,
    KA_CAMERA3,
    KA_CAMERA4,

    KA_CSHOT_HEAD,
    KA_CSHOT_BODY,
    KA_CSHOT_RLEG,
    KA_CSHOT_LLEG,
    KA_CSHOT_RHAND,
    KA_CSHOT_LHAND,

    KA_POSE_LAY,
    KA_POSE_CRAWL,
    KA_POSE_WALK,
    KA_POSE_RUN,
    KA_SWARM,
    KA_FOLLOW,
    //	KA_SKILL_STEAL,
    KA_SKILL_SCIENCE,
    //	KA_SKILL_TAME,

    KA_SPELL1,
    KA_SPELL2,
    KA_SPELL3,
    KA_SPELL4,
    KA_SPELL5,
    KA_SPELL6,
    KA_SPELL7,
    KA_SPELL8,

    KA_WEAPON1,
    KA_WEAPON2,
    KA_WEAPON3,
    KA_WEAPON4,

    KA_QUICK1,
    KA_QUICK2,
    KA_QUICK3,
    KA_QUICK4,

    KA_INFO1,
    KA_INFO2,
    KA_INFO3,
    KA_INFO4,
    KA_TEXT1,
    KA_TEXT2,
    KA_MINIMAP,

    KA_QSAVE,
    KA_QLOAD,

    KA_SELECT1,
    KA_SELECT2,
    KA_SELECT3,
    //	KA_SELECT4,
    //	KA_SELECT5,
    //	KA_SELECT6,
    KA_SELECTALL,

    //	KA_TUTORIAL_INTERFACE,
    KA_TUTORIAL_SCRIPT,

#ifndef NODEBUGINFO
    KA_DEBUG_SHOT,
    KA_DEBUG_SHOT_SEQ,
    KA_DEBUG_CAM_SAVE,
    KA_DEBUG_CAM_LOAD,
    KA_DEBUG_CAM_CNTR,
    KA_DEBUG_RND_TERRAIN,
    KA_DEBUG_RND_SHADOWS,
    KA_DEBUG_RND_UNITS,
    KA_DEBUG_RND_IFACE,
    KA_DEBUG_RND_OBJECTS,
    KA_DEBUG_RND_EFFECTS,
    KA_DEBUG_RND_SKY,
    KA_DEBUG_WIREFRAME,
    KA_DEBUG_BBOXES,
    KA_DEBUG_VISIBLE_ONLY,
    KA_DEBUG_ADD_TIME,
    KA_DEBUG_SUB_TIME,
    KA_DEBUG_ADD_WIND,
    KA_DEBUG_SUB_WIND,
    KA_DEBUG_WEATHER_CLEAR,
    KA_DEBUG_WEATHER_RAIN,
    KA_DEBUG_WEATHER_SNOW,
    KA_DEBUG_WEATHER_LGTN,
#endif // NODEBUGINFO

    TOTAL_NUM_KBD_ACTIONS
};

enum EGameOptionTypes
{
    OPTION_TYPE_SCROLL,
    OPTION_TYPE_SWITCH
};

enum EGameOptions
{
    OPTION_VOLUME_SFX,
    OPTION_VOLUME_STREAM,
    OPTION_VOLUME_VOICE,
    OPTION_POWER_KBD,
    OPTION_POWER_MOUSE,
    OPTION_SCROLL_BORDER,
    OPTION_RUBBER_SELECT,
    OPTION_SHADOW_UNITS,
    OPTION_SHADOW_BUILDINGS,
    OPTION_SHADOW_FLORA,
    OPTION_MARKS_ON,
    OPTION_FOOTPRINTS,
    OPTION_SELECTION_TYPE,
    OPTION_SHOW_PATH,
    OPTION_BRIGHTNESS,
    OPTION_CONTRAST,
    OPTION_GAMMA,
    OPTION_SHOW_FLYINGHP,
    OPTION_SHOW_TUTORIAL,
    OPTION_AUTOSAVE,
    OPTION_TOOLTIP_TIME,
    OPTION_SWITCH_FILTERS,
    OPTION_REVERSE_CAMERA_X,
    OPTION_REVERSE_CAMERA_Y,
    OPTION_STEREO_REVERSE,
    OPTION_DIFFICULTY,

    TOTAL_NUM_GAME_OPTIONS
};

enum EGameOptionGroups
{
    OG_VIDEO,
    OG_SOUND,
    OG_SENS,
    OG_GAME,
    OG_SELECT,
    OG_ACTIONS,
    OG_CAMERA,
    OG_ITEMS,
    OG_SPELLS,
    OG_CSHOTS,
    OG_WINDOWS,
#ifndef NODEBUGINFO
    OG_DEBUG1,
    OG_DEBUG2,
#endif // NODEBUGINFO

    TOTAL_NUM_OPTION_GROUPS
};

struct SKbdKeyDesc
{
    int nScanCode;
    const char *pszKeyName;
    char pszLocalKeyName[64];
};
struct SKbdActionDesc
{
    EKbdActions eAction;
    const char *pszActionName;

    int nGroup;
    int nLine;
};
struct SGameOptionDesc
{
    EGameOptions eOption;
    const char *pszOptionName;

    EGameOptionTypes eType;
    int nRange;

    int nGroup;
    int nLine;
};

namespace NKeyMap {
extern SGameOptionDesc allOptions[TOTAL_NUM_GAME_OPTIONS];

extern SKbdKeyDesc allKeys[TOTAL_NUM_KBD_KEYS];
extern SKbdActionDesc allActions[TOTAL_NUM_KBD_ACTIONS];
extern char keyMap[TOTAL_NUM_SCANCODES];

void Init();
void Fini();
const char *GetKeyNameByAction(EKbdActions eAction);
}; // namespace NKeyMap
