#pragma once
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <SceneRuntime/PhysicsWorld.h>
#include <SceneRuntime/SceneEnvironment.h>
#include <cmath>
#include <stdexcept>

namespace JointValidation
{
    inline void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    inline SceneRuntime::SceneLayout Body()
    {
        SceneRuntime::SceneLayout layout; SceneRuntime::ScenePlacement object;object.id="body";object.name="Body";
        object.boxCollider.emplace();object.rigidBody.emplace();object.position={2,0,3};layout.objects.push_back(object);return layout;
    }
    inline void Run()
    {
        using namespace SceneRuntime; auto planar=Body();planar.objects[0].rigidBody->planar=true;
        planar.objects[0].rigidBody->gravityScale=0;planar.objects[0].rigidBody->velocity={1,2,8};planar.objects[0].rigidBody->angularVelocity={1,1,0};
        auto parsed=SceneLayout::Parse(planar.Serialize());Require(parsed.objects[0].rigidBody->planar,"2D physics round trips");
        PhysicsWorld physics;ScenePhysics::States states;std::string error;
        for(int tick=0;tick<60;++tick) Require(physics.Advance(planar,states,1.0/60,0,0,false,{},error),"2D body advances");
        Require(planar.objects[0].position[0]>2.5f && planar.objects[0].position[1]>1 && std::abs(planar.objects[0].position[2]-3)<1e-5f,"2D moves in XY and locks Z translation");
        Require(std::abs(planar.objects[0].rotation[0])<1e-5f && std::abs(planar.objects[0].rotation[1])<1e-5f,"2D locks X/Y angular movement");
        for(const std::string type:{"fixed","hinge","distance","swingTwist"})
        {
            auto layout=Body();auto& object=layout.objects[0];object.position={2,0,0};object.joint.emplace();auto& joint=*object.joint;joint.type=type;
            joint.connectedAnchor={2,0,0};joint.minDistance=joint.maxDistance=0;
            if(type=="distance") {joint.connectedAnchor={0,0,0};joint.minDistance=joint.maxDistance=2;}
            auto saved=SceneLayout::Parse(layout.Serialize());Require(saved.objects[0].joint==object.joint,"Joint metadata round trips");
            PhysicsWorld world;ScenePhysics::States bodyStates;
            for(int tick=0;tick<90;++tick) Require(world.Advance(layout,bodyStates,1.0/60,0,0,false,{},error),"Joint fixed stepping succeeds");
            const auto& position=layout.objects[0].position;
            Require(type=="distance" ? std::abs(std::hypot(position[0],position[1])-2)<0.05f : std::abs(position[0]-2)<0.05f && std::abs(position[1])<0.05f,"Joint anchor resists gravity or maintains distance");
            const auto snapshot=layout.Serialize();layout.objects[0].joint->target="missing";
            const auto invalid=layout.Serialize();Require(!world.Advance(layout,bodyStates,1.0/60,0,0,false,{},error) && layout.Serialize()==invalid,"invalid Joint fails without changing candidate");
            layout=SceneLayout::Parse(snapshot);Require(world.Advance(layout,bodyStates,1.0/60,0,0,false,{},error),"Joint world survives failed rebuild");
        }
        auto invalid=Body();invalid.objects[0].joint.emplace();invalid.objects[0].joint->axis={0,0,0};
        bool rejected=false;try {invalid.Serialize();} catch(...) {rejected=true;} Require(rejected,"zero Joint axis is rejected");
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        auto layout=Body();layout.objects[0].rigidBody->planar=true;
        SceneRuntime::SceneEnvironment environment;std::string error;
        Require(environment.Initialize(renderer,std::filesystem::absolute("Content"),layout,error),"2D environment initializes");
        for(int tick=0;tick<60;++tick) environment.Update(1.0/60,true,true);
        const auto& position=environment.World().Layout().objects[0].position;
        Require(position[1]<-5 && std::abs(position[2]-3)<1e-5f,"SceneEnvironment automatically advances standalone 2D physics without Script or external MovePlayers");
        Require(renderer.WaitForIdle(),"2D environment GPU completion");
    }
}
