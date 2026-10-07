#pragma once
#include <SceneRuntime/ScenePhysics.h>
#include <SceneRuntime/SceneTransforms.h>
#include <Engine/Core/Json.h>
#include <cmath>
#include <limits>
#include <stdexcept>
#include "../Editor/src/GameSession.h"
#if defined(_DEBUG)
#include "../Editor/src/ComponentPanel.h"
#include <imgui.h>
#endif

namespace PhysicsValidation
{
    inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
    inline SceneRuntime::SceneLayout Layout()
    {
        using namespace SceneRuntime;
        ScenePlacement floor; floor.id="floor"; floor.name="Floor"; floor.position={0,-0.5f,0};
        floor.boxCollider.emplace(); floor.boxCollider->size={20,1,20};
        ScenePlacement wall; wall.id="wall"; wall.name="Wall"; wall.position={2,2,0};
        wall.boxCollider.emplace(); wall.boxCollider->size={0.1f,4,20};
        ScenePlacement player; player.id="player"; player.name="Player"; player.position={0,2,0};
        player.boxCollider.emplace(); player.playerController.emplace(); player.playerController->useGravity=true;
        SceneLayout layout; layout.objects={floor,wall,player}; return layout;
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        const auto layout=Layout(); const auto saved=layout.Serialize();
        Editor::GameSession session; std::string error;
        Require(session.Play(renderer,root,layout,error),"physics session initializes");
        Require(session.Runtime()->MovePlayers(0.1,0,0),"physics session falls");
        Require(session.Pause(),"physics session pauses");
        const auto before=session.Runtime()->World().Layout().objects.back().position;
        session.Update(0.1,true);
        Require(session.Runtime()->World().Layout().objects.back().position==before,"pause freezes falling player");
        Require(session.Step(),"physics session steps");
        Require(session.Runtime()->World().Layout().objects.back().position[1]<before[1],"step advances gravity once without input");
        Require(session.Stop() && session.Play(renderer,root,layout,error),"physics session restarts");
        Require(session.Runtime()->World().Layout().objects.back().position==layout.objects.back().position,"restart restores authored position");
        Require(session.Runtime()->MovePlayers(0.1,0,0),"restarted physics falls");
        Require(std::abs(session.Runtime()->World().Layout().objects.back().position[1]-before[1])<0.0001f,"restart resets falling velocity");
        Require(session.Stop() && layout.Serialize()==saved,"physics play does not modify authored data");
#if defined(_DEBUG)
        Editor::EditState state;
        for (int frame=0;frame<2;++frame)
            Require(renderer.Render({0.025f,0.035f,0.055f,1},[](auto*,float){},[&] {
                ImGui::SetNextWindowPos({20,20}); ImGui::SetNextWindowSize({550,600});
                ImGui::Begin("Physics Inspector validation");
                Editor::ComponentPanel::Draw(state,layout.objects.back(),nullptr);
                ImGui::End();
            })!=Engine::RenderResult::Failed,"physics Inspector renders without stack imbalance");
        Require(renderer.WaitForIdle(),"physics Inspector GPU completes");
#endif
    }
    inline void Run()
    {
        using namespace SceneRuntime;
        const auto playground=SceneRuntime::SceneLayout::Load("Content/Assets/Scenes/PhysicsPlayground.json");
        Require(SceneRuntime::SceneLayout::Parse(playground.Serialize()).objects.back().playerController->useGravity,"physics playground is valid");
        auto layout=Layout();
        const auto serialized=layout.Serialize();
        const auto restored=SceneLayout::Parse(serialized);
        Require(restored.objects.back().SameComponents(layout.objects.back()),"physics component JSON roundtrip");
        auto old=Engine::Json::parse(serialized);
        // Find by type; serialization order is not a data contract.
        for (auto& component : old["objects"][2]["components"])
            if (component["type"]=="PlayerController") { component.erase("gravity"); component.erase("jumpSpeed"); component.erase("useGravity"); }
        Require(!SceneLayout::Parse(old.dump()).objects.back().playerController->useGravity,"old controllers retain gravity off");
        for (const float size : {0.0f,-1.0f,std::numeric_limits<float>::infinity()})
        {
            auto invalid=layout; invalid.objects[0].boxCollider->size[0]=size;
            bool rejected=false; try { invalid.Serialize(); } catch (const std::exception&) { rejected=true; }
            Require(rejected,"invalid collider bounds rejected");
        }
        ScenePhysics::States states;
        for (int tick=0;tick<120;++tick) Require(ScenePhysics::Advance(layout,states,1.0/60,0,0,false),"gravity tick");
        Require(std::abs(layout.objects.back().position[1]-0.5f)<0.0001f && states.at("player").grounded,"player lands on floor");
        Require(ScenePhysics::Advance(layout,states,1.0/60,0,0,true),"jump tick");
        Require(layout.objects.back().position[1]>0.5f && states.at("player").verticalSpeed>0,"grounded jump rises");
        const float velocity=states.at("player").verticalSpeed;
        Require(ScenePhysics::Advance(layout,states,1.0/60,0,0,true),"airborne jump tick");
        Require(states.at("player").verticalSpeed<velocity,"airborne jump cannot reset velocity");
        for (int tick=0;tick<120;++tick) Require(ScenePhysics::Advance(layout,states,1.0/60,0,0,false),"jump landing tick");
        Require(states.at("player").grounded,"jump lands again");
        layout.objects.back().playerController->moveSpeed=1000;
        Require(ScenePhysics::Advance(layout,states,0.1,1,0,false),"fast wall sweep");
        Require(std::abs(layout.objects.back().position[0]-1.45f)<0.0001f,"thin wall prevents tunneling");
        Require(ScenePhysics::Advance(layout,states,0.1,-1,0,false),"move away from wall");
        Require(layout.objects.back().position[0]<1,"wall does not trap player");
        layout=Layout(); states.clear(); layout.objects[0].boxCollider->enabled=false;
        for (int tick=0;tick<120;++tick) Require(ScenePhysics::Advance(layout,states,1.0/60,0,0,false),"disabled floor tick");
        Require(layout.objects.back().position[1]<0,"disabled collider allows falling");
        layout=Layout(); states.clear(); layout.objects.back().playerController->enabled=false;
        Require(ScenePhysics::Advance(layout,states,0.1,1,1,true) && layout.objects.back().position==std::array<float,3>{0,2,0},"disabled controller remains still");
        layout=Layout(); states.clear();
        ScenePlacement parent; parent.id="parent"; parent.scale={2,2,2}; layout.objects.push_back(parent);
        layout.objects[2].parentId="parent"; layout.objects[2].position={0,0.5f,0};
        for (int tick=0;tick<120;++tick) Require(ScenePhysics::Advance(layout,states,1.0/60,0,0,false),"parented gravity tick");
        Require(std::abs(layout.objects[2].position[1]-0.5f)<0.0001f,"parent scale applied to collider and world gravity");
        Require(!ScenePhysics::Advance(layout,states,-1,0,0,false),"negative physics time rejected");
        Require(!ScenePhysics::Advance(layout,states,0.1,std::numeric_limits<float>::quiet_NaN(),0,false),"nonfinite physics input rejected");
    }
}
