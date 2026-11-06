// Source effects from 421604cc78b4 ruby_hard_vfx/ruby_magic_vfx.
// Native actors keep one retained GPU material pass; G05 supplies authored growth.
struct RubySprites {Mesh opacity[32];Mesh& Bucket(float a){int n=int(std::ceil(Clamp(a,0,1)*32))-1;return opacity[(std::max)(0,(std::min)(31,n))];}};
Vec Camera() const{return {input.camera[0],input.camera[1],input.camera[2]};}
void RubyBeam(Mesh& m,Vec a,Vec b,Vec camera,float width,float u0,float u1){Vec side=Unit(Cross(b-a,camera-(a+b)*.5f))*width;unsigned i=m.Add(a-side,u0,0);m.Add(b-side,u1,0);m.Add(b+side,u1,1);m.Add(a+side,u0,1);m.Quad(i,i+1,i+2,i+3);}
void RubyDisc(RubySprites& bins,Vec at,Vec camera,float radius,float alpha){
 Vec toward=Unit(camera-at),x=Unit(Cross(toward,{0,0,1})),y=Cross(x,toward);
 for(int band=0;band<8;++band){float r0=radius*band/8,r1=radius*(band+1)/8,a=alpha*std::exp(-4.f*(band+.5f)*(band+.5f)/64.f);if(a<.008f)continue;auto& m=bins.Bucket(a);
  for(int j=0;j<16;++j){float u=j*6.283185307f/16,v=(j+1)*6.283185307f/16;Vec p=x*std::cos(u)+y*std::sin(u),q=x*std::cos(v)+y*std::sin(v);auto k=m.Add(at+p*r0,0,0);m.Add(at+q*r0,0,0);m.Add(at+q*r1,0,0);m.Add(at+p*r1,0,0);m.Quad(k,k+1,k+2,k+3);}
 }
}
void RubySpritesDraw(RubySprites& sprites,ID3D11ShaderResourceView* texture,unsigned rgb,bool additive){for(unsigned b=0;b<32;++b)if(!sprites.opacity[b].indices.empty())draw(sprites.opacity[b],texture,(((b+1)*255/32)<<24)|(rgb&0xffffffu),false,additive);}
const EmbeddedGolemNode& RubyNode(int actor,const char* bone) const {
 for(int i=0;i<24;++i)if(!std::strcmp(EmbeddedRubyBones[i],bone)&&input.ruby[actor].nodes[i].valid)return input.ruby[actor].nodes[i];throw std::runtime_error("ruby current bone missing");
}
Vec RubyCore(int actor) const{return SavedPoint(RubyNode(actor,"core"),{0,0,0});}
Vec RubyFlow(Vec a,Vec b,float t,float time,float phase){Vec side=Unit(Cross(b-a,{0,0,1}));float envelope=std::sin(t*3.141592654f);Vec p=a+(b-a)*t+Vec{0,0,1.1f*envelope};return p+side*(envelope*(.22f*std::sin(t*17-time*4+phase)+.10f*std::sin(t*31+time*2+phase)))+Vec{0,0,.16f*envelope*std::cos(t*21-time*3+phase)};}
void RubyHealing(float time){
 if(!textures[4])throw std::runtime_error("ruby magic material missing");auto* head=Node(0);Vec a=RubyCore(1),b=NodePoint(head,{0,-.10f,1.17f}),camera=Camera();Mesh filaments;
 for(int strand=0;strand<3;++strand){Mesh ribbon;float phase=strand*2.0943951f;for(int i=0;i<=80;++i){float t=i/80.f;Vec p=RubyFlow(a,b,t,time,phase),d=RubyFlow(a,b,t+.002f,time,phase)-RubyFlow(a,b,t-.002f,time,phase),side=Unit(Cross(d,camera-p));float width=(.18f+.04f*strand)*(.55f+.45f*std::sin(t*3.141592654f));ribbon.Add(p-side*width,t*3-time*(.8f+strand*.08f),0);ribbon.Add(p+side*width,t*3-time*(.8f+strand*.08f),1);if(i){unsigned k=i*2;ribbon.Quad(k-2,k,k+1,k-1);}if(i&&i%2==0)RubyBeam(filaments,RubyFlow(a,b,(i-2)/80.f,time,phase),p,camera,.014f,0,1);}draw(ribbon,textures[4],0xb5ff746b,false,true);}
 draw(filaments,nullptr,0xc8ffc3ae,false,true);RubySprites motes;for(int j=0;j<16;++j){float t=std::fmod(time*.32f+j/16.f,1.f);RubyDisc(motes,RubyFlow(a,b,t,time,j*2.4f),camera,.08f+.03f*std::sin(j+time*5),.30f);}RubySpritesDraw(motes,nullptr,0xff9c7b,true);
}
void RubyGrowth(float tick){
 if(input.grow_born<=0||tick<input.grow_born)return;if(!RubyResources().valid||!textures[3])throw std::runtime_error("ruby authored growth resource missing");float t=Clamp((tick-input.grow_born)/36.f,0,1);t=t*t*(3-2*t);Vec at=GolemGroundPosition({input.obelisk_x,input.obelisk_y,0});at.z-=4.1f*(1-t);Mesh stone,ruby;std::vector<Vec> stoneNormals,rubyNormals;
 for(const auto& p:RubyResources().parts)if(p.actor==1)for(const auto& v:p.vertices){auto& m=v.kind==4?ruby:stone;auto& normals=v.kind==4?rubyNormals:stoneNormals;auto k=m.Add(at+Vec{v.x+p.offset.x,v.y+p.offset.y,v.z+p.offset.z},v.u,1-v.v);m.indices.push_back(WORD(k));normals.push_back({v.nx,v.ny,v.nz});}
 draw_authored(stone,stoneNormals,textures[3],0xd9d4d4d4,true);draw_authored(ruby,rubyNormals,textures[3],0xffffdddd,false);
}
void RubyPulse(float time){
 if(input.pulse_born<=0)return;float age=time*18-input.pulse_born;const auto& a=input.ruby[1];Vec base={a.base[0],a.base[1],a.base[2]},camera=Camera();
 if(age<27){RubySprites charge;RubyDisc(charge,RubyCore(1),camera,.4f+age/27.f*.55f,.12f+age/27.f*.18f);RubySpritesDraw(charge,nullptr,0xff5540,true);return;}
 if(!textures[4])throw std::runtime_error("ruby pulse material missing");float radius=(age-27)*2.5f/18.f;if(radius<.05f||radius>90)return;int segments=(std::min)(640,(std::max)(96,int(std::ceil(radius*6.283185307f/.45f))));Mesh core;
 for(int layer=0;layer<2;++layer){Mesh veil;for(int i=0;i<=segments;++i){float a=i*6.283185307f/segments,r=radius+.13f*std::sin(a*13-time*4+layer*2),z=1.08f+.07f*std::sin(a*19+time*3),x=base.x+r*std::cos(a),y=base.y+r*std::sin(a);bool inside=x>=28&&x<=100&&y>=26&&y<=62;float floor=inside?GolemGround(x,y):base.z,height=layer?.65f:1.00f;veil.Add({x,y,floor+z-height*.5f},a*radius/3-time*.55f+layer*.4f,1);veil.Add({x,y,floor+z+height*.5f},a*radius/3-time*.55f+layer*.4f,0);if(i){float pa=(i-1)*6.283185307f/segments,px=base.x+radius*std::cos(pa),py=base.y+radius*std::sin(pa);if(inside&&px>=28&&px<=100&&py>=26&&py<=62){unsigned k=i*2;veil.Quad(k-2,k,k+1,k-1);}}}draw(veil,textures[4],layer?0x68ffa596:0x88ff5b5b,false,true);}
 for(int strand=0;strand<2;++strand){Vec previous{};for(int i=0;i<=segments;++i){float a=i*6.283185307f/segments,r=radius+.12f*std::sin(a*11+time*4+strand*2),z=1.f+(strand?-.16f:.17f)+.08f*std::sin(a*17-time*5+strand*2),x=base.x+r*std::cos(a),y=base.y+r*std::sin(a);bool inside=x>=28&&x<=100&&y>=26&&y<=62;Vec p={x,y,(inside?GolemGround(x,y):base.z)+z};if(i&&inside&&previous.x>=28&&previous.x<=100&&previous.y>=26&&previous.y<=62)RubyBeam(core,previous,p,camera,.012f,0,1);previous=p;}}
 draw(core,nullptr,0x92ffb7a8,false,true);
}
void Ruby(){
 float tick=float(input.tick),time=tick/18.f,age=tick-input.slam_born;
 if(input.slam_born>0&&age>=0&&age<112){Mesh rays,cores;for(float spread:{-24.f,0.f,24.f}){float a=(input.slam_angle+spread)*.01745329252f;Vec direction={std::sin(a),-std::cos(a),0},side={std::cos(a),std::sin(a),0};float end=age<22?55.f:(age-22)*12.f/18.f,start=age<22?0.f:(std::max)(0.f,end-4.5f);for(float distance=start;distance<end;distance+=.7f){Vec at={input.slam_x+direction.x*distance,input.slam_y+direction.y*distance,0};if(at.x<28||at.x>100||at.y<26||at.y>62)break;at.z=GolemGround(at.x,at.y);float width=age<22?.07f:.42f;Vec next=at+direction*.68f;next.z=GolemGround(next.x,next.y);auto k=rays.Add(at-side*width+Vec{0,0,.035f},0,0);rays.Add(next-side*width+Vec{0,0,.035f},0,0);rays.Add(next+side*width+Vec{0,0,.035f},0,0);rays.Add(at+side*width+Vec{0,0,.035f},0,0);rays.Quad(k,k+1,k+2,k+3);if(age>=22)Crystal(cores,at,.13f,.25f+.45f*(1-(end-distance)/5),distance*2);}}draw(rays,nullptr,age<22?0x91e63325:0xd0ff3b23,false,true);draw(cores,nullptr,0xe8ff7056,false,true);}
 RubyGrowth(tick);
 for(int actor=0;actor<2;++actor)if(input.ruby[actor].present&&!input.ruby[actor].dead){RubySprites glow;RubyDisc(glow,RubyCore(actor),Camera(),.52f,.12f+.03f*std::sin(time*3));RubySpritesDraw(glow,nullptr,0xff3424,true);}
 if(!input.ruby[1].present||input.ruby[1].dead)return;
 if(input.role==1)RubyHealing(time);
 if(input.role==2){Vec at=RubyCore(1);RubySprites sparks;for(int i=0;i<6;++i){float a=i*1.047197551f+time*1.8f;RubyDisc(sparks,at+Vec{.6f*std::cos(a),.6f*std::sin(a),.12f*std::sin(a*2)},Camera(),.06f,.20f);}RubySpritesDraw(sparks,nullptr,0xffa170,true);}
 if(input.role==3)RubyPulse(time);
}
void CollapseDust(){
 float age=(input.tick-input.impact_born)/18.f;if(input.impact_born<=0||age<0||age>3)return;if(!textures[5])throw std::runtime_error("collapse dust atlas missing");
 struct DustAnchor{float x,y,delay;};static constexpr DustAnchor pieces[]={{58.6f,16.8f,0},{61.1f,16.8f,5},{63.5f,16.6f,2},{66,16.5f,10},{59.8f,14.9f,7},{62.6f,14.7f,14},{65.2f,14.6f,17}};
 RubySprites dust;int index=0;for(const auto& piece:pieces){float eventAge=age-piece.delay/18.f;if(eventAge<0){++index;continue;}for(int j=0;j<12;++j){float t=eventAge-j*.014f;if(t<0||t>1.8f)continue;unsigned seed=index*1709+j*97;float a=j*2.399963f+GolemNoise(seed),r=.22f+(1.7f+GolemNoise(seed+3))*(1-std::exp(-t*2.5f));Vec at=GolemGroundPosition({piece.x+std::cos(a)*r,piece.y+std::sin(a)*r*.68f,.12f+t*(.20f+.30f*GolemNoise(seed+4))});Vec toward=Unit(Camera()-at),x=Unit(Cross(toward,{0,0,1})),y=Cross(x,toward);float size=.18f+t*(.66f+.26f*GolemNoise(seed+2)),alpha=.23f*(1-t/1.8f)*(1-t/1.8f)*Clamp(t/.08f,0,1);auto& m=dust.Bucket(alpha);int frame=(std::min)(14,2+int(t*7));float u=(frame%4)*.25f,v=(frame/4)*.25f;auto k=m.Add(at-x*size-y*size*.75f,u,v+.25f);m.Add(at+x*size-y*size*.75f,u+.25f,v+.25f);m.Add(at+x*size+y*size*.75f,u+.25f,v);m.Add(at-x*size+y*size*.75f,u,v);m.Quad(k,k+1,k+2,k+3);}++index;}RubySpritesDraw(dust,textures[5],0xc8bca8,false);
}
