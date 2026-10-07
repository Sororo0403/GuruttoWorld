#pragma once
#include <SceneRuntime/ScriptRuntime.h>
#include <Engine/Core/Json.h>
#include <cmath>
#include <stdexcept>
#include "../Editor/src/GameSession.h"

namespace ScriptValidation
{
    inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
    inline void SceneApi()
    {
        using namespace SceneRuntime;
        ScriptDefinition producer;
        producer.start=[](ScriptContext& c) {
            Require(c.scene && c.scene->Find("template") && c.scene->FindByName("Template").size()==1,"script scene lookup");
            Require(c.scene->GetComponent("template",&ScenePlacement::boxCollider)!=nullptr,"typed component lookup through scene object");
            c.scene->Instantiate("template",{5,0,0});
            c.scene->Emit({"damage",c.object.id,"receiver",3});
        };
        producer.update=[](ScriptContext& c) { c.state["ticks"]+=1; c.object.position[0]=c.state["ticks"]; };
        ScriptDefinition consumer;
        consumer.update=[](ScriptContext&) {};
        consumer.onEvent=[](ScriptContext& c) {
            c.object.position[1]+=c.event->value;
            c.scene->Destroy(c.event->sender);
        };
        Require(ScriptRegistry::Register("ValidationProducer",producer) && ScriptRegistry::Register("ValidationConsumer",consumer),"scene API scripts register");
        ScenePlacement source; source.id="template"; source.name="Template"; source.boxCollider.emplace();
        ScenePlacement child; child.id="child"; child.name="Child"; child.parentId=source.id;
        ScenePlacement emitter; emitter.id="emitter"; emitter.name="Emitter"; emitter.scripts.push_back({"script",true,"ValidationProducer",{}});
        ScenePlacement receiver; receiver.id="receiver"; receiver.name="Receiver"; receiver.scripts.push_back({"script",true,"ValidationConsumer",{}});
        SceneLayout layout; layout.objects={child,source,emitter,receiver};
        ScriptRuntime runtime; std::string error;
        Require(runtime.Update(layout,0.1,error) && layout.objects.size()==6,"deferred hierarchy instantiation");
        Require(layout.objects[4].parentId==layout.objects[5].id && layout.objects[5].position[0]==5,"forward parent references remapped");
        Require(layout.objects[3].position[1]==0,"events delivered on following frame");
        Require(runtime.Update(layout,0.1,error) && layout.objects.size()==5 && layout.objects[2].position[1]==3,"targeted event and deferred destruction");
        ScriptDefinition broken;
        broken.update=[](ScriptContext& c) { c.state["attempt"]+=1; c.object.position[0]=9; ScenePlacement bad; bad.name="Bad"; bad.scale={0,1,1}; c.scene->Spawn(bad); };
        Require(ScriptRegistry::Register("ValidationBrokenSpawn",broken),"broken spawn script registers");
        layout.objects[2].scripts[0].behaviour="ValidationBrokenSpawn";
        const auto saved=layout.Serialize();
        Require(!runtime.Update(layout,0.1,error) && layout.Serialize()==saved,"invalid spawn preserves complete scene");
        layout.objects[2].scripts[0].behaviour="ValidationConsumer";
        runtime.QueueEvent({"damage","template","receiver",2});
        Require(runtime.Update(layout,0.1,error) && layout.objects.size()==3,"deletion removes subtree and failure preserves event queue");
        runtime.Stop(layout);
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime;
        ScriptDefinition definition;
        definition.update=[](ScriptContext& c) {
            c.state["ticks"]+=1; c.object.position[0]=c.state["ticks"];
            if (c.Pressed("Fire")) c.scene->Instantiate("cube",{0,0,0});
            if (c.Pressed("Remove")) c.scene->Destroy("cube");
            if (c.Pressed("Bad")) { ScenePlacement bad; bad.name="Missing"; bad.SetModel("Assets/Models/missing.obj"); c.scene->Spawn(bad); }
            if (c.Pressed("Ui")) { ScenePlacement ui; ui.name="Runtime image"; ui.rectTransform.emplace(); ui.image.emplace(); c.scene->Spawn(ui); }
        };
        static bool registered=false;
        if (!registered) { Require(ScriptRegistry::Register("ValidationRuntimeSpawner",definition),"runtime spawner registers"); registered=true; }
        const auto root=std::filesystem::absolute("Content");
        auto layout=SceneLayout::Load(root/"Assets/Scenes/ScriptPlayground.json",root);
        ScenePlacement cube; cube.id="cube"; cube.name="Cube"; cube.SetModel("Assets/Models/Cube.obj");
        ScenePlacement spawner; spawner.id="spawner"; spawner.name="Spawner"; spawner.scripts.push_back({"script",true,"ValidationRuntimeSpawner",{}});
        layout.objects.push_back(cube); layout.objects.push_back(spawner);
        Editor::GameSession session; std::string error;
        if (!session.Play(renderer,root,layout,error)) throw std::runtime_error("dynamic runtime starts: "+error);
        auto* environment=session.Runtime();
        const auto objects=layout.objects.size();
        environment->SetInputActions({},{{"Fire",true}}); session.Update(0.1,true);
        Require(environment->World().Layout().objects.size()==objects+1,"spawn reaches live render world");
        environment->SetInputActions({},{{"Bad",true}}); session.Update(0.1,true);
        Require(environment->World().Layout().objects.size()==objects+1,"failed model load preserves live topology");
        environment->SetInputActions({},{{"Remove",true}}); session.Update(0.1,true);
        Require(environment->World().Layout().objects.size()==objects,"delete reaches live render world");
        environment->SetInputActions({},{{"Ui",true}}); session.Update(0.1,true);
        Require(environment->World().Layout().objects.back().image.has_value(),"runtime UI resources prepared");
        const auto& current=environment->World().Layout();
        const auto found=std::find_if(current.objects.begin(),current.objects.end(),[](const auto& item) { return item.id=="spawner"; });
        Require(found!=current.objects.end() && found->position[0]==3,"structural changes preserve script state and failed frame rolls it back");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { session.Draw(commands,renderer.GetWidth(),renderer.GetHeight()); })!=Engine::RenderResult::Failed,"runtime spawned objects render");
        Require(renderer.WaitForIdle() && session.Stop(),"runtime resources stop safely");
    }
    inline void Run()
    {
        using namespace SceneRuntime;
        static int started=0,updated=0,stopped=0;
        started=updated=stopped=0;
        ScriptDefinition definition; definition.fields={{"speed",{2,0,10}}};
        definition.start=[](ScriptContext& context) { ++started; context.state["frames"]=0; };
        definition.update=[](ScriptContext& context) { ++updated; context.state["frames"]+=1; context.object.position[0]+=context.Value("speed",2)*static_cast<float>(context.seconds); };
        definition.stop=[](ScriptContext&) { ++stopped; };
        Require(ScriptRegistry::Register("ValidationMover",definition),"custom script registration");
        Require(!ScriptRegistry::Register("ValidationMover",definition),"duplicate script registration rejected");
        auto invalid=definition; invalid.fields["speed"].initial=20;
        Require(!ScriptRegistry::Register("InvalidMover",invalid),"invalid script metadata rejected");
        ScenePlacement object; object.id="actor"; object.name="Actor";
        object.scripts.push_back({"script",true,"ValidationMover",{{"speed",4.0f}}});
        object.scripts.push_back({"spin",true,"Spin",{{"speed",90.0f}}});
        SceneLayout layout; layout.objects={object};
        const auto restored=SceneLayout::Parse(layout.Serialize());
        Require(restored.objects.back().scripts==object.scripts,"multiple scripts roundtrip");
        Require(object.HasComponentId("script") && object.HasComponentId("spin"),"script IDs participate in uniqueness");
        auto bad=layout; bad.objects[0].scripts[1].id="script";
        bool rejected=false; try { bad.Serialize(); } catch (const std::exception&) { rejected=true; }
        Require(rejected,"duplicate script IDs rejected");
        ScriptRuntime runtime; std::string error;
        Require(runtime.Update(layout,0.1,error) && runtime.Update(layout,0.1,error),"custom scripts update");
        Require(started==1 && updated==2 && std::abs(layout.objects[0].position[0]-0.8f)<0.0001f,"script starts once and uses saved parameters");
        Require(layout.objects[0].rotation[1]>0,"second script runs independently");
        layout.objects[0].scripts[0].enabled=false;
        Require(runtime.Update(layout,0.1,error) && stopped==1,"disable calls script stop once");
        layout.objects[0].scripts[0].enabled=true;
        Require(runtime.Update(layout,0.1,error) && started==2,"reenable starts new script instance");
        runtime.Stop(layout); runtime.Stop(layout);
        Require(stopped==2,"shutdown stops each instance once");
        auto unknown=layout; unknown.objects[0].scripts[0].behaviour="Missing";
        const auto unknownSaved=SceneLayout::Parse(unknown.Serialize());
        Require(unknownSaved.objects[0].scripts[0].behaviour=="Missing","unknown script preserved for later registration");
        Require(!runtime.Update(unknown,0.1,error) && !error.empty(),"unknown runtime script reports error");
        SceneApi();
    }
}
