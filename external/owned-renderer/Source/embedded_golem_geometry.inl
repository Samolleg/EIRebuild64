// Plugin 421604cc78b4 ordinary effects, using typed synchronous inputs.
float GolemNoise(unsigned n){n^=n>>16;n*=0x7feb352d;n^=n>>15;n*=0x846ca68b;n^=n>>16;return (n&0xffffff)/16777216.f;}

void SpellRing(Mesh&m,Vec center,float radius,float width){
 unsigned start=(unsigned)m.uv.size();for(int i=0;i<=96;i++){float a=i*6.283185307f/96;for(float r:{radius-width,radius})m.Add(center+Vec{r*std::cos(a),r*std::sin(a),.025f},0,0);}
 for(int i=0;i<96;i++){unsigned k=start+i*2;m.Quad(k,k+2,k+3,k+1);}
}
void Crystal(Mesh&m,Vec center,float radius,float height,float angle){
 Vec top=center+Vec{0,0,height},bottom=center+Vec{0,0,-.035f};
 for(int i=0;i<5;i++){
  float a=angle+i*6.283185307f/5,b=a+6.283185307f/5;Vec x=center+Vec{radius*std::cos(a),radius*std::sin(a),height*.25f},y=center+Vec{radius*std::cos(b),radius*std::sin(b),height*.25f};
  unsigned k=m.Add(top,.76f,.94f);m.Add(x,.55f,.57f);m.Add(y,.94f,.57f);m.indices.insert(m.indices.end(),{(WORD)k,(WORD)(k+1),(WORD)(k+2)});
  k=m.Add(bottom,.76f,.55f);m.Add(y,.94f,.57f);m.Add(x,.55f,.57f);m.indices.insert(m.indices.end(),{(WORD)k,(WORD)(k+1),(WORD)(k+2)});
 }
}

