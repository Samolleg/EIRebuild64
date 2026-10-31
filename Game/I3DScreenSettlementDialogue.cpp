#include "StdAfx.h"
#include "I3DScreenSettlementDialogue.h"
#include "I3DScreenMission.h"
#include "I3DMain.h"
#include "I3DScrollText.h"
#include "WorldInterface.h"
#include "WorldServer.h"
#include "WorldClient.h"
#include "PlayerClient.h"
#include "PlayerServer.h"
#include "UnitClientSpecific.h"
#include "UnitServer.h"
#include "AnimationSystem2.h"
#include "Camera.h"
#include "TextManager.h"
#include "ItemManager.h"
#include "SoundManager.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include "ModMessages.h"
#include "GameJournal.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <set>
#include <stdexcept>

namespace {
constexpr int Rows = 5;
CString Local(const wchar_t *text)
{
    CString result;
    const int n=WideCharToMultiByte(1251,0,text,-1,nullptr,0,nullptr,nullptr);
    WideCharToMultiByte(1251,0,text,-1,result.GetBuffer(n),n,nullptr,nullptr);
    result.ReleaseBuffer(); return result;
}
CString Goodbye() { return Local(L"\u0417\u0430\u043a\u043e\u043d\u0447\u0438\u0442\u044c \u0440\u0430\u0437\u0433\u043e\u0432\u043e\u0440"); }
CString Unavailable() { return Local(L"\u0421\u0435\u0439\u0447\u0430\u0441 \u043f\u043e\u0433\u043e\u0432\u043e\u0440\u0438\u0442\u044c \u043d\u0435\u043b\u044c\u0437\u044f. \u041f\u0440\u043e\u0432\u0435\u0440\u044c\u0442\u0435 \u043f\u043e\u0434\u0445\u043e\u0434 \u043a \u0441\u043e\u0431\u0435\u0441\u0435\u0434\u043d\u0438\u043a\u0443 \u0438 \u0431\u0435\u0437\u043e\u043f\u0430\u0441\u043d\u043e\u0441\u0442\u044c \u0432\u0441\u0435\u0433\u043e \u043e\u0442\u0440\u044f\u0434\u0430."); }
void Notice() { ModMessages::Show(Unavailable()); }
std::string Lower(std::string value)
{
    for (auto &c:value) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
int Integer(const CString &text)
{
    char *end=nullptr; const long value=std::strtol(text,&end,10);
    if (!end || end==static_cast<LPCSTR>(text) || *end || value<0 || value>100000)
        throw std::runtime_error("invalid briefing integer");
    return int(value);
}
float Real(const CString &text)
{
    char *end=nullptr; const float value=std::strtof(text,&end);
    if (!end || end==static_cast<LPCSTR>(text) || *end || !std::isfinite(value))
        throw std::runtime_error("invalid briefing camera coordinate");
    return value;
}
}

struct CI3DScreenSettlementDialogue::Command
{
    enum Kind { Show, Hide, Animation, Camera, NoLips, Phrase } kind;
    std::string name;
    CString words, caption;
    int unit=-1, number=0, slot=-1, shape=-1;
    float angle=0, distance=4, height=1.5f;
    bool customCamera=false, weapon=false;
};

// A presentation owns only client placement, animation playback and camera.
// It never runs a server command, unit Segment, callback, AI or gameplay RNG.
struct CI3DScreenSettlementDialogue::Presentation
{
    struct Actor {
        CMapObjectPtr object;
        std::uint64_t identity;
        CPlot3D position, staged;
        CQuaternion rotation;
        const SADEntry *animation;
        std::vector<const SADEntry *> queue;
        AnimationSwitchCallback callback;
        float time;
        bool mouth, hidden, visible=false, speaking=false;
        int mouthTicks, mouthState;
    };
    std::map<int,Actor> actors;
    CCamera *camera;
    CCamera::CampPlacement placement;
    DWORD flags;
    SCameraMovement<CPlot3D> scroll;
    SCameraMovement<float> rotate,pitch,zoom;
    std::list<SEarthquakeEffect> quakes;
    bool falloutBriefing;
    CCamera::FalloutMode briefingMode;
    CPlot3D left,right,middle;
    int hero,npc,third=-1;
    float elapsed=0;
    int placementIndex;
    float cameraTimeShift;

