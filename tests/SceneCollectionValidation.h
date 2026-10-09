#pragma once
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <SceneRuntime/SceneEnvironment.h>
#include "../Editor/src/EditHistory.h"
#include <stdexcept>

namespace SceneCollectionValidation
{
    inline void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    inline SceneRuntime::SceneLayout Scene()
    {
        SceneRuntime::SceneLayout layout;
        SceneRuntime::ScenePlacement parent; parent.id="parent"; parent.name="Parent"; parent.position={4,0,0};
        SceneRuntime::ScenePlacement child; child.id="child"; child.name="Child"; child.parentId=parent.id; child.position={2,0,0};
        child.scripts.push_back({}); child.scripts.back().behaviour="FollowTarget"; child.scripts.back().data["target"]=SceneRuntime::ScriptValue{SceneRuntime::ScriptObjectReference{parent.id}};
        layout.objects={parent,child}; return layout;
    }
    inline void Schema()
    {
        using namespace SceneRuntime; auto current=Scene(); SceneCollection collection; collection.Reset(current,"A");
        auto merged=collection.Add(current,Scene(),"B");
        Require(merged.objects.size()==4 && merged.objects[2].id=="parent-scene-1" && merged.objects[3].parentId==merged.objects[2].id,"additive IDs and parent references remap");
        const auto& reference=std::get<ScriptObjectReference>(merged.objects[3].scripts[0].data.at("target").value);
        Require(reference.id==merged.objects[2].id,"additive script references remap");
        auto cameras=current;cameras.objects[0].camera.emplace();cameras.settings.mainCamera="parent";
        SceneCollection cameraCollection;cameraCollection.Reset(cameras,"Old camera");
        const auto switched=cameraCollection.Add(cameras,cameras,"New camera",true);
        Require(switched.settings.mainCamera=="parent-scene-1","active camera follows exact additive ID remapping");
        const auto json=merged.Serialize(); auto saved=SceneLayout::Parse(json); SceneCollection restored; restored.Reset(saved);
        Require(restored.Entries().size()==2,"scene ownership round trips");
        Editor::EditHistory history; history.Reset({current.Serialize(),{}}); history.Observe({json,{}},{});
        Require(history.CanUndo() && SceneLayout::Parse(history.Target(false).json).objects.size()==2,"additive edit is undoable");
        merged.objects[1].persistent=true;
        auto unloaded=collection.Unload(merged,"A");
        Require(unloaded.objects.size()==3 && unloaded.objects[0].id=="child" && unloaded.objects[0].parentId.empty() && unloaded.objects[0].position[0]==6,"persistent child detaches with world position");
        Require(SceneLayout::Parse(unloaded.Serialize()).objects[0].persistent,"persistent setting is saved");
        ScenePlacement spawned; spawned.id="runtime-spawn"; spawned.name="Runtime"; unloaded.objects.push_back(spawned);
        auto empty=collection.Unload(unloaded,"B"); Require(empty.objects.size()==1 && empty.objects[0].id=="child","dynamic objects unload with active scene while persistent objects remain");
        bool rejected=false; try {collection.Unload(empty,"missing");} catch(const std::exception&) {rejected=true;}
        Require(rejected,"unknown scene cannot unload");
        SceneCollection generated;generated.Reset(current,"Owner");auto owned=generated.Add(current,Scene(),"Active");
        size_t nextId=1;ScriptScene api(owned,nextId);api.currentOwner="parent";
        const auto spawnedId=api.Spawn({});api.Commit();
        const auto withoutOwner=generated.Unload(owned,"Owner");
        Require(std::none_of(withoutOwner.objects.begin(),withoutOwner.objects.end(),[&](const auto& object){return object.id==spawnedId;}),"spawned roots unload with their script owner scene");
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime; const auto root=std::filesystem::absolute("generated/tests/scene-collection");
        std::filesystem::create_directories(root/"Assets/Scenes"); std::filesystem::create_directories(root/"Shaders");
        for(const auto& entry:std::filesystem::recursive_directory_iterator("Content/Shaders"))
            if(entry.is_regular_file()) {
                const auto target=root/"Shaders"/entry.path().lexically_relative("Content/Shaders");
                std::filesystem::create_directories(target.parent_path());std::filesystem::copy_file(entry.path(),target,std::filesystem::copy_options::overwrite_existing);
            }
        auto stopped=std::make_shared<int>(0);
        ScriptDefinition definition; definition.update=[](ScriptContext& c){c.object.position[2]=++c.state["ticks"];};
        definition.stop=[stopped](ScriptContext&){++*stopped;};
        Require(ScriptRegistry::Register("ValidationPersistentScene",definition),"persistent runtime definition");
        auto initial=Scene(); initial.objects[1].scripts.clear(); initial.objects[0].persistent=true;
        initial.objects[0].button.emplace();initial.objects[0].button->action="loadSceneAdditive";initial.objects[0].button->target="Assets/Scenes/B.json";
        ScriptComponent script; script.behaviour="ValidationPersistentScene"; initial.objects[0].scripts.push_back(script);
        initial.Save(root/"Assets/Scenes/A.json"); auto other=Scene(); other.objects[1].scripts.clear();other.objects[0].scripts.push_back(script); other.Save(root/"Assets/Scenes/B.json");
        auto bad=other; bad.objects[0].SetModel("Assets/Models/missing.obj"); bad.Save(root/"Assets/Scenes/Bad.json");
        SceneEnvironment environment; std::string error;
        Require(environment.Initialize(renderer,root,root/"Assets/Scenes/A.json",error),"initial collection prepares");
        environment.Update(1.0/60,true,true);
        bool deferred=false;
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float){
            environment.Draw(commands,renderer.GetWidth(),renderer.GetHeight());environment.Click("parent");deferred=environment.World().Layout().objects.size()==2;
        })!=Engine::RenderResult::Failed && deferred,"UI scene click does not replace resources while recording GPU commands");
        environment.FlushSceneChanges();Require(environment.World().Layout().objects.size()==4,"queued runtime additive scene prepares before the next draw");
        const auto snapshot=environment.World().Layout().Serialize();
        Require(!environment.LoadScene("Assets/Scenes/Bad.json",true,error) && environment.World().Layout().Serialize()==snapshot && environment.Scenes().Entries().size()==2,"failed additive resources preserve world and ownership");
        Require(environment.UnloadScene("Assets/Scenes/A.json",error),"initial scene unloads");
        Require(environment.World().Layout().objects.size()==4,"persistent root preserves its descendants");
        environment.Update(1.0/60,true,true);
        Require(environment.World().Layout().objects[0].position[2]==2,"persistent script state survives additive unload");
        Require(environment.LoadScene("Assets/Scenes/B.json",false,error),"single scene switch keeps persistent subtree");
        environment.Update(1.0/60,true,true);
        Require(environment.World().Layout().objects[0].position[2]==3,"persistent script state survives single switch");
        Require(environment.World().Layout().objects[2].position[2]==1 && *stopped==1,"same-ID single reload creates a new Script instance and stops the previous nonpersistent instance exactly once");
        Require(!environment.LoadScene("../outside.json",true,error),"scene path cannot escape content");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float){environment.Draw(commands,renderer.GetWidth(),renderer.GetHeight());})!=Engine::RenderResult::Failed,"combined scene renders");
        Require(renderer.WaitForIdle(),"scene collection GPU completes");
    }
}