void GolemBlade(Mesh&m,const GolemSpellPart&p,Vec at,Vec axis,float rotation,float heading,float scale){
 for(int i=0;i<p.count;i++){const auto&v=p.vertices[i];Vec point=Spin(Vec{v.x,v.y,v.z}*scale,axis,rotation);point=Spin(point,{0,0,1},heading);auto k=m.Add(at+point,v.u,1-v.v);m.indices.push_back((WORD)k);}
}
Mesh GolemTube(Vec a,Vec b,float radius,float time){
 Mesh m;Vec direction=Unit(b-a),side=Unit(Cross(direction,{0,0,1})),up=Unit(Cross(side,direction));int rings=24,sides=10;
 for(int i=0;i<=rings;i++)for(int j=0;j<=sides;j++){
  float t=(float)i/rings,angle=j*6.283185307f/sides;float r=radius*(1+.05f*std::sin(t*29-time*16));
  m.Add(a+(b-a)*t+side*(r*std::cos(angle))+up*(r*std::sin(angle)),t*12-time*3,(float)j/sides);
 }
 for(int i=0;i<rings;i++)for(int j=0;j<sides;j++){unsigned k=i*(sides+1)+j;m.Quad(k,k+1,k+sides+2,k+sides+1);}return m;
}
// Closed volumetric laser hoop, sharing the beam's animated material.
Mesh GolemLaserHoop(Vec center,float outer,float tube,float time){
 Mesh m;constexpr int rings=96,sides=8;float radius=outer-tube;
 for(int i=0;i<=rings;i++){float a=i*6.283185307f/rings;Vec radial={std::cos(a),std::sin(a),0};
  for(int j=0;j<=sides;j++){float b=j*6.283185307f/sides;m.Add(center+radial*(radius+tube*std::cos(b))+Vec{0,0,tube*std::sin(b)},i*.18f-time*3,(float)j/sides);}
 }
 for(int i=0;i<rings;i++)for(int j=0;j<sides;j++){unsigned k=i*(sides+1)+j;m.Quad(k,k+1,k+sides+2,k+sides+1);}return m;
}
template<class Emit>void GolemLaserHaloGeometry(int boss,float time,Emit emit){
 int phase=(int)input.phase;float radius=input.ring_radius;
 if(input.active!=1||input.stage!=5||(phase!=8&&phase!=9&&phase!=10)||!std::isfinite(radius)||radius<=0||radius>24)return;
 Vec at=Base();at.z+=.95f;
 for(int layer=0;layer<(phase==9?3:1);layer++){
  float thickness=phase!=9?.045f:layer==0?.16f:layer==1?.07f:.022f;
  auto hoop=GolemLaserHoop(at,radius,thickness,time);
  emit(hoop,phase!=9?0x64379cff:layer==0?0x8a379cff:layer==1?0xd77cdfff:0xf4edffff);
 }
}
void GolemGroundShard(Mesh&mesh,Vec at,float length,float width,float height,float angle,unsigned seed){
 float c=std::cos(angle),s=std::sin(angle);
 Vec local[]={{-length*.52f,-width*.36f,0},{length*.43f,-width*.5f,0},
  {length*.56f,width*.14f,0},{-length*.34f,width*.5f,0},
  {-length*.12f,-width*.11f,height},{length*.26f,width*.12f,height*.62f}};
 Vec points[6];for(int i=0;i<6;i++)points[i]=at+Vec{local[i].x*c-local[i].y*s,local[i].x*s+local[i].y*c,local[i].z};
 constexpr int faces[][3]={{0,1,4},{1,5,4},{1,2,5},{2,3,5},{3,4,5},{3,0,4},{0,3,2},{0,2,1}};
 float u=.03f+(seed%3)*.12f,v=.59f+((seed/3)%2)*.025f;
 for(auto&f:faces){auto k=mesh.Add(points[f[0]],u,v);mesh.Add(points[f[1]],u+.16f,v+.02f);mesh.Add(points[f[2]],u+.05f,v+.12f);mesh.indices.insert(mesh.indices.end(),{(WORD)k,(WORD)(k+1),(WORD)(k+2)});}
}
template<class Emit>void GolemGroundFields(int tick,Emit emit){
 Mesh shards;
 for(int field=0;field<256;field++){
  float end=input.fields[field].end,born=input.fields[field].born;if(end<=tick||born>tick)continue;
  Vec center={input.fields[field].x,input.fields[field].y,input.fields[field].z};
  float radius=input.fields[field].r;if(!std::isfinite(radius)||radius<=0||radius>5.01f)continue;
  int count=radius>3?160:28;float age=(tick-born)/18.f,arrival=Clamp(age/.65f,0,1),fade=Clamp((end-tick)/6.f,0,1);
  for(int i=0;i<count;i++){
   unsigned seed=(unsigned)(field*1919+i*73);float a=GolemNoise(seed)*6.283185307f,r=std::sqrt(GolemNoise(seed+1))*radius*.96f;
   float x=center.x+r*std::cos(a)*arrival,y=center.y+r*std::sin(a)*arrival;
   float jump=std::sin(arrival*3.14159265f)*(.4f+GolemNoise(seed+2));
   Vec at={x,y,GolemGround(x,y)+center.z+.065f+jump};
   float size=(.26f+GolemNoise(seed+3)*.44f)*fade;
   GolemGroundShard(shards,at,size,size*(.40f+GolemNoise(seed+4)*.4f),(.10f+GolemNoise(seed+5)*.24f)*fade,a+2*GolemNoise(seed+6),seed);
   // Bounded batches stay below the WORD index limit even if all 256 fields
   // overlap. Expiry simply stops emitting; no entity/particle-list mutation.
   if(shards.uv.size()>=48000){emit(shards);shards=Mesh{};}
  }
 }
 if(!shards.indices.empty())emit(shards);
}
Mesh FinalWall(Vec a,Vec b,float time,int band,bool flowing){
 Mesh m;int n=(int)std::ceil(std::hypot(b.x-a.x,b.y-a.y)/.65f);Vec delta=b-a;
 for(int i=0;i<=n;i++){
  float t=(float)i/n;
  for(int edge=0;edge<2;edge++){
   float h=(band+edge)*.8f;
   float crest=band==4&&edge?.22f*std::sin(t*n*.75f+time*3.7f):0;
   Vec p=a+delta*t+Vec{0,0,h+crest};
   m.Add(p,t*n*.31f+(flowing?.04f*std::sin(time+t*5):0),h*.38f-time*.32f);
  }
 }
 for(int i=0;i<n;i++){unsigned k=i*2;m.Quad(k,k+2,k+3,k+1);}return m;
}
Mesh FinalBeacon(Vec at,float time){
 Mesh m;constexpr int n=72;
 for(int i=0;i<=n;i++){
  float a=i*6.283185307f/n;
  for(float r:{.92f,1.10f})m.Add(at+Vec{r*std::cos(a),r*std::sin(a),.05f},0,0);
 }
 for(int i=0;i<n;i++){unsigned k=i*2;m.Quad(k,k+2,k+3,k+1);}
 for(int i=0;i<4;i++){
  float a=i*1.5707963f+time*1.7f;Vec r={std::cos(a),std::sin(a),0};
  Vec p=at+r*1.25f+Vec{0,0,.18f+.12f*std::sin(time*8)};
  auto k=m.Add(p+r*.18f+Vec{0,0,.50f},0,0);m.Add(p+Vec{-.13f*r.y,.13f*r.x,.50f},0,0);
  m.Add(p,0,0);m.Quad(k,k+1,k+2,k+2);
 }
 return m;
}
void Stage(float time){
 int boss=0;
 int tick=input.tick;float ground=Base().z;Mesh rings;
 auto* body=textures[0];auto* detail=textures[1];
 auto solid=[&](Mesh&m,ID3D11ShaderResourceView*texture,unsigned color){draw(m,texture,color,true,false);m=Mesh{};};
 GolemGroundFields(tick,[&](Mesh&m){solid(m,detail,0xffb9e5ff);});
 // Ground fields show only physical fragments. Rings belong exclusively to
 // pending casts/projectiles, so impact removes the warning immediately.
 // Telegraph each landing point before the real blade arrives.
 for(int shot=0;shot<8;shot++){
  float end=input.shots[shot].end,born=input.shots[shot].born;if(end<=tick||end<=born)continue;
  Vec start={input.shots[shot].x,input.shots[shot].y,input.shots[shot].z},target={input.shots[shot].ex,input.shots[shot].ey,input.shots[shot].ez};
  start=GolemGroundPosition(start);target=GolemGroundPosition(target);
  float t=Clamp((tick-born)/(end-born),0,1),distance=std::hypot(target.x-start.x,target.y-start.y);Vec at=start+(target-start)*t+Vec{0,0,4*(3+distance*.08f)*t*(1-t)+.5f*t};
  GolemQuaternion q={input.shots[shot].q[0],input.shots[shot].q[1],input.shots[shot].q[2],input.shots[shot].q[3]};Vec axis=Unit(Cross(target-start,{0,0,1}));
  for(const auto&p:golemThrownBlade)if(p.hand==(int)input.shots[shot].hand&&p.stage==(int)input.shots[shot].stage){Mesh blade;for(int i=0;i<p.count;i++){const auto&v=p.vertices[i];Vec point=Spin(GolemRotate(q,{v.x,v.y,v.z}),axis,t*5.5f);auto k=blade.Add(at+point,v.u,1-v.v);blade.indices.push_back((WORD)k);}solid(blade,p.texture==1?body:detail,0xffd5edff);}
  SpellRing(rings,target,5,.065f);
 }
 if(input.phase==6&&input.throw_launched!=1)for(int i=0;i<(int)input.throw_count;i++)SpellRing(rings,GolemGroundPosition({input.casts[i][0],input.casts[i][1],0}),5,.065f);
 // The replacement grows from the actual animated rear attachment after impact.
 for(int hand=0;hand<2;hand++){
  int stage=(int)input.stage;if(stage>=5||stage<2)continue;
  float release=input.release[hand],ready=input.ready[hand];
  if(release<=0||tick<release+45||tick>=ready)continue;float grow=Clamp((tick-release-45)/27.f,0,1);auto node=Node(hand?2:1);if(!node)continue;
  for(const auto&p:golemThrownBlade)if(p.hand==hand&&p.stage==stage){Mesh blade;for(int i=0;i<p.count;i++){const auto&v=p.vertices[i];Vec point={v.x*std::sqrt(grow),v.y*std::sqrt(grow),(v.z-.9f)*grow};auto k=blade.Add(NodePoint(node,point),v.u,1-v.v);blade.indices.push_back((WORD)k);}solid(blade,p.texture==1?body:detail,0xffb4ebff);}
 }
 draw(rings,nullptr,0x9f6fcfff,false,true);
 int phase=(int)input.phase;
 GolemLaserHaloGeometry(boss,time,[&](Mesh&m,unsigned color){draw(m,textures[2],color,false,true);});
 if(phase==8||phase==9||phase==10){
  auto node=Node(0);Vec origin=NodePoint(node,{0,-.10f,1.17f});
  float ex=input.beam_ex,ey=input.beam_ey;
  if(phase!=9){float dx=input.aim_x-Base().x,dy=input.aim_y-Base().y,len=std::hypot(dx,dy);if(len>.01f){dx/=len;dy/=len;float length=200;
   if(dx>.000001f)length=(std::min)(length,(100-Base().x)/dx);if(dx<-.000001f)length=(std::min)(length,(28-Base().x)/dx);
   if(dy>.000001f)length=(std::min)(length,(62-Base().y)/dy);if(dy<-.000001f)length=(std::min)(length,(26-Base().y)/dy);
   ex=Base().x+dx*length;ey=Base().y+dy*length;
  }}
  Vec end={ex,ey,ground+.8f};auto* flowTexture=textures[2];
  if(phase==9){
   for(int layer=0;layer<3;layer++){auto tube=GolemTube(origin,end,layer==0?.5f:layer==1?.24f:.085f,time);draw(tube,flowTexture,layer==0?0x8a379cff:layer==1?0xd77cdfff:0xf4edffff,false,true);}
  }else{
   Mesh charge;float progress=phase==10?1.f:Clamp(input.timer/36.f,0,1);Crystal(charge,origin,.15f+.28f*progress,.3f+.5f*progress,time*3);draw(charge,body,0xe89fe9ff,false,true);
  }
  Vec groundStart=Base()+Vec{0,0,.055f},groundEnd={ex,ey,ground+.055f};Vec side=Unit(Cross(groundEnd-groundStart,{0,0,1}))*.5f;Mesh warning;
  auto k=warning.Add(groundStart-side,0,0);warning.Add(groundStart+side,0,1);warning.Add(groundEnd+side,1,1);warning.Add(groundEnd-side,1,0);warning.Quad(k,k+1,k+2,k+3);
  draw(warning,nullptr,phase==9?0x5a7cddff:0x42329edc,false,true);
 }
}
