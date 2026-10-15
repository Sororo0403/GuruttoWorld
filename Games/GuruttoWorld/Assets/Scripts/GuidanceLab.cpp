#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SceneRuntime/ScriptModuleApi.h>
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

bool Register_GuidanceLab(void*,SceneRuntime::RegisterModuleScript);
namespace {
using namespace SceneRuntime;
constexpr const char* gallery="Assets/Scenes/GuidanceGallery.json";
const std::array<std::string,6> names{"定番  擬人法","定番  反復法","珍しい  換喩","珍しい  漸層法","激レア  Anadiplosis","激レア  Epanalepsis"};
const std::array<std::string,6> descriptions{
    "広場の建物が、近づいたあなたを迎える。",
    "同じ入口とテラスを、歩くたびに読み取る。",
    "店の営みを頼りに、左右の広場を巡る。",
    "小庭から大テラスへ上り、広場へ帰る。",
    "時計塔から温室へ、景色の起点を引き継ぐ。",
    "入口の門へ、街を巡って帰ってくる。"};
struct Result {bool present=false;float seconds=0,distance=0,bells=0,coins=0;std::string status;};
struct Session {bool mirror=false,blind=false,exportError=false;std::array<Result,6> last{};} session;
std::string Course(int mode,bool mirror) {return "Assets/Scenes/GuidanceLab/Course-"+std::to_string(mode)+(mirror?"B":"A")+".json";}
std::filesystem::path Root() {
 HMODULE module=nullptr;
 GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
     reinterpret_cast<LPCWSTR>(&Register_GuidanceLab),&module);
 std::array<wchar_t,32768> path{};
 if(GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()))) {
  auto folder=std::filesystem::path(path.data()).parent_path();
  while(!folder.empty()) {if(std::filesystem::exists(folder/"Assets/Project.json"))return folder;
   const auto parent=folder.parent_path();if(parent==folder)break;folder=parent;}
 }
 return std::filesystem::current_path()/"Games/GuruttoWorld";
}
void Text(ScriptContext& c,const std::string& id,const std::string& value) {
 const auto* source=c.scene->Find(id);
 if(source && source->text && source->text->text!=value) {
  auto changed=*source;changed.text->text=value;c.scene->SetComponents(id,changed);
 }
}
void Refresh(ScriptContext& c) {
 Text(c,"lab-layout",session.mirror?"配置 B  /  Mで反転":"配置 A  /  Mで反転");
 Text(c,"lab-blind",session.blind?"比較モード：技法名を隠す  /  Bで切替":"発表モード：技法名を表示  /  Bで切替");
 for(int i=0;i<6;++i) {
  const auto prefix=std::to_string(i+1)+"   ";
  Text(c,"lab-name-"+std::to_string(i),prefix+(session.blind?"コース "+std::to_string(i+1):names[i]));
  Text(c,"lab-description-"+std::to_string(i),session.blind?"鐘を2つ鳴らして、街の扉を開いてください。":descriptions[i]);
  const auto& r=session.last[i];
  std::ostringstream out;out<<std::fixed<<std::setprecision(1);
  if(r.present) out<<(r.status=="complete"?"到着":"中断")<<"  "<<r.seconds<<"秒 / "<<r.distance<<"m / 鐘 "<<static_cast<int>(r.bells)<<"/2 / 花 "<<static_cast<int>(r.coins)<<"/6";
  else out<<"未プレイ";
  Text(c,"lab-result-"+std::to_string(i),out.str());
 }
 Text(c,"lab-export",session.exportError?"記録の保存に失敗しました。書き込み先を確認してください。":"記録は自動保存されます。R：やり直す / Tab・Esc：一覧 / M：配置反転");
}
void Record(ScriptContext& c,const std::string& status) {
 if(c.state["recorded"] || !c.state["started"])return;
 c.state["recorded"]=1;
 const int mode=static_cast<int>(c.Value("mode"));
 auto& result=session.last[mode];
 result={true,c.state["elapsed"],c.state["distance"],c.state["bell0"]+c.state["bell1"],c.state["collectedCoins"],status};
 std::array<wchar_t,32768> overridePath{};
 const auto overrideLength=GetEnvironmentVariableW(L"WP1_GUIDANCE_RESULTS",overridePath.data(),static_cast<DWORD>(overridePath.size()));
 const auto file=overrideLength && overrideLength<overridePath.size()?std::filesystem::path(overridePath.data()):Root()/"generated/guidance/trials-v5.csv";
 std::error_code ec;std::filesystem::create_directories(file.parent_path(),ec);
 if(ec) {session.exportError=true;return;}
 const bool header=!std::filesystem::exists(file) || std::filesystem::file_size(file,ec)==0;
 std::ofstream output(file,std::ios::app);output.imbue(std::locale::classic());
 if(header)output<<"study_version,run_id,case_id,layout,blind,status,seconds,distance_m,bells,coins,stationary_seconds\n";
 output<<"5,"<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()<<','
     <<mode<<','<<(c.Value("mirror")>0?'B':'A')<<','<<(c.state["blind"]>0?1:0)<<','<<status<<','
     <<std::fixed<<std::setprecision(3)<<result.seconds<<','<<result.distance<<','<<result.bells<<','<<result.coins<<','<<c.state["stationary"]<<'\n';
 output.flush();session.exportError=!output;
}
void Launch(ScriptContext& c,int mode) {c.scene->LoadScene(Course(mode,session.mirror));}
ScriptDefinition Controller() {
 ScriptDefinition s;
 s.fields={{"mode",{-1,-2,5}},{"mirror",{0,0,1}}};
 s.start=[](ScriptContext& c) {
  const int mode=static_cast<int>(c.Value("mode"));
  if(mode>=0) {session.mirror=c.Value("mirror")>0;c.state["blind"]=session.blind?1.0f:0.0f;}
  if(mode==-1)Refresh(c);
 };
 s.onEvent=[](ScriptContext& c) {
  if(!c.event || !c.scene)return;
  if(c.event->name=="guidance:gallery")c.scene->LoadScene(gallery);
  if(static_cast<int>(c.Value("mode"))!=-1)return;
  if(c.event->name=="guidance:mirror") {session.mirror=!session.mirror;Refresh(c);}
  if(c.event->name=="guidance:blind") {session.blind=!session.blind;Refresh(c);}
  for(int i=0;i<6;++i)if(c.event->name=="guidance:course:"+std::to_string(i))Launch(c,i);
 };
 s.update=[](ScriptContext& c) {
  if(!c.scene)return;
  const int mode=static_cast<int>(c.Value("mode"));
  if(c.Pressed("GuidanceGallery")) {if(mode>=0)Record(c,"menu");c.scene->LoadScene(gallery);return;}
  if(mode==-2)return;
  if(mode==-1) {
   for(int i=0;i<6;++i)if(c.Pressed("GuidanceCourse"+std::to_string(i+1))) {Launch(c,i);return;}
   if(c.Pressed("GuidanceMirror")){session.mirror=!session.mirror;Refresh(c);}
   if(c.Pressed("GuidanceBlind")){session.blind=!session.blind;Refresh(c);}
   if(c.Pressed("Cancel"))c.scene->LoadScene("Assets/Scenes/TitleStreet.json");
   return;
  }
  if(c.Pressed("GuidanceMenu")) {Record(c,"menu");c.scene->LoadScene(gallery);return;}
  if(c.Pressed("GuidanceMirror")) {Record(c,"mirror");session.mirror=!session.mirror;Launch(c,mode);return;}
  if(c.Pressed("Restart") || c.Pressed("Cancel")) {Record(c,c.Pressed("Restart")?"restart":"menu");return;}
  const auto* player=c.scene->Find("park-player");if(!player)return;
  const auto p=player->position;const auto playerId=player->id;
  c.state["collectedCoins"]=6.0f-static_cast<float>(c.scene->FindByName("ParkCoin").size());
  if(c.state["done"])return;
  if(!c.state["started"] && (c.Input("MoveForward")+c.Input("MoveBack")+c.Input("MoveLeft")+c.Input("MoveRight")>.1f)) {
   c.state["started"]=1;c.state["px"]=p[0];c.state["pz"]=p[2];
  }
  if(!c.state["started"])return;
  const float dt=static_cast<float>(c.seconds),distance=std::hypot(p[0]-c.state["px"],p[2]-c.state["pz"]);
  c.state["elapsed"]+=dt;if(distance<4)c.state["distance"]+=distance;
  if(distance<.005f)c.state["stationary"]+=dt;
  for(int i=0;i<2;++i) {
   const auto id=std::to_string(i);const auto* bell=c.scene->Find("stage-bell-"+id);
   if(!bell || c.state["bell"+id])continue;
   const float bellDistance=std::hypot(p[0]-bell->position[0],p[2]-bell->position[2]);
   if(std::abs(p[1]-bell->position[1])>1.0f || (bellDistance>1.2f && !(bellDistance<2.6f && c.Pressed("Attack"))))continue;
   const std::array<float,3> origin{p[0],p[1]+.2f,p[2]};
   auto ray=bell->position;float length=0;
   for(size_t axis=0;axis<3;++axis){ray[axis]-=origin[axis];length+=ray[axis]*ray[axis];}
   length=std::sqrt(length);if(length>.01f)for(auto& v:ray)v/=length;
   const auto obstacle=c.scene->Raycast(origin,ray,length,0xffffffffu,false,playerId);
   if(obstacle && obstacle->distance<length-.15f)continue;
   const auto bellId=bell->id;
   c.state["bell"+id]=1;c.scene->Destroy("bell-seal-"+id);c.scene->Destroy("stage-lock-token-"+id);
   ScriptEvent ring;ring.name="guidance:ring";ring.target=bellId;c.scene->Emit(std::move(ring));
   ScriptEvent sound;sound.name="sunrise:play";sound.target="audio-Mechanism";c.scene->Emit(std::move(sound));
  }
  const bool unlocked=c.state["bell0"] && c.state["bell1"];
  if(unlocked && !c.state["unlocked"]) {c.state["unlocked"]=1;c.scene->Destroy("stage-exit-lock");}
  c.scene->SetUiValue("guidanceBells",c.state["bell0"]+c.state["bell1"]);
  c.state["px"]=p[0];c.state["pz"]=p[2];
  const auto* goal=c.scene->Find("lab-destination");
  if(unlocked && goal && std::abs(p[1]-goal->position[1])<1.5f && std::hypot(p[0]-goal->position[0],p[2]-goal->position[2])<1.7f) {
   c.state["done"]=1;c.scene->SetUiValue("guidanceComplete",1);Record(c,"complete");
   ScriptEvent event;event.name="sunrise:play";event.target="audio-Clear";c.scene->Emit(std::move(event));
  }
 };
 s.stop=[](ScriptContext& c) {if(c.Value("mode")>=0)Record(c,"interrupted");};
 return s;
}
float Smooth(float a,float b,float value) {
 const float t=std::clamp((value-a)/(b-a),0.0f,1.0f);return t*t*(3-2*t);
}
ScriptDefinition Camera() {
 ScriptDefinition s;
 s.dataFields={{"target",{ScriptObjectReference{}}},{"focus0",{ScriptObjectReference{}}},
     {"focus1",{ScriptObjectReference{}}},{"focus2",{ScriptObjectReference{}}},{"focus3",{ScriptObjectReference{}}}};
 s.fields={{"mode",{4,0,5}},{"mirror",{0,0,1}},{"height",{4.3f,3,6}},
     {"distance",{7.4f,6,10}},{"lead",{4.7f,2,7}},{"weight",{.23f,0,.4f}},
     {"boundary0",{23,12,35}},{"boundary1",{40,30,55}},{"boundary2",{65,55,80}}};
 s.lateUpdate=[](ScriptContext& c) {
  const auto* p=c.Reference("target");if(!p || !c.scene)return;
  const float dt=static_cast<float>(c.seconds),blend=1-std::exp(-5*dt);
  const auto ground=c.scene->Raycast(p->position,{0,-1,0},1.8f,0xffffffffu,false,p->id);
  if(ground && ground->normal[1]>.7f)c.state["floor"]+=(ground->position[1]-c.state["floor"])*(1-std::exp(-10*dt));
  auto focus=p->position;float sum=0;
  std::array<float,3> centroid{};
  for(int i=0;i<4;++i)if(const auto* f=c.Reference("focus"+std::to_string(i))) {
   const float d=std::hypot(f->position[0]-p->position[0],f->position[2]-p->position[2]);
   const float weight=1/(16+d*d);sum+=weight;
   for(size_t axis=0;axis<3;++axis)centroid[axis]+=f->position[axis]*weight;
  }
  if(sum>0)for(size_t axis=0;axis<3;++axis)focus[axis]=centroid[axis]/sum;
  if(!c.state["motionReady"]) {c.state["px"]=p->position[0];c.state["pz"]=p->position[2];c.state["motionReady"]=1;}
  const float vx=std::clamp((p->position[0]-c.state["px"])/std::max(dt,.001f),-5.0f,5.0f);
  const float vz=std::clamp((p->position[2]-c.state["pz"])/std::max(dt,.001f),-5.0f,5.0f);
  c.state["px"]=p->position[0];c.state["pz"]=p->position[2];
  c.state["vx"]+=(vx-c.state["vx"])*blend;c.state["vz"]+=(vz-c.state["vz"])*blend;
  const float bias=std::clamp(c.state["vx"]*.48f,-2.3f,2.3f)+std::clamp((focus[0]-p->position[0])*c.Value("weight"),-.7f,.7f);
  const float ahead=c.state["vz"]<-.3f?-2.4f:c.Value("lead");
  c.state["lead"]+=(ahead-c.state["lead"])*blend;
  std::array<float,3> wanted{p->position[0]+bias,c.state["floor"]+c.Value("height"),p->position[2]-c.Value("distance")};
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
  c.object.rotation={std::atan2(c.object.position[1]-(c.state["floor"]+1.9f),dz),0,0};
 };
 return s;
}
ScriptDefinition Welcome() {
 ScriptDefinition s;s.fields={{"kind",{0,0,2}}};s.dataFields={{"target",{ScriptObjectReference{}}}};
 s.start=[](ScriptContext& c) {c.state["baseY"]=c.object.position[1];c.state["baseX"]=c.object.position[0];};
 s.update=[](ScriptContext& c) {
  const auto* p=c.Reference("target");if(!p)return;
  const float distance=std::hypot(p->position[0]-c.object.position[0],p->position[2]-c.object.position[2]);
  const float welcome=1-Smooth(9,17,distance);
  c.state["open"]+=(welcome-c.state["open"])*(1-std::exp(-3*static_cast<float>(c.seconds)));
  if(c.Value("kind")==2)c.object.position[0]=c.state["baseX"]+std::clamp((p->position[0]-c.state["baseX"])*.06f,-.24f,.24f)*c.state["open"];
  else if(c.Value("kind")==1)c.object.rotation[0]=-.52f*c.state["open"];
  else c.object.position[1]=c.state["baseY"]+.55f*c.state["open"];
 };
 return s;
}
ScriptDefinition Bell() {
 ScriptDefinition s;
 s.onEvent=[](ScriptContext& c) {if(c.event && c.event->name=="guidance:ring")c.state["lit"]=1;};
 s.update=[](ScriptContext& c) {
  if(!c.state["lit"])return;
  c.state["time"]+=static_cast<float>(c.seconds);
  c.object.rotation[2]=std::sin(c.state["time"]*8)*.25f*std::exp(-c.state["time"]);
 };
 return s;
}
}
bool Register_GuidanceLab(void* owner,SceneRuntime::RegisterModuleScript add) {
 const auto controller=Controller(),camera=Camera(),welcome=Welcome(),bell=Bell();
 return add(owner,"GuidanceLab",&controller) && add(owner,"GuidanceCamera",&camera) && add(owner,"GuidanceWelcome",&welcome) && add(owner,"GuidanceBell",&bell);
}
