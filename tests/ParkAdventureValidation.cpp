#include "ShaderCacheValidation.h"
#include "ParkResourceValidation.h"
#include "LayoutDeltaValidation.h"
#include "SpriteResourceValidation.h"
#include "AllScenesValidation.h"
#include <SceneRuntime/SceneEnvironment.h>
#include <SceneRuntime/ScriptModuleApi.h>
#include <SceneRuntime/Prefab.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Resources/RenderTexture.h>
#include <Engine/Platform/Window.h>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <Engine/Core/Profiler.h>
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
    const ScenePlacement& Find(const SceneLayout& layout,const std::string& id)
    {
        const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& object){return object.id==id;});
        Require(found!=layout.objects.end(),"Missing object: "+id); return *found;
    }
    void ValidationParity()
    {
        const auto valid=SceneLayout::Load("Content/Assets/Scenes/ParkAdventure.json");
        valid.Validate();
        for(int variant=0;variant<7;++variant) {
            auto layout=valid;
            switch(variant) {
            case 0: layout.objects[0].position[0]=std::numeric_limits<float>::quiet_NaN(); break;
            case 1: layout.objects[0].scale[1]=0; break;
            case 2: layout.objects[1].id=layout.objects[0].id; break;
            case 3: layout.objects[0].parentId="missing-parent"; break;
            case 4: layout.objects[0].parentId=layout.objects[1].id; layout.objects[1].parentId=layout.objects[0].id; break;
            case 5: layout.settings.mainCamera="missing-camera"; break;
            case 6: Find(layout,"park-player").playerController->moveSpeed=-1; break;
            }
            bool direct=false,serialized=false;
            try { layout.Validate(); } catch(const std::exception&) { direct=true; }
            try { static_cast<void>(layout.Serialize()); } catch(const std::exception&) { serialized=true; }
            Require(direct && serialized,"Runtime and serialized validation reject invalid layout equally");
        }
    }
    void EditableRules()
    {
        auto layout=SceneLayout::Load("Content/Assets/Scenes/ParkAdventure.json");
        auto& player=Find(layout,"park-player");auto& script=player.scripts.front();
        script.parameters["maxHp"]=7;
        script.data["statsTemplate"]={std::string("{hp}:{coins}")};
        script.data["returnScene"]={std::string("Assets/Scenes/Game.json")};
        script.data["enemies"]={ScriptValue::Array{}};script.data["switches"]={ScriptValue::Array{}};
        script.data["checkpoints"]={ScriptValue::Array{}};script.data["useNameLookup"]={false};
        const auto coinId=std::get<ScriptObjectReference>(std::get<ScriptValue::Array>(script.data.at("coins").value).front().value).id;
        const auto position=Find(layout,coinId).position;
        auto& coin=Find(layout,coinId);coin.id="renamed-collectible";coin.name="Custom collectible";
        for(auto& object:layout.objects) if(object.parentId==coinId) object.parentId=coin.id;
        script.data["coins"]={ScriptValue::Array{{ScriptObjectReference{coin.id}}}};
        player.position=position;
        ScriptRuntime runtime;std::string error;
        Require(runtime.Update(layout,.01,error),error);
        Require(Find(layout,"park-stats").text->text=="7:1","Saved health/template/explicit reference rules apply");
        Require(std::none_of(layout.objects.begin(),layout.objects.end(),[](const auto& p){return p.id=="renamed-collectible";}),"Renamed collectible is found through its reference");
        Require(runtime.Update(layout,.01,error,{},{{"Cancel",true}}),error);
        const auto commands=runtime.TakeSceneCommands();
        Require(commands.size()==1 && commands[0].scene=="Assets/Scenes/Game.json","Editable return scene applies");
    }
    void RemovedScriptOwners()
    {
        static int stopped=0;
        static float sum=0;
        stopped=0; sum=0;
        ScriptDefinition definition;
        definition.update=[](ScriptContext& context) {
            context.object.position[0]=3;
            if(context.Pressed("Remove") && context.object.id=="root") context.scene->Destroy("root");
        };
        definition.stop=[](ScriptContext& context) { ++stopped; sum+=context.object.position[0]; };
        Require(ScriptRegistry::Register("ParkOwnerRetentionTest",definition),"Owner test registers");
        ScenePlacement root; root.id="root"; root.name="root";
        ScriptComponent script; script.id="script"; script.behaviour="ParkOwnerRetentionTest";
        root.scripts.push_back(script);
        auto child=root; child.id="child"; child.parentId="root";
        SceneLayout layout; layout.objects={root,child};
        ScriptRuntime runtime; std::string error;
        Require(runtime.Update(layout,1.0/60,error,{},{{"Remove",true}}),error);
        Require(layout.objects.empty() && stopped==2 && sum==6,"Subtree stop retains updated owners exactly once");
        runtime.Stop(layout);
        Require(stopped==2,"Removed scripts are not stopped twice");
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
    void UpdateTrace()
    {
        Engine::Window window; Engine::DirectX12Renderer renderer;
        Require(window.Create(L"Park steady update trace",640,360) && renderer.Initialize(window.GetHandle()),"Update trace renderer");
        SceneEnvironment scene; std::string error; const auto root=std::filesystem::absolute("Content");
        Require(scene.Initialize(renderer,root,root/"Assets/Scenes/ParkAdventure.json",error),error);
        const auto tick=[&] { Require(scene.MovePlayers(1.0/60,0,0),"Steady fixed update"); scene.Update(1.0/60,true,true); };
        for(int i=0;i<30;++i) tick();
        static_cast<void>(Engine::CpuProfiler::Take());
        std::filesystem::create_directories("generated/park");
        std::ofstream output("generated/park/updates.csv"); output<<"frame,scope,milliseconds,calls\n";
        std::map<std::string,Engine::ProfileSample> totals;
        double total=0,maximum=0;
        constexpr int Frames=240;
        for(int frame=0;frame<Frames;++frame) {
            const auto begin=std::chrono::steady_clock::now(); tick();
            const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            total+=elapsed; maximum=std::max(maximum,elapsed);
            output<<frame<<",Total update,"<<elapsed<<",1\n";
            for(const auto& sample:Engine::CpuProfiler::Take()) {
                auto& sum=totals[sample.name]; sum.name=sample.name; sum.milliseconds+=sample.milliseconds; sum.calls+=sample.calls;
                output<<frame<<","<<sample.name<<","<<sample.milliseconds<<","<<sample.calls<<"\n";
            }
        }
        std::cout<<"Steady CPU update: "<<total/Frames<<" ms/frame, max "<<maximum<<" ms\n";
        for(const auto& [name,sample]:totals) std::cout<<name<<": "<<sample.milliseconds/Frames<<" ms/frame, "<<double(sample.calls)/Frames<<" calls/frame\n";
        Require(static_cast<bool>(output) && renderer.WaitForIdle(),"Steady trace saved");
    }

    void StutterTrace()
    {
        Engine::Window window; Engine::DirectX12Renderer renderer;
        Require(window.Create(L"Park stutter trace",1280,720) && renderer.Initialize(window.GetHandle()),"Trace renderer initializes");
        const auto root=std::filesystem::absolute("Content");
        std::filesystem::create_directories("generated/park");
        std::ofstream output("generated/park/stutter.csv");
        output<<"scenario,frame,objects,update_ms,render_present_ms,gpu_ms,physics_ms,components_ms,render_rebuild_ms,physics_rebuild_ms,ui_rebuild_ms,scene_assets_ms,material_scan_ms,material_scan_calls,player_z,player_y,use_gravity\n";
        const auto milliseconds=[](auto start) { return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(); };
        for(int scenario=0;scenario<3;++scenario) {
            SceneEnvironment scene; std::string error;
            auto layout=SceneLayout::Load(root/"Assets/Scenes/ParkAdventure.json");
            const auto position=scenario==1?std::array<float,3>{-2,1.05f,4}:scenario==2?std::array<float,3>{3,1.05f,34}:std::array<float,3>{0,1.05f,1};
            Find(layout,"park-player").position=position;
            Find(layout,"park-camera").position={position[0]*.55f,10,position[2]-13};
            Require(scene.Initialize(renderer,root,layout,error),error);
            double updateTotal=0,updateMaximum=0,gpuMaximum=0; int worst=0;
            float maximumHeight=position[1]; bool floated=false;
            for(int frame=0;frame<120;++frame) {
                static_cast<void>(Engine::CpuProfiler::Take());
                const float movement=scenario==1 && frame<90?.35f:0;
                const bool jump=scenario==2 && frame==30,held=scenario==2 && frame>=30 && frame<100;
                scene.SetInputActions({{"MoveForward",movement},{"Jump",held?1.0f:0.0f}},{{"Jump",jump}});
                const auto begin=std::chrono::steady_clock::now();
                Require(scene.MovePlayers(1.0/60,0,movement,jump),"Trace fixed update"); scene.Update(1.0/60,true,true);
                const double update=milliseconds(begin);
                const auto renderBegin=std::chrono::steady_clock::now();
                Require(renderer.Render(scene.World().Layout().settings.background,[&](ID3D12GraphicsCommandList* commands,float) {
                    scene.Draw(commands,1280,720);
                })!=Engine::RenderResult::Failed,"Trace renders");
                const auto& telemetry=renderer.Telemetry();
                const auto scope=[&](const char* name) { for(const auto& sample:telemetry.cpuScopes) if(sample.name==name) return sample.milliseconds; return 0.0; };
                output<<scenario<<","<<frame<<","<<scene.World().Layout().objects.size()<<","<<update<<","<<milliseconds(renderBegin)<<",";
                if(telemetry.gpuSample) output<<telemetry.gpuMilliseconds;
                output<<","<<scope("Physics and player movement")<<","<<scope("Components update")<<","<<scope("Script render resource rebuild")<<","<<scope("Physics world rebuild")<<","<<scope("UI resource rebuild")<<","<<scope("Scene asset resolution")<<","<<scope("Material asset database scan")<<",";
                unsigned int scans=0; for(const auto& sample:telemetry.cpuScopes) if(sample.name=="Material asset database scan") scans=sample.calls;
                const auto& player=Find(scene.World().Layout(),"park-player");
                maximumHeight=std::max(maximumHeight,player.position[1]); floated=floated || !player.playerController->useGravity;
                output<<scans<<","<<player.position[2]<<","<<player.position[1]<<","<<player.playerController->useGravity<<"\n";
                if(frame>=10) { updateTotal+=update; if(update>updateMaximum) { updateMaximum=update;worst=frame; } if(telemetry.gpuSample) gpuMaximum=std::max(gpuMaximum,telemetry.gpuMilliseconds); }
            }
            std::cout<<"Scenario "<<scenario<<": update mean "<<updateTotal/110<<" ms, max "<<updateMaximum<<" ms at frame "<<worst<<", GPU max "<<gpuMaximum<<" ms\n";
            if(scenario==2) Require(maximumHeight>1.3f && floated,"Trace executes actual jump and hover");
            Require(renderer.WaitForIdle(),"Trace GPU finishes");
        }
        SceneEnvironment motion; std::string error;
        Require(motion.Initialize(renderer,root,root/"Assets/Scenes/ParkAdventure.json",error),error);
        int unchanged=0; float previous=Find(motion.World().Layout(),"park-player").position[2];
        std::ofstream positions("generated/park/motion-144.csv"); positions<<"frame,player_z,delta_z,render_z,render_delta_z\n";
        float previousRender=previous; int unchangedRender=0;
        for(int frame=0;frame<120;++frame) {
            motion.SetInputActions({{"MoveForward",1.0f}},{});
            Require(motion.MovePlayers(1.0/144,0,1),"High refresh movement"); motion.Update(1.0/144,true,true);
            const float current=Find(motion.World().Layout(),"park-player").position[2];
            if(std::abs(current-previous)<1e-6f) ++unchanged;
            DirectX::XMFLOAT4X4 render;
            Require(motion.World().RenderWorldMatrix("park-player",render),"Interpolated player matrix");
            if(std::abs(render._43-previousRender)<1e-6f) ++unchangedRender;
            positions<<frame<<","<<current<<","<<current-previous<<","<<render._43<<","<<render._43-previousRender<<"\n";
            previous=current; previousRender=render._43;
        }
        std::cout<<"144 Hz update simulation: "<<unchanged<<"/120 physics repeats, "<<unchangedRender<<"/120 render repeats\n";
        Require(static_cast<bool>(output),"Trace saved");
    }
    void Render()
    {
        Engine::Window window;Engine::DirectX12Renderer renderer;
        Require(window.Create(L"Park validation",1280,720) && renderer.Initialize(window.GetHandle()),"Renderer initializes");
        SceneEnvironment scene;std::string error;
        const auto root=std::filesystem::absolute("Content");
        Require(scene.Initialize(renderer,root,root/"Assets/Scenes/ParkAdventure.json",error),error);
        const auto begin=std::chrono::steady_clock::now();
        for(int frame=0;frame<40;++frame) {
            Require(scene.MovePlayers(1.0/60,0,0),"Environment fixed update");scene.Update(1.0/60,true,true);
        }
        std::cout<<"CPU scene update: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/40<<" ms/frame (40 stationary frames)\n";
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
        std::ifstream schemaFile("Content/Assets/Scripts/ParkScriptSchema.schema.json");
        const auto schema=Engine::Json::parse(schemaFile);
        for(const auto& [name,source]:schema.at("scripts").items()) {
            const auto& definition=ScriptRegistry::Definitions().at(name);
            Require(definition.fields.size()==source.at("parameters").size(),"Schema parameter count differs from native metadata");
            for(const auto& [key,field]:source.at("parameters").items()) {
                const auto& native=definition.fields.at(key);
                Require(native.initial==field.at("initial").get<float>() && native.minimum==field.at("minimum").get<float>() && native.maximum==field.at("maximum").get<float>(),"Native parameter differs from schema");
            }
            ScenePlacement authored;authored.id=name;authored.scripts={{"rules",true,name,{},definition.dataFields}};
            SceneLayout authoredScene;authoredScene.objects={authored};
            const auto serialized=Engine::Json::parse(authoredScene.Serialize());
            const auto& components=serialized.at("objects").at(0).at("components");
            const auto savedScript=std::find_if(components.begin(),components.end(),[](const auto& component){return component.at("type")=="Script";});
            Require(savedScript!=components.end(),"Schema test script is serialized");
            const auto savedData=savedScript->value("data",Engine::Json{{"kind","object"},{"value",Engine::Json::object()}});
            Require(savedData.at("value")==source.at("data"),"Native data defaults differ from schema");
            for(const auto& [key,purpose]:source.at("purposes").items()) {
                Require(purpose=="sceneAsset" && definition.dataPurposes.at(key)==ScriptDataPurpose::SceneAsset,"Native data purpose differs from schema");
            }
        }
        if(GetEnvironmentVariableA("WP1_ALL_SCENES_ONLY",nullptr,0)) {AllScenesValidation::Run();return 0;}
        for(const char* name:{"Entrance","Terraces","Canal","SwitchGarden","Vista"}) {
            const auto prefab=SceneLayout::Load(std::filesystem::path("Content/Assets/Prefabs/Park")/(std::string(name)+".prefab"));
            Require(Prefab::Extract(prefab,"module").objects.size()==prefab.objects.size(),"Reusable module hierarchy");
        }
        LayoutDeltaValidation::Run();
        if(GetEnvironmentVariableA("WP1_PARK_RESOURCE_ONLY",nullptr,0)) {
            Engine::Window window; Engine::DirectX12Renderer renderer; std::string error;
            Require(window.Create(L"Park resource regression",640,360) && renderer.Initialize(window.GetHandle()),"Resource renderer initializes");
            Require(ValidateShaderCache(renderer.GetDevice(),renderer.GetCommandQueue(),error),error);
            SpriteResourceValidation::Run(renderer.GetDevice(),renderer.GetCommandQueue(),std::filesystem::current_path()/"Content");
            ParkResourceValidation::Run(renderer);
            std::cout<<"PASS: pipeline sharing, resource deltas and rollback\n"; return 0;
        }
        if(GetEnvironmentVariableA("WP1_PARK_UPDATE_ONLY",nullptr,0)) { UpdateTrace(); return 0; }
        if(GetEnvironmentVariableA("WP1_PARK_STUTTER_ONLY",nullptr,0)) { StutterTrace(); return 0; }
        ValidationParity(); EditableRules(); RemovedScriptOwners();
        if (!GetEnvironmentVariableA("WP1_PARK_RENDER_ONLY",nullptr,0)) Playthrough();
        Render();std::cout<<"PASS: park validation\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<"\n";
        for (const auto& entry:Engine::Log::Recent()) if(entry.level==Engine::LogLevel::Error) std::cerr<<entry.text<<"\n";
        return 1;
    }
}
