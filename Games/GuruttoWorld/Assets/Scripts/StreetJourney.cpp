#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SceneRuntime/ScriptModuleApi.h>
#include <algorithm>
#include <cmath>
namespace {
using namespace SceneRuntime;
float Smooth(float a,float b,float value) {
 const float t=std::clamp((value-a)/(b-a),0.0f,1.0f);return t*t*(3-2*t);
}
ScriptDefinition Camera() {
 ScriptDefinition s;
 s.dataFields={{"target",{ScriptObjectReference{}}}};
 s.fields={{"height",{4.3f,3,6}},{"distance",{7.4f,6,10}},{"lead",{4.7f,2,7}},{"fixedView",{0,0,1}}};
 s.lateUpdate=[](ScriptContext& c) {
  const auto* p=c.Reference("target");if(!p || !c.scene)return;
  const float dt=static_cast<float>(c.seconds),blend=1-std::exp(-5*dt);
  const bool fixedView=c.Value("fixedView",0)>.5f;
  const auto ground=c.scene->Raycast(p->position,{0,-1,0},1.8f,0xffffffffu,false,p->id);
  const bool reset=!c.state["ready"] || std::hypot(p->position[0]-c.state["px"],p->position[2]-c.state["pz"])>6;
  if(ground && ground->normal[1]>.7f) {
   if(reset)c.state["floor"]=ground->position[1];
   else c.state["floor"]+=(ground->position[1]-c.state["floor"])*(1-std::exp(-8*dt));
  }
  c.state["px"]=p->position[0];c.state["pz"]=p->position[2];
  const auto anchor=[&](const char* key,float value,float margin) {
   if(reset)c.state[key]=value;
   else c.state[key]=std::clamp(c.state[key],value-margin,value+margin);
   return c.state[key];
  };
  const float x=anchor("anchorX",p->position[0],.35f),z=anchor("anchorZ",p->position[2],.45f);
  struct Shot {float side,height,back,aimX,aimZ,aimY;};
  Shot shot{.5f,4.4f,7.6f,.2f,3.2f,1.7f};
  const auto mix=[&](Shot next,float t) {
   shot.side+=(next.side-shot.side)*t;shot.height+=(next.height-shot.height)*t;
   shot.back+=(next.back-shot.back)*t;shot.aimX+=(next.aimX-shot.aimX)*t;
   shot.aimZ+=(next.aimZ-shot.aimZ)*t;shot.aimY+=(next.aimY-shot.aimY)*t;
  };
  if(!fixedView) {
   mix({2.6f,4.8f,7.8f,.4f,3.2f,1.6f},Smooth(14,24,z));
   mix({3.2f,5.2f,8,.4f,3.2f,1.6f},Smooth(60,70,z));
  }
  std::array<float,3> wanted{x+shot.side,c.state["floor"]+shot.height,z-shot.back};
  const auto safe=[&](const std::array<float,3>& candidate) {
   std::array<float,3> delta{};float length=0;
   for(size_t i=0;i<3;++i){delta[i]=candidate[i]-p->position[i];length+=delta[i]*delta[i];}
   length=std::sqrt(length);if(length<.01f)return candidate;
   for(auto& v:delta)v/=length;
   float available=length;
   for(float x:{-.25f,0.0f,.25f}) {
    auto origin=p->position;origin[0]+=x;
    if(const auto hit=c.scene->Raycast(origin,delta,length,0xffffffffu,false,p->id))
     available=std::min(available,std::max(.3f,hit->distance-.4f));
   }
   std::array<float,3> result{};
   for(size_t i=0;i<3;++i)result[i]=p->position[i]+delta[i]*available;
   return result;
  };
  wanted=safe(wanted);
  const auto length=[&](const std::array<float,3>& v){return std::hypot(std::hypot(v[0]-p->position[0],v[2]-p->position[2]),v[1]-p->position[1]);};
  if(!fixedView && length(wanted)<4.5f) {
   // Preserve the approach side while finding clearance at a corner.
   const auto candidate=safe({x+shot.side*.65f,c.state["floor"]+shot.height+.6f,z-shot.back*.75f});
   if(length(candidate)>length(wanted)+.4f)wanted=candidate;
  }
  if(reset) {c.object.position=wanted;c.state["ready"]=1;}
  else for(size_t i=0;i<3;++i)c.object.position[i]+=(wanted[i]-c.object.position[i])*blend;
  c.object.position=safe(c.object.position);
  std::array<float,3> aim{x+shot.aimX,c.state["floor"]+shot.aimY,z+shot.aimZ};
  for(size_t i=0;i<3;++i) {
   const auto key="aim"+std::to_string(i);
   if(reset)c.state[key]=aim[i];else c.state[key]+=(aim[i]-c.state[key])*blend;
   aim[i]=c.state[key];
  }
  const float dx=aim[0]-c.object.position[0],dz=std::max(1.0f,aim[2]-c.object.position[2]);
  const float yaw=std::atan2(dx,dz),pitch=std::atan2(c.object.position[1]-aim[1],std::hypot(dx,dz));
  const auto turn=[&](float from,float to) {
   const float delta=std::atan2(std::sin(to-from),std::cos(to-from));
   return from+std::clamp(delta*(1-std::exp(-5*dt)),-.45f*dt,.45f*dt);
  };
  // A fixed view follows translation only; wall clearance remains a shared safeguard.
  if(fixedView)c.object.rotation={std::atan2(2.7f,std::hypot(.3f,10.8f)),std::atan2(-.3f,10.8f),0};
  else c.object.rotation={reset?pitch:turn(c.object.rotation[0],pitch),reset?yaw:turn(c.object.rotation[1],yaw),0};
  if(c.object.camera)c.object.camera->verticalFov=58;

 };
 return s;
}
ScriptDefinition JourneyController() {
 ScriptDefinition s;
 s.update=[](ScriptContext& c) {
  if(!c.scene)return;
  const auto* player=c.scene->Find("park-player");if(!player)return;
  const auto p=player->position;
  if(!c.state["open"])if(const auto* lever=c.scene->Find("journey-lever")) {
   const float distance=std::hypot(p[0]-lever->position[0],p[2]-lever->position[2]);
   if(distance<3 && std::abs(p[1]-lever->position[1])<1.5f && c.Pressed("Attack")) {
    c.state["open"]=1;c.scene->Destroy("journey-shutter");
    ScriptEvent ring;ring.name="street:gear";ring.target="journey-lever";c.scene->Emit(std::move(ring));
    ScriptEvent sound;sound.name="sunrise:play";sound.target="audio-Mechanism";c.scene->Emit(std::move(sound));
   }
  }
  c.scene->SetUiValue("journeyOpen",c.state["open"]);
  if(c.state["complete"])return;
  const auto* goal=c.scene->Find("street-destination");
  if(c.state["open"] && goal && std::abs(p[1]-goal->position[1])<1.3f && std::hypot(p[0]-goal->position[0],p[2]-goal->position[2])<1.5f) {
   c.state["complete"]=1;c.scene->SetUiValue("journeyComplete",1);
   ScriptEvent sound;sound.name="sunrise:play";sound.target="audio-Clear";c.scene->Emit(std::move(sound));
  }
 };
 return s;
}
ScriptDefinition Gear() {
 ScriptDefinition s;
 s.onEvent=[](ScriptContext& c) {if(c.event && c.event->name=="street:gear")c.state["lit"]=1;};
 s.update=[](ScriptContext& c) {
  if(!c.state["lit"])return;
  c.state["time"]+=static_cast<float>(c.seconds);
  c.object.rotation[2]=std::sin(c.state["time"]*8)*.25f*std::exp(-c.state["time"]);
 };
 return s;
}
}
bool Register_StreetJourney(void* owner,SceneRuntime::RegisterModuleScript add) {
 const auto camera=Camera(),gear=Gear(),journey=JourneyController();
 return add(owner,"StreetJourney",&journey) && add(owner,"StreetCamera",&camera) && add(owner,"StreetGear",&gear);
}
