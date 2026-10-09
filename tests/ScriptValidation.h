#pragma once
#include <SceneRuntime/ScriptRuntime.h>
#include <Engine/Core/Json.h>
#include <SceneRuntime/Prefab.h>
#include <limits>
#include <cmath>
#include <stdexcept>
#include "../Editor/src/GameSession.h"
#include "../Editor/src/EditHistory.h"
#include "ScriptPhaseValidation.h"

namespace ScriptValidation
{
    inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
    inline void TypedData()
    {
        using namespace SceneRuntime;
        ScriptDefinition definition;
        definition.dataFields={{"label",{std::string("default")}},{"active",{true}},
            {"target",{ScriptObjectReference{}}},{"points",{ScriptValue::Array{{2.0f},{3.0f}}}},
            {"settings",{ScriptValue::Object{{"speed",{4.0f}}}}}};
        definition.update=[](ScriptContext& c) {
            Require(c.Data("missing")==nullptr,"missing typed data is null");
            Require(std::get<std::string>(c.Data("label")->value)=="saved","saved string reaches script");
            Require(std::get<ScriptValue::Array>(c.Data("points")->value).size()==2,"array reaches script");
            Require(std::get<ScriptValue::Object>(c.Data("settings")->value).contains("speed"),"structured data reaches script");
            const auto count=c.dataState->find("count");
            const float previous=count==c.dataState->end() ? 0 : std::get<float>(count->second.value);
            (*c.dataState)["count"]={previous+1};
            c.object.position[0]=previous+1;
            if (const auto* target=c.Reference("target")) c.object.position[1]=target->position[1];
            if (c.Pressed("Fail")) (*c.dataState)["bad"]={std::numeric_limits<float>::quiet_NaN()};
        };
        Require(ScriptRegistry::Register("ValidationTypedData",definition),"typed data definition registers");
        ScenePlacement actor; actor.id="actor"; actor.name="Actor";
        ScriptComponent script; script.behaviour="ValidationTypedData"; script.data=definition.dataFields;
        script.data["label"]={std::string("saved")}; script.data["target"]={ScriptObjectReference{"target"}};
        actor.scripts={script};
        ScenePlacement target; target.id="target"; target.name="Target"; target.position[1]=7;
        SceneLayout layout; layout.objects={actor,target};
        layout=SceneLayout::Parse(layout.Serialize());
        Require(layout.objects[0].scripts[0].data==script.data,"typed values roundtrip with their types");
        ScriptRuntime runtime; std::string error;
        Require(runtime.Update(layout,0.1,error) && layout.objects[0].position[1]==7,"typed reference resolves live target");
        const auto saved=layout.Serialize();
        Require(!runtime.Update(layout,0.1,error,{},{{"Fail",true}}) && layout.Serialize()==saved,"invalid typed state rolls back frame");
        Require(runtime.Update(layout,0.1,error) && layout.objects[0].position[0]==2,"failed typed state was not retained");
        layout.objects[0].scripts[0].data["target"]={ScriptObjectReference{"deleted"}};
        Require(runtime.Update(layout,0.1,error),"missing reference is safely null");
        layout.objects[0].scripts[0].data["active"]={std::string("wrong type")};
        Require(!runtime.Update(layout,0.1,error),"wrong public data type is rejected");
        auto invalid=definition; invalid.dataFields["bad"]={std::numeric_limits<float>::infinity()};
        Require(!ScriptRegistry::Register("ValidationInvalidData",invalid),"invalid typed defaults are rejected");
        auto invalidLayout=SceneLayout::Parse(saved);
        invalidLayout.objects[0].scripts[0].data["bad"]={ScriptValue::Array(257)};
        bool rejected=false; try { static_cast<void>(invalidLayout.Serialize()); } catch (const std::exception&) { rejected=true; }
        Require(rejected,"oversized data array rejected before save");
        auto encoded=Engine::Json::parse(saved);
        encoded["objects"][0]["components"][0]["data"]={{"kind","unknown"},{"value",0}};
        rejected=false; try { static_cast<void>(SceneLayout::Parse(encoded.dump())); } catch (const std::exception&) { rejected=true; }
        Require(rejected,"unknown typed encoding rejected");
        SceneLayout clone; actor.scripts[0].data["nested"]={ScriptValue::Array{{ScriptValue::Object{{"ref",{ScriptObjectReference{"target"}}}}}}};
        target.parentId="actor"; clone.objects={actor,target}; size_t nextId=1;
        ScriptScene scene(clone,nextId); const auto copy=scene.Instantiate("actor",{0,0,0}); scene.Commit();
        const auto copied=std::find_if(clone.objects.begin(),clone.objects.end(),[&](const auto& item) { return item.id==copy; });
        const auto child=std::find_if(clone.objects.begin(),clone.objects.end(),[&](const auto& item) { return item.parentId==copy; });
        Require(copied!=clone.objects.end() && child!=clone.objects.end(),"typed hierarchy clones");
        Require(std::get<ScriptObjectReference>(copied->scripts[0].data.at("target").value).id==child->id,"cloned reference follows cloned target");
        const auto& nested=std::get<ScriptValue::Object>(std::get<ScriptValue::Array>(copied->scripts[0].data.at("nested").value)[0].value);
        Require(std::get<ScriptObjectReference>(nested.at("ref").value).id==child->id,"nested cloned references remap");
        SceneLayout prefabScene; SceneLayout asset; asset.objects={actor,target};
        const auto prefabRoot=Prefab::Instantiate(prefabScene,asset,"Assets/Prefabs/Typed.prefab",{0,0,0});
        const auto& prefabActor=prefabScene.objects[0];
        Require(prefabActor.id==prefabRoot && std::get<ScriptObjectReference>(prefabActor.scripts[0].data.at("target").value).id==prefabScene.objects[1].id,
            "prefab typed references follow prefab IDs");
        runtime.Stop(layout);
    }
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
        definition.dataFields={{"label",{std::string("runtime")}}};
        definition.update=[](ScriptContext& c) {
            Require(c.Data("label") && std::get<std::string>(c.Data("label")->value)=="runtime","typed data reaches Editor and App common runtime");
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
        ScenePlacement referenceTarget; referenceTarget.id="reference-target"; referenceTarget.name="Reference target"; referenceTarget.position={2,3,4};
        ScenePlacement follower; follower.id="follower"; follower.name="Follower";
        ScriptComponent follow; follow.behaviour="FollowTarget"; follow.data["target"]={ScriptObjectReference{"reference-target"}}; follower.scripts={follow};
        layout.objects.push_back(referenceTarget); layout.objects.push_back(follower);
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
        const auto following=std::find_if(current.objects.begin(),current.objects.end(),[](const auto& item) { return item.id=="follower"; });
        Require(following!=current.objects.end() && following->position==referenceTarget.position,"built-in typed behaviour reaches runtime transforms");
        const auto beforeClone=current.Serialize(); std::vector<std::string> created;
        Require(environment->World().DuplicateObjects({"reference-target","follower"},{4,0,0},created,error) && created.size()==2,"Editor duplicates typed reference owners");
        const auto& duplicated=environment->World().Layout();
        const auto copiedFollower=std::find_if(duplicated.objects.begin(),duplicated.objects.end(),[&](const auto& item) { return item.id==created[1]; });
        Require(copiedFollower!=duplicated.objects.end() && std::get<ScriptObjectReference>(copiedFollower->scripts[0].data.at("target").value).id==created[0],
            "Editor duplicate remaps typed reference to duplicated target");
        Editor::EditHistory history; history.Reset({beforeClone,"follower",{"follower"}});
        history.Observe({duplicated.Serialize(),created[1],created},{});
        Require(history.CanUndo() && environment->World().ReplaceLayout(SceneLayout::Parse(history.Target(false).json),root,error,true),"typed data and references restore through Editor Undo");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { session.Draw(commands,renderer.GetWidth(),renderer.GetHeight()); })!=Engine::RenderResult::Failed,"runtime spawned objects render");
        Require(renderer.WaitForIdle() && session.Stop(),"runtime resources stop safely");
        ScriptPhaseValidation::Runtime(renderer);
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
        TypedData();
        ScriptPhaseValidation::Schema();
    }
}
