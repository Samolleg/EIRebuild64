#pragma once
#include <vector>
#include <unordered_set>

// Fixes.cpp: deduplicate for the world lifetime; drain at most 12 IDs per tick.
struct ModNpcRefreshQueue {
 std::vector<int> ids;std::unordered_set<int> scheduled;size_t next=0;
 bool scanPending=true,heroesPending=true;
 void Reset(){ids.clear();scheduled.clear();next=0;scanPending=true;heroesPending=true;}
 bool Add(int id){if(!scheduled.insert(id).second)return false;ids.push_back(id);return true;}
 template<class F>size_t Process(F&&refresh){
  size_t processed=0;while(processed<12&&next<ids.size()){int id=ids[next++];++processed;refresh(id);}
  if(next==ids.size()){ids.clear();next=0;}return processed;
 }
};
