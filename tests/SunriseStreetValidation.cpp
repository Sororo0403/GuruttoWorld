#include <SceneRuntime/SceneEnvironment.h>
#include <SceneRuntime/ScriptModuleApi.h>
#include <SceneRuntime/TitleMenu.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Resources/RenderTexture.h>
#include <Engine/Platform/Window.h>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
bool Register_SunriseStreet(void*,SceneRuntime::RegisterModuleScript);
namespace {
using namespace SceneRuntime;
void Require(bool condition,const std::string& message) {if(!condition)throw std::runtime_error(message);}
bool Register(void*,const char* name,const ScriptDefinition* definition) {return ScriptRegistry::Register(name,*definition);}
ScenePlacement& Find(SceneLayout& layout,const std::string& id) {
 auto it=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& o){return o.id==id;});
 Require(it!=layout.objects.end(),"Missing "+id);return *it;
}
bool Has(const SceneLayout& layout,const std::string& id) {return std::any_of(layout.objects.begin(),layout.objects.end(),[&](const auto& o){return o.id==id;});}
const ScenePlacement& Find(const SceneLayout& layout,const std::string& id) {
 auto it=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& o){return o.id==id;});
 Require(it!=layout.objects.end(),"Missing "+id);return *it;
}
struct Simulation {
 SceneLayout layout=SceneLayout::Load("Games/GuruttoWorld/Assets/Scenes/SunriseStreet.json");
 PhysicsWorld physics;ScenePhysics::States states;ScriptRuntime scripts;UiState ui;int frames=0;
 explicit Simulation(const std::string& path="Games/GuruttoWorld/Assets/Scenes/SunriseStreet.json") {
  layout=SceneLayout::Load(path);
  for(auto& o:layout.objects)for(auto& script:o.scripts)if(script.behaviour=="SunriseSound")script.parameters["volume"]=0;
 }
 ~Simulation() {scripts.Stop(layout);}
 void Tick(float x=0,float z=0,bool jump=false,bool held=false,bool attack=false) {
  constexpr double dt=1.0/60;
  const std::map<std::string,float> input{{"MoveRight",std::max(0.0f,x)},{"MoveLeft",std::max(0.0f,-x)},{"MoveForward",std::max(0.0f,z)},{"MoveBack",std::max(0.0f,-z)},{"Jump",held?1.0f:0.0f}};
  const std::map<std::string,bool> pressed{{"Jump",jump},{"Attack",attack}};std::string error;
  Require(scripts.Update(layout,dt,error,input,pressed,&physics,ScriptPhase::FixedUpdate),error);
  Require(physics.Advance(layout,states,dt,x,z,jump,"Games/GuruttoWorld",error),error);
  Require(scripts.Update(layout,dt,error,input,pressed,&physics),error);
  Require(scripts.Update(layout,dt,error,input,pressed,&physics,ScriptPhase::LateUpdate),error);
  for(const auto& [key,value]:scripts.TakeUiCommands().values)ui.values[key]=value;
  ++frames;
 }
 void Walk(float x,float z,bool attack=true) {
  for(int t=0;t<1600;++t) {
   const auto p=Find(layout,"park-player").position;
   if(std::hypot(x-p[0],z-p[2])<.2f || ui.Value("parkClear")==1) {for(int i=0;i<20;++i)Tick();return;}
   Tick(std::clamp((x-p[0])*3,-1.0f,1.0f),std::clamp((z-p[2])*3,-1.0f,1.0f),false,false,attack && frames%25==0);
  }
  const auto p=Find(layout,"park-player").position;
  throw std::runtime_error("Waypoint "+std::to_string(x)+","+std::to_string(z)+" blocked at "+std::to_string(p[0])+","+std::to_string(p[1])+","+std::to_string(p[2]));
 }
 void JumpTo(float x,float z) {
  for(int t=0;t<240;++t) {
   const auto p=Find(layout,"park-player").position;
   if(t>20 && std::hypot(x-p[0],z-p[2])<.22f && states.at("park-player").grounded) return;
   Tick(std::clamp((x-p[0])*4,-1.0f,1.0f),std::clamp((z-p[2])*4,-1.0f,1.0f),t==0,t<42);
  }
  const auto p=Find(layout,"park-player").position;
  throw std::runtime_error("Jump to "+std::to_string(x)+","+std::to_string(z)+" failed at "+std::to_string(p[0])+","+std::to_string(p[1])+","+std::to_string(p[2]));
 }
};
void Playthrough() {
 Simulation s;const auto authored=SceneLayout::Load("Games/GuruttoWorld/Assets/Scenes/SunriseStreet.json").Serialize();
 for(int i=0;i<60;++i)s.Tick();
 Require(s.states.at("park-player").grounded,"Grounded on street");
 const float y=Find(s.layout,"park-player").position[1];s.Tick(0,0,true,true);
 for(int i=0;i<30;++i)s.Tick(0,0,false,true);
 Require(Find(s.layout,"park-player").position[1]>y+.5f,"Jump rises");
 for(int i=0;i<130;++i)s.Tick();
 s.Walk(-2,7);s.Walk(2,14);s.Walk(-7,14);s.Walk(-7,29);
 Require(Find(s.layout,"park-player").position[1]>3.5f,"Roof reached through physical staircase");
 s.Walk(-9,35);s.JumpTo(-11.5f,35);s.JumpTo(-9,35);
 s.Walk(-7,38);s.Walk(-7,49);
 Require(Find(s.layout,"park-player").position[1]<1.5f,"Roof rejoins courtyard at street level");
 s.Walk(-7,46.5f,false);s.Walk(7,46.5f,false);s.Walk(7,30.6f,false);s.Walk(7,25,false);s.Walk(7,30.6f,false);
 Require(Has(s.layout,"crate"),"Secret entrance remains closed before attacking it");
 s.Walk(15,30.6f);s.Walk(15,33);
 Require(!Has(s.layout,"crate"),"Attack opens physical secret doorway");
 Require(s.ui.Value("parkClear")==0,"Exploration does not skip course");
 s.Walk(12,30.6f);s.Walk(7,30.6f);s.Walk(7,46.5f,false);s.Walk(0,46.5f,false);s.Walk(0,49,false);
 s.Walk(3.8f,46.8f,false);Require(Has(s.layout,"switch-canal"),"Walking alone does not activate gear");
 s.Tick(0,0,false,false,true);for(int i=0;i<70;++i)s.Tick();
 Require(!Has(s.layout,"switch-canal"),"Attack activates bridge gear");
 Require(std::abs(Find(s.layout,"canal-bridge").position[0])<.01f,"Bridge arrives in path");
 Require(Has(s.layout,"park-gate"),"One gear keeps final gate closed");
 s.Walk(10,50.8f);s.Walk(10,52.8f);s.JumpTo(12,55.3f);s.JumpTo(10.5f,58);s.JumpTo(10,60.2f);s.Walk(8,63);s.Walk(0,63);
 // Verify the main bridge too, traversing it in both directions.
 s.Walk(0,49);s.Walk(0,63);s.Walk(0,64);
 Find(s.layout,"park-player").position[1]=-5;s.Tick();
 Require(Find(s.layout,"park-player").position[2]>60,"Fall returns to canal checkpoint");
 s.Walk(4,66);s.Walk(4,70);s.Walk(4,74);s.Walk(-4,75);s.Walk(-4,80);s.Walk(-3.8f,84);
 s.Tick(0,0,false,false,true);for(int i=0;i<40;++i)s.Tick();
 Require(!Has(s.layout,"park-gate"),"Second gear opens exit");
 s.Walk(3,90);s.Walk(0,99);s.Walk(0,108);s.Walk(0,111);
 Require(s.ui.Value("parkClear")==1,"Stage clears through physics play");
 const auto coins=std::count_if(s.layout.objects.begin(),s.layout.objects.end(),[](const auto& o){return o.name=="ParkCoin";});
 for(const auto& o:s.layout.objects)if(o.name=="ParkCoin")std::cout<<"Remaining "<<o.id<<"\n";
 Require(coins==0,"All twelve collectibles reached");
 const auto& stats=Find(s.layout,"park-stats");
 Require(stats.text && stats.text->text.find("発見 2/2")!=std::string::npos,"Both optional spaces discovered");
 Require(SceneLayout::Load("Games/GuruttoWorld/Assets/Scenes/SunriseStreet.json").Serialize()==authored,"Play preserves saved scene");
 std::string error;s.scripts.TakeSceneCommands();
 Require(s.scripts.Update(s.layout,1.0/60,error,{},{{"Restart",true}},&s.physics),error);
 auto commands=s.scripts.TakeSceneCommands();Require(commands.size()==1 && commands[0].scene=="Assets/Scenes/SunriseStreet.json","Restart loads new stage");
 Require(s.scripts.Update(s.layout,1.0/60,error,{},{{"Cancel",true}},&s.physics),error);
 commands=s.scripts.TakeSceneCommands();Require(commands.size()==1 && commands[0].scene=="Assets/Scenes/TitleStreet.json","Cancel returns to title");
 std::cout<<"PASS physical playthrough: "<<s.frames<<" frames, roof stairs and ledge, secret room, canal stepping stones and bridge, 12/12, 2 discoveries, both gears, fall, clear, restart, return\n";
}
void DirectRoute() {
 Simulation s;for(int i=0;i<60;++i)s.Tick();
 s.Walk(7,14);s.Walk(7,40);s.Walk(7,49);s.Walk(3.8f,46.8f);
 s.Tick(0,0,false,false,true);for(int i=0;i<70;++i)s.Tick();
 s.Walk(0,49);s.Walk(0,63);s.Walk(4,66);s.Walk(4,74);s.Walk(-4,75);s.Walk(-3.8f,84);
 s.Tick(0,0,false,false,true);for(int i=0;i<40;++i)s.Tick();
 s.Walk(0,90);s.Walk(0,108);s.Walk(0,111);
 Require(s.ui.Value("parkClear")==1,"Ground route clears without optional roof or room");
 Require(Has(s.layout,"flower-piece-2") && Has(s.layout,"flower-piece-5"),"Exploration rewards remain optional");
 Require(Find(s.layout,"park-stats").text->text.find("発見 0/2")!=std::string::npos,"Ground route enters neither optional space");
 std::cout<<"PASS direct ground route, optional room and roof skipped\n";
}
void BlockPlaythrough() {
 Simulation s("Games/GuruttoWorld/Assets/Scenes/SunriseBlock.json");
 for(int i=0;i<60;++i)s.Tick();
 Require(s.states.at("park-player").grounded,"Block entrance grounded");
 const auto& camera=Find(s.layout,"park-camera");
 Require(camera.position[1]<6 && camera.position[2]>-8,"Close camera at street level");
 s.Walk(-2,8);s.Walk(0,18);s.Walk(9,20);s.Walk(8,22);s.Walk(11,38);s.Walk(8,43);
 s.Walk(5,46);s.Walk(11,50);s.Walk(15,54);
 Require(!Has(s.layout,"enemy-0") && !Has(s.layout,"enemy-1"),"Courtyard fight completed");
 s.Walk(7,52.6f,false);
 Require(Has(s.layout,"switch-roof"),"Walking does not trigger rooftop mechanism");
 Require(std::abs(Find(s.layout,"roof-shutter").position[0]+6)<.01f,"Roof shutter starts closed");
 s.Tick(0,0,false,false,true);for(int i=0;i<75;++i)s.Tick();
 Require(!Has(s.layout,"switch-roof"),"Attack operates rooftop gear");
 Require(Find(s.layout,"roof-shutter").position[0]<-10.9f,"Gear slides physical shutter open");
 s.Walk(-6,58);s.Walk(-6,71);s.Walk(-6,82);
 Require(Find(s.layout,"park-player").position[1]>6,"Full staircase reaches high cafe roof");
 s.Walk(-7,89);s.Walk(0,91);
 Require(s.ui.Value("parkClear")==1,"Short block completes at rooftop view");
 for(const auto& o:s.layout.objects)if(o.name=="ParkCoin")std::cout<<"Remaining block reward: "<<o.id<<"\n";
 Require(std::none_of(s.layout.objects.begin(),s.layout.objects.end(),[](const auto& o){return o.name=="ParkCoin";}),"All six block rewards reachable");
 std::string error;s.scripts.TakeSceneCommands();
 Require(s.scripts.Update(s.layout,1.0/60,error,{},{{"Restart",true}},&s.physics),error);
 const auto commands=s.scripts.TakeSceneCommands();
 Require(commands.size()==1 && commands[0].scene=="Assets/Scenes/SunriseBlock.json","Block restart returns to same slice");
 std::cout<<"PASS short city block: "<<s.frames<<" frames, close camera, corner, two enemies, shutter, 18 steps, roof, six rewards, clear and restart\n";
}
void AudioPool() {
 Simulation s;for(int i=0;i<30;++i)s.Tick();
 const auto count=s.layout.objects.size();
 for(int repeat=0;repeat<8;++repeat) {
  s.Tick(0,0,false,false,true);s.Tick();s.Tick();
  Require(s.ui.Value("audio-Attack")==static_cast<float>(repeat+1),"Preloaded cue replays successfully");
  Require(s.layout.objects.size()==count,"Sound replay never spawns or destroys objects");
  for(int i=0;i<30;++i)s.Tick();
 }
 for(int i=0;i<90;++i)s.Tick();
 Require(s.ui.Value("audio-Attack")==8,"Idle cue never replays spontaneously");
 Require(s.layout.objects.size()==count,"Idle sound sources remain allocated");
 std::cout<<"PASS fixed audio pool: eight replays, no object churn\n";
}
    void Capture(Engine::DirectX12Renderer& renderer,SceneEnvironment& scene,const std::string& name)
    {
        constexpr UINT width=1280,height=720,pitch=width*4;
        Engine::RenderTexture target;
        Require(target.Resize(renderer,width,height),"Capture target");
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC description{};description.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width=static_cast<UINT64>(pitch)*height;description.Height=1;
        description.DepthOrArraySize=1;description.MipLevels=1;description.SampleDesc.Count=1;
        description.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        Require(SUCCEEDED(renderer.GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"Capture buffer");
        Require(renderer.Render({0,0,0,1},[&](ID3D12GraphicsCommandList* commands,float) {
            Require(target.Begin(commands,scene.World().Layout().settings.background),"Capture begins");
            scene.Draw(commands,width,height);Require(target.End(commands),"Capture ends");
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition={target.GetResource(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
            commands->ResourceBarrier(1,&barrier);
            D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=target.GetResource();source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION destination{};destination.pResource=readback.Get();destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,width,height,1,pitch};
            commands->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);commands->ResourceBarrier(1,&barrier);
        })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"Capture renders");
        unsigned char* pixels=nullptr;const D3D12_RANGE range{0,static_cast<SIZE_T>(pitch)*height};
        Require(SUCCEEDED(readback->Map(0,&range,reinterpret_cast<void**>(&pixels))),"Capture maps");
        const auto folder=std::filesystem::path("generated/sunrise/previews");std::filesystem::create_directories(folder);
        std::ofstream output(folder/(name+".ppm"),std::ios::binary);output<<"P6\n"<<width<<" "<<height<<"\n255\n";
        for(UINT y=0;y<height;++y) for(UINT x=0;x<width;++x) output.write(reinterpret_cast<char*>(pixels+y*pitch+x*4),3);
        const D3D12_RANGE empty{0,0};readback->Unmap(0,&empty);Require(static_cast<bool>(output),"Capture saved");
    }

void Render() {
 Engine::Window window;Engine::DirectX12Renderer renderer;
 Require(window.Create(L"Sunrise street validation",1280,720) && renderer.Initialize(window.GetHandle()),"Renderer");
 const auto root=std::filesystem::absolute("Games/GuruttoWorld");std::string error;
 for(const auto& [name,pose]:std::vector<std::pair<std::string,std::array<float,3>>>{{"entrance",{0,1.05f,6}},{"terrace",{-7,4,30}},{"secret",{15,1.05f,33}},{"canal",{0,1.05f,49}},{"vista",{0,1.05f,99}}}) {
  auto layout=SceneLayout::Load(root/"Assets/Scenes/SunriseStreet.json");Find(layout,"park-player").position=pose;
  Find(layout,"park-camera").position={pose[0],10.5f,pose[2]-14};SceneEnvironment scene;
  Require(renderer.WaitForIdle() && scene.Initialize(renderer,root,layout,error),error);
  for(int i=0;i<90;++i) {Require(scene.MovePlayers(1.0/60,0,0),"Fixed update");scene.Update(1.0/60,true,true);}
  Capture(renderer,scene,name);Require(renderer.WaitForIdle(),"Capture complete");
 }
 SceneEnvironment title;Require(title.Initialize(renderer,root,root/"Assets/Scenes/TitleStreet.json",error),error);
 auto& canvas=*Find(title.World().Layout(),"title-canvas").canvas;
 Require(canvas.menu->entries.front().target=="Assets/Scenes/SunriseBlock.json","START target");
 // The App owns the menu controller and performs its returned scene transition.
 TitleMenu menu;menu.Configure(*canvas.menu);menu.SetTransitionDuration(3.2f);
 TitleMenuInput input;input.active=true;menu.Update(input,.1);
 const auto event=title.Click("world-start");menu.ActivateUi(event.event);
 bool started=false;
 for(int i=0;i<240;++i) {
  const auto action=menu.Update(input,1.0/60);
  title.SeekAnimation(3.0f,3.0f,menu.TransitionProgress()*3.2f);
  if(action==TitleMenuAction::Start) {Require(title.LoadScene(menu.PendingTarget(),false,error),error);started=true;break;}
 }
 Require(started,"Departure emits START");
 std::cout<<"Title after click: "<<title.ActiveSceneName()<<"\n";
 Require(title.ActiveSceneName().find("SunriseBlock")!=std::string::npos,"START enters short block through departure");
 Capture(renderer,title,"from-title");Require(renderer.WaitForIdle(),"GPU complete");
}
void RenderBlock() {
 Engine::Window window;Engine::DirectX12Renderer renderer;
 Require(window.Create(L"Sunrise block validation",1280,720) && renderer.Initialize(window.GetHandle()),"Block renderer");
 const auto root=std::filesystem::absolute("Games/GuruttoWorld");std::string error;
 for(const auto& [name,pose]:std::vector<std::pair<std::string,std::array<float,3>>>{{"block-entrance",{0,1.05f,1}},{"block-corner",{9,1.05f,22}},{"block-courtyard",{8,1.05f,47}},{"block-stairs",{-6,3.8f,72}},{"block-roof",{-5,6.1f,87}}}) {
  auto layout=SceneLayout::Load(root/"Assets/Scenes/SunriseBlock.json");Find(layout,"park-player").position=pose;
  Find(layout,"park-camera").position={pose[0],pose[1]+4.6f,pose[2]-7.8f};SceneEnvironment scene;
  Require(renderer.WaitForIdle() && scene.Initialize(renderer,root,layout,error),error);
  for(int i=0;i<90;++i) {Require(scene.MovePlayers(1.0/60,0,0),"Block fixed update");scene.Update(1.0/60,true,true);}
  Capture(renderer,scene,name);Require(renderer.WaitForIdle(),"Block capture complete");
 }
}
}
int main() {
 try {
  std::filesystem::create_directories("generated/sunrise");Engine::Log::Initialize("generated/sunrise/validation.log");
  Require(Register_SunriseStreet(nullptr,Register),"Scripts register");
  const bool blockOnly=GetEnvironmentVariableA("WP1_SUNRISE_BLOCK_ONLY",nullptr,0)!=0;
  if(!GetEnvironmentVariableA("WP1_SUNRISE_RENDER_ONLY",nullptr,0)) {if(!blockOnly){AudioPool();Playthrough();DirectRoute();}BlockPlaythrough();}
  if(!GetEnvironmentVariableA("WP1_SUNRISE_CPU_ONLY",nullptr,0)){if(!blockOnly)Render();RenderBlock();}
  std::cout<<"PASS SunriseStreet\n";return 0;
 } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";for(const auto& log:Engine::Log::Recent())if(log.level==Engine::LogLevel::Error)std::cerr<<log.text<<"\n";return 1;}
}
