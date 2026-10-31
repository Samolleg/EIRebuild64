#include "StdAfx.h"
#include "SettlementMob.h"
#include "WorldServer.h"
#include "PlayerServer.h"
#include "UnitServer.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include "ModSpellTargeting.h"
#include "GameJournal.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace Settlement {
namespace {
float Cross(Point a, Point b, Point c)
{
    return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
}
float Distance2(Point a, Point b)
{
    return (a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y);
}
Point XY(const CPlot3D &p) { return {p.x,p.y}; }
// Registrations use a convex, counterclockwise contour. The margin expands
// only an existing membership, so entering never reaches through a wall.
bool InArea(const PeaceArea &area, const CPlot3D &p, float margin = 0)
{
    if (!area.Enabled() || p.z < area.minZ-margin || p.z > area.maxZ+margin) return false;
    for (std::size_t i=0;i<area.polygon.size();++i) {
        const auto a=area.polygon[i], b=area.polygon[(i+1)%area.polygon.size()];
        if (Cross(a,b,XY(p)) < -margin*std::sqrt(Distance2(a,b))) return false;
    }
    return true;
}
bool NearGate(const PeaceArea &area, const CPlot3D &a, const CPlot3D &b)
{
    if (a.z<area.minZ-area.hysteresis || a.z>area.maxZ+area.hysteresis ||
        b.z<area.minZ-area.hysteresis || b.z>area.maxZ+area.hysteresis) return false;
    const auto p=area.gateA, q=area.gateB;
    const float length2=Distance2(p,q);
    auto nearGatePoint = [&](const CPlot3D &v) {
        const float t=((v.x-p.x)*(q.x-p.x)+(v.y-p.y)*(q.y-p.y))/length2;
        return t>=0 && t<=1 && std::abs(Cross(p,q,XY(v))) <= (area.hysteresis+1)*std::sqrt(length2);
    };
    return nearGatePoint(a) || nearGatePoint(b) ||
        (Cross(p,q,XY(a))*Cross(p,q,XY(b))<=0 &&
         Cross(XY(a),XY(b),p)*Cross(XY(a),XY(b),q)<=0);
}
CUnit *Unit(CMapObject *object)
{
    return object && object->WhoAmI()==HPC_UNIT ? static_cast<CUnit *>(object) : nullptr;
}
CUnitServer *Server(CUnit *unit) { return static_cast<CUnitServer *>(unit->GetUnitSpecific()); }
bool Hostile(CUnit *a, CUnit *b) { return a->IsEnemy(b) || b->IsEnemy(a); }
bool OffensiveEffect(int kind)
{
    auto *mod=ModRuntime::Current();
    return mod && ModSpellTargeting::Offensive(kind,mod->CombatSetting("JMVSelfCast")!=0,
        mod->CombatSetting("JMVParticles")!=0,&mod->SpellAddon());
}
float Radius(CSpell *spell)
{
    if (!spell) return 0;
    float radius=spell->GetRadius();
    ModSkills::Config skill;
    if (auto *mod=ModRuntime::Current(); mod && mod->SkillConfig(spell,skill))
        radius=(std::max)(radius,skill.areaRadius);
    return (std::max)(0.f,radius);
}
bool Offensive(CSpell *spell)
{
    if (!spell) return false;
    ModSkills::Config skill;
    auto *mod=ModRuntime::Current();
    return spell->IsOffensive() || (mod && mod->SkillConfig(spell,skill));
}
bool Reaches(CUnit *unit, const CPlot3D &point, float radius)
{
    return distanceXY2(unit->GetCP(),point) <= sqr(radius+unit->GetUnitRadius()) &&
        std::abs(unit->GetCP().z-point.z) <= radius+unit->GetUnitHeight();
}
}

