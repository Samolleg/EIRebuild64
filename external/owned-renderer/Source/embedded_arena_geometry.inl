// Dejavu 421604cc78b4259db8be4ad5698525164a70e847: arena_renderer/waves/elemental_vfx.
// Pure geometry only; no retail ABI, draw calls, or CPU lighting.
struct Vec {float x,y,z; Vec operator+(Vec b)const{return{x+b.x,y+b.y,z+b.z};} Vec operator-(Vec b)const{return{x-b.x,y-b.y,z-b.z};} Vec operator*(float f)const{return{x*f,y*f,z*f};}};
static float Dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static Vec Cross(Vec a,Vec b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static Vec Unit(Vec a){float d=std::sqrt(Dot(a,a));return d>.00001f?a*(1/d):Vec{1,0,0};}
using Plot4 = NativePackedGroup;
using UV = NativeTextureCoordinate;
struct Mesh {
 std::vector<Plot4> plots;std::vector<UV> uv;std::vector<std::uint16_t> indices;
 unsigned Add(Vec p,float u,float v){unsigned i=(unsigned)uv.size();if(!(i&3))plots.push_back({});auto& b=plots.back();unsigned l=i&3;b.x[l]=p.x;b.y[l]=p.y;b.z[l]=p.z;b.w[l]=1;uv.push_back({u,v});return i;}
 void Quad(unsigned a,unsigned b,unsigned c,unsigned d){for(unsigned v:{a,b,c,a,c,d})indices.push_back((std::uint16_t)v);}
 void Pad(){while(uv.size()&3)Add({0,0,0},0,0);}
};
static float RadiusAt(float time){return ShieldRadius+.018f*std::sin(time*1.1f);}
static Vec ShieldApex(Vec base,float time){return base+Vec{0,0,ShieldFoot+RadiusAt(time)};}
static Mesh Hemisphere(Vec center,Vec camera,float time,float tile){
 constexpr int S=64,L=20;constexpr float pi=3.14159265358979323846f;
 Mesh m;float r=RadiusAt(time);
 for(int j=0;j<=L;j++)for(int i=0;i<=S;i++){
  float a=2*pi*i/S,p=pi*.5f*j/L;float cp=std::cos(p);
  // V increases away from the crown. Advect the texture from the incoming
  // stream's contact point down towards the rim, rather than up from ground.
  m.Add(center+Vec{r*cp*std::cos(a),r*cp*std::sin(a),ShieldFoot+r*std::sin(p)},tile*i/S+time*.018f,tile*(1.f-(float)j/L)-time*.13f);
 }
 struct Cell{int i,j;float distance;};std::vector<Cell> cells;cells.reserve(S*L);
 for(int j=0;j<L;j++)for(int i=0;i<S;i++){
  float a=2*pi*(i+.5f)/S,p=pi*.5f*(j+.5f)/L;
  Vec q=center+Vec{r*std::cos(p)*std::cos(a),r*std::cos(p)*std::sin(a),r*std::sin(p)}-camera;
  cells.push_back({i,j,Dot(q,q)});
 }
 std::sort(cells.begin(),cells.end(),[](const Cell&a,const Cell&b){return a.distance>b.distance;});
 for(const auto& c:cells){unsigned a=c.j*(S+1)+c.i;m.Quad(a,a+1,a+S+2,a+S+1);}
 return m;
}
static Vec FlowPoint(Vec start,Vec end,float t,float time,float phase){
 constexpr float pi=3.14159265358979323846f;Vec lateral=Unit(Cross(end-start,{0,0,1}));
 Vec p1=start+(end-start)*.35f+Vec{0,0,1.2f},p2=end+Vec{0,0,3.3f};float s=1-t;
 Vec curve=start*(s*s*s)+p1*(3*s*s*t)+p2*(3*s*t*t)+end*(t*t*t);
 float env=std::sin(pi*t);env*=env;
 // Both the position and the terminal tangent stay attached. The stream
 // bends down vertically into the dome crown, instead of hitting its side.
 return curve+lateral*(env*(.42f*std::sin(t*12-time*1.7f+phase)+.14f*std::sin(t*25+time*.8f+phase)))+Vec{0,0,env*.32f*std::cos(t*15-time*1.1f+phase)};
}
static Mesh Ribbon(Vec start,Vec end,Vec camera,float time,int strand){
 Mesh m;constexpr int N=80;float phase=strand*2.0943951f;
 for(int i=0;i<=N;i++){
  float t=(float)i/N;Vec p=FlowPoint(start,end,t,time,phase);
  Vec tangent=FlowPoint(start,end,t+.002f,time,phase)-FlowPoint(start,end,t-.002f,time,phase);
  Vec side=Unit(Cross(tangent,camera-p));float width=(.44f+strand*.055f)*(.80f+.20f*std::sin(t*18-time+phase));
  m.Add(p-side*width,t*3-time*(.32f+strand*.04f),0);m.Add(p+side*width,t*3-time*(.32f+strand*.04f),1);
 }
 for(int i=0;i<N;i++){unsigned a=i*2;m.Quad(a,a+2,a+3,a+1);}return m;
}
static Vec Spin(Vec v,Vec axis,float angle){float c=std::cos(angle),s=std::sin(angle);return v*c+Cross(axis,v)*s+axis*(Dot(axis,v)*(1-c));}
static Mesh FractureFlash(Vec base,float age){
 Mesh m;
 if(age<0||age>.35f)return m;
 for(float level:{1.75f,3.3f,4.9f,6.5f,8.25f}){
  float width=level<6.5f?1.1f-(level-.84f)*.055f:.83f;
  for(int i=0;i<4;i++){
   float a=i*1.5707963268f,b=(i+1)*1.5707963268f;
   Vec p={width*(std::cos(a)-std::sin(a)),width*(std::sin(a)+std::cos(a)),0};
   Vec q={width*(std::cos(b)-std::sin(b)),width*(std::sin(b)+std::cos(b)),0};
   p.z=level-.18f*p.x+.12f*p.y;q.z=level-.18f*q.x+.12f*q.y;
   unsigned n=m.Add(base+p+Vec{0,0,-.035f},0,0);m.Add(base+q+Vec{0,0,-.035f},1,0);m.Add(base+q+Vec{0,0,.035f},1,1);m.Add(base+p+Vec{0,0,.035f},0,1);m.Quad(n,n+1,n+2,n+3);
  }
 }
 return m;
}
// Supplemental lightning ribbons and closed 3D ice splinters only.
// Flames, embers, mist and sparks are native CParticle systems.
static constexpr float VfxPi=3.14159265358979323846f;
static float VfxClamp(float x,float lo,float hi){return (std::max)(lo,(std::min)(hi,x));}
static float VfxHash(unsigned x){x^=x>>16;x*=0x7feb352d;x^=x>>15;x*=0x846ca68b;x^=x>>16;return (x&0xffffffu)/16777215.f;}
static float VfxNoise(unsigned seed,float time){float t=std::floor(time),f=time-t;f=f*f*(3-2*f);return VfxHash(seed+(unsigned)t*7919u)*(1-f)+VfxHash(seed+((unsigned)t+1u)*7919u)*f;}
static Vec VfxPolar(Vec base,float r,float a,float z){return base+Vec{r*std::cos(a),r*std::sin(a),z};}
struct VfxSprites {
 Mesh opacity[8];
 Mesh& Bucket(float alpha){int b=(int)std::ceil(VfxClamp(alpha,0,1)*8)-1;return opacity[(std::max)(0,(std::min)(7,b))];}

};
struct VfxElement {
 VfxSprites fire,frost,sparks,ice;
 Mesh bolts,cores;
};
static void VfxBeam(Mesh&m,Vec a,Vec b,Vec camera,float width,float u0,float u1){
 Vec side=Unit(Cross(b-a,camera-(a+b)*.5f))*width;
 unsigned i=m.Add(a-side,u0,0);m.Add(b-side,u1,0);m.Add(b+side,u1,1);m.Add(a+side,u0,1);m.Quad(i,i+1,i+2,i+3);
}
static void VfxElectric(VfxElement&fx,Vec base,Vec camera,float radius,float time,bool charge){
 int n=(int)VfxClamp(std::ceil(2*VfxPi*radius/.40f),64,768);
 float strength=charge?.4f:1.f;
 for(int strand=0;strand<3;strand++){
  Vec previous={};
  for(int i=0;i<=n;i++){
   int j=i%n;unsigned seed=j*37u+strand*1193u;
   float a=2*VfxPi*j/n,noise=VfxNoise(seed,time*15)-.5f;
   float r=radius+(strand-1)*.38f+.65f*noise;
   float z=1.05f+(strand-1)*.13f+.38f*(VfxNoise(seed+13,time*17)-.5f);
   Vec p=VfxPolar(base,(std::max)(.1f,r),a,z);
   if(i){VfxBeam(fx.bolts,previous,p,camera,.072f*strength,i*.17f-time*2,(i+1)*.17f-time*2);VfxBeam(fx.cores,previous,p,camera,.013f*strength,0,1);}
   if(j%9==0){
    float direction=VfxNoise(seed+29,time*12)-.5f;
    Vec mid=p+Vec{std::cos(a)*direction*.8f,std::sin(a)*direction*.8f,.13f*std::sin(time*23+seed)};
    Vec end=p+Vec{std::cos(a)*direction*1.8f,std::sin(a)*direction*1.8f,.30f*(VfxNoise(seed+31,time*11)-.5f)};
    VfxBeam(fx.bolts,p,mid,camera,.045f*strength,0,.6f);VfxBeam(fx.bolts,mid,end,camera,.027f*strength,.6f,1);
    VfxBeam(fx.cores,p,mid,camera,.009f*strength,0,1);VfxBeam(fx.cores,mid,end,camera,.006f*strength,0,1);
   }
   previous=p;
  }
 }
}
static void VfxIceShard(VfxSprites&dst,Vec p,float angle,float width,float height,float alpha,float time){
 Vec x={std::cos(angle)*width,std::sin(angle)*width,0},y={-std::sin(angle)*width*.65f,std::cos(angle)*width*.65f,0};
 Vec c=p+Vec{.08f*std::cos(angle+time),.08f*std::sin(angle+time),height*.28f};
 Vec v[]={p,c+x,c+y,c-x,c-y,p+Vec{height*.18f*std::cos(angle),height*.18f*std::sin(angle),height}};
 for(int i=0;i<4;i++)for(int top=0;top<2;top++){
  Vec a=v[top?5:0],b=v[1+i],d=v[1+(i+1)%4];
  auto&m=dst.Bucket(alpha);
  unsigned first=m.Add(a,0,0);m.Add(top?b:d,0,0);m.Add(top?d:b,0,0);
  m.indices.push_back((std::uint16_t)first);m.indices.push_back((std::uint16_t)(first+1));m.indices.push_back((std::uint16_t)(first+2));
 }
}
static void VfxFrost(VfxElement&fx,Vec base,Vec /*camera*/,float radius,float time,bool charge){
 float strength=charge?.38f:1.f;
 for(int layer=0;layer<(charge?1:3);layer++){
  float r=layer==0?radius:std::floor(radius/4)*4-layer*4;if(r<.5f)continue;
  float age=(radius-r)/60.f,fade=layer==0?1.f:std::pow((std::max)(0.f,1-age/.22f),1.2f)*.4f;
  int n=(int)VfxClamp(std::ceil(2*VfxPi*r/1.05f),20,384);
  for(int i=0;i<n;i++){
   unsigned seed=i*137u+(layer?(unsigned)(r/4)*541u:0)+977;
   float a=2*VfxPi*i/n,rand=VfxHash(seed);
   Vec p=VfxPolar(base,r,a,.05f);
   float h=(.32f+.46f*rand)*(charge?.65f:1.f);
   VfxIceShard(fx.ice,p,a+rand*3,.13f+.15f*rand,h,.70f*fade*strength,time);


  }
 }
}
static VfxElement ElementalWave(Vec base,Vec camera,float radius,int kind,float time,bool charge){
 VfxElement fx;if(!std::isfinite(radius)||radius<0||radius>124||kind<1||kind>3)return fx;
 radius=(std::max)(.6f,radius);
 if(kind==1)VfxElectric(fx,base,camera,radius,time,charge);

 if(kind==3)VfxFrost(fx,base,camera,radius,time,charge);
 return fx;
}
