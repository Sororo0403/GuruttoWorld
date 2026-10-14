#pragma once
#include <SceneRuntime/Animator.h>
#include <SceneRuntime/SceneLayout.h>
#include <SceneRuntime/SceneWorld.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Camera.h>
#include <cmath>
#include <set>
#include "SkinningValidation.h"
#include "BlendTreeValidation.h"
#include "AnimationEventValidation.h"
#include "IkValidation.h"
#include "IkPlaybackValidation.h"
#include "RootMotionValidation.h"
#include "RootMotionPlaybackValidation.h"
#include "RagdollValidation.h"
namespace AnimatorValidation {
inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
/// <summary>既存名と自動補完名が衝突するglTFを生成し、全クリップを名前で参照できることを確認します。</summary>
inline void ClipNames() {
    std::ifstream source("Games/GuruttoWorld/Assets/Models/AnimatedBox.gltf");const auto original=Engine::Json::parse(source);
    const auto directory=std::filesystem::absolute("generated/tests/skeletal-clip-names/Assets/Models");
    std::filesystem::create_directories(directory);
    for(const auto& names:std::array<std::array<std::string,3>,3>{{{"Move-2","Move","Move"},{"Move","Move","Move-1"},{"","Clip0","Clip0"}}}) {
        auto gltf=original;for(size_t index=0;index<names.size();++index) {
            if(names[index].empty()) gltf["animations"][index].erase("name");else gltf["animations"][index]["name"]=names[index];
        }
        const auto path=directory/"Collisions.gltf";{std::ofstream file(path);file<<gltf.dump();}
        std::string error;const auto rig=Engine::Skeleton::Load(path,error);Require(rig&&rig->clips.size()==3,"colliding clip fixture imports");
        std::set<std::string> imported;std::vector<std::array<float,4>> rotations;
        for(const auto& clip:rig->clips) {
            Require(imported.insert(clip.name).second,"skeletal import gives every clip a unique selectable name");
            rotations.push_back(Engine::Skeleton::Sample(*rig,clip.name,.5,false).back().rotation);
        }
        Require(rotations[0]!=rotations[1]&&rotations[0]!=rotations[2]&&rotations[1]!=rotations[2],"each imported name selects its own authored animation");
        for(const auto& name:names) if(!name.empty()) Require(imported.contains(name),"automatic clip names preserve existing authored names");
    }
}
inline void Run() {
    ClipNames();
    RagdollValidation::SchemaAndPhysics();
    RootMotionValidation::Run();
    IkValidation::Run();
    BlendTreeValidation::Schema();
    AnimationEventValidation::Schema();
    using namespace Engine; using namespace SceneRuntime;
    std::string error;
    const auto rig=Skeleton::Load(std::filesystem::absolute("Games/GuruttoWorld/Assets/Models/AnimatedBox.gltf"),error);
    Require(rig && rig->clips.size()==3 && !rig->meshes.empty(),"glTF skeleton and clips import");
    const auto rest=Skeleton::Sample(*rig,"",0,true);
    const auto bent=Skeleton::Sample(*rig,"Walk",0.5,false);
    const auto first=Skeleton::Skin(*rig,rig->meshes[0],Skeleton::Matrices(*rig,rest));
    const auto second=Skeleton::Skin(*rig,rig->meshes[0],Skeleton::Matrices(*rig,bent));
    bool moved=false;
    for (size_t i=0;i<first.vertices.size();++i) {
        moved=moved || std::abs(first.vertices[i].position[0]-second.vertices[i].position[0])>0.05f;
        const auto& n=second.vertices[i].normal;
        Require(std::abs(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]-1)<0.001f,"skinned normals normalized");
    } Require(moved,"weighted upper bone deforms mesh");
    const auto loop=Skeleton::Sample(*rig,"Walk",1.5,true);
    Require(std::abs(loop.back().rotation[2]-bent.back().rotation[2])<0.001f,"skeletal clip loops");
    const auto blend=Skeleton::Blend(rest,bent,0.5f);
    Require(blend.size()==rest.size() && blend.back().rotation!=rest.back().rotation,"quaternion blend interpolates");
    AnimatorComponent animator; animator.states={{"Idle","Idle",1,true},{"Walk","Walk",1,true}};
    animator.transitions={{"Idle","Walk","speed",">",0.05f,0.2f,-1},{"Walk","Idle","speed","<=",0.05f,0.2f,-1}};
    AnimatorState state; Animator::Advance(animator,state,*rig,0,{});
    Require(state.current=="Idle","initial Animator state");
    Animator::Advance(animator,state,*rig,0.1,{{"speed",1.0f}});
    Require(state.current=="Walk" && state.blendElapsed>0 && state.blendElapsed<state.blendDuration,"conditional transition blends");
    AnimatorState independent; Animator::Advance(animator,independent,*rig,0.1,{});
    Require(independent.current=="Idle","Animator instances independent");
    animator.enabled=false; const auto time=state.time;
    Animator::Advance(animator,state,*rig,1,{{"speed",0.0f}}); Require(state.time==time,"disabled Animator clock frozen");
    animator.enabled=true; animator.states[1].clip="missing"; bool rejected=false;
    try { Animator::Validate(animator,rig.get()); } catch (...) { rejected=true; } Require(rejected,"unknown clips rejected");
    const auto scene=SceneLayout::Load("Games/GuruttoWorld/Assets/Scenes/AnimatorPlayground.json");
    const auto restored=SceneLayout::Parse(scene.Serialize());
    Require(restored.objects.back().animator==scene.objects.back().animator,"Animator schema roundtrip");
}
/// <summary>Material のみを実行時に変更しても骨格の時計と個体別上書きが継続することを検証します。</summary>
inline void MaterialDelta(Engine::DirectX12Renderer& renderer) {
    using namespace SceneRuntime;
    ScriptDefinition change;
    change.update=[](ScriptContext& c) {
        if(c.Pressed("ChangeMaterial")) {
            c.object.material.emplace();
            c.object.material->asset="Assets/Materials/Red.mat";
        }
    };
    Require(ScriptRegistry::Register("ValidationAnimatorMaterialDelta",change),"material delta script registers");
    const auto root=std::filesystem::absolute("Games/GuruttoWorld");
    auto layout=SceneLayout::Load(root/"Assets/Scenes/AnimatorPlayground.json"); std::string error;
    auto player=std::find_if(layout.objects.begin(),layout.objects.end(),[](const auto& p){return p.id=="Player";});
    Require(player!=layout.objects.end(),"material delta fixture has animated Player");
    ScriptComponent script; script.behaviour="ValidationAnimatorMaterialDelta"; player->scripts.push_back(script);
    SceneWorld world; Require(world.Initialize(renderer,root,layout,root/"Shaders/Mesh.hlsl",&error),error.c_str());
    Require(world.SetAnimatorParameter("Player","deltaSentinel",.75f),"material delta retains individual overrides");
    Require(world.UpdateComponents(.15),"animated instance advances before material delta");
    const auto before=*world.AnimatorStatus("Player");
    world.SetInputActions({},{{"ChangeMaterial",true}});
    Require(world.UpdateComponents(.05),"material-only runtime script update succeeds");
    const auto* after=world.AnimatorStatus("Player");
    Require(after && after->current==before.current && std::abs(after->time-before.time-.05)<1e-6 &&
        after->parameterOverrides.at("deltaSentinel")==.75f,"material delta preserves existing Animator clock and state");
    const auto changed=std::find_if(world.Layout().objects.begin(),world.Layout().objects.end(),[](const auto& p){return p.id=="Player";});
    Require(changed->Material()==std::filesystem::path("Assets/Materials/Red.mat"),"material delta commits replacement material");
    Engine::Camera camera; camera.SetPosition({0,3,-10}); camera.SetPerspective(DirectX::XM_PIDIV4,2,.1f,100);
    Require(renderer.Render({0,0,0,1},[&](auto* commands,float){world.Draw(commands,camera,Engine::DirectionalLight{});})!=Engine::RenderResult::Failed && renderer.WaitForIdle(),
        "preserved animated instance and replacement material render together");
}
inline void Runtime(Engine::DirectX12Renderer& renderer) {
    {
        Engine::ModelRenderer model;const auto path=std::filesystem::absolute("generated/tests/skeletal-clip-names/Assets/Models/Collisions.gltf");
        Require(model.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),path,std::filesystem::absolute("Games/GuruttoWorld/Shaders/Mesh.hlsl")),"renamed clips prepare GPU model");
        DirectX::XMFLOAT4X4 display,identity;DirectX::XMStoreFloat4x4(&display,DirectX::XMMatrixScaling(.3f,.3f,.3f)*DirectX::XMMatrixTranslation(0,0,.5f));
        DirectX::XMStoreFloat4x4(&identity,DirectX::XMMatrixIdentity());Engine::DirectionalLight light;light.enabled=false;
        std::vector<std::vector<unsigned char>> images;
        for(const auto& clip:model.Rig()->clips) {
            Require(model.ApplyPose(Engine::Skeleton::Sample(*model.Rig(),clip.name,.5,false)),"unique clip name applies its pose");
            images.push_back(SkinningValidation::Capture(renderer,[&](auto* commands){model.Draw(commands,display,identity,light);}));
        }
        Require(images.size()==3&&images[0]!=images[1]&&images[0]!=images[2]&&images[1]!=images[2],"every colliding authored clip remains distinct in the GPU framebuffer");
    }
    MaterialDelta(renderer);
    RagdollValidation::Runtime(renderer);
    RootMotionPlaybackValidation::Runtime(renderer);
    IkPlaybackValidation::Runtime(renderer);
    AnimationEventValidation::Runtime(renderer);
    BlendTreeValidation::Runtime(renderer);
    SkinningValidation::Rendering(renderer);
    SkinningValidation::Model(renderer);
    using namespace SceneRuntime;
    const auto content=std::filesystem::absolute("Games/GuruttoWorld");
    auto scene=SceneLayout::Load(content/"Assets/Scenes/AnimatorPlayground.json"); std::string error;
    SceneWorld world; Require(world.Initialize(renderer,content,scene,content/"Shaders/Mesh.hlsl",&error),error.c_str());
    std::string copy; Require(world.DuplicateObject("Player",{3,0,0},copy,error),"skeletal instance duplicates");
    world.SetInputActions({{"MoveRight",1.0f}},{}); Require(world.UpdateComponents(0.05),"skeletal pose uploads");
    Require(world.AnimatorStateName("Player")=="Walk" && world.AnimatorStateName(copy)=="Walk","runtime action drives transition");
    auto settings=world.Layout().objects.back(); settings.animator->enabled=false;
    Require(world.SetComponents(copy,settings,content,error),"disable duplicate Animator");
    world.SetInputActions({},{}); Require(world.UpdateComponents(0.05),"independent state updates");
    Require(world.AnimatorStateName("Player")=="Idle" && world.AnimatorStateName(copy)=="Idle","disabled duplicate retains reset initial state");
    Require(world.RemoveObjects({copy},error) && world.AnimatorStateName(copy).empty(),"remove releases animation state");
    auto bad=world.Layout(); bad.objects.back().animator->states[0].clip="missing";
    Require(!world.ReplaceLayout(bad,content,error) && world.AnimatorStateName("Player")=="Idle","invalid clips preserve runtime");
    scene.settings.postEffects.enabled=true;
    for (auto& object : scene.objects) if (object.directionalLight) object.directionalLight->shadowsEnabled=true;
    const auto authored=scene.Serialize();
    Editor::GameSession session; Require(session.Play(renderer,content,scene,error),"GPU animation enters Editor playback");
    Require(session.Update(.1,true),"Editor animation advances");
    Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { session.Draw(commands,64,32); })!=Engine::RenderResult::Failed,"Editor draws GPU animation with HDR and shadows");
    Require(session.Pause() && !session.Update(.1,true) && session.Step(),"GPU animation supports pause and step");
    Require(renderer.WaitForIdle() && session.Stop() && scene.Serialize()==authored,"animation Stop preserves authored snapshot");
    SceneEnvironment app; Require(app.Initialize(renderer,content,scene,error),"App GPU animation environment prepares");
    app.Update(.1,true,true);
    Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { app.Draw(commands,64,32); })!=Engine::RenderResult::Failed,"App draws GPU animation with HDR and shadows");
    Require(renderer.WaitForIdle(),"skeletal uploads complete");
}
}