void ValidatePeaceArea(PeaceArea &area)
{
    auto fail=[] { throw std::runtime_error("Settlement peace area: invalid contour, gate or limits"); };
    const auto finite=[](float n) { return std::isfinite(n); };
    if (area.polygon.size()<3 || area.polygon.size()>128 || !finite(area.minZ) || !finite(area.maxZ) ||
        area.minZ>=area.maxZ || !finite(area.hysteresis) || area.hysteresis<0 || area.hysteresis>2 ||
        !finite(area.quietSeconds) || area.quietSeconds<0 || area.quietSeconds>3600 ||
        area.faction<0 || area.faction>=N_MAX_PLAYERS || area.gateObject<0 || area.exit<0 ||
        area.alarmKey.empty() || area.legacyZone.empty() || area.legacyEntrances.empty()) fail();
    area.center={};
    for (const auto &p:area.polygon) {
        if (!finite(p.x) || !finite(p.y) || std::abs(p.x)>100000 || std::abs(p.y)>100000) fail();
        area.center.x+=p.x; area.center.y+=p.y;
    }
    area.center.x/=float(area.polygon.size()); area.center.y/=float(area.polygon.size());
    area.radius=0;
    for (std::size_t i=0;i<area.polygon.size();++i) {
        const auto a=area.polygon[i], b=area.polygon[(i+1)%area.polygon.size()];
        if (Distance2(a,b)<.0001f) fail();
        for (const auto &p:area.polygon) if (Cross(a,b,p)<-.001f) fail();
        if (Cross(a,b,area.center)<=.001f) fail();
        area.radius=(std::max)(area.radius,std::sqrt(Distance2(area.center,a)));
    }
    for (auto p:{area.gateA,area.gateB,area.inside}) if (!finite(p.x) || !finite(p.y)) fail();
    if (Distance2(area.gateA,area.gateB)<1 || Distance2(area.gateA,area.gateB)>400 ||
        !InArea(area,CPlot3D(area.inside.x,area.inside.y,(area.minZ+area.maxZ)*.5f)) ||
        std::abs(Cross(area.gateA,area.gateB,area.inside))<.01f) fail();
}

std::uint32_t World::AreaAt(const CPlot3D &point, std::uint32_t previous) const
{
    if (previous && Contains(previous)) {
        const auto &area=GetDefinition(previous).peace;
        if (InArea(area,point,area.hysteresis)) return previous;
    }
    for (const auto &entry:instances)
        if (InArea(GetDefinition(entry.first).peace,point)) return entry.first;
    return 0;
}

void World::InitializePeace(CWorldServer &world)
{
    for (const auto &entry:instances) {
        const auto &area=GetDefinition(entry.first).peace;
        if (!area.Enabled()) continue;
        for (const auto &p:area.polygon)
            if (!world.aiMap.TileInBounds(Float2AITile(p.x),Float2AITile(p.y)))
                throw std::runtime_error("Settlement peace contour is outside the loaded map");
        const auto *gate=world.GetMapObject(area.gateObject);
        if (!gate || area.exit>=world.exits.GetSize())
            throw std::runtime_error("Settlement gate/exit is missing on this map");
        auto &exit=world.exits[area.exit];
        const auto midpoint=Point{(area.gateA.x+area.gateB.x)*.5f,(area.gateA.y+area.gateB.y)*.5f};
        if (Distance2(XY(gate->GetCP()),midpoint)>4 ||
            !exit.IsIn(gate->GetCP().x,gate->GetCP().y) ||
            exit.szZone.CompareNoCase(area.legacyZone.c_str())!=0 ||
            std::find(area.legacyEntrances.begin(),area.legacyEntrances.end(),exit.nZoneEntrance)==area.legacyEntrances.end())
            throw std::runtime_error("Settlement gate does not match the registered local exit");
        for (const auto &other:instances) {
            if (other.first>=entry.first) continue;
            const auto &b=GetDefinition(other.first).peace;
            if (!b.Enabled()) continue;
            if (b.exit==area.exit || (b.legacyZone==area.legacyZone && b.legacyEntrances==area.legacyEntrances))
                throw std::runtime_error("Settlement local exit is registered twice");
            // Separating axis theorem for the two validated convex contours.
            bool separate=area.maxZ<b.minZ || b.maxZ<area.minZ;
            for (const auto *a:{&area,&b}) for (std::size_t i=0;i<a->polygon.size();++i) {
                const auto p=a->polygon[i],q=a->polygon[(i+1)%a->polygon.size()];
                const auto &points=a==&area?b.polygon:area.polygon;
                if (std::all_of(points.begin(),points.end(),[&](Point v){return Cross(p,q,v)<0;})) separate=true;
            }
            if (!separate) throw std::runtime_error("Settlement peace areas overlap");
        }
    }
}