    Presentation(int mainHero,int target):camera(&theCamera),hero(mainHero),npc(target)
    {
        placement.position=camera->GetCP(); placement.orientation=camera->GetOrientation();
        placement.anchor=camera->ptAnchorPoint; placement.anchorHeight=camera->fAnchorHeight;
        placement.distance=camera->fDistance; placement.orbit=camera->qRotate; placement.pitch=camera->fPitch;
        placement.carrier=camera->dwCarrierID; placement.mode=camera->falloutMode;
        placement.baselineValid=camera->falloutBaselineValid; placement.baseline=camera->falloutBaselineRotation;
        placement.baselinePitch=camera->falloutBaselinePitch; placement.telephoto=camera->falloutTelephotoMultiplier;
        flags=camera->GetFlags(); scroll=camera->scroll; rotate=camera->rotate; pitch=camera->pitch; zoom=camera->zoom;
        placementIndex=camera->nCurrentPlacementIndex; cameraTimeShift=camera->fpCameraTimeShift;
        camera->nCurrentPlacementIndex=-1;
        quakes.swap(camera->earthquakes);
        falloutBriefing=camera->falloutBriefing; briefingMode=camera->falloutBriefingMode;
        camera->SuspendFalloutForBriefing(); camera->SetCarrier(DWORD(-1)); camera->LockCameraControl(true);
        left=theWorld.GetUnit(npc)->GetCP(); right=theWorld.GetUnit(hero)->GetCP();
        const CPlot3D delta=right-left;
        middle=(left+right)*.5f+CPlot3D(-delta.y,delta.x,0)*.5f;
    }
    static CUnit *Live(Actor &actor)
    {
        auto *object=static_cast<CMapObject *>(actor.object);
        return object && object->GetRTID()==actor.identity ? static_cast<CUnit *>(object):nullptr;
    }
    void Add(int id)
    {
        if (actors.count(id)) return;
        auto *unit=theWorld.GetUnit(id);
        if (!unit || !unit->GetAnimationSystem2() || !unit->GetAnimationSystem2()->GetCurrentAnimation())
            throw std::runtime_error("briefing actor is not ready on the client");
        auto *specific=static_cast<CUnitClientSpecific *>(unit->GetUnitSpecific());
        if (specific->dialoguePresentation) throw std::runtime_error("briefing actor already has a presentation");
        auto *animation=unit->GetAnimationSystem2();
        Actor actor{}; actor.object=unit; actor.identity=unit->GetRTID();
        actor.position=actor.staged=unit->GetCP(); actor.rotation=unit->GetOrientation();
        actor.animation=animation->lpADEntry; actor.time=animation->fpTime; actor.callback=animation->SwitchCallback;
        for (POSITION p=animation->entries.GetHeadPosition();p;) actor.queue.push_back(animation->entries.GetNext(p));
        actor.mouth=specific->bMouthSpeakingMode; actor.mouthTicks=specific->nMouthSwitchTicks;
        actor.mouthState=specific->nCurrentMouthState; actor.hidden=specific->bBriefingWeaponForcedHidden;
        actors.emplace(id,std::move(actor));
        animation->SwitchCallback=nullptr; animation->entries.RemoveAll();
        specific->dialoguePresentation=true;
    }
    void Animate(Actor &actor,int special)
    {
        auto *unit=Live(actor); if (!unit) return;
        const SADEntry *entry=unit->GetSpecialAnimationEntry(special);
        if (entry) unit->GetAnimationSystem2()->SetAnimation(entry);
    }
    void Show(int id,int slot,bool weapon)
    {
        auto &actor=actors.at(id); auto *unit=Live(actor); if (!unit) return;
        actor.visible=true;
        if (id!=hero && id!=npc) third=id;
        actor.staged=id==hero?right:id==npc?left:middle;
        if (slot==0) actor.staged=left;
        else if (slot==1) actor.staged=right;
        else if (slot==2) actor.staged=middle;
        unit->SetCP(actor.staged);
        CPlot3D direction=(id==hero?left:right)-actor.staged; direction.z=0;
        if (fabs(direction)>.001f) unit->SetOrientationXY(direction);
        if (ModRuntime::Current() && ModRuntime::Current()->BriefingWeaponsEnabled() && !weapon)
            static_cast<CUnitClientSpecific *>(unit->GetUnitSpecific())->SetBriefingWeaponHidden(true);
        Animate(actor,2);
    }
    void Hide(int id)
    {
        auto &actor=actors.at(id); actor.visible=false;
        if (auto *unit=Live(actor)) unit->SetCP(actor.staged+CPlot3D(0,0,-100));
    }
    void Speak(int id,bool lips,int animation)
    {
        for (auto &entry:actors) {
            auto &actor=entry.second; actor.speaking=entry.first==id && lips;
            if (auto *unit=Live(actor)) {
                auto *specific=static_cast<CUnitClientSpecific *>(unit->GetUnitSpecific());
                specific->MouthClose(true);
                Animate(actor,entry.first==id?(animation>0?animation:1):2);
            }
        }
    }
    void Step(float seconds)
    {
        elapsed+=seconds;
        for (auto &entry:actors) if (auto *unit=Live(entry.second)) {
            unit->GetAnimationSystem2()->Step(seconds*1000.f/DW_SEGMENT_TIME);
            auto *specific=static_cast<CUnitClientSpecific *>(unit->GetUnitSpecific());
            if (entry.second.speaking && (int(elapsed*6)&1)) specific->MouthOpen();
            else specific->MouthClose();
        }
    }
    void Silence()
    {
        for (auto &entry:actors) {
            entry.second.speaking=false;
            if (auto *unit=Live(entry.second)) static_cast<CUnitClientSpecific *>(unit->GetUnitSpecific())->MouthClose(true);
        }
    }
    void Camera(const Command &command,int speaker)
    {
        for (auto &entry:actors) if (auto *unit=Live(entry.second))
            unit->SetCP(entry.second.staged+CPlot3D(0,0,entry.second.visible?0.f:-100.f));
        int shot=command.number;
        if (!shot) shot=speaker==hero?2:speaker==npc?3:4;
        int focus=-1; float angle=0,distance=6,height=1.5f;
        switch (shot) {
        case 1: distance=(std::max)(4.f,fabs(right-left)*3); break;
        case 2: focus=hero; angle=-FP_PI/6; distance=4; break;
        case 3: focus=npc; angle=FP_PI/6; distance=4; break;
        case 4: focus=third; distance=4; break;
        case 5: angle=FP_PI/12; distance=(std::max)(4.f,fabs(right-left)*2.7f); height=3; break;
        case 6: focus=hero; angle=-FP_PI/4; distance=3.5f; break;
        case 7: focus=npc; angle=FP_PI/4; distance=3.5f; break;
        case 8: focus=third; angle=FP_PI/12; distance=3.5f; break;
        case 9: angle=-FP_PI/12; distance=(std::max)(4.f,fabs(right-left)*2.4f); height=3; break;
        case 10: focus=hero; angle=-FP_PI/12; distance=3; height=1; break;
        case 11: focus=npc; angle=FP_PI/12; distance=3; height=1; break;
        case 12: focus=third; angle=-FP_PI/12; distance=3; height=1; break;
        }
        if ((shot==4 || shot==8 || shot==12) && focus<0) throw std::runtime_error("camera requires a third participant");
        if (command.customCamera) { angle=command.angle; distance=command.distance; height=command.height; }
        CPlot3D to=(left+right)*.5f,dir(left.y-right.y,right.x-left.x,0);
        const auto at=actors.find(focus);
        if (at!=actors.end()) {
            to=at->second.staged;
            if (auto *unit=Live(at->second)) {
                dir=-unit->GetYAxis();
                to+=dir*unit->GetPrototype()->fDialogCameraDistance;
                to.z+=unit->GetPrototype()->fDialogCameraHeight;
            }
        }
        if (fabs(dir)<.001f) dir=CPlot3D(0,-1,0);
        dir.z=0; dir.Normalize();
        CPlot3D from(std::cos(angle)*dir.x+std::sin(angle)*dir.y,-std::sin(angle)*dir.x+std::cos(angle)*dir.y,0);
        from=to+from*distance+CPlot3D(0,0,height);
        camera->MoveTo(from,to);
    }
    ~Presentation() noexcept
    {
        for (auto &entry:actors) if (auto *unit=Live(entry.second)) {
            const auto &actor=entry.second;
            auto *specific=static_cast<CUnitClientSpecific *>(unit->GetUnitSpecific());
            specific->dialoguePresentation=false;
            unit->SetCP(actor.position); unit->SetOrientation(actor.rotation);
            try {
            auto *animation=unit->GetAnimationSystem2();
            animation->SwitchCallback=nullptr; animation->entries.RemoveAll();
            animation->SetAnimation(actor.animation); animation->SetTime(actor.time);
            for (const auto *queued:actor.queue) animation->entries.AddTail(queued);
            animation->SwitchCallback=actor.callback;
            specific->SetBriefingWeaponHidden(actor.hidden);
            specific->bMouthSpeakingMode=actor.mouth; specific->nMouthSwitchTicks=actor.mouthTicks;
            if (actor.mouthState) specific->MouthOpen(true); else specific->MouthClose(true);
            } catch (CException *error) { error->Delete(); GameJournal::AllocationFailureInCatch("Dialogue playback restoration"); }
              catch (...) { GameJournal::AllocationFailureInCatch("Dialogue playback restoration"); }
        }
        camera->SetCP(placement.position); camera->SetOrientation(placement.orientation);
        camera->ptAnchorPoint=placement.anchor; camera->fAnchorHeight=placement.anchorHeight;
        camera->fDistance=placement.distance; camera->qRotate=placement.orbit; camera->fPitch=placement.pitch;
        camera->dwCarrierID=placement.carrier; camera->falloutMode=placement.mode;
        camera->falloutBaselineValid=placement.baselineValid; camera->falloutBaselineRotation=placement.baseline;
        camera->falloutBaselinePitch=placement.baselinePitch; camera->falloutTelephotoMultiplier=placement.telephoto;
        camera->falloutBriefing=falloutBriefing; camera->falloutBriefingMode=briefingMode;
        camera->scroll=scroll; camera->rotate=rotate; camera->pitch=pitch; camera->zoom=zoom;
        camera->earthquakes.swap(quakes);
        camera->nCurrentPlacementIndex=placementIndex; camera->fpCameraTimeShift=cameraTimeShift;
        camera->ResetAllFlags(); camera->SetFlag(flags);
    }
};

CI3DScreenSettlementDialogue::CI3DScreenSettlementDialogue(CInterface3D *ui,int npc,std::string topic)
    : CI3DScreen(ui),target(npc),forced(std::move(topic)) {}
CI3DScreenSettlementDialogue::~CI3DScreenSettlementDialogue() = default;

void CI3DScreenSettlementDialogue::OnActivate()
{
    auto *back=AddFigure(); back->MakeBar(20,100,145,406,655,590);
    back->SetTexture(nullptr); back->UseDefaultColors(TRUE); back->SetDefaultColor(RGBA_MAKE(0,0,0,175));
    AddFigure()->MakeBarFrame(10,100,140,401,660,595,5,2,254,180,249);
    AddSurface(dbgnew CI3DSurface(pI3D,150,410,650,580));
    AddObject(topicScroll=dbgnew CI3DScrollBar(pI3D,CRect(632,426,642,564),3));
    AddObject(text=dbgnew CI3DScrollText(pI3D,"",CRect(160,418,635,575)));
    text->Show(FALSE);
    for (int i=0;i<Rows;++i) AddArea()->MakeRect(160,420+i*28,624,447+i*28);
    try {
        world=&theWorldServer;
        token=world->Settlements().BeginDialogue(*world,0,target);
        if (!token) { Fail("opening conditions changed"); return; }
        theWorld.SetCombatPause(true);
        const auto &session=world->Settlements().Dialogue();
        presentation=std::make_unique<Presentation>(session.hero,session.target);
        topics=world->Settlements().DialogueTopics(*world,0,target);
        for (const auto &topic:topics) {
            CString caption;
            if (theWorld.GetTextManager()->GetText(CString("briefing ")+topic.binding.text.c_str()))
                theWorld.GetTextManager()->GetLine(caption);
            if (caption.IsEmpty()) throw std::runtime_error("missing briefing title");
            labels.push_back(caption);
        }
        topicScroll->SetRange((std::max)(0,int(topics.size())+1-Rows)); topicScroll->SetPos(0);
        RedrawTopics();
        if (!forced.empty()) {
            auto found=std::find_if(topics.begin(),topics.end(),[&](const auto &topic){return topic.key==forced;});
            if (found==topics.end()) throw std::runtime_error("forced topic is unavailable");
            SelectTopic(int(found-topics.begin()));
        }
    } catch (const std::exception &error) { Fail(error.what()); }
}
void CI3DScreenSettlementDialogue::StopVoice() noexcept
{
    const auto sound=voice; voice=SND_EMPTY; speaker=-1;
    try {
        if (sound!=SND_EMPTY) theWorld.GetSoundManager()->CancelFX(sound);
        if (presentation) presentation->Silence();
    } catch (CException *error) { error->Delete(); GameJournal::AllocationFailureInCatch("Dialogue audio cleanup"); }
      catch (...) { GameJournal::AllocationFailureInCatch("Dialogue audio cleanup"); }
}
void CI3DScreenSettlementDialogue::OnDeactivate()
{
    StopVoice(); presentation.reset();
    if (world) world->Settlements().EndDialogue(token);
    token=0; world=nullptr;
}
void CI3DScreenSettlementDialogue::Fail(const char *detail)
{
    StopVoice(); presentation.reset(); phase=Phase::Error;
    GameJournal::Event("SETTLEMENT","dialogue_error","target=%d detail=%s",target,GameJournal::Quote(detail).c_str());
    topicScroll->Show(FALSE); surfaces[0]->Show(FALSE); text->Show(TRUE); text->SetText(Unavailable());
}
void CI3DScreenSettlementDialogue::RedrawTopics()
{
    auto *surface=surfaces[0]; surface->GetDC(); surface->Clear();
    for (int i=0;i<Rows && i+offset<=int(topics.size());++i)
        surface->DrawText(i+offset==int(topics.size())?Goodbye():labels[i+offset],CRect(12,10+i*28,472,37+i*28),1,RGB(228,215,167),DT_LEFT);
    surface->ReleaseDC();
}

void CI3DScreenSettlementDialogue::ReadProgram(const Settlement::DialogueTopic &topic)
{
    commands.clear(); auto *manager=theWorld.GetTextManager();
    if (!manager->GetText(CString("briefing ")+topic.binding.text.c_str())) throw std::runtime_error("missing briefing text");
    CStringArray words; std::set<int> participants; bool phrase=false;
    while (manager->FindNextCommand()) {
        manager->GetStringArray(words);
        if (!words.GetSize()) continue;
        if (commands.size()>=4096) throw std::runtime_error("briefing is too large");
        Command command{};
        const auto name=Lower(static_cast<LPCSTR>(words[0]));
        if (name=="#nolips") {
            if (words.GetSize()!=1) throw std::runtime_error("invalid nolips command");
            command.kind=Command::NoLips;
        } else if (name=="#animation") {
            if (words.GetSize()!=2) throw std::runtime_error("invalid animation command");
            command.kind=Command::Animation; command.number=Integer(words[1]);
        } else if (name=="#camera") {
            if (words.GetSize()!=2 && words.GetSize()!=5) throw std::runtime_error("invalid camera command");
            command.kind=Command::Camera; command.number=Integer(words[1]);
            if (command.number<1 || command.number>12) throw std::runtime_error("unsupported camera shot");
            if (words.GetSize()==5) {
                command.customCamera=true; command.angle=ToRadian(Real(words[2]));
                command.distance=Real(words[3]); command.height=Real(words[4]);
                if (command.distance<.1f || command.distance>200 || std::abs(command.height)>100) throw std::runtime_error("camera limits exceeded");
            }
        } else if (name=="#show" || name=="#hide" || name=="#phrase") {
            if (words.GetSize()<2 || words.GetSize()>4 || (name=="#phrase" && words.GetSize()>3)) throw std::runtime_error("invalid actor command");
            command.kind=name=="#show"?Command::Show:name=="#hide"?Command::Hide:Command::Phrase;
            command.name=static_cast<LPCSTR>(words[1]);
            try { command.unit=world->Settlements().Object(*world,world->Settlements().Dialogue().settlement,command.name,true); }
            catch (const std::exception &) { command.unit=-1; }
            if (command.unit>=0) {
                auto *unit=world->GetUnit(command.unit);
                if (!unit || unit->IsDead() || (command.unit!=world->Settlements().Dialogue().hero &&
                    world->Settlements().ModeFor(*world,unit)!=Settlement::Mode::Peaceful)) throw std::runtime_error("briefing actor is not available in the settlement");
                participants.insert(command.unit);
            } else {
                if (command.kind==Command::Phrase) throw std::runtime_error("unbound briefing speaker");
                auto prop=command.name; std::replace(prop.begin(),prop.end(),'_',' '); prop=Lower(prop);
                const auto *shape=theWorld.GetItemManager()->GetQuestItemShape(prop.c_str());
                if (!shape) throw std::runtime_error("unbound briefing actor or prop");
                command.shape=shape->dwTextureTypeIndex;
            }
            if (command.kind==Command::Phrase) {
                if (words.GetSize()==3) command.number=Integer(words[2]);
                manager->GetUntilNextCommand(command.words);
                command.caption=theWorld.GetUnit(command.unit)?theWorld.GetUnit(command.unit)->GetName():CString();
                phrase=true;
            } else if (words.GetSize()>=3) {
                if (command.kind==Command::Hide) {
                    if (Lower(static_cast<LPCSTR>(words[2]))!="run") throw std::runtime_error("unsupported hide modifier");
                } else {
                    command.slot=Integer(words[2])-1;
                    if (command.slot<0 || command.slot>20) throw std::runtime_error("invalid staging slot");
                    if (words.GetSize()==4) {
                        if (Lower(static_cast<LPCSTR>(words[3]))!="weapon") throw std::runtime_error("unsupported show modifier");
                        command.weapon=true;
                    }
                }
            }
        } else throw std::runtime_error("unsupported briefing command: "+name);
        commands.push_back(std::move(command));
    }
    if (!phrase) throw std::runtime_error("briefing has no phrases");
    int special=0;
    for (const auto &command:commands) {
        if (command.kind==Command::Animation) special=command.number;
        if (command.kind==Command::Phrase) {
            auto *unit=theWorld.GetUnit(command.unit);
            if (special && (!unit || !unit->GetSpecialAnimationEntry(special))) throw std::runtime_error("missing actor animation");
            special=0;
        }
    }
    for (auto &command:commands) if (command.kind==Command::Phrase && command.caption.IsEmpty()) {
        Settlement::Name qualified;
        const auto local=Settlement::ParseName(command.name,qualified)?qualified.local:command.name;
        if (manager->GetText(CString("pers ")+local.c_str())) manager->GetLine(command.caption);
        if (command.caption.IsEmpty()) command.caption=Local(L"\u0421\u043e\u0431\u0435\u0441\u0435\u0434\u043d\u0438\u043a");
    }
    for (int unit:participants) presentation->Add(unit);
}

void CI3DScreenSettlementDialogue::SelectTopic(int index)
{
    if (index<0 || index>=int(topics.size())) { phase=Phase::Closing; pI3D->ExitFromModal(); return; }
    try {
        ReadProgram(topics[index]);
        if (!world->Settlements().SelectDialogueTopic(*world,token,topics[index].key)) throw std::runtime_error("topic conditions changed");
        resource=topics[index].binding.text; next=0; phase=Phase::Phrase;
        topicScroll->Show(FALSE); surfaces[0]->Show(FALSE); text->Show(TRUE); text->SetText("");
        Advance();
    } catch (const std::exception &error) { Fail(error.what()); }
}
void CI3DScreenSettlementDialogue::Advance()
{
    StopVoice(); int animation=0; Command camera{}; camera.kind=Command::Camera; lips=true;
    while (next<commands.size()) {
        const auto &command=commands[next++];
        switch (command.kind) {
        case Command::NoLips: lips=false; break;
        case Command::Animation: animation=command.number; break;
        case Command::Camera: camera=command; break;
        case Command::Show:
            if (command.unit>=0) presentation->Show(command.unit,command.slot,command.weapon); else ShowProp(command);
            break;
        case Command::Hide:
            if (command.unit>=0) presentation->Hide(command.unit); else HideProp(command.name);
            break;
        case Command::Phrase: {
            CString caption=command.caption;
            if (caption.IsEmpty()) caption=Local(L"\u0421\u043e\u0431\u0435\u0441\u0435\u0434\u043d\u0438\u043a");
            CString line=CString("\aC\xff\xb3\x31 ")+caption+":\n\aC\xe4\xd7\xa7 "+command.words+"\n";
            text->AddText(line); text->SetPosToEnd();
            presentation->Camera(camera,command.unit); presentation->Speak(command.unit,lips,animation); speaker=command.unit;
            if (command.number>0) {
                CString path; path.Format("briefing\\%s\\%d.mp3",resource.c_str(),command.number);
                voice=theWorld.GetSoundManager()->BeginMP3Dialog(path,256,100);
            }
            phraseTime=(std::max)(1.f,command.words.GetLength()/20.f);
            phase=std::any_of(commands.begin()+next,commands.end(),[](const auto &c){return c.kind==Command::Phrase;})?Phase::Phrase:Phase::Summary;
            return;
        }
        }
    }
    phase=Phase::Summary;
}
void CI3DScreenSettlementDialogue::Skip()
{
    StopVoice();
    while (next<commands.size()) {
        const auto &command=commands[next++];
        if (command.kind==Command::Phrase) text->AddText(command.caption+":\n"+command.words+"\n");
    }
    text->SetPosToEnd(); phase=Phase::Summary;
}
void CI3DScreenSettlementDialogue::Confirm()
{
    if (phase==Phase::Phrase) { Advance(); return; }
    if (phase==Phase::Summary) {
        if (next<commands.size()) Advance();
        if (!world->Settlements().FinishDialogue(*world,token)) { Fail("completion rejected"); return; }
    }
    phase=Phase::Closing; pI3D->ExitFromModal();
}
void CI3DScreenSettlementDialogue::ShowProp(const Command &command)
{
    HideProp(command.name);
    if (command.slot<0) throw std::runtime_error("prop requires a display slot");
    const int x[]={300,500,200,400,600,75,725}, y[]={300,300,300,300,300,500,500};
    const int slot=command.slot%7;
    Prop prop{command.name,nullptr,nullptr,nullptr,nullptr};
    CString figure,texture; figure.Format("initqu%ditem",command.shape); texture.Format("quitem%04d",command.shape);
    prop.figure=AddFigure(figure);
    if (!prop.figure) throw std::runtime_error("missing briefing prop figure");
    prop.texture=AddTexture(256,256);
    PasteTexture(prop.texture,texture,64,64,64,64,0,192);
    prop.figure->SetTexture(prop.texture); prop.figure->SetRelativeToScreen(x[slot],y[slot],12);
    prop.figure->SetOrientation(CQuaternion(CPlot3D(1,0,0),FP_PI));
    props.push_back(prop);
}
void CI3DScreenSettlementDialogue::HideProp(const std::string &name)
{
    for (auto p=props.begin();p!=props.end();++p) if (p->name==name) {
        RemoveFigure(p->figure); RemoveTexture(p->texture); props.erase(p); break;
    }
}
BOOL CI3DScreenSettlementDialogue::Draw(CRenderer *renderer)
{
    IWorldClient.Draw(); renderer->Clear(0,0,D3DCLEAR_ZBUFFER,0,1,0);
    return CI3DScreen::Draw(renderer);
}
void CI3DScreenSettlementDialogue::OnIdle()
{
    CI3DScreen::OnIdle();
    if (phase==Phase::Error || phase==Phase::Closing) return;
    try {
        if (&theWorldServer!=world || !world->Settlements().DialogueValid(*world,token)) { Fail("session invalidated"); return; }
        if (phase==Phase::Topics) {
            int position; if (topicScroll->GetPos(position)) { offset=position; RedrawTopics(); }
        }
        const float seconds=(std::max)(0.f,(std::min)(.2f,GetFrameTime()));
        if (presentation) presentation->Step(seconds);
        phraseTime-=seconds;
        const bool done=voice!=SND_EMPTY?!theWorld.GetSoundManager()->IsFXValid(voice):phraseTime<=0;
        if (done && phase==Phase::Phrase) Advance();
        else if (done && phase==Phase::Summary && next<commands.size()) Advance();
        else if (done && speaker>=0) StopVoice();
    } catch (const std::exception &error) { Fail(error.what()); }
}
void CI3DScreenSettlementDialogue::OnLButtonDown(UINT flags,CPoint point)
{
    CI3DScreen::OnLButtonDown(flags,point); if (GetProcessed()) return;
    if (phase==Phase::Topics) { const int hit=HitTest(point); if (hit>=0 && hit<Rows) SelectTopic(offset+hit); }
    else Confirm();
    SetProcessed();
}
void CI3DScreenSettlementDialogue::OnKeyDown(UINT key,UINT repeat,UINT flags)
{
    if (flags&0x4000) { SetProcessed(); return; }
    if (key==VK_ESCAPE || key==VK_RETURN) {
        if (phase==Phase::Phrase) Skip();
        else if (phase==Phase::Topics || phase==Phase::Error) { phase=Phase::Closing; pI3D->ExitFromModal(); }
        else Confirm();
    } else if (key==VK_SPACE && phase!=Phase::Topics) Confirm();
    SetProcessed();
}

bool CI3DScreenMission::RequestSettlementDialogue(int target,const std::string &topic)
{
    try {
    if (!theWorldServer.Settlements().DialogueAvailable(theWorldServer,0,target)) return false;
    auto *unit=theWorldServer.GetUnit(target);
    settlementTalkTarget=target; settlementTalkIdentity=unit->GetRTID();
    settlementTalkStart=theWorldServer.GetSegmentTimeCounter(); settlementTalkMove=-1; settlementForcedTopic=topic;
    UpdateSettlementDialogue(); return true;
    } catch (const std::exception &error) { GameJournal::Event("SETTLEMENT","dialogue_request_error","detail=%s",GameJournal::Quote(error.what()).c_str()); Notice(); return true; }
}
bool CI3DScreenMission::UpdateSettlementDialogue()
{
    if (settlementTalkTarget<0) return false;
    auto *npc=theWorldServer.GetUnit(settlementTalkTarget);
    auto *player=theWorldServer.GetPlayer(0);
    auto *hero=player && player->party.people.GetSize()?theWorldServer.GetUnit(player->party.people[0].nID):nullptr;
    const int now=theWorldServer.GetSegmentTimeCounter();
    if (!npc || npc->GetRTID()!=settlementTalkIdentity || !hero || hero->IsDead() || hero->CheckFlag(UNIT_BLOCKED) ||
        !theWorldServer.Settlements().DialogueAvailable(theWorldServer,0,settlementTalkTarget) || now-settlementTalkStart>30000/int(DW_SEGMENT_TIME)) {
        settlementTalkTarget=-1; Notice(); return false;
    }
    if (distanceXY2(hero->GetCP(),npc->GetCP())<=16) {
        const int target=settlementTalkTarget; const auto topic=settlementForcedTopic; settlementTalkTarget=-1;
        if (!theWorldServer.Settlements().DialogueReady(theWorldServer,0,target)) { Notice(); return false; }
        // DoModal captures the user's pause BEFORE BeginDialogue takes its lock.
        pI3D->DoModal(dbgnew CI3DScreenSettlementDialogue(pI3D,target,topic),false,false);
        return true;
    }
    if (IWorld.IsCombatPause()) { settlementTalkTarget=-1; Notice(); return false; }
    if (settlementTalkMove<0 || now-settlementTalkMove>=2000/int(DW_SEGMENT_TIME)) {
        CPlot3D direction=hero->GetCP()-npc->GetCP(); direction.z=0;
        if (fabs(direction)<.001f) direction=CPlot3D(0,1,0); else direction.Normalize();
        CPlot3D destination=npc->GetCP()+direction*(hero->GetUnitRadius()+npc->GetUnitRadius()+.6f);
        CArray<CUnit *,CUnit *> units;
        if (auto *client=theWorld.GetUnit(hero->GetID())) units.Add(client);
        theWorld.GetPlayer()->UnitCommandMoveTo(units,destination,FALSE); settlementTalkMove=now;
    }
    return false;
}
void CI3DScreenMission::ForceBriefing(LPCTSTR briefing)
{
    try {
    auto *player=theWorldServer.GetPlayer(0); if (!player) return;
    std::string key; int npc=-1;
    for (const auto &var:player->party.vars) {
        if (var.second!=1 || var.first.compare(0,2,"b.")!=0) continue;
        const auto dot=var.first.find('.',2);
        if (dot==std::string::npos || (var.first!=briefing && var.first.substr(dot+1)!=briefing)) continue;
        Settlement::Name name;
        if (!Settlement::ParseName(var.first.substr(dot+1),name) || !theWorldServer.Settlements().Contains(name.settlement)) continue;
        if (!key.empty()) { Notice(); return; }
        key=var.first; npc=theWorldServer.Settlements().Object(theWorldServer,0,var.first.substr(2,dot-2),true);
    }
    if (!key.empty() && !RequestSettlementDialogue(npc,key)) Notice();
    } catch (const std::exception &error) { GameJournal::Event("SETTLEMENT","dialogue_force_error","detail=%s",GameJournal::Quote(error.what()).c_str()); Notice(); }
}
