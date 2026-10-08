#pragma once
#include <SceneRuntime/Animator.h>
#include <SceneRuntime/SceneLayout.h>
#include <SceneRuntime/SceneWorld.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <cmath>
#include "SkinningValidation.h"
#include "BlendTreeValidation.h"
#include "AnimationEventValidation.h"
#include "IkValidation.h"
#include "IkPlaybackValidation.h"
#include "RootMotionValidation.h"
#include "RootMotionPlaybackValidation.h"
namespace AnimatorValidation {
inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
inline void Run() {
    RootMotionValidation::Run();
    IkValidation::Run();
    BlendTreeValidation::Schema();
    AnimationEventValidation::Schema();
    using namespace Engine; using namespace SceneRuntime;
    std::string error;
    const auto rig=Skeleton::Load(std::filesystem::absolute("Content/Assets/Models/AnimatedBox.gltf"),error);
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
    const auto scene=SceneLayout::Load("Content/Assets/Scenes/AnimatorPlayground.json");
    const auto restored=SceneLayout::Parse(scene.Serialize());
    Require(restored.objects.back().animator==scene.objects.back().animator,"Animator schema roundtrip");
}
inline void Runtime(Engine::DirectX12Renderer& renderer) {
    RootMotionPlaybackValidation::Runtime(renderer);
    IkPlaybackValidation::Runtime(renderer);
    AnimationEventValidation::Runtime(renderer);
    BlendTreeValidation::Runtime(renderer);
    SkinningValidation::Rendering(renderer);
    SkinningValidation::Model(renderer);
    using namespace SceneRuntime;
    const auto content=std::filesystem::absolute("Content");
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
