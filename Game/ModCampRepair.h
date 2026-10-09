#pragma once
#include <algorithm>
namespace ModCampRepair {
enum class Mode {Equipped,Selected};
inline bool QuoteAllowed(int shop,bool metal){return shop!=1||!metal;}
inline bool RestoreAllowed(Mode mode,int shop,bool metal){
 return QuoteAllowed(shop,metal)&&(mode!=Mode::Equipped||shop!=2);
}
// Bound native repair arithmetic, including the small HP epsilon and minimum fee.
inline int Price(float price,float coefficient,float hp,float maxHP){
 const float damage=1.f-((std::max)(0.f,hp)+0.00001f)/maxHP;
 if(damage==0||coefficient==0)return 0;
 int fee=int(price*coefficient*damage);
 if(fee>0&&fee<=1)fee=0;
 return fee<=0?1:fee;
}
template<class Metal,class ItemPrice>
inline int Quote(bool native,int shop,int count,Metal metal,ItemPrice price){
 int total=0;for(int i=0;i<count;++i)if(!native||QuoteAllowed(shop,metal(i)))total+=price(i);
 return total;
}
struct Validity {bool confirm,cancel;};
inline Validity SelectedValidity(bool native,int count,int price){
 const bool valid=count>0&&(!native||price!=0);return {valid,valid};
}
template<class Metal,class Restore>
inline void RestoreItems(bool native,Mode mode,int shop,int count,Metal metal,Restore restore){
 for(int i=0;i<count;++i)if(!native||RestoreAllowed(mode,shop,metal(i)))restore(i);
}
}
