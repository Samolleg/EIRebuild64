// Repack saved authored data. Never executes source-repository code.
import {execFileSync} from 'node:child_process';
import {mkdirSync,writeFileSync} from 'node:fs';
import {join} from 'node:path';
export function importGolem(repository,destination,git='git',ruby=false) {
 const commit='421604cc78b4259db8be4ad5698525164a70e847';
 const source=name=>execFileSync(git,['show',commit+':src/generated/'+name],{cwd:repository,maxBuffer:8*1024*1024}).toString('utf8');
 const arrays=text=>new Map([...text.matchAll(/static const (?:GolemVertex|RubyActorVertex) (\w+)\[\]=\{([\s\S]*?)\};/g)].map(m=>[m[1],[...m[2].matchAll(/\{([^{}]+)\}/g)].map(v=>v[1].split(',').map(n=>Number(n.trim().replace(/f$/,''))))]));
 const parts=[];let names='';
 const add=(table,name,kind,bone,stage=0,texture=0,hand=0,actor=0,core=0,offset=[0,0,0],count=null)=>{
  const vertices=table.get(name);if(!vertices||!vertices.length||vertices.length%3||vertices.length>60000||count!==null&&count!==vertices.length)throw Error('invalid '+name);
  for(const v of vertices)if((v.length!==5&&v.length!==9)||v.some(x=>!Number.isFinite(x)))throw Error('bad vertex '+name);
  parts.push({kind,bone,stage,texture,hand,actor,core,offset,vertices});
 };
 if(ruby){
  const text=source('ruby_actor_data.h'),table=arrays(text);
  for(const m of text.matchAll(/\{([01]),"([^"\n]+)",(rubyV\w+),\(int\)_countof\(\3\),([^{}]+)\}/g))add(table,m[3],4,m[2],0,1,0,+m[1],0,m[4].split(',').map(n=>parseFloat(n)));
  if(parts.length!==26)throw Error('ruby part count');
 }else{
  const damage=source('golem_damage_data.h'),death=source('golem_death_data.h'),spell=source('golem_spell_data.h');
  const d=arrays(damage),e=arrays(death),s=arrays(spell);
  names=damage.match(/static const GolemPartName golemPartNames\[\]=\{([\s\S]*?)\};/)[1];
  for(const m of damage.matchAll(/\{"([^"\n]+)",(golemPiece\d+),(\d+),(\d+),([12])\}/g))add(d,m[2],0,m[1],+m[4],+m[5],0,0,0,[0,0,0],+m[3]);
  for(const stage of [4,5])add(d,'golemCore'+stage,1,'n03',stage,1);
  for(const m of death.matchAll(/\{"([^"\n]+)",(golemDeathPiece\d+),(\d+),([12]),(true|false)\}/g))add(e,m[2],2,m[1],5,+m[4],0,0,m[5]==='true'?1:0,[0,0,0],+m[3]);
  for(const m of spell.matchAll(/\{(golemThrown\d+),(\d+),([12]),([01]),([234])\}/g))add(s,m[1],3,'',+m[5],+m[3],+m[4],0,0,[0,0,0],+m[2]);
  if(parts.length!==115)throw Error('golem part count '+parts.length);
 }
 const chunks=[];const header=Buffer.alloc(12);header.write('GAV1');header.writeUInt32LE(1,4);header.writeUInt32LE(parts.length,8);chunks.push(header);
 for(const p of parts){
  const h=Buffer.alloc(72);[p.kind,p.stage,p.texture,p.hand,p.actor,p.core,p.vertices.length].forEach((v,i)=>h.writeUInt32LE(v,i*4));h.write(p.bone,28,31,'ascii');p.offset.forEach((v,i)=>h.writeFloatLE(v,60+i*4));chunks.push(h);
  const data=Buffer.alloc(p.vertices.length*36);p.vertices.forEach((v,i)=>{for(let k=0;k<8;k++)data.writeFloatLE(v[k]??0,i*36+k*4);data.writeUInt32LE(v[8]??0,i*36+32);});chunks.push(data);
 }
 mkdirSync(destination,{recursive:true});const file=join(destination,ruby?'ruby-v1.bin':'golem-v1.bin');const bytes=Buffer.concat(chunks);writeFileSync(file,bytes);
 return {file,bytes:bytes.length,parts:parts.length,names,commit};
}
