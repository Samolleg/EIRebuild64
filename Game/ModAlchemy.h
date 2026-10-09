#pragma once
#include <stdexcept>
// Bound source38636-38654: existing spell construction result, not quick input.
namespace ModAlchemy {
// Bound native38357-38430: preflight before consumption, shared item ownership.
inline bool ScrollAllowed(bool allowed,bool quick,bool scroll){return allowed&&quick&&scroll;}
template<class SpellItem,class Result,class Preflight,class Duplicate,class Consume,class Publish>
bool Deconstruct(Result*& result,Preflight preflight,Duplicate duplicate,Consume consume,Publish publish){
 SpellItem* spell=nullptr;
 if(!preflight(spell))return false;
 result=duplicate();
 if(!result){if(spell)spell->Release();return false;}
 result->AddRef();consume();publish(result,spell);
 if(spell)spell->Release();
 result->Release();result=nullptr;return true;
}
inline bool Enabled(bool perks,bool alchemy){return perks&&alchemy;}
template<class Preview,class Consume,class Create,class Publish>
void Commit(bool alchemy,Preview*& preview,Consume consume,Create create,Publish publish){
 if(!preview)return;
 consume();
 if(alchemy){
  auto* result=create("tiny potion 1");
  if(!result)throw std::runtime_error("Cannot create native Alchemy result");
  result->AddRef();result->AddSpell(preview->GetSpell(0));publish(result);result->Release();
 }else publish(preview);
 preview->Release();preview=nullptr;
}
}
