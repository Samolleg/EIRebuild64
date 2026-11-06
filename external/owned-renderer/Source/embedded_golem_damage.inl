Vec SavedPoint(const EmbeddedGolemNode& node,Vec point) const {
 if(!node.valid)throw std::runtime_error("golem saved bone missing");const auto& q=node.rotation;
 return Vec{node.position[0],node.position[1],node.position[2]}+GolemRotate({q[0],q[1],q[2],q[3]},point);
}
float Support(const std::vector<Vec>& points,Vec center,float lift) const {
 float height=-1.e20f;for(auto p:points)height=(std::max)(height,GolemGround(center.x+p.x,center.y+p.y)-p.z+lift);return height;
}
void Fractures() {
 const bool death=input.dead&&input.death_born>0;int index=0;
 const float born=death?input.death_born:input.detach_born,age=(input.tick-born)/18.f;
 if(death||born>0&&age>=0&&age<=7)for(const auto& part:resource.parts){
  if(death?part.kind!=2:part.kind!=0||int(part.stage)<=input.detach_from||int(part.stage)>input.detach_to)continue;
  if(!death&&index>=24)break;
  const int bone=std::atoi(part.bone+1);if(bone<0||bone>=30)throw std::runtime_error("golem resource bone invalid");
  const auto& pose=death?input.death_pose[bone]:input.detached_pose[bone];
  std::vector<Vec> local;local.reserve(part.count);Vec center{};
  for(const auto& v:part.vertices){const auto p=SavedPoint(pose,{v.x,v.y,v.z});local.push_back(p);center=center+p;}
  center=center*(1.f/part.count);for(auto& p:local)p=p-center;
  const float angle=2.3999632f*index;Vec axis=Unit(Vec{std::sin(angle),-std::cos(angle),death?.3f:.2f});
  Vec velocity;float delay=0,turn=1.7f;
  if(death){Vec away=center-Vec{input.death_base[0],input.death_base[1],input.death_base[2]};away.z=0;if(Dot(away,away)<.01f)away={std::cos(angle),std::sin(angle),0};away=Unit(away);
   float speed=part.core?1.5f:.45f+.18f*(index%4);velocity=away*speed+Vec{.12f*std::sin(angle),.12f*std::cos(angle),part.core?1.6f:.25f+.12f*(index%3)};
   turn=1.1f+.31f*(index%5);delay=part.core?.42f:.03f*(index%5);
  }else velocity={1.2f*std::cos(angle),1.2f*std::sin(angle),1.1f+.1f*((index+1)%4)};
  const float time=(std::max)(0.f,age-delay),flight=(std::min)(time,death?2.7f:1.4f);
  std::vector<Vec> moved; moved.reserve(local.size());for(auto p:local)moved.push_back(Spin(p,axis,flight*turn));
  Vec c=center+velocity*flight+Vec{0,0,-4.9f*flight*flight};const float floor=Support(moved,c,death?.025f:.035f);
  if(death&&c.z<floor){
   const float start=(velocity.z+std::sqrt(velocity.z*velocity.z+19.6f*(std::max)(0.f,center.z-floor)))/9.8f;
   const float settle=(std::min)(time,start+.20f);moved.clear();for(auto p:local)moved.push_back(Spin(p,axis,settle*turn));
   c=center+velocity*settle;c.z=Support(moved,c,.025f);
  }else c.z=(std::max)(c.z,floor);
  Mesh mesh;for(unsigned i=0;i<moved.size();++i){const auto& v=part.vertices[i];auto k=mesh.Add(c+moved[i],v.u,1-v.v);mesh.indices.push_back(WORD(k));}
  auto* texture=textures[part.texture==1?0:1];if(!texture)throw std::runtime_error("golem authored material missing");
  draw(mesh,texture,death?0xffa8b3bfu:0xffacb8c4u,true,false);
  if(death&&part.core&&time<2.3f){float fade=(std::max)(0.f,1-time/2.3f);unsigned alpha=unsigned(200*fade*fade);if(alpha)draw(mesh,texture,(alpha<<24)|(input.hard?0xff8a8au:0x8ae8ffu),false,true);}
  ++index;
 }
 if(input.dead||input.stage<3)return;
 auto* node=Node(0);if(!node)throw std::runtime_error("golem core attachment missing");Mesh glow;
 const Vec center=input.stage==3?Vec{.30f,-.37f,2.18f}:Vec{0,-.10f,1.17f};
 if(input.stage==3){Vec points[]={{0,0,.72f},{.43f,0,0},{0,-.40f,0},{-.43f,0,0},{0,.40f,0},{0,0,-.72f}};
  for(int end:{0,5})for(int side=0;side<4;++side)for(int k:{end,1+side,1+(side+1)%4}){auto id=glow.Add(NodePoint(node,center+points[k]*.20f),0,0);glow.indices.push_back(WORD(id));}
 }else for(const auto& part:resource.parts)if(part.kind==1&&part.stage==input.stage)for(const auto& v:part.vertices){auto k=glow.Add(NodePoint(node,center+(Vec{v.x,v.y,v.z}-center)*1.008f),v.u,1-v.v);glow.indices.push_back(WORD(k));}
 float pulse=.75f+.25f*std::sin(input.tick/18.f*3.2f);unsigned alpha=unsigned((input.stage==5?150:95)*pulse);
 draw(glow,input.stage>=4?textures[0]:nullptr,(alpha<<24)|(input.hard?0xff887au:0x88e2ffu),false,true);
}