bool World::LocalExit(int exit) const
{
    for (const auto &entry:instances) {
        const auto &area=GetDefinition(entry.first).peace;
        if (area.Enabled() && area.exit==exit) return true;
    }
    return false;
}
bool World::LocalTravel(const std::string &zone, int entrance) const
{
    for (const auto &entry:instances) {
        const auto &area=GetDefinition(entry.first).peace;
        if (area.Enabled() && _stricmp(area.legacyZone.c_str(),zone.c_str())==0 &&
            std::find(area.legacyEntrances.begin(),area.legacyEntrances.end(),entrance)!=area.legacyEntrances.end()) return true;
    }
    return false;
}

Mode World::ModeFor(CWorldServer &world, CUnit *unit) const
{
    if (!unit || unit->IsDead()) return Mode::Outside;
    auto found=visitors.find(unit->GetID());
    const auto old=found!=visitors.end() && found->second.identity==unit->GetRTID()?found->second.settlement:0;
    const auto id=AreaAt(unit->GetCP(),old);
    if (!id) return Mode::Outside;
    if (unit->IsEnemy(GetDefinition(id).peace.faction)) return Mode::Hostile;
    return instances.at(id).alarm ? Mode::Alarm : Mode::Peaceful;
}

bool World::Harmful(CUnit *unit, const SUnitBasicCmd &command) const
{
    if (command.IsAttack()) return !command.pTarget.IsEmpty() && !command.pTarget->IsDead();
    if (command.IsCast()) return Offensive(Server(unit)->GetSpell(command));
    return command.cmd==UC_TRAP || (command.IsSkill() &&
        (command.nQSpellNum==RPGMSKL_STEALING || command.nQSpellNum==RPGMSKL_TAME));
}
bool World::Unsafe(CWorldServer &world, CUnit *unit) const
{
    if (!unit || unit->IsDead()) return false;
    auto *server=Server(unit);
    if (Harmful(unit,server->curCommand) || Harmful(unit,server->nextCommand)) return true;
    const auto &effects=unit->GetSpellEffects();
    for (INT_PTR i=0;i<effects.GetSize();++i)
        if (effects[i].nDuration>0 && OffensiveEffect(effects[i].nSpellID)) return true;
    const auto &neighbors=server->logic.environment.units;
    for (POSITION p=neighbors.GetHeadPosition();p;) {
        auto *enemy=Unit(neighbors.GetNext(p));
        if (!enemy || enemy->IsDead() || !Hostile(unit,enemy)) continue;
        auto *ai=Server(enemy);
        if (ai->logic.GetAggressionTarget()==unit) return true;
        for (const auto *cmd:{&ai->curCommand,&ai->nextCommand}) {
            if (!Harmful(enemy,*cmd)) continue;
            if (cmd->pTarget==unit || (cmd->IsCast() && Reaches(unit,cmd->GetTargetCP(),Radius(ai->GetSpell(*cmd))))) return true;
        }
    }
    // These are observations only: an arrow/effect already released is never
    // removed, retargeted or neutralized by crossing a settlement boundary.
    for (const auto &entry:world.effects) {
        const auto *arrow=dynamic_cast<CEffectArrowBase *>(entry.second);
        if (!arrow || arrow->bDelivered) continue;
        if (arrow->nTargetID==unit->GetID()) return true;
        const auto *explosive=dynamic_cast<CEffectArrowFigure *>(entry.second);
        if (explosive && explosive->explosivePending && Reaches(unit,explosive->explosivePoint,explosive->explosiveRadius)) return true;
    }
    for (POSITION p=world.spells.GetHeadPosition();p;) {
        CEffectSpell *spell=world.spells.GetNext(p);
        if (spell->castPhase==CP_FINISH || !Offensive(&spell->spell)) continue;
        auto *caster=Unit(spell->pWho);
        if (spell->pTarget==unit || (Radius(&spell->spell)>0 && Reaches(unit,spell->ptTarget,Radius(&spell->spell)) &&
            (!caster || spell->spell.IsValidTarget(caster,unit)))) return true;
    }
    return false;
}

