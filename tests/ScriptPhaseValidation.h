#pragma once
#include <SceneRuntime/ScriptRuntime.h>
#include <SceneRuntime/SceneEnvironment.h>
#include "../Editor/src/GameSession.h"
#include <memory>
#include <stdexcept>
#include <cmath>

namespace ScriptPhaseValidation
{
    inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
    inline void Schema()
    {
        using namespace SceneRuntime;
        auto order=std::make_shared<std::string>();
        ScriptDefinition definition;
        definition.start=[order](ScriptContext&) { *order+="S"; };
        definition.fixedUpdate=[order](ScriptContext& c) {
            Require(c.phase==ScriptPhase::FixedUpdate && std::abs(c.seconds-1.0/60)<1e-8,"fixed callback receives fixed time and phase");
            *order+="F"; c.scene->Emit({"tick",c.object.id,c.object.id,1});
            c.scene->SetAnimatorParameter(c.object.id,"speed",3);
        };
        definition.update=[order](ScriptContext& c) {
            Require(c.phase==ScriptPhase::Update,"normal callback receives normal phase"); *order+="U";
        };
        definition.lateUpdate=[order](ScriptContext& c) {
            Require(c.phase==ScriptPhase::LateUpdate,"late callback receives late phase"); *order+="L";
            c.state["late"]+=1; c.object.position[0]=c.state["late"];
            if (c.Pressed("Fail")) throw std::runtime_error("late failure");
        };
        definition.onEvent=[order](ScriptContext& c) { if (c.event && c.event->name=="tick") *order+="E"; };
        Require(ScriptRegistry::Register("ValidationPhases",definition),"phase definition registers");
        ScriptDefinition fixedOnly; fixedOnly.fixedUpdate=[](ScriptContext&) {};
        ScriptDefinition lateOnly; lateOnly.lateUpdate=[](ScriptContext&) {};
        Require(ScriptRegistry::Register("ValidationFixedOnly",fixedOnly) && ScriptRegistry::Register("ValidationLateOnly",lateOnly),"fixed-only and late-only definitions register");
        ScenePlacement owner; owner.id="owner"; owner.name="Owner"; owner.scripts.push_back({"script",true,"ValidationPhases",{}});
        owner.animator.emplace(); owner.animator->states[0].clip="Walk";
        SceneLayout layout; layout.objects={owner}; ScriptRuntime runtime; std::string error;
        Require(ScriptRuntime::HasPhase(layout,ScriptPhase::FixedUpdate) && ScriptRuntime::HasPhase(layout,ScriptPhase::LateUpdate),"enabled phases detected");
        Require(runtime.Update(layout,1.0/60,error,{},{},nullptr,ScriptPhase::FixedUpdate),"fixed phase runs");
        Require(runtime.Update(layout,1.0/60,error,{},{},nullptr,ScriptPhase::FixedUpdate),"second fixed phase runs");
        Require(runtime.Update(layout,0.1,error) && runtime.AnimatorParameters().at("owner").at("speed")==3,"fixed commands and events reach normal update");
        Require(runtime.Update(layout,0.1,error,{},{},nullptr,ScriptPhase::LateUpdate) && *order=="SFFEEUL","start once, fixed events preserved and normal before late");
        const auto saved=layout.Serialize();
        Require(!runtime.Update(layout,0.1,error,{},{{"Fail",true}},nullptr,ScriptPhase::LateUpdate) && layout.Serialize()==saved,"failed late callback preserves scene");
        Require(runtime.Update(layout,0.1,error,{},{},nullptr,ScriptPhase::LateUpdate) && layout.objects[0].position[0]==2,"failed late callback preserves internal state");
        layout.objects[0].scripts[0].enabled=false;
        Require(!ScriptRuntime::HasPhase(layout,ScriptPhase::FixedUpdate),"disabled phase not scheduled"); runtime.Stop(layout);
    }
    inline SceneRuntime::SceneLayout Layout()
    {
        using namespace SceneRuntime;
        ScenePlacement floor; floor.id="floor"; floor.name="Floor"; floor.position={0,-0.5f,0}; floor.boxCollider.emplace(); floor.boxCollider->size={20,1,20};
        floor.SetModel("Assets/Models/Cube.obj");
        ScenePlacement body; body.id="body"; body.name="Body"; body.position={0,3,0}; body.boxCollider.emplace(); body.boxCollider->shape="sphere"; body.rigidBody.emplace();
        body.SetModel("Assets/Models/Cube.obj");
        ScenePlacement driver; driver.id="driver"; driver.name="Driver"; driver.scripts.push_back({"script",true,"ValidationFixedDriver",{}});
        SceneLayout layout; layout.objects={floor,body,driver}; return layout;
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime;
        ScriptDefinition driver;
        driver.fixedUpdate=[](ScriptContext& c) {
            Require(std::abs(c.seconds-1.0/60)<1e-8,"runtime fixed delta is stable");
            c.state["ticks"]+=1; c.object.position[0]=c.state["ticks"];
            if (c.Pressed("Fire")) { c.object.position[1]+=1; c.scene->AddImpulse("body",{1,0,0}); }
            if (c.Pressed("Bad")) { ScenePlacement bad; bad.name="Missing"; bad.SetModel("Assets/Models/missing-phase-model.obj"); c.scene->Spawn(bad); }
        };
        driver.update=[](ScriptContext& c) { c.object.rotation[1]+=static_cast<float>(c.seconds); };
        driver.lateUpdate=[](ScriptContext& c) {
            const auto* body=c.scene->Find("body"); Require(body!=nullptr,"late sees physics body"); c.object.position[2]=body->position[1];
            if (c.Pressed("LateBad")) { ScenePlacement bad; bad.name="Late missing"; bad.SetModel("Assets/Models/missing-phase-model.obj"); c.scene->Spawn(bad); }
        };
        static bool registered=false;
        if (!registered) { Require(ScriptRegistry::Register("ValidationFixedDriver",driver),"runtime fixed driver registers"); registered=true; }
        const auto root=std::filesystem::absolute("Content"); const auto scene=Layout(); const auto saved=scene.Serialize(); std::string error;
        Editor::GameSession thirty,oneTwenty;
        Require(thirty.Play(renderer,root,scene,error) && oneTwenty.Play(renderer,root,scene,error),"fixed comparison sessions start");
        for (int frame=0;frame<30;++frame) thirty.Update(1.0/30,true);
        for (int frame=0;frame<120;++frame) oneTwenty.Update(1.0/120,true);
        const auto& a=thirty.Runtime()->World().Layout(); const auto& b=oneTwenty.Runtime()->World().Layout();
        Require(a.objects[2].position[0]==60 && b.objects[2].position[0]==60,"30 and 120 fps run the same fixed tick count");
        Require(std::abs(a.objects[1].position[1]-b.objects[1].position[1])<1e-5,"physics is independent of render frame partition");
        Require(a.objects[2].position[2]==a.objects[1].position[1],"late observes this frame's final physics position");
        Require(thirty.Pause(),"fixed session pauses"); const auto paused=a.Serialize(); thirty.Update(0.1,true);
        Require(thirty.Runtime()->World().Layout().Serialize()==paused,"pause freezes all phases");
        Require(thirty.Step() && thirty.Runtime()->World().Layout().objects[2].position[0]==61,"Step executes exactly one fixed tick");
        Require(thirty.Stop() && thirty.Play(renderer,root,scene,error),"fixed session restarts");
        thirty.Runtime()->SetInputActions({},{{"Fire",true}}); thirty.Update(1.0/120,true);
        Require(thirty.Runtime()->World().Layout().objects[2].position[0]==0,"short frame defers fixed tick");
        thirty.Runtime()->SetInputActions({},{}); thirty.Update(1.0/120,true);
        Require(thirty.Runtime()->World().Layout().objects[2].position[1]==1,"pressed input survives frame without a fixed tick");
        thirty.Runtime()->SetInputActions({},{{"Fire",true}}); thirty.Update(0.1,true);
        Require(thirty.Runtime()->World().Layout().objects[2].position[1]==2,"pressed input fires only once in a multi-tick frame");
        thirty.Runtime()->SetInputActions({},{{"Fire",true}}); thirty.Update(1.0/120,true);
        Require(thirty.Pause() && thirty.Step() && thirty.Runtime()->World().Layout().objects[2].position[1]==2,"Pause discards deferred pressed input before Step");
        Require(thirty.Play(renderer,root,scene,error),"paused fixed session resumes");
        thirty.Runtime()->SetInputActions({},{}); thirty.Update(1.0/120,true);
        thirty.Runtime()->SetInputActions({},{{"Fire",true}}); thirty.Update(1.0/120,true);
        const auto focusFires=thirty.Runtime()->World().Layout().objects[2].position[1];
        thirty.Update(1.0/120,false); thirty.Runtime()->SetInputActions({},{}); thirty.Update(1.0/120,true);
        Require(thirty.Runtime()->World().Layout().objects[2].position[1]==focusFires,"focus loss discards deferred pressed input");
        Require(oneTwenty.Stop() && oneTwenty.Play(renderer,root,scene,error),"rollback reference starts fresh");
        Require(thirty.Stop() && thirty.Play(renderer,root,scene,error),"rollback session starts fresh");
        thirty.Update(1.0/60,true); oneTwenty.Update(1.0/60,true);
        const auto before=thirty.Runtime()->World().Layout().Serialize();
        thirty.Runtime()->SetInputActions({},{{"Bad",true}});
        Require(!thirty.Runtime()->MovePlayers(0.1,0,0),"fixed model load failure rejected after physics ticks");
        Require(thirty.Runtime()->World().Layout().Serialize()==before,"fixed batch failure preserves all scene state");
        thirty.Runtime()->SetInputActions({},{}); thirty.Update(1.0/60,true); thirty.Update(1.0/60,true);
        oneTwenty.Update(1.0/60,true);
        Require(thirty.Runtime()->World().Layout().objects[2].position[0]==2,"fixed failure restores script state and accumulator");
        Require(std::abs(thirty.Runtime()->World().Layout().objects[1].position[1]-oneTwenty.Runtime()->World().Layout().objects[1].position[1])<1e-5,
            "fixed failure restores Jolt body velocity and position");
        const auto lateBefore=thirty.Runtime()->World().Layout().Serialize();
        thirty.Runtime()->SetInputActions({},{{"LateBad",true}});
        Require(!thirty.Runtime()->World().UpdateComponents(0.1) && thirty.Runtime()->World().Layout().Serialize()==lateBefore,"late resource failure rolls back normal and late together");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { thirty.Draw(commands,renderer.GetWidth(),renderer.GetHeight()); })!=Engine::RenderResult::Failed,"phase session renders");
        Require(renderer.WaitForIdle() && thirty.Stop() && oneTwenty.Stop() && scene.Serialize()==saved,"phase shutdown preserves edit scene");
    }
}
