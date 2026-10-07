#pragma once
#include <SceneRuntime/PhysicsWorld.h>
#include <Engine/Core/Json.h>
#include <cmath>
#include <stdexcept>
#include "../Editor/src/GameSession.h"
#if defined(_DEBUG)
#include "../Editor/src/ComponentPanel.h"
#endif

namespace RigidBodyValidation
{
    inline void Require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
    inline SceneRuntime::SceneLayout Layout(bool character=false)
    {
        using namespace SceneRuntime;
        ScenePlacement floor; floor.id="floor"; floor.name="Floor"; floor.position={0,-0.5f,0}; floor.boxCollider.emplace(); floor.boxCollider->size={20,1,20};
        ScenePlacement actor; actor.id="actor"; actor.name="Actor"; actor.position={0,2,0}; actor.boxCollider.emplace();
        if (character)
        {
            actor.boxCollider->shape="capsule"; actor.playerController.emplace(); actor.playerController->usePhysics=true; actor.playerController->useGravity=true;
        }
        else { actor.boxCollider->shape="sphere"; actor.rigidBody.emplace(); actor.rigidBody->mass=2; }
        SceneLayout layout; layout.objects={floor,actor}; return layout;
    }
    inline void Run()
    {
        using namespace SceneRuntime;
        const auto root=std::filesystem::absolute("Content");
        auto layout=Layout(); std::string error; ScenePhysics::States states; PhysicsWorld world;
        const auto tick=[&](double seconds=1.0/60,float x=0,float z=0,bool jump=false) {
            if (!world.Advance(layout,states,seconds,x,z,jump,root,error)) throw std::runtime_error("rigid tick: "+error);
        };
        Require(SceneLayout::Parse(layout.Serialize()).objects[1].rigidBody==layout.objects[1].rigidBody,"rigid body schema roundtrip");
        for (int i=0;i<120;++i) tick();
        Require(std::abs(layout.objects[1].position[1]-0.5f)<0.03f,"dynamic sphere settles on floor");
        Require(world.AddImpulse("actor",{4,8,0}),"dynamic impulse accepted"); tick(0.1);
        Require(layout.objects[1].position[0]>0.1f && layout.objects[1].position[1]>0.7f,"impulse respects mass and moves body");
        const auto ray=world.Raycast({0,5,0},{0,-2,0},10,0xffffffffu,false,"actor");
        Require(ray && ray->object=="floor" && std::abs(ray->distance-5)<0.02f && ray->normal[1]>0.99f,"normalized raycast returns distance and surface normal");
        auto invalid=layout; invalid.objects[1].rigidBody->mass=0;
        const auto valid=layout.Serialize();
        Require(!world.Advance(invalid,states,0.1,0,0,false,root,error) && invalid.objects[1].rigidBody->mass==0,"invalid body settings rejected without partial layout changes");
        Require(layout.Serialize()==valid,"invalid candidate preserves caller's prior scene");
        tick();
        layout=Layout(); world.Reset(); states.clear();
        layout.objects[1].position={-2,1,0}; layout.objects[1].rigidBody->gravityScale=0; layout.objects[1].rigidBody->linearDamping=0;
        layout.objects[1].rigidBody->restitution=1; layout.objects[1].rigidBody->velocity={4,0,0};
        auto opponent=layout.objects[1]; opponent.id="opponent"; opponent.name="Opponent"; opponent.position={2,1,0}; opponent.rigidBody->velocity={-4,0,0}; layout.objects.push_back(opponent);
        for (int i=0;i<60;++i) tick();
        Require(layout.objects[1].position[0]<-2 && layout.objects[2].position[0]>2,"dynamic bodies exchange impulses and rebound");
        layout=Layout(); world.Reset(); states.clear(); layout.objects[1].boxCollider->mask=0;
        for (int i=0;i<90;++i) tick();
        Require(layout.objects[1].position[1]<-5,"collision layer mask rejects floor response");
        layout=Layout(); world.Reset(); states.clear();
        layout.objects[1].position={0,1,0}; layout.objects[1].boxCollider->radius=0.1f;
        layout.objects[1].rigidBody->gravityScale=0; layout.objects[1].rigidBody->linearDamping=0; layout.objects[1].rigidBody->velocity={100,0,0};
        ScenePlacement wall; wall.id="wall"; wall.name="Wall"; wall.position={2,1,0}; wall.boxCollider.emplace(); wall.boxCollider->size={0.05f,5,5}; layout.objects.push_back(wall);
        tick(0.1);
        Require(layout.objects[1].position[0]<1.95f,"CCD catches thin wall at high velocity");
        layout=Layout(); world.Reset(); states.clear();
        layout.objects[1].position={-2,1,0}; layout.objects[1].rigidBody->gravityScale=0; layout.objects[1].rigidBody->linearDamping=0; layout.objects[1].rigidBody->velocity={4,0,0};
        ScenePlacement trigger; trigger.id="trigger"; trigger.name="Trigger"; trigger.position={0,1,0}; trigger.boxCollider.emplace(); trigger.boxCollider->isTrigger=true; trigger.boxCollider->layer=3;
        layout.objects.push_back(trigger);
        int enters=0,exits=0,stays=0;
        for (int i=0;i<120;++i)
        {
            tick(); for (const auto& event : world.Events()) if (event.trigger && (event.first=="trigger" || event.second=="trigger"))
            { enters+=event.phase=="Enter"; exits+=event.phase=="Exit"; stays+=event.phase=="Stay"; }
        }
        Require(enters==1 && exits==1 && stays>0 && layout.objects[1].position[0]>5,"trigger enter/stay/exit fires once and permits passage");
        Require(!world.Raycast({0,1,-5},{0,0,1},10,0xffffffffu,false),"raycast excludes triggers by default");
        const auto triggerRay=world.Raycast({0,1,-5},{0,0,1},10,1u<<3,true);
        Require(triggerRay && triggerRay->object=="trigger","raycast optionally includes matching trigger layer");
        Require(!world.Raycast({0,1,-5},{0,0,1},10,1,true),"raycast respects layer mask");
        layout=Layout(true); world.Reset(); states.clear();
        for (int i=0;i<120;++i) tick();
        Require(states.at("actor").grounded && std::abs(layout.objects[1].position[1]-1)<0.04f,"capsule character lands and reports grounded");
        tick(0.1,0,0,true); Require(layout.objects[1].position[1]>1.3f && states.at("actor").verticalSpeed>0,"advanced character jumps");
        layout=Layout(true); world.Reset(); states.clear();
        layout.objects[0].rotation={0,0,0.2f}; layout.objects[0].position={0,-0.25f,0}; layout.objects[0].boxCollider->size={20,0.5f,20};
        for (int i=0;i<120;++i) tick();
        const auto start=layout.objects[1].position;
        for (int i=0;i<60;++i) tick(1.0/60,1);
        Require(layout.objects[1].position[0]>start[0]+3 && layout.objects[1].position[1]>start[1]+0.5f,"character walks up an oriented slope");
        layout=Layout(true); world.Reset(); states.clear();
        layout.objects[0].rigidBody.emplace(); layout.objects[0].rigidBody->motion="kinematic";
        for (int i=0;i<120;++i) tick();
        for (int i=0;i<60;++i) { layout.objects[0].position[0]+=0.03f; tick(); }
        Require(layout.objects[1].position[0]>1.2f && states.at("actor").grounded,"character follows moving platform without input");
        layout=Layout(); world.Reset(); states.clear();
        layout.objects[0].boxCollider->shape="mesh"; layout.objects[0].boxCollider->model="Assets/Models/Cube.obj";
        layout.objects[0].boxCollider->center={0,0,0}; layout.objects[0].scale={10,1,10}; layout.objects[0].position={0,-0.75f,0};
        for (int i=0;i<120;++i) tick();
        Require(std::abs(layout.objects[1].position[1]-0.5f)<0.04f,"rigid body collides with imported triangle mesh");
        layout.objects[1].boxCollider->shape="mesh"; layout.objects[1].boxCollider->model="Assets/Models/Cube.obj"; layout.objects[1].boxCollider->convex=true;
        layout.objects[1].position={0,3,0};
        for (int i=0;i<120;++i) tick();
        Require(layout.objects[1].position[1]>0.5f && layout.objects[1].position[1]<1,"dynamic convex model collider settles");
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime;
        ScriptDefinition script;
        script.update=[](ScriptContext& c) {
            Require(c.scene->Raycast({0,10,-6},{0,-1,0},20).has_value(),"script can query live physics");
            if (c.Pressed("Kick")) c.scene->AddImpulse("Ball1",{0,8,0});
        };
        script.onEvent=[](ScriptContext& c) { if (c.event->name=="collisionEnter") { c.state["hits"]+=1; c.object.name="Contacts "+std::to_string(static_cast<int>(c.state["hits"])); } };
        static bool registered=false;
        if (!registered) { Require(ScriptRegistry::Register("ValidationPhysicsEvents",script),"physics event script registers"); registered=true; }
        const auto root=std::filesystem::absolute("Content");
        auto layout=SceneLayout::Load(root/"Assets/Scenes/RigidBodyPlayground.json",root);
        const auto floor=std::find_if(layout.objects.begin(),layout.objects.end(),[](const auto& item) { return item.id=="Floor"; });
        floor->scripts.push_back({"events",true,"ValidationPhysicsEvents",{}});
        const auto saved=layout.Serialize();
        Editor::GameSession session; std::string error;
        if (!session.Play(renderer,root,layout,error)) throw std::runtime_error("rigid sample starts: "+error);
        auto* environment=session.Runtime();
        const auto ball=[&]() {
            const auto& objects=environment->World().Layout().objects;
            return std::find_if(objects.begin(),objects.end(),[](const auto& item) { return item.id=="Ball1"; })->position;
        };
        Require(environment->MovePlayers(0.1,0,0) && session.Update(0.1,true),"live rigid body advances");
        Require(ball()[1]<4,"rigid ball falls in Editor playback");
        Require(session.Pause(),"rigid playback pauses"); const auto paused=ball();
        session.Update(0.1,true); Require(ball()==paused,"pause freezes rigid body");
        Require(session.Step() && ball()!=paused,"step advances one physics frame");
        Require(session.Play(renderer,root,layout,error),"paused physics resumes");
        environment->SetInputActions({},{{"Kick",true}}); session.Update(1.0/60,true);
        const auto beforeKick=ball();
        environment->SetInputActions({},{}); Require(environment->MovePlayers(0.1,0,0),"script impulse reaches physics");
        Require(ball()[1]>beforeKick[1],"deferred script impulse raises ball");
        for (int i=0;i<120;++i) { Require(environment->MovePlayers(1.0/60,0,0),"sample physics advances"); session.Update(1.0/60,true); }
        const auto& current=environment->World().Layout().objects;
        const auto updatedFloor=std::find_if(current.begin(),current.end(),[](const auto& item) { return item.id=="Floor"; });
        Require(updatedFloor->name.starts_with("Contacts "),"physics collision events reach scripts in playback");
        Require(renderer.Render({0.025f,0.035f,0.055f,1},[&](auto* commands,float) { session.Draw(commands,renderer.GetWidth(),renderer.GetHeight()); })!=Engine::RenderResult::Failed,"rigid sample renders imported shapes and materials");
        Require(renderer.WaitForIdle(),"rigid sample rendering completes");
#if defined(_DEBUG)
        Editor::EditState edit;
        const auto actor=std::find_if(current.begin(),current.end(),[](const auto& item) { return item.id=="Ball1"; });
        Require(renderer.Render({0,0,0,1},[](auto*,float){},[&] { ImGui::Begin("Rigid inspector validation"); Editor::ComponentPanel::Draw(edit,*actor,nullptr); ImGui::End(); })!=Engine::RenderResult::Failed,"rigid inspector displays without stack errors");
        Require(renderer.WaitForIdle(),"rigid inspector rendering completes");
#endif
        Require(session.Stop() && layout.Serialize()==saved,"stopping physics preserves authored scene");
        Require(session.Play(renderer,root,layout,error),"rigid sample restarts"); environment=session.Runtime();
        Require(ball()[1]==4,"restart restores body transforms and initial velocities");
        Require(renderer.WaitForIdle() && session.Stop(),"restarted physics resources stop safely");
    }
}