bool World::CanInteract(CWorldServer &world, int player, std::uint32_t id, CUnit *npc) const
{
    if (!Contains(id) || !GetDefinition(id).peace.Enabled() || instances.at(id).alarm) return false;
    auto *owner=world.GetPlayer(player);
    if (!owner) return false;
    if (npc && (npc->IsDead() || AreaAt(npc->GetCP())!=id || npc->IsEnemy(player) || Unsafe(world,npc))) return false;
    bool present=false;
    std::set<int> seen;
    auto member=[&](CUnit *unit) {
        if (!unit || unit->IsDead() || !seen.insert(unit->GetID()).second) return true;
        if (Unsafe(world,unit)) return false;
        if (AreaAt(unit->GetCP())==id && !unit->IsEnemy(GetDefinition(id).peace.faction) &&
            (!npc || (!Hostile(unit,npc) && distanceXY2(unit->GetCP(),npc->GetCP())<=16))) present=true;
        return true;
    };
    for (const auto &entry:owner->unitsUnderControl) if (!member(entry.second)) return false;
    for (int i=0;i<owner->party.people.GetSize();++i) if (!member(world.GetUnit(owner->party.people[i].nID))) return false;
    return present;
}

void World::Event(CWorldServer &, std::uint32_t id, const char *name, int a, int b, int c)
{
    GameJournal::Event("SETTLEMENT",name,"namespace=%s player=%d member=%d value=%d",Prefix(id).c_str(),a,b,c);
}
void World::Publish(CWorldServer &world, std::uint32_t id)
{
    auto *player=world.GetPlayer(0);
    if (!player) return;
    const auto &state=instances.at(id);
    int inside=0;
    for (const auto &entry:visitors)
        if (entry.second.settlement==id) ++inside;
    auto write=[&](const char *name,float value) {
        const auto key=Prefix(id)+name;
        if (player->party.vars.GetVar(key.c_str())!=value)
            static_cast<CPlayerServer *>(player->GetPlayerSpecific())->SetVar(key.c_str(),value);
    };
    write("AlarmActive",state.alarm?1.f:0.f);
    write("AlarmReasons",float(state.reasons));
    write("Inside",float(inside));
    write("Mode",float(inside ? (state.alarm?Mode::Alarm:Mode::Peaceful) : Mode::Outside));
}
void World::Incident(CWorldServer &world, std::uint32_t id, std::uint32_t reason)
{
    if (!id) return;
    auto &state=instances.at(id);
    const bool changed=!state.alarm || (state.reasons&reason)!=reason;
    state.alarm=true; state.reasons|=reason; state.lastIncident=world.GetSegmentTimeCounter();
    if (changed) { Event(world,id,"alarm",0,1,int(state.reasons)); Publish(world,id); }
}
bool World::AllowAction(CWorldServer &world, CUnit *actor, CMapObject *target, const CPlot3D &point,
                        CSpell *spell, bool trap, bool executing)
{
    if (Empty() || !actor || (spell && !Offensive(spell))) return true;
    const auto actorArea=AreaAt(actor->GetCP()), targetArea=AreaAt(target?target->GetCP():point);
    const float radius=Radius(spell);
    // Protect friendly residents and objects using the current diplomacy.
    // Hostile actors and real enemy targets retain the normal combat pipeline.
    if (actor->GetOwner()) {
        auto *victim=Unit(target);
        const auto area=targetArea?targetArea:actorArea;
        if (area && !actor->IsEnemy(GetDefinition(area).peace.faction) &&
            ((trap && actorArea) || (target && (!victim || !Hostile(actor,victim))))) return false;
        if (radius>0) {
            CUnitList caught;
            world.aiMap.GetUnitsNear(point,caught,radius+2);
            for (POSITION p=caught.GetHeadPosition();p;) {
                auto *unit=caught.GetNext(p);
                const auto local=AreaAt(unit->GetCP());
                if (!local || unit->IsDead() || actor->IsEnemy(GetDefinition(local).peace.faction) || Hostile(actor,unit)) continue;
                if (Reaches(unit,point,radius) && (spell->IsValidTarget(actor,unit) ||
                    (ModRuntime::Current() && ModRuntime::Current()->PointSkill(spell)))) return false;
            }
        }
    }
    if (executing) {
        const auto reason=spell?Reason::Spell:Reason::Attack;
        Incident(world,actorArea,reason); Incident(world,targetArea,reason);
        if (radius>0) for (const auto &entry:instances) {
            const auto &area=GetDefinition(entry.first).peace;
            if (!area.Enabled() || point.z+radius<area.minZ || point.z-radius>area.maxZ) continue;
            if (InArea(area,point,radius)) Incident(world,entry.first,reason);
        }
    }
    return true;
}
void World::DamageAt(CWorldServer &world, CUnit *victim)
{
    if (victim) Incident(world,AreaAt(victim->GetCP()),Reason::Damage);
}
void World::AlarmAt(CWorldServer &world, int, const CPlot3D &point)
{
    Incident(world,AreaAt(point),Reason::NativeAlarm);
}

