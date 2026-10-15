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
 s.fields={{"height",{4.3f,3,6}},{"distance",{7.4f,6,10}},{"lead",{4.7f,2,7}}};
 s.lateUpdate=[](ScriptContext& c) {
  const auto* p=c.Reference("target");if(!p || !c.scene)return;
  const float dt=static_cast<float>(c.seconds),blend=1-std::exp(-5*dt);
  const auto ground=c.scene->Raycast(p->position,{0,-1,0},1.8f,0xffffffffu,false,p->id);
  if(ground && ground->normal[1]>.7f)c.state["floor"]+=(ground->position[1]-c.state["floor"])*(1-std::exp(-10*dt));
  if(!c.state["motionReady"]) {c.state["px"]=p->position[0];c.state["pz"]=p->position[2];c.state["motionReady"]=1;}
  if(std::hypot(p->position[0]-c.state["px"],p->position[2]-c.state["pz"])>6)c.state["ready"]=0;
  const float vz=std::clamp((p->position[2]-c.state["pz"])/std::max(dt,.001f),-5.0f,5.0f);
  c.state["px"]=p->position[0];c.state["pz"]=p->position[2];
  c.state["vz"]+=(vz-c.state["vz"])*blend;
  float bias=0;
  const float ahead=c.state["vz"]<-.3f?-2.4f:c.Value("lead");
  c.state["lead"]+=(ahead-c.state["lead"])*blend;
  float height=c.Value("height"),distance=c.Value("distance");
  {
   const float cafe=Smooth(15,21,p->position[2])*(1-Smooth(28,33,p->position[2]));
   const float fork=Smooth(28,34,p->position[2])*(1-Smooth(45,49,p->position[2]));
   const float roof=Smooth(68,72,p->position[2]);
   bias=3.8f*cafe-1.2f*fork-2.2f*roof;
   height=4.6f+.8f*fork+1.0f*roof;distance=7.3f+.5f*fork;
  }
  std::array<float,3> wanted{p->position[0]+bias,c.state["floor"]+height,p->position[2]-distance};
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
  if(length(wanted)<4.5f) {
   // Give corners an alternative camera aisle before accepting a close pull-in.
   for(float side:{-2.5f,2.5f}) {
    const auto candidate=safe({p->position[0]+side,c.state["floor"]+c.Value("height"),p->position[2]-c.Value("distance")});
    if(length(candidate)>length(wanted)+.6f)wanted=candidate;
   }
  }
  if(!c.state["ready"]) {c.object.position=wanted;c.state["aimX"]=p->position[0]+bias;c.state["ready"]=1;}
  else for(size_t i=0;i<3;++i)c.object.position[i]+=(wanted[i]-c.object.position[i])*blend;
  c.object.position=safe(c.object.position);
  const float dz=std::max(1.0f,p->position[2]+c.state["lead"]-c.object.position[2]);
  const float yaw=std::atan2(p->position[0]+bias*.35f-c.object.position[0],dz);
  c.object.rotation={std::atan2(c.object.position[1]-(c.state["floor"]+1.9f),dz),yaw,0};
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
