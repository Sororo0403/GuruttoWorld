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
const std::array<std::string,6> names{"定番  擬人法","定番  反復法","珍しい  換喩","珍しい  漸層法","激レア  Anadiplosis","激レア  Anapodoton"};
const std::array<std::string,6> descriptions{
    "鉢植えが枝で手招きして、通りへ誘う。",
    "同じ庇を、同じ強さで繰り返す。",
    "カップから、その先のカフェを連想する。",
    "植栽を小・中・大と段階的に強める。",
    "前の終わりの目印を、次の始まりへ渡す。",
    "門とカップの対応を示し、最後の帰結を空ける。"};
struct Result {bool present=false;float seconds=0,distance=0,choice=0,decision=0,backtracks=0;std::string status;};
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
  Text(c,"lab-description-"+std::to_string(i),session.blind?"奥のカフェを見つけてください。":descriptions[i]);
  const auto& r=session.last[i];
  std::ostringstream out;out<<std::fixed<<std::setprecision(1);
  if(r.present) out<<(r.status=="complete"?"到着":"中断")<<"  "<<r.seconds<<"秒 / "<<r.distance<<"m / 最初の分岐 "<<(r.choice>0?"右":r.choice<0?"左":"未選択");
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
 result={true,c.state["elapsed"],c.state["distance"],c.state["choice"],c.state["decision"],c.state["backtracks"],status};
 std::array<wchar_t,32768> overridePath{};
 const auto overrideLength=GetEnvironmentVariableW(L"WP1_GUIDANCE_RESULTS",overridePath.data(),static_cast<DWORD>(overridePath.size()));
 const auto file=overrideLength && overrideLength<overridePath.size()?std::filesystem::path(overridePath.data()):Root()/"generated/guidance/trials-v2.csv";
 std::error_code ec;std::filesystem::create_directories(file.parent_path(),ec);
 if(ec) {session.exportError=true;return;}
 const bool header=!std::filesystem::exists(file) || std::filesystem::file_size(file,ec)==0;
 std::ofstream output(file,std::ios::app);output.imbue(std::locale::classic());
 if(header)output<<"study_version,run_id,case_id,layout,blind,status,seconds,distance_m,first_branch,decision_seconds,backtracks,stationary_seconds\n";
 output<<"2,"<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()<<','
     <<mode<<','<<(c.Value("mirror")>0?'B':'A')<<','<<(c.state["blind"]>0?1:0)<<','<<status<<','
     <<std::fixed<<std::setprecision(3)<<result.seconds<<','<<result.distance<<','<<result.choice<<','<<result.decision<<','<<result.backtracks<<','<<c.state["stationary"]<<'\n';
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
  const auto p=player->position;
  if(c.state["done"]) {c.state["after"]+=static_cast<float>(c.seconds);if(c.state["after"]>.8f && !c.state["queued"]) {c.state["queued"]=1;c.scene->LoadScene(gallery);}return;}
  if(!c.state["started"] && (c.Input("MoveForward")+c.Input("MoveBack")+c.Input("MoveLeft")+c.Input("MoveRight")>.1f)) {
   c.state["started"]=1;c.state["px"]=p[0];c.state["pz"]=p[2];
  }
  if(!c.state["started"])return;
  const float dt=static_cast<float>(c.seconds),distance=std::hypot(p[0]-c.state["px"],p[2]-c.state["pz"]);
  c.state["elapsed"]+=dt;if(distance<4)c.state["distance"]+=distance;
  if(distance<.005f)c.state["stationary"]+=dt;
  if(!c.state["approach"] && p[2]>=10) {c.state["approach"]=1;c.state["approachTime"]=c.state["elapsed"];}
  const float branch=p[2]>=18 && p[2]<36 && std::abs(p[0])>5.8f?(p[0]>0?1.0f:-1.0f):0;
  if(branch && !c.state["choice"]) {c.state["choice"]=branch;c.state["decision"]=c.state["elapsed"]-c.state["approachTime"];}
  if(branch)c.state["inBranch"]=1;
  if(c.state["inBranch"] && p[2]<13) {c.state["backtracks"]+=1;c.state["inBranch"]=0;}
  c.state["px"]=p[0];c.state["pz"]=p[2];
  const float goalX=c.Value("mirror")>0?-8.5f:8.5f;
  if(std::hypot(p[0]-goalX,p[2]-46)<1.6f) {
   c.state["done"]=1;Record(c,"complete");
   auto frozen=*player;if(frozen.playerController)frozen.playerController->moveSpeed=0;
   c.scene->SetComponents(player->id,frozen);
   ScriptEvent event;event.name="sunrise:play";event.target="audio-Clear";c.scene->Emit(std::move(event));
  }
 };
 s.stop=[](ScriptContext& c) {if(c.Value("mode")>=0)Record(c,"interrupted");};
 return s;
}
ScriptDefinition Camera() {
 ScriptDefinition s;s.dataFields={{"target",{ScriptObjectReference{}}}};
 s.lateUpdate=[](ScriptContext& c) {
  const auto* p=c.Reference("target");if(!p || !c.scene)return;
  const float blend=1-std::exp(-7*static_cast<float>(c.seconds));
  if(!c.state["ready"]) {c.state["x"]=p->position[0];c.state["ready"]=1;}
  c.state["x"]=std::clamp(c.state["x"],p->position[0]-.65f,p->position[0]+.65f);
  const std::array<float,3> wanted{c.state["x"],4.9f,p->position[2]-7.8f};
  for(size_t i=0;i<3;++i)c.object.position[i]+=(wanted[i]-c.object.position[i])*blend;
  std::array<float,3> d{};float length=0;
  for(size_t i=0;i<3;++i){d[i]=c.object.position[i]-p->position[i];length+=d[i]*d[i];}
  length=std::sqrt(length);
  if(length>.01f) {
   for(auto& v:d)v/=length;float safe=length;
   for(float x:{-.22f,0.0f,.22f}) {auto origin=p->position;origin[0]+=x;
    const auto hit=c.scene->Raycast(origin,d,length,0xffffffffu,false,p->id);
    if(hit)safe=std::min(safe,std::max(.12f,hit->distance-.35f));}
   for(size_t i=0;i<3;++i)c.object.position[i]=p->position[i]+d[i]*safe;
  }
  const float dx=c.state["x"]-c.object.position[0],dz=p->position[2]+4.5f-c.object.position[2];
  c.object.rotation={std::atan2(c.object.position[1]-1.1f,std::hypot(dx,dz)),std::atan2(dx,dz),0};
 };
 return s;
}
ScriptDefinition Gesture() {
 ScriptDefinition s;s.fields={{"mirror",{0,0,1}}};
 s.update=[](ScriptContext& c) {
  c.state["time"]+=static_cast<float>(c.seconds);
  c.object.rotation[2]=(c.Value("mirror")>0?-1.0f:1.0f)*(-.2f+.35f*std::sin(c.state["time"]*3));
 };
 return s;
}
}
bool Register_GuidanceLab(void* owner,SceneRuntime::RegisterModuleScript add) {
 const auto controller=Controller(),camera=Camera(),gesture=Gesture();
 return add(owner,"GuidanceLab",&controller) && add(owner,"GuidanceCamera",&camera) && add(owner,"GuidanceGesture",&gesture);
}
