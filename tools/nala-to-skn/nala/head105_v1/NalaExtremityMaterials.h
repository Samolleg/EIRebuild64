#pragma once
// Blueprint defines cut; the low 16 bits are the database material ordinal.
namespace NalaExtremityMaterials {
struct Material {unsigned ordinal,cuffs,socks;};
inline constexpr Material Line[]={{28,222,223},{29,224,228},{30,225,229},{31,226,230},{44,227,231}};
inline unsigned Match(unsigned slot,unsigned subtype){
 unsigned prototype=subtype>>16,ordinal=subtype&0xffffu;
 if(!((slot==6&&prototype==4)||(slot==5&&prototype==3)))return 0;
 for(const auto&m:Line)if(m.ordinal==ordinal)return slot==6?m.cuffs:m.socks;
 return 0;
}
}
