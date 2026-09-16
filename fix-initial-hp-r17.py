from pathlib import Path
import shutil
r=Path(__file__).resolve().parent
p=r/'Game/ModPackage.cpp';b=p.read_bytes();old=b' ModStats::Armor(stagedCommon,unit,npcArmorCalculation);';assert b.count(old)==1
backup=r/'evidence/addon-integration/pre-r17/Game/ModPackage.cpp';assert not backup.exists();shutil.copy2(p,backup)
new=''' // SetupStats supplies prototype common HP before SetRPGStats performs its two passes.
 // A fresh skeleton has no previous distribution to preserve proportionally.
 bool used=false,fresh=true;for(const auto&h:staged.hls)if(h.state>RPGALS_NOTUSED){used=true;fresh=fresh&&h.fpHP==0&&h.fpMaxHP==0;}
 if(used&&fresh){
  if(!std::isfinite(stagedCommon.ssHP.fpMax)||stagedCommon.ssHP.fpMax<=0||!std::isfinite(staged.hls[RPGHL_BODY].fpHPWeight)||staged.hls[RPGHL_BODY].fpHPWeight<=0)throw std::runtime_error("Fresh unit requires prototype HP and body weight");
  staged.DistributeHP(stagedCommon.ssHP.fpMax);
 }
 ModStats::Armor(stagedCommon,unit,npcArmorCalculation);'''.replace('\n','\r\n').encode()
p.write_bytes(b.replace(old,new))
