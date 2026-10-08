#pragma once
#include "SkinningValidation.h"
#include "../Editor/src/GameSession.h"
#include <SceneRuntime/SceneEnvironment.h>
#include <SceneRuntime/SceneTransforms.h>

namespace RootMotionPlaybackValidation
{
    using EnvironmentValidation::Require;
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime; using namespace Engine; using namespace DirectX;
        const auto content=std::filesystem::absolute("Content"); std::string error;
        const auto scene=SceneLayout::Load(content/"Assets/Scenes/RootMotionPlayground.json",content);
        SceneWorld world; Require(world.Initialize(renderer,content,scene,content/"Shaders/Mesh.hlsl",&error),error.c_str());
        for (int frame=0;frame<13;++frame) Require(world.UpdateComponents(.1),"root motion object advances");
        Require(std::abs(world.Layout().objects[4].position[0]-1.3f)<.001f && world.Layout().objects[3].position==scene.objects[3].position,"root motion object continues through loop while disabled comparison stays fixed");
        const auto rig=Skeleton::Load(content/"Assets/Models/RootMotionBox.gltf",error); Require(rig!=nullptr,error.c_str());
        const auto root=std::ranges::find_if(rig->nodes,[](const auto& node) { return node.name=="Root"; });
        Require(root!=rig->nodes.end(),"root bone imports"); const auto rootIndex=static_cast<size_t>(root-rig->nodes.begin());
        const auto normalized=Skeleton::Matrices(*rig,world.AnimatorStatus("Root1")->pose),restMatrices=Skeleton::Matrices(*rig,Skeleton::Sample(*rig,"",0,false));
        Require(std::abs(normalized[rootIndex]._41-restMatrices[rootIndex]._41)<.001f,"moving object renders globally in-place root bone");
        Require(std::abs(world.Layout().objects[5].rotation[1])>.1f,"root motion applies turning rotation");
        SceneWorld firstFrame; Require(firstFrame.Initialize(renderer,content,scene,content/"Shaders/Mesh.hlsl",&error) && firstFrame.UpdateComponents(.1),error.c_str());
        ModelRenderer baked,moving;
        Require(baked.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),content/"Assets/Models/RootMotionBox.gltf",content/"Shaders/Mesh.hlsl") &&
            moving.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),content/"Assets/Models/RootMotionBox.gltf",content/"Shaders/Mesh.hlsl"),"root GPU comparison models prepare");
        Require(baked.ApplyPose(Skeleton::Sample(*baked.Rig(),"Turn",.1,true)) && moving.ApplyPose(firstFrame.AnimatorStatus("Root2")->pose),"baked and extracted root poses prepare");
        XMFLOAT4X4 initialWorld,actualWorld,display,projection;
        Require(SceneTransforms::Compose(scene.objects[5],initialWorld) && firstFrame.WorldMatrix("Root2",actualWorld),"root comparison world transforms");
        const auto bounds=baked.Bounds(); const float displayScale=.6f/std::max({bounds.Extents.x,bounds.Extents.y,bounds.Extents.z});
        XMStoreFloat4x4(&display,XMMatrixTranslation(-bounds.Center.x-scene.objects[5].position[0],-bounds.Center.y,-bounds.Center.z)*XMMatrixScaling(displayScale,displayScale,displayScale)*XMMatrixTranslation(0,0,.5f));
        XMStoreFloat4x4(&initialWorld,XMLoadFloat4x4(&initialWorld)*XMLoadFloat4x4(&display)); XMStoreFloat4x4(&actualWorld,XMLoadFloat4x4(&actualWorld)*XMLoadFloat4x4(&display));
        XMStoreFloat4x4(&projection,XMMatrixIdentity()); DirectionalLight light; light.enabled=false;
        const auto expected=SkinningValidation::Capture(renderer,[&](auto* commands) { baked.Draw(commands,initialWorld,projection,light); });
        const auto actual=SkinningValidation::Capture(renderer,[&](auto* commands) { moving.Draw(commands,actualWorld,projection,light); });
        Require(actual==expected,"extracted root object movement matches baked animation GPU image");
        Require(static_cast<size_t>(std::ranges::count_if(actual,[](unsigned char value) { return value!=0; }))>actual.size()/4+20,"root GPU comparison renders visible geometry");
        auto hierarchy=scene; ScenePlacement parent; parent.id="carrier"; parent.position={3,0,0}; parent.rotation={0,0,XM_PIDIV2}; parent.scale={2,2,2};
        hierarchy.objects[4].parentId=parent.id; hierarchy.objects.push_back(parent);
        SceneWorld inherited; Require(inherited.Initialize(renderer,content,hierarchy,content/"Shaders/Mesh.hlsl",&error) && inherited.UpdateComponents(.1),error.c_str());
        XMFLOAT4X4 inheritedWorld; Require(inherited.WorldMatrix("Root1",inheritedWorld),"parented root world matrix");
        Require(std::abs(inheritedWorld._41-3)<.001f && std::abs(inheritedWorld._42-.2f)<.001f,"root displacement inherits parent rotation and scale");
        auto withIk=scene; AnimatorIkConstraint reach; reach.name="reach"; reach.root="Root"; reach.middle="Upper"; reach.tip="Tip";
        reach.target={1,1,0}; reach.hint={2,1,0}; reach.worldSpace=true; withIk.objects[4].animator->ik={reach};
        SceneWorld constrained; Require(constrained.Initialize(renderer,content,withIk,content/"Shaders/Mesh.hlsl",&error) && constrained.UpdateComponents(.1),error.c_str());
        const auto tip=std::ranges::find_if(rig->nodes,[](const auto& node) { return node.name=="Tip"; }); Require(tip!=rig->nodes.end(),"root fixture IK tip");
        const auto matrices=Skeleton::Matrices(*rig,constrained.AnimatorStatus("Root1")->pose); const auto& tipMatrix=matrices[static_cast<size_t>(tip-rig->nodes.begin())];
        Require(constrained.WorldMatrix("Root1",inheritedWorld),"constrained moving world matrix"); XMFLOAT3 endpoint;
        XMStoreFloat3(&endpoint,XMVector3TransformCoord(XMVectorSet(tipMatrix._41,tipMatrix._42,tipMatrix._43,1),XMLoadFloat4x4(&rig->inverseRoot)*XMMatrixScaling(rig->importScale,rig->importScale,rig->importScale)*XMLoadFloat4x4(&inheritedWorld)));
        Require(std::abs(endpoint.x-1)+std::abs(endpoint.y-1)+std::abs(endpoint.z)<.001f,"World IK evaluates after root object movement");
        auto tiny=scene; tiny.objects[4].animator->states[0].speed=.0001f;
        SceneWorld slow; Require(slow.Initialize(renderer,content,tiny,content/"Shaders/Mesh.hlsl",&error) && slow.UpdateComponents(.1),error.c_str());
        Require(slow.Layout().objects[4].position[0]>0 && slow.Layout().objects[4].position[0]<.00002f,"tiny root motion is not discarded by transform tolerance");
        auto failing=scene; failing.objects[5].scale={2,1,1}; SceneWorld atomic;
        failing.objects[4].scripts={{"root-control",true,"RootMotionControl",{{"enabled",1.0f}}}};
        Require(atomic.Initialize(renderer,content,failing,content/"Shaders/Mesh.hlsl",&error),error.c_str()); const auto original=atomic.Layout().Serialize();
        Require(!atomic.UpdateComponents(.1) && atomic.Layout().Serialize()==original && atomic.AnimatorStatus("Root1")->time==0,"unrepresentable late root rotation preserves earlier object movement and clocks");
        Require(!atomic.AnimatorStatus("Root1")->rootMotionOverride,"late root failure rolls back Script override");
        auto collision=scene; collision.objects[4].boxCollider=BoxColliderComponent{}; collision.objects[4].boxCollider->size={.5f,.5f,.5f};
        collision.objects[4].playerController=PlayerControllerComponent{}; collision.objects[4].playerController->usePhysics=true;
        ScenePlacement wall; wall.id="wall"; wall.position={1,0,0}; wall.boxCollider=BoxColliderComponent{}; wall.boxCollider->size={.1f,4,4}; collision.objects.push_back(wall);
        SceneWorld blocked; Require(blocked.Initialize(renderer,content,collision,content/"Shaders/Mesh.hlsl",&error),error.c_str());
        for (int frame=0;frame<20;++frame) Require(blocked.UpdateComponents(.1) && blocked.MovePlayers(.1,1,0,false),"root movement collision and physics input update");
        Require(blocked.Layout().objects[4].position[0]>.5f && blocked.Layout().objects[4].position[0]<.71f,"root controller cannot tunnel through wall or double apply player input");
        if (!ScriptRegistry::Definitions().contains("RootToggleTest")) {
            ScriptDefinition toggle; toggle.update=[](ScriptContext& context) {
                context.state["calls"]+=1;
                context.scene->SetRootMotion(context.object.id,context.state["calls"]==1 ? std::optional<bool>(false) : std::nullopt);
            }; Require(ScriptRegistry::Register("RootToggleTest",std::move(toggle)),"root toggle test registers");
        }
        auto controlled=scene; controlled.objects[4].scripts={{"toggle",true,"RootToggleTest",{}}};
        SceneWorld scripted; Require(scripted.Initialize(renderer,content,controlled,content/"Shaders/Mesh.hlsl",&error) && scripted.UpdateComponents(.1),error.c_str());
        Require(scripted.Layout().objects[4].position[0]==0 && scripted.AnimatorStatus("Root1")->time>.09 && scripted.Layout().objects[4].animator->rootMotion,"Script disables root movement while preserving clock and authored setting");
        Require(scripted.UpdateComponents(.1) && scripted.Layout().objects[4].position[0]>.09f && !scripted.AnimatorStatus("Root1")->rootMotionOverride && scripted.AnimatorStatus("Root1")->time>.19,"Script restores root setting without resetting animation");
        SceneEnvironment app; Require(app.Initialize(renderer,content,scene,error),error.c_str()); app.Update(.1,true,true);
        Require(app.World().Layout().objects[4].position[0]>.09f,"App applies root object movement");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { app.Draw(commands,64,32); })!=RenderResult::Failed && renderer.WaitForIdle(),"App renders moving roots");
        Editor::GameSession session; Require(session.Play(renderer,content,scene,error) && session.Update(.1,true) && session.Pause(),"Editor root playback pauses");
        const auto position=session.Runtime()->World().Layout().objects[4].position;
        Require(!session.Update(.1,true) && session.Runtime()->World().Layout().objects[4].position==position,"paused root object remains fixed");
        Require(session.Step() && session.Runtime()->World().Layout().objects[4].position[0]>position[0],"Editor step moves root object once");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { session.Draw(commands,64,32); })!=RenderResult::Failed && renderer.WaitForIdle(),"Editor renders moving roots");
        const auto authored=scene.Serialize(); Require(session.Stop() && scene.Serialize()==authored,"Stop restores authored root transforms");
    }
}
