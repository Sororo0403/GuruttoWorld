#pragma once
#include <SceneRuntime/Ragdoll.h>
#include <SceneRuntime/PhysicsWorld.h>
#include <SceneRuntime/SceneWorld.h>
#include <SceneRuntime/SceneEnvironment.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Camera.h>
#include "../Editor/src/EditHistory.h"
namespace RagdollValidation
{
    inline void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    inline Engine::SkeletonData Rig()
    {
        Engine::SkeletonData rig; DirectX::XMStoreFloat4x4(&rig.inverseRoot,DirectX::XMMatrixIdentity()); rig.importScale=2;
        Engine::BonePose tip; tip.position={0,1,0}; rig.nodes={{"root",-1,{}},{"tip",0,tip}}; Engine::RiggedMesh mesh; mesh.joints={0,1}; rig.meshes.push_back(mesh); return rig;
    }
    inline void SchemaAndPhysics()
    {
        using namespace SceneRuntime;
        auto rig=Rig(); SceneLayout layout; ScenePlacement owner; owner.id=owner.name="actor"; owner.position={0,3,0}; owner.SetModel("Assets/Models/AnimatedBox.gltf"); owner.animator.emplace(); owner.ragdoll.emplace(); layout.objects={owner};
        const auto original=layout.Serialize(); Editor::EditHistory history; history.Reset({original,"actor"}); std::string error;
        Require(Ragdoll::Generate(layout,"actor",rig,{},error),error.c_str());
        Require(layout.objects.size()==3 && layout.objects[0].ragdoll->bones.size()==2,"ragdoll generates only mapped skin joints");
        Require(layout.objects[1].rigidBody->motion=="kinematic" && layout.objects[2].joint && !layout.objects[2].joint->enabled,"inactive ragdoll starts with kinematic bodies and disabled constraints");
        const auto restored=SceneLayout::Parse(layout.Serialize()); Require(restored.objects[0].ragdoll==layout.objects[0].ragdoll,"ragdoll bindings and bind offsets survive serialization");
        history.Observe({layout.Serialize(),"actor"},{}); history.Commit(); Require(history.Target(false).json==original,"one Undo removes the entire generated rig");
        auto pose=std::vector<Engine::BonePose>{rig.nodes[0].rest,rig.nodes[1].rest}; pose[0].position={.5f,0,0}; Ragdoll::Synchronize(layout,"actor",rig,pose);
        Require(std::abs(layout.objects[1].position[0]-1)<.001f,"kinematic ragdoll follows animation with import scale compensation");
        layout.objects[0].ragdoll->active=true; Ragdoll::Synchronize(layout,"actor",rig,pose);
        Require(layout.objects[1].rigidBody->motion=="dynamic" && layout.objects[2].joint->enabled,"activation switches bodies and constraints together");
        const auto reconstructed=Ragdoll::Pose(layout,layout.objects[0],rig,pose);
        Require(std::abs(reconstructed[0].position[0]-.5f)<.001f && std::abs(reconstructed[1].position[1]-1)<.001f,"ragdoll restores bone local pose from world bodies");
        PhysicsWorld physics; ScenePhysics::States states;
        for(int i=0;i<30;++i) Require(physics.Advance(layout,states,1.0/60,0,0,false,{},error),error.c_str());
        const auto fallen=Ragdoll::Pose(layout,layout.objects[0],rig,pose);
        Require(fallen[0].position[1]<-.1f,"gravity moves the skeleton through the ragdoll body pose");
        Require(std::abs(std::hypot(layout.objects[2].position[0]-layout.objects[1].position[0],layout.objects[2].position[1]-layout.objects[1].position[1])-2)<.08f,"swing twist anchors maintain limb length");
        auto copy=*layout.objects[0].ragdoll; const auto previous=copy.bones[0].body; copy.Remap(std::map<std::string,std::string>{{previous,"copied-body"}}); Require(copy.bones[0].body=="copied-body","ragdoll references remap for copied hierarchies");
        auto invalid=*layout.objects[0].ragdoll; invalid.bones.push_back(invalid.bones.front()); bool rejected=false;
        try {invalid.Validate();} catch(...) {rejected=true;} Require(rejected,"duplicate ragdoll bone mappings are rejected");
        layout.objects[0].ragdoll->active=false; Ragdoll::Synchronize(layout,"actor",rig,pose); Require(layout.objects[1].rigidBody->motion=="kinematic" && !layout.objects[2].joint->enabled,"deactivation resumes animation tracking");
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime; const auto root=std::filesystem::absolute("Content"); auto layout=SceneLayout::Load(root/"Assets/Scenes/AnimatorPlayground.json"); std::string error;
        auto owner=std::find_if(layout.objects.begin(),layout.objects.end(),[](const auto& p){return p.id=="Player";}); Require(owner!=layout.objects.end(),"ragdoll runtime fixture has a Player"); owner->ragdoll.emplace(); owner->position[1]+=5;
        SceneWorld world; Require(world.Initialize(renderer,root,layout,root/"Shaders/Mesh.hlsl",&error),error.c_str());
        Require(world.GenerateRagdoll("Player",root,error),error.c_str());
        auto generated=world.Layout(); auto player=std::find_if(generated.objects.begin(),generated.objects.end(),[](const auto& p){return p.id=="Player";}); player->ragdoll->active=true; const auto ownerPosition=player->position;
        Require(world.ReplaceLayout(generated,root,error,true),error.c_str()); const auto before=world.AnimatorStatus("Player")->pose;
        for(int i=0;i<12;++i) Require(world.MovePlayers(1.0/60,1,1,true) && world.UpdateComponents(1.0/60),"ragdoll fixed physics and animation pose prepare together");
        const auto currentPlayer=std::find_if(world.Layout().objects.begin(),world.Layout().objects.end(),[](const auto& p){return p.id=="Player";});
        Require(currentPlayer->position==ownerPosition && (!currentPlayer->playerController || currentPlayer->playerController->enabled==player->playerController->enabled),"active ragdoll suppresses owner motion while preserving authored controller enable state");
        const auto& after=world.AnimatorStatus("Player")->pose; bool moved=false; for(size_t i=0;i<after.size();++i) moved=moved || std::abs(after[i].position[1]-before[i].position[1])>.01f;
        Require(moved,"SceneWorld applies physical bone movement to live Animator pose");
        Engine::Camera camera; camera.SetPosition({0,3,-10}); camera.SetPerspective(DirectX::XM_PIDIV4,2,.1f,100);
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float){world.Draw(commands,camera,Engine::DirectionalLight{});})!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"ragdoll skin pose reaches the shared GPU rendering path");
        SceneEnvironment environment; Require(environment.Initialize(renderer,root,generated,error),error.c_str());
        const auto initial=environment.World().AnimatorStatus("Player")->pose;
        environment.Update(.1,true,true); const auto& simulated=environment.World().AnimatorStatus("Player")->pose;
        bool simulatedMove=false; for(size_t i=0;i<initial.size();++i) simulatedMove=simulatedMove || std::abs(initial[i].position[1]-simulated[i].position[1])>.01f;
        Require(simulatedMove,"SceneEnvironment advances ragdoll physics without an external player movement call");
        Require(renderer.WaitForIdle(),"autonomous ragdoll resources finish before destruction");
    }
}
