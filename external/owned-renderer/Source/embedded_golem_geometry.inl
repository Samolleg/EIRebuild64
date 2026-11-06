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
   Vec p=a+delta*t;p.z=GolemGround(p.x,p.y)+h+crest;
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
 auto solid=[&](Mesh&m,ID3D11ShaderResourceView*texture,unsigned color){draw(m,texture,Tint(color),true,false);m=Mesh{};};
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
  for(const auto&p:resource.parts)if(p.kind==3&&p.hand==(int)input.shots[shot].hand&&p.stage==(int)input.shots[shot].stage){Mesh blade;for(int i=0;i<p.count;i++){const auto&v=p.vertices[i];Vec point=Spin(GolemRotate(q,{v.x,v.y,v.z}),axis,t*5.5f);auto k=blade.Add(at+point,v.u,1-v.v);blade.indices.push_back((WORD)k);}solid(blade,p.texture==1?body:detail,0xffd5edff);}
  SpellRing(rings,target,5,.065f);
 }
 if(input.phase==6&&input.throw_launched!=1)for(int i=0;i<(int)input.throw_count;i++)SpellRing(rings,GolemGroundPosition({input.casts[i][0],input.casts[i][1],0}),5,.065f);
 // The replacement grows from the actual animated rear attachment after impact.
 for(int hand=0;hand<2;hand++){
  int stage=(int)input.stage;if(stage>=5||stage<2)continue;
  float release=input.release[hand],ready=input.ready[hand];
  if(release<=0||tick<release+45||tick>=ready)continue;float grow=Clamp((tick-release-45)/27.f,0,1);auto node=Node(hand?2:1);if(!node)continue;
  for(const auto&p:resource.parts)if(p.kind==3&&p.hand==hand&&p.stage==stage){Mesh blade;for(int i=0;i<p.count;i++){const auto&v=p.vertices[i];Vec point={v.x*std::sqrt(grow),v.y*std::sqrt(grow),(v.z-.9f)*grow};auto k=blade.Add(NodePoint(node,point),v.u,1-v.v);blade.indices.push_back((WORD)k);}solid(blade,p.texture==1?body:detail,0xffb4ebff);}
 }
 draw(rings,nullptr,Tint(0x9f6fcfff),false,true);
 int phase=(int)input.phase;
 GolemLaserHaloGeometry(boss,time,[&](Mesh&m,unsigned color){draw(m,input.hard?nullptr:textures[2],Tint(color),false,true);});
 if(phase==8||phase==9||phase==10){
  auto node=Node(0);Vec origin=NodePoint(node,{0,-.10f,1.17f});
  float ex=input.beam_ex,ey=input.beam_ey;
  if(phase!=9){float dx=input.aim_x-Base().x,dy=input.aim_y-Base().y,len=std::hypot(dx,dy);if(len>.01f){dx/=len;dy/=len;float length=200;
   if(dx>.000001f)length=(std::min)(length,(100-Base().x)/dx);if(dx<-.000001f)length=(std::min)(length,(28-Base().x)/dx);
   if(dy>.000001f)length=(std::min)(length,(62-Base().y)/dy);if(dy<-.000001f)length=(std::min)(length,(26-Base().y)/dy);
   ex=Base().x+dx*length;ey=Base().y+dy*length;
  }}
  Vec end={ex,ey,GolemGround(ex,ey)+.8f};auto* flowTexture=input.hard?nullptr:textures[2];
  if(phase==9){
   for(int layer=0;layer<3;layer++){auto tube=GolemTube(origin,end,layer==0?.5f:layer==1?.24f:.085f,time);draw(tube,flowTexture,Tint(layer==0?0x8a379cff:layer==1?0xd77cdfff:0xf4edffff),false,true);}
  }else{
   Mesh charge;float progress=phase==10?1.f:Clamp(input.timer/36.f,0,1);Crystal(charge,origin,.15f+.28f*progress,.3f+.5f*progress,time*3);draw(charge,body,Tint(0xe89fe9ff),false,true);
  }
  Vec groundStart={Base().x,Base().y,GolemGround(Base().x,Base().y)+.055f},groundEnd={ex,ey,GolemGround(ex,ey)+.055f};Vec side=Unit(Cross(groundEnd-groundStart,{0,0,1}))*.5f;Mesh warning;
  const int steps=(std::max)(1,int(std::ceil(std::hypot(ex-groundStart.x,ey-groundStart.y)/.65f)));
  for(int i=0;i<=steps;++i){const float t=float(i)/steps;auto p=groundStart+(groundEnd-groundStart)*t;for(float sign:{-1.f,1.f}){auto q=p+side*sign;q.z=GolemGround(q.x,q.y)+.055f;warning.Add(q,t,sign<0?0.f:1.f);}if(i){auto k=unsigned(i*2);warning.Quad(k-2,k,k+1,k-1);}}
  draw(warning,nullptr,Tint(phase==9?0x5a7cddff:0x42329edc),false,true);
 }
}

