#pragma once
#include <cstddef>
#include <unordered_map>
namespace ModHitHistory {
struct Count {int victim=0,hits=0,misses=0;};
enum class Reason {Disabled,NoAttacker,First,ChangedVictim,AllLocations,Counted};
struct Result {float score;Count before,after;Reason reason;bool zero=false,maximum=false;int missCap=0;};
inline bool Enabled(bool attacks,bool directCustom){return attacks||directCustom;}
template<class WeaponsMaster>
Result Advance(const Count& previous,float score,int defense,WeaponsMaster weaponsMaster)
{
    Result result{score,previous,previous,Reason::Counted};
    auto& next=result.after;
    if(score>=defense){++next.hits;if(next.misses>10||next.misses<0)next.misses=0;}
    else {++next.misses;if(next.hits>10||next.hits<0)next.hits=0;}
    if(next.hits==9){result.score=0;next.hits=0;result.zero=true;}
    // Native float -> int, called lazily on the defender, after hit compensation.
    const int master=int(weaponsMaster());result.missCap=master?7:9;
    if(next.misses==result.missCap){result.score=255;next.misses=0;result.maximum=true;}
    return result;
}
class Ledger {
    std::unordered_map<int,Count> counts;
public:
    const Count* Find(int attacker)const {auto i=counts.find(attacker);return i==counts.end()?nullptr:&i->second;}
    std::size_t Size()const{return counts.size();}
    void Clear(){counts.clear();}
    template<class WeaponsMaster>
    Result Observe(bool enabled,bool hasAttacker,int attacker,int victim,bool allLocations,
                   float score,int defense,WeaponsMaster weaponsMaster)
    {
        Result result{score,{}, {},!enabled?Reason::Disabled:Reason::NoAttacker};
        if(!enabled||!hasAttacker)return result;
        auto i=counts.find(attacker);
        if(i!=counts.end()){
            result.before=result.after=i->second;
            if(allLocations){result.reason=Reason::AllLocations;return result;}
            if(i->second.victim==victim){result=Advance(i->second,score,defense,weaponsMaster);i->second=result.after;return result;}
        }
        result.reason=i==counts.end()?Reason::First:Reason::ChangedVictim;
        result.after={victim,score>=defense?1:0,score>=defense?0:1};
        counts[attacker]=result.after;return result;
    }
};
// Exact native process-global transient cache; no Unit refs, save data or epoch timer.
inline Ledger& State(){static Ledger ledger;return ledger;}
void ClearRuntimeHitHistory(const char* reason);
}
