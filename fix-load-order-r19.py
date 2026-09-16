from pathlib import Path
r=Path(__file__).resolve().parent;p=r/'Game/ModPackage.cpp';b=p.read_bytes();old=b' Require("table_indexes");indexRef=luaL_ref(state,LUA_REGISTRYINDEX);';assert b.count(old)==1
p.write_bytes(b.replace(old,b' // Match main.lua require order: table callbacks must override default callbacks.\r\n Require("default");lua_pop(state,1);\r\n'+old))
