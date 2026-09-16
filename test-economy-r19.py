from pathlib import Path
r=Path(__file__).resolve().parent;p=r/'Game/ModPackage.cpp';b=p.read_bytes();mark=b'bool InspectModPackageCommandLine(int &result){';assert b.count(mark)==1
code=r'''
nlohmann::json CModPackage::CheckEconomy(){
 ModRuntime::Session session(*this);
 auto require=[](bool v,const char*msg){if(!v)throw std::runtime_error(msg);};
 auto number=[&](const char*fn,float n){CallbackFrame frame(state);BeginCallback(fn);lua_pushnumber(state,n);Call(1,1);return Number(state,fn);};
 auto run=[&](const char*code){if(luaL_loadstring(state,code)||lua_pcall(state,0,0,0))throw std::runtime_error(lua_tostring(state,-1));};
 int scalarCases=0,shareCases=0;
 for(int intelligence:{10,25,50,100,150,255}){
  SRPGUnitStats server={};server.psIntelligence.fpEffectiveValue=float(intelligence);server.fpExperience=100;server.fExpPenalty=-50;
  SRPGUserUnitStats client={};client.cIntelligence=BYTE(intelligence);
  double expected=pow(1.01,intelligence-25);float multiplier=server.GetExpMultiplier();
  require(fabs(multiplier-expected)<.0001*expected&&multiplier==client.GetExpMultiplier(),"Server/client XP multiplier mismatch");
  double mana=number("GetManaBonus",float(intelligence));require(fabs(server.GetManaBonusMultiplier()-mana)<.0001&&server.GetManaBonusMultiplier()==client.GetManaBonusMultiplier(),"Server/client mana multiplier mismatch");
  server.GainExperience(10);double penalty=-50+10*expected;
  require(fabs(server.fpExperience-(100+(std::max)(0.0,penalty)))<.001&&fabs(server.fExpPenalty-(std::min)(0.0,penalty))<.001,"Experience penalty repayment mismatch");scalarCases++;
 }
 for(int players:{1,2,4,8})for(float exp:{0.f,100.f,1001.f}){require(fabs(DistributeExperience(exp,players)-exp/players)<.0001,"LMP XP split mismatch");shareCases++;}
 bool badPlayers=false;try{DistributeExperience(10,0);}catch(const std::runtime_error&){badPlayers=true;}require(badPlayers,"Zero player count accepted");
 for(bool lmp:{false,true}){
  SShopSet shops;for(int i=0;i<(lmp?4:5);i++){CShop*shop=new CShop;for(int j=0;j<NUM_SHOP_KOEFFS;j++)shop->fKoeffs[j]=float(100+i*10+j);shops.shops.push_back(shop);}
  RecalcShops(shops,lmp);
  const float expected[4][9]={{0,0,0,0,15,1,5,2,.25f},{10,.5f,6.5f,1.5f,0,0,0,0,0},{12.5f,.5f,10,5,10,1,10,5,.45f},{30,.5f,25,5,1.5f,1,1,1,.05f}};
  // Read the installed table assignments as an independent oracle in the external checker;
  // native sentinels here check first entries, untouched campaign shop, and the tenth slot.
  require(shops.shops[0]->fKoeffs[4]==15&&shops.shops[1]->fKoeffs[2]==6.5f&&shops.shops[2]->fKoeffs[8]==.45f,"Installed shop coefficients not applied");
  for(int i=0;i<(lmp?4:5);i++)require(shops.shops[i]->fKoeffs[9]==float(109+i*10),"Tenth shop slot changed");
  if(!lmp)for(int j=0;j<9;j++)require(shops.shops[4]->fKoeffs[j]==float(140+j),"Untouched fifth shop changed");
  std::vector<float> before;for(auto&shop:shops.shops)for(float v:shop->fKoeffs)before.push_back(v);
  run("__r19_shop_callback=RecalcShopsPriceMultipliers; RecalcShopsPriceMultipliers=function(addr,lmp) local a=Array(addr,Memory.Type.Float,lmp and 36 or 45,false); __r19_expired=a; a:set(0,999); error('rollback probe') end");
  bool failed=false;try{RecalcShops(shops,lmp);}catch(const std::runtime_error&){failed=true;}
  run("RecalcShopsPriceMultipliers=__r19_shop_callback; __r19_shop_callback=nil; assert(not pcall(function() return __r19_expired:value(0) end)); __r19_expired=nil");
  require(failed,"Shop failed callback accepted");size_t n=0;for(auto&shop:shops.shops)for(float v:shop->fKoeffs)require(v==before[n++],"Partial shop write escaped rollback");
 }
 return {{"success",true},{"scalarCases",scalarCases},{"shareCases",shareCases},{"shopModes",2},{"shopRollback",true},{"expiredShopArrayRejected",true},{"nativeExperiencePenalty",true},{"clientServerParity",true},{"gameplayActivated",false}};
}
'''.replace('\n','\r\n').encode()
p.write_bytes(b.replace(mark,code+mark))
