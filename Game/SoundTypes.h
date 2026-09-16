#pragma once
// -------------------------- Priority constants ---------------------------------------------
enum
{
#ifdef _A4
    ESNDPR_AMBIENT = 1,
    ESNDPR_STEPS = 2,
    ESNDPR_BUILDINGS = 3,
    ESNDPR_COMBAT = 4,
    ESNDPR_MAGIC = 5,
    ESNDPR_INTERFACE = 6,
    ESNDPR_SPEECH = 7
#else
    ESNDPR_AMBIENT = 1,
    ESNDPR_STEPS = 2,
    ESNDPR_INTERFACE = 3,
    ESNDPR_COMBAT = 4,
    ESNDPR_MAGIC = 5,
    ESNDPR_SPEECH = 6
#endif //_A4
};
/////////////////////////////////////////////////////////////////////////////////////
enum ESNDMARKERTYPE
{
    EM_INSTANT = 4,   // immediately change streaming audio
    EM_FAST = 3,      // change audio using 2nd markers
    EM_SAFE = 2,      // change audio using 1st markers
    EM_ENDOFTRACK = 1 // change audio when end of track reached
};

enum ESNDSTREAMSTATE
{
    ESMST_EMPTY = 0,    // buffer is empty
    ESMST_PLAYING = 1,  // buffer is playing
    ESMST_SWITCHING = 2 // buffer is playing and waiting for switching
};
/////////////////////////////////////////////////////////////////////////////////////
typedef unsigned int HSOUND;
typedef unsigned int HSNDAMBIENT;
typedef unsigned int HSNDLIST;

const HSOUND SND_EMPTY = -1; // HSOUND referencing empty sound
/////////////////////////////////////////////////////////////////////////////////////