void World::Segment(CWorldServer &world)
{
    if (Empty()) return;
    auto *player=world.GetPlayer(0);
    if (!player) return;
    std::set<int> living;
    auto track=[&](CUnit *unit,int index) {
        if (!unit || unit->IsDead() || !living.insert(unit->GetID()).second) return;
        auto &visit=visitors[unit->GetID()];
        const auto cp=unit->GetCP();
        const bool known=visit.identity==unit->GetRTID();
        const auto old=known?visit.settlement:0, next=AreaAt(cp,old);
        const auto before=known?CPlot3D(visit.previous[0],visit.previous[1],visit.previous[2]):cp;
        if (old!=next) {
            if (old) Event(world,old,"leave",0,index,NearGate(GetDefinition(old).peace,before,cp)?1:0);
            if (next) Event(world,next,"enter",0,index,known && NearGate(GetDefinition(next).peace,before,cp)?1:0);
        }
        visit.identity=unit->GetRTID(); visit.settlement=next; visit.previous={cp.x,cp.y,cp.z};
    };
    for (int i=0;i<player->party.people.GetSize();++i) track(world.GetUnit(player->party.people[i].nID),i);
    for (const auto &entry:player->unitsUnderControl) track(entry.second,-1);
    for (auto p=visitors.begin();p!=visitors.end();) {
        if (!living.count(p->first)) p=visitors.erase(p); else ++p;
    }
    for (auto &entry:instances) {
        const auto &area=GetDefinition(entry.first).peace;
        if (!area.Enabled()) continue;
        std::uint32_t causes=0;
        if (player->party.vars.GetVar(area.alarmKey.c_str())!=0) causes|=Reason::Story;
        CUnitList units;
        world.aiMap.GetUnitsNear(CPlot3D(area.center.x,area.center.y,0),units,area.radius+area.hysteresis+2);
        for (POSITION p=units.GetHeadPosition();p;) {
            auto *unit=units.GetNext(p);
            if (unit->IsDead() || !InArea(area,unit->GetCP(),area.hysteresis)) continue;
            if (unit->IsEnemy(area.faction) || Unsafe(world,unit)) causes|=Reason::Attack;
        }
        auto &state=entry.second;
        if (causes) Incident(world,entry.first,causes);
        else if (state.alarm) {
            const int quiet=int(std::ceil(area.quietSeconds*1000.0/DW_SEGMENT_TIME));
            if (world.GetSegmentTimeCounter()-state.lastIncident>=quiet) {
                state.alarm=false; state.reasons=0; state.lastIncident=-1;
                Event(world,entry.first,"alarm",0,0,0);
            } else state.reasons=Reason::Quiet;
        }
        Publish(world,entry.first);
    }
}
}
