#include <SceneRuntime/SceneEnvironment.h>
#include <SceneRuntime/ScriptModuleApi.h>
#include <SceneRuntime/Prefab.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Resources/RenderTexture.h>
#include <Engine/Platform/Window.h>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

bool Register_ParkAdventure(void*,SceneRuntime::RegisterModuleScript);

namespace
{
    using namespace SceneRuntime;
    void Require(bool condition,const std::string& message)
    { if (!condition) throw std::runtime_error(message); }
    bool Register(void*,const char* name,const ScriptDefinition* definition)
    { return ScriptRegistry::Register(name,*definition); }
    ScenePlacement& Find(SceneLayout& layout,const std::string& id)
    {
        const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& object){return object.id==id;});
        Require(found!=layout.objects.end(),"Missing object: "+id); return *found;
    }
    struct Simulation
    {
        SceneLayout layout=SceneLayout::Load("Content/Assets/Scenes/ParkAdventure.json");
        PhysicsWorld physics;
        ScenePhysics::States states;
        ScriptRuntime scripts;
        UiState ui;
        int frames=0;
        Simulation()
        {
            // Keep all playable geometry and scripts; omit scenery with no collision from CPU replay.
            std::erase_if(layout.objects,[](const auto& item) {
                return item.id.starts_with("tree-") || item.id.starts_with("flower-") ||
                    item.id.starts_with("bench-") || item.id.starts_with("distant-") ||
                    item.id.starts_with("tower-") || item.id=="park-sky" || item.id=="park-music";
            });
        }
        void Tick(float x=0,float z=0,bool jump=false,bool held=false,bool attack=false)
        {
            constexpr double dt=1.0/60;
            const std::map<std::string,float> input{{"MoveRight",std::max(0.0f,x)},{"MoveLeft",std::max(0.0f,-x)},
                {"MoveForward",std::max(0.0f,z)},{"MoveBack",std::max(0.0f,-z)},{"Jump",held?1.0f:0.0f}};
            const std::map<std::string,bool> pressed{{"Jump",jump},{"Attack",attack}};
            std::string error;
            Require(scripts.Update(layout,dt,error,input,pressed,&physics,ScriptPhase::FixedUpdate),error);
            Require(physics.Advance(layout,states,dt,x,z,jump,"Content",error),error);
            Require(scripts.Update(layout,dt,error,input,pressed,&physics),error);
            Require(scripts.Update(layout,dt,error,input,pressed,&physics,ScriptPhase::LateUpdate),error);
            for (const auto& [key,value]:scripts.TakeUiCommands().values) ui.values[key]=value;
            ++frames;
        }
        void WalkTo(float x,float z,bool platform=false)
        {
            for(int tick=0;tick<1200;++tick) {
                if (ui.Value("parkClear")==1) return;
                const auto position=Find(layout,"park-player").position;
                const float dx=x-position[0],dz=z-position[2];
                if(std::hypot(dx,dz)<0.22f) {for(int wait=0;wait<20;++wait) Tick(0,0,false,false,true);return;}
                const bool gap=position[2]>36.1f && position[2]<37.6f && states["park-player"].grounded;
                const bool hop=platform && tick%42==0 && states["park-player"].grounded;
                Tick(std::clamp(dx*3,-1.0f,1.0f),std::clamp(dz*3,-1.0f,1.0f),gap || hop,gap || (position[2]>36 && position[2]<41),frames%24==0);
            }
            const auto position=Find(layout,"park-player").position;
            throw std::runtime_error("Waypoint timed out: "+std::to_string(x)+","+std::to_string(z)+
                " at "+std::to_string(position[0])+","+std::to_string(position[1])+","+std::to_string(position[2]));
        }
    };
    void Playthrough()
    {
        Simulation sim;
        const auto authored=SceneLayout::Load("Content/Assets/Scenes/ParkAdventure.json").Serialize();
        for(int tick=0;tick<60;++tick) sim.Tick();
        std::cout<<"Float and landing verified\n"<<std::flush;
        Require(sim.states.at("park-player").grounded,"Player lands using Jolt");
        sim.Tick(0,0,true,true);
        const float before=Find(sim.layout,"park-player").position[1];
        for(int tick=0;tick<30;++tick) sim.Tick(0,0,false,true);
        Require(Find(sim.layout,"park-player").position[1]>before+.5f,"Held jump preserves useful jump height");
        Require(!Find(sim.layout,"park-player").playerController->useGravity,"Timed airborne float activates");
        for(int tick=0;tick<100;++tick) sim.Tick(0,0,false,true);
        Require(Find(sim.layout,"park-player").playerController->useGravity,"Float duration is bounded");
        for(int tick=0;tick<60;++tick) sim.Tick();
        sim.WalkTo(-2,6); sim.WalkTo(2,11); sim.WalkTo(0,17);
        const auto lower=Find(sim.layout,"Terraces-coin-a").position;
        const auto upper=Find(sim.layout,"Terraces-coin-b").position;
        const auto last=Find(sim.layout,"Terraces-coin-c").position;
        sim.WalkTo(lower[0],lower[2],true); sim.WalkTo(upper[0],upper[2],true);
        sim.WalkTo(0,24); sim.WalkTo(0,27); sim.WalkTo(last[0],last[2]); sim.WalkTo(0,30);
        std::cout<<"Terraces verified\n"<<std::flush;
        sim.WalkTo(0,35); sim.WalkTo(0,43); sim.WalkTo(0,46);
        Require(Find(sim.layout,"park-player").position[2]>41,"Actual character crosses canal gap");
        std::cout<<"Canal verified\n"<<std::flush;
        // A fall restores the latest checkpoint, keeping collected rewards.
        Find(sim.layout,"park-player").position[1]=-5;
        sim.Tick();
        Require(Find(sim.layout,"park-player").position[2]>40,"Fall restores checkpoint");
        sim.WalkTo(0,59);
        Require(std::any_of(sim.layout.objects.begin(),sim.layout.objects.end(),[](const auto& p){return p.id=="park-gate";}),"Gate stays closed before switches");
        sim.WalkTo(-4,51); sim.WalkTo(-4,53);
        Require(std::any_of(sim.layout.objects.begin(),sim.layout.objects.end(),[](const auto& p){return p.id=="park-gate";}),"One switch does not open gate");
        sim.WalkTo(4,56); sim.WalkTo(4,58);
        Require(std::none_of(sim.layout.objects.begin(),sim.layout.objects.end(),[](const auto& p){return p.id=="park-gate";}),"Two switches open gate");
        sim.WalkTo(0,61); sim.WalkTo(0,69); sim.WalkTo(0,74,true);
        Require(sim.ui.Value("parkClear")==1,"Playable route reaches clear state");
        for (const auto& object:sim.layout.objects) if(object.name=="ParkEnemy")
            throw std::runtime_error("Patrol survived replay: "+object.id);
        Require(Find(sim.layout,"park-camera").position[2]>55,"Camera follows into final section");
        Require(SceneLayout::Load("Content/Assets/Scenes/ParkAdventure.json").Serialize()==authored,"Play does not change authored scene");
        const auto remaining=std::count_if(sim.layout.objects.begin(),sim.layout.objects.end(),[](const auto& p){return p.name=="ParkCoin";});
        std::cout<<"Playthrough: "<<sim.frames<<" frames, "<<12-remaining<<"/12 collectibles\n";
        for (const auto& object:sim.layout.objects) if(object.name=="ParkCoin")
            std::cout<<"Uncollected: "<<object.id<<" at "<<object.position[0]<<","<<object.position[1]<<","<<object.position[2]<<"\n";
        Require(remaining==0,"All twelve collectibles are reachable by physical playthrough");
        std::string error;
        Require(sim.scripts.Update(sim.layout,1.0/60,error,{},{{"Restart",true}},&sim.physics),error);
        const auto commands=sim.scripts.TakeSceneCommands();
        Require(commands.size()==1 && commands[0].scene=="Assets/Scenes/ParkAdventure.json","Restart reloads authored stage");
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
        const auto folder=std::filesystem::path("generated/park/previews");std::filesystem::create_directories(folder);
        std::ofstream output(folder/(name+".ppm"),std::ios::binary);output<<"P6\n"<<width<<" "<<height<<"\n255\n";
        for(UINT y=0;y<height;++y) for(UINT x=0;x<width;++x) output.write(reinterpret_cast<char*>(pixels+y*pitch+x*4),3);
        const D3D12_RANGE empty{0,0};readback->Unmap(0,&empty);Require(static_cast<bool>(output),"Capture saved");
    }
    void Render()
    {
        Engine::Window window;Engine::DirectX12Renderer renderer;
        Require(window.Create(L"Park validation",1280,720) && renderer.Initialize(window.GetHandle()),"Renderer initializes");
        SceneEnvironment scene;std::string error;
        const auto root=std::filesystem::absolute("Content");
        Require(scene.Initialize(renderer,root,root/"Assets/Scenes/ParkAdventure.json",error),error);
        for(int frame=0;frame<40;++frame) {
            Require(scene.MovePlayers(1.0/60,0,0),"Environment fixed update");scene.Update(1.0/60,true,true);
        }
        Capture(renderer,scene,"entrance");
        auto layout=SceneLayout::Load(root/"Assets/Scenes/ParkAdventure.json");
        Find(layout,"park-player").position={0,1.05f,35};
        Find(layout,"park-camera").position={0,10,22};
        SceneEnvironment canal;
        Require(renderer.WaitForIdle() && canal.Initialize(renderer,root,layout,error),error);
        for(int frame=0;frame<20;++frame) {Require(canal.MovePlayers(1.0/60,0,0),"Canal update");canal.Update(1.0/60,true,true);}
        Capture(renderer,canal,"canal");
        Require(renderer.WaitForIdle(),"GPU completes");
    }
}
int main()
{
    try {
        Require(Engine::Log::Initialize("generated/park/validation.log"),"Validation log opens");
        Require(Register_ParkAdventure(nullptr,Register),"Game scripts register");
        for(const char* name:{"Entrance","Terraces","Canal","SwitchGarden","Vista"}) {
            const auto prefab=SceneLayout::Load(std::filesystem::path("Content/Assets/Prefabs/Park")/(std::string(name)+".prefab"));
            Require(Prefab::Extract(prefab,"module").objects.size()==prefab.objects.size(),"Reusable module hierarchy");
        }
        if (!GetEnvironmentVariableA("WP1_PARK_RENDER_ONLY",nullptr,0)) Playthrough();
        Render();std::cout<<"PASS: park validation\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<"\n";
        for (const auto& entry:Engine::Log::Recent()) if(entry.level==Engine::LogLevel::Error) std::cerr<<entry.text<<"\n";
        return 1;
    }
}
