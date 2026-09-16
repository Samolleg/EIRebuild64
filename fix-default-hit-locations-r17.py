from pathlib import Path
import shutil
r=Path(__file__).resolve().parent
p=r/'Game/MapObjectManager.cpp';b=p.read_bytes();old=b'            pUnitStats->hls[i].fpMaxHP = 100;\r\n';assert b.count(old)==1
backup=r/'evidence/addon-integration/pre-r17/Game/MapObjectManager.cpp';assert not backup.exists();shutil.copy2(p,backup)
p.write_bytes(b.replace(old,old+b'            // Default locations have equal HP; use equal nonzero distribution weights.\r\n            pUnitStats->hls[i].fpHPWeight = 1;\r\n'))
