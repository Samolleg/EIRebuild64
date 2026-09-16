#pragma once
#include <cstddef>
class CSoundManager;
namespace ModAudioPolicy {void Connect();void Reset();void Tick(CSoundManager*);int Priority(int,const char*,bool,bool,bool);void Variation(const char*,bool,bool,bool,float&,float&);bool Name(CSoundManager*,const char*,char*,size_t,const char*&);bool Check();}

#include "MiniaudioOneShotSfx.h"
namespace ModAudioPolicy {
struct AckScope {
 MiniaudioOneShotSfx::PartyVoiceBinding previous;
 bool wasActive;
 AckScope(unsigned unit,unsigned type,bool enabled);
 ~AckScope();
 AckScope(const AckScope&)=delete;
 AckScope& operator=(const AckScope&)=delete;
};
bool TakeAck(bool speech,bool dialog,bool looping,MiniaudioOneShotSfx::PartyVoiceBinding&);
bool ReuseIdle(CSoundManager*,unsigned,const char*);
}
