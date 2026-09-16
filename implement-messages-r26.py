from pathlib import Path
import re
r=Path(__file__).resolve().parent;ref=r.parent/'src_addon_world_background_data_20260916/src/addon/Patches'
def edit(name,fn):
 p=r/name;b=p.read_bytes();dest=r/'evidence/addon-integration/pre-r26'/name;dest.parent.mkdir(parents=True,exist_ok=True)
 if not dest.exists():dest.write_bytes(b)
 p.write_bytes(fn(b.decode('cp1251')).encode('cp1251'))
(r/'Game/ModScriptMessageQueue.h').write_bytes((ref/'ScriptMessageQueue.h').read_bytes())
(r/'Game/ModMessages.h').write_text('''#pragma once
#include "ModScriptMessageQueue.h"
namespace ModMessages {
inline ScriptMessages::Queue queue;
inline const char* Show(const char*text){std::string formatted;if(!ScriptMessages::Format(text,formatted))return "Expected nonblank plain text, up to4096 bytes, without control codes";return queue.Push(std::move(formatted));}
}
''',encoding='utf-8')
edit('Game/I3DScreenMission.h',lambda s:s.replace('#pragma once','#pragma once\r\n#include "ModMessages.h"').replace('    void Init(int iMode, bool bClosed);','    virtual ~CI3DObjectOutput(){ModMessages::queue.Unbind(this);}\r\n    void Init(int iMode, bool bClosed);'))
def ui(s):
 s=s.replace('#include "StdAfx.h"','#include "StdAfx.h"\r\n#include "ModMessages.h"\r\n#include "ModRuntime.h"')
 a='void CI3DObjectOutput::Init(int iMode, bool bClosed)\r\n{';assert a in s;s=s.replace(a,a+'\r\n    if(ModRuntime::Current())ModMessages::queue.Bind(this);')
 a='void CI3DObjectOutput::OnIdle()\r\n{';assert a in s
 s=s.replace(a,a+'\r\n    auto messages=ModMessages::queue.Take(this);\r\n    if(!messages.empty()){if(nMode!=0){nMode=0;ShowCurrentMode();}slide_dir=-1;slide_bIsSlide=TRUE;for(const auto&message:messages)AddOutput(message.c_str());}')
 return s
edit('Game/I3DScreenMission.cpp',ui)
edit('Game/Unit.h',lambda s:s.replace('const DWORD HLDAMAGE_BACKSTAB = 1;','const DWORD HLDAMAGE_POTION = 512;\r\nconst DWORD HLDAMAGE_BACKSTAB = 1;'))
def mob(s):
 s=s.replace('#include "ModRuntime.h"','#include "ModRuntime.h"\r\n#include "ModMessages.h"')
 for n in ['ShowMessage','AckPotion']:
  s,count=re.subn(r'("'+n+r'","[^"]*",)false',r'\1true',s);assert count==1
 a=' if(cmd.command==300)';s=s.replace(a,' if(cmd.command==359){auto*s=ServerArg(cmd);if(s){s->InformFailure(ACK_HERO_ACK_USE_POTION);theWorld.ReceiveDamage(UnitArg(cmd)->GetID(),0,HLDAMAGE_POTION);}result=0;return true;}\r\n'+a)
 a=' case 382:';s=s.replace(a,' case 389:if(IWorldConnector.IsLMP())throw std::runtime_error("ShowMessage requires single player");if(const char*error=ModMessages::Show(cmd.szParams[0]))throw std::runtime_error(error);break;\r\n'+a);return s
edit('Game/ModMobCommands.h',mob)
def flying(s):
 s=s.replace('#include "StdAfx.h"','#include "StdAfx.h"\r\n#include "ModRuntime.h"')
 a='        int nLength = szTmp.GetLength();';assert s.count(a)==1
 s=s.replace(a,a+'\r\n        const bool potion=ModRuntime::Current()&&(it->dwDamageFlags&HLDAMAGE_POTION);\r\n        if(potion&&it->nAmount<1)nLength=0;')
 a='        int nInfoWidth = nLength * FHP_WIDTH;';assert a in s;s=s.replace(a,'        int nInfoWidth = (nLength+(potion?1:0)) * FHP_WIDTH;')
 a='        DWORD specular = RGBA_MAKE(0, 0, 0, 255);';assert s.count(a)==1;s=s.replace(a,'        if(potion)color=RGBA_MAKE(255,255,255,alpha);\r\n'+a)
 a='        x = xx + (nInfoWidth - nLength * FHP_WIDTH) / 2.f;';assert a in s
 s=s.replace(a,'        x = xx + (nInfoWidth - (nLength+(potion?1:0)) * FHP_WIDTH) / 2.f;\r\n        if(potion){QuadRect tex={345.f/512.f,356.f/512.f,81.f/512.f,-FHP_DIGIT_HEIGHT};QuadRect pos={x-2,y,FHP_WIDTH*1.5f,FHP_HEIGHT};quads.AddQuad(pos,tex,color,specular);x+=FHP_WIDTH*1.5f;}');return s
edit('Game/FlyingHitPoints.cpp',flying)
def tests(s):
 s=s.replace('pending.command=359','pending.command=384')
 a='            require(JMV_CheckRegisteredLootCodec()';i=s.index(a)
 block='''            {
                std::string value;require(ScriptMessages::Format("A\\r\\nB",value)&&value.find("A\\nB\\n")!=std::string::npos,"Message newline normalization mismatch");require(!ScriptMessages::Format("\\aF1hidden",value)&&!ScriptMessages::Format("   ",value),"Message controls/blank accepted");
                ScriptMessages::Queue queue;int owner=0,other=1;require(queue.Push("early")!=nullptr,"Message accepted without mission");queue.Bind(&owner);for(int i=0;i<64;i++)require(queue.Push("text")==nullptr,"Message queue admission failed");require(queue.Push("overflow")!=nullptr&&queue.Take(&other).empty(),"Message queue capacity/owner mismatch");require(queue.Take(&owner).size()==64,"Message queue drain mismatch");queue.Push("stale");queue.Unbind(&owner);queue.Bind(&other);require(queue.Take(&other).empty(),"Stale mission message leaked");
            }
'''
 s=s[:i]+block.replace('\n','\r\n')+s[i:];s=s.replace('return {{"registeredLootMarkerCodec"','return {{"messageQueueLifecycle",true},{"registeredLootMarkerCodec"');return s
edit('Game/ModRuntime.cpp',tests)