void ReflectCylinder(Mesh&m,Vec a,Vec b,float ra,float rb,int sides=8){
 Vec d=Unit(b-a),x=Unit(Cross(d,std::abs(d.z)>.9f?Vec{0,1,0}:Vec{0,0,1})),y=Cross(d,x);
 unsigned start=(unsigned)m.uv.size();
 for(int ring=0;ring<2;ring++)for(int j=0;j<sides;j++){float angle=j*6.283185307f/sides;m.Add((ring?b:a)+(x*std::cos(angle)+y*std::sin(angle))*(ring?rb:ra),0,0);}
 for(int j=0;j<sides;j++){unsigned k=start+j,n=start+(j+1)%sides;m.Quad(k,n,n+sides,k+sides);}
 unsigned ca=m.Add(a,0,0),cb=m.Add(b,0,0);
 for(int j=0;j<sides;j++){unsigned k=start+j,n=start+(j+1)%sides;m.indices.insert(m.indices.end(),{(WORD)ca,(WORD)n,(WORD)k,(WORD)cb,(WORD)(k+sides),(WORD)(n+sides)});}
}
void ReflectArrow(Mesh&m,Vec at,Vec direction){
 Vec d=Unit(direction),x=Unit(Cross(d,{0,0,1})),y=Cross(d,x);
 ReflectCylinder(m,at-d*.9f,at,.025f,.025f);
 ReflectCylinder(m,at-d*.10f,at+d*.22f,.10f,0,4);
 for(int j=0;j<3;j++){
  float a=j*2.0943951f;Vec r=x*std::cos(a)+y*std::sin(a),w=Cross(d,r)*.009f;
  Vec p[]={at-d*.9f,at-d*.92f+r*.12f,at-d*.65f+r*.1f,at-d*.6f};
  unsigned k=(unsigned)m.uv.size();for(float sign:{-1.f,1.f})for(auto v:p)m.Add(v+w*sign,0,0);
  m.Quad(k,k+1,k+2,k+3);m.Quad(k+7,k+6,k+5,k+4);
  for(int t=0;t<4;t++)m.Quad(k+t,k+(t+1)%4,k+(t+1)%4+4,k+t+4);
 }
}
template<class Emit>void ReflectGeometry(float tick,Emit emit){
 int used=input.reflection_used;
 for(int i=0;i<used;i++){
  float born=input.reflections[i].born,end=input.reflections[i].end,arrival=end-4;
  if(tick<born||tick>=end||arrival<=born)continue;
  const auto& slot=input.reflections[i];Vec start{slot.start[0],slot.start[1],slot.start[2]},target{slot.target[0],slot.target[1],slot.target[2]};
  float t=Clamp((tick-born)/(arrival-born),0,1);int kind=(int)input.reflections[i].kind;
  Vec at=start+(target-start)*t,direction=target-start;Mesh core,halo;
  if(kind<=2){if(tick<arrival)ReflectArrow(core,at,direction);emit(core,0xffd9e9f2,false);continue;}
  unsigned color=kind==3?0xff6819:kind==4?0x68e5aa:kind==5?0x79baff:0xc490ff;
  if(kind==5){
   Vec side=Unit(Cross(direction,{0,0,1}));Vec previous=start;
   for(int k=1;k<=24;k++){
    float f=k/24.f;Vec next=start+direction*f;
    if(k<24)next=next+side*((GolemNoise(i*751+k*31+(unsigned)tick*13)-.5f)*.7f)+Vec{0,0,(GolemNoise(k*87+i*17)-.5f)*.35f};
    ReflectCylinder(core,previous,next,.026f,.026f,5);ReflectCylinder(halo,previous,next,.095f,.095f,5);previous=next;
   }
  }else{
   // A volumetric missile with a twisting trail, rather than flat radial cards.
   ReflectCylinder(core,at-Unit(direction)*.12f,at+Unit(direction)*.13f,.13f,0,10);
   for(int j=0;j<14;j++){
    float f=(float)j/14,angle=j*2.4f+tick*.7f;Vec d=Unit(direction),x=Unit(Cross(d,{0,0,1})),y=Cross(d,x);
    Vec p=at-d*(f*.9f)+(x*std::cos(angle)+y*std::sin(angle))*(f*.12f);
    ReflectCylinder(halo,p-d*.14f,p+d*.07f,(1-f)*.13f,.005f,6);
   }
  }
  emit(halo,0x65000000|color,true);emit(core,0xedffffff,true);
 }
}
bool ReflectAuraActive(int stage,int mode,bool active,bool dead){return active&&!dead&&stage>=3&&stage<=5&&mode!=4;}
float ReflectAuraDensity(float radius){float t=(std::max)(0.f,1.f-radius*radius);return t*t*t;}
static constexpr int ReflectAuraBins=16;
template<class Emit>void ReflectAuraGeometry(int boss,float tick,Vec camera,Emit emit){
 if(!ReflectAuraActive((int)input.stage,(int)input.phase,input.active==1,input.dead))return;
 auto node=Node(0);if(!node)return;
 Mesh density[ReflectAuraBins];float time=tick/18.f;
 Vec center=NodePoint(node,{0,0,1.05f});
 Vec view=Unit(camera-center),right=Unit(Cross({0,0,1},view)),up=Unit(Cross(view,right));
 constexpr int S=96,R=32;
 for(int layer=0;layer<2;layer++){
  Vec origin=center+view*2.35f+right*(layer?.16f*std::sin(time*.6f):0)+up*(layer?.14f*std::cos(time*.5f):0);
  float width=layer?2.1f:2.45f,height=layer?2.55f:3.0f;
  auto point=[&](int ring,int segment){
   float r=(float)ring/R,a=segment*6.283185307f/S;
   float drift=1+.045f*std::sin(a*3+time*.6f)+.025f*std::sin(a*5-time*.4f);
   return origin+right*(width*r*std::cos(a)*drift)+up*(height*r*std::sin(a)*drift)+view*(.12f*(1-r*r));
  };
  for(int j=0;j<R;j++)for(int i=0;i<S;i++){
   float r=(j+.5f)/R,a=(i+.5f)*6.283185307f/S;
   float alpha=ReflectAuraDensity(r)*(layer?.085f:.18f)*(.90f+.10f*std::sin(a*2-time*.5f+r*3));
   int bin=(int)std::floor(alpha*255.f/3.f);if(bin<1)continue;bin=(std::min)(ReflectAuraBins-1,bin);
   auto&m=density[bin];unsigned k=m.Add(point(j,i),0,0);m.Add(point(j,i+1),0,0);m.Add(point(j+1,i+1),0,0);m.Add(point(j+1,i),0,0);m.Quad(k,k+1,k+2,k+3);
  }
 }
 for(int i=1;i<ReflectAuraBins;i++)if(!density[i].indices.empty())emit(density[i],((unsigned)(i*3)<<24)|0x659effu);
}
