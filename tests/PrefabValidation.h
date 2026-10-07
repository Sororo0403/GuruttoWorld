#pragma once
#include <SceneRuntime/Prefab.h>
#include "../Editor/src/ProjectCatalog.h"
#include <stdexcept>
#include "../Editor/src/GameSession.h"

namespace PrefabValidation
{
    inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
    inline void Advanced()
    {
        using namespace SceneRuntime;
        const auto root=std::filesystem::absolute("generated/tests/prefabs-advanced");
        std::filesystem::create_directories(root/"Assets/Prefabs");
        ScenePlacement parent; parent.id="base"; parent.name="Base"; parent.boxCollider.emplace();
        ScenePlacement child; child.id="child"; child.name="Child"; child.parentId="base"; child.boxCollider.emplace();
        SceneLayout base; base.objects={parent,child};
        const std::filesystem::path path="Assets/Prefabs/Base.prefab";
        base.Save(root/path);
        SceneLayout scene;
        const auto id=Prefab::Instantiate(scene,base,path,{4,0,0});
        const auto other=Prefab::Instantiate(scene,base,path,{8,0,0});
        scene.objects[0].boxCollider->center={0,2,0}; scene.objects[0].name="Local";
        const auto changes=Prefab::Overrides(scene,id);
        Require(changes.size()==2,"prefab property overrides enumerate separately from placement");
        Prefab::RevertOverride(scene,id,"/name");
        Require(scene.objects[0].name=="Base" && scene.objects[0].boxCollider->center[1]==2,"revert restores only selected property");
        Prefab::ApplyOverride(scene,id,"/components/collider/center",root); Prefab::Refresh(scene,root);
        Require(scene.objects[2].boxCollider->center[1]==2 && Prefab::Overrides(scene,id).empty(),"apply updates source and other instances, clears applied override");
        scene.objects[0].name="Variant"; scene.objects[1].boxCollider->center={0,3,0};
        auto variant=Prefab::Variant(scene,id);
        const std::filesystem::path variantPath="Assets/Prefabs/Variant.prefab";
        variant.Save(root/variantPath);
        base=SceneLayout::Load(root/path); base.objects[1].boxCollider->size={2,4,6}; base.Save(root/path);
        SceneLayout variants;
        Prefab::Refresh(variant,root);
        Prefab::Instantiate(variants,variant,variantPath,{0,0,0}); Prefab::Refresh(variants,root);
        Require(variants.objects[0].name=="Variant" && variants.objects[1].boxCollider->center[1]==3 && variants.objects[1].boxCollider->size[1]==4,"variant inherits base changes and preserves its own overrides");
        Prefab::RevertInstance(scene,id,root);
        Require(scene.objects[0].name=="Base" && scene.objects[0].position[0]==4 && scene.objects[1].boxCollider->center[1]==0,"whole instance revert keeps placement");
        SceneLayout nested;
        const auto inner=Prefab::Instantiate(nested,base,path,{1,0,0});
        ScenePlacement outer; outer.id="outer"; outer.name="Outer";
        for (auto& item : nested.objects) if (item.id==inner) item.parentId=outer.id;
        nested.objects.insert(nested.objects.begin(),outer);
        const auto outerAsset=Prefab::Extract(nested,outer.id);
        Require(outerAsset.objects[1].prefab.has_value(),"extract preserves nested prefab link");
        const std::filesystem::path outerPath="Assets/Prefabs/Outer.prefab";
        outerAsset.Save(root/outerPath);
        SceneLayout instances;
        const auto outerId=Prefab::Instantiate(instances,outerAsset,outerPath,{0,0,0});
        base.objects[1].boxCollider->size={3,5,7}; base.Save(root/path); Prefab::Refresh(instances,root);
        Require(instances.objects[2].boxCollider->size[1]==5,"nested base update propagates through outer prefab");
        Require(Prefab::Extract(instances,outerId).objects[1].prefab.has_value(),"reapplying outer prefab retains nested source link");
        SceneLayout a,b; ScenePlacement av,bv; av.id="a"; av.name="A"; bv.id="b"; bv.name="B";
        a.objects={av}; b.objects={bv};
        const auto aBaseline=a.Serialize(),bBaseline=b.Serialize();
        a.objects[0].prefab=PrefabLink{"Assets/Prefabs/B.prefab","b","a",bBaseline};
        b.objects[0].prefab=PrefabLink{"Assets/Prefabs/A.prefab","a","b",aBaseline};
        a.Save(root/"Assets/Prefabs/A.prefab"); b.Save(root/"Assets/Prefabs/B.prefab");
        const auto saved=a.Serialize(); bool rejected=false;
        try { Prefab::Refresh(a,root); } catch (const std::exception&) { rejected=true; }
        Require(rejected && a.Serialize()==saved,"cyclic prefab dependency rejected atomically");
        Prefab::Refresh(instances,root);
        Require(instances.objects.size()==3,"cycle detection scope cleans up after failure");
        static_cast<void>(other);
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        const auto scene=SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/PrefabPlayground.json");
        const auto saved=scene.Serialize();
        Editor::GameSession session; std::string error;
        Require(session.Play(renderer,root,scene,error),"prefab playground initializes");
        Require(session.Update(1.0/60,true),"prefab scripts animate during Play");
        Require(renderer.Render({0.025f,0.035f,0.055f,1},[&](auto* commands,float) {
            session.Draw(commands,renderer.GetWidth(),renderer.GetHeight());
        })!=Engine::RenderResult::Failed,"prefab playground renders");
        Require(renderer.WaitForIdle(),"prefab playground GPU completes");
        Require(session.Stop() && scene.Serialize()==saved,"prefab playback preserves authored scene");
    }
    inline void Run()
    {
        using namespace SceneRuntime;
        const auto root=std::filesystem::absolute("generated/tests/prefabs");
        std::filesystem::create_directories(root/"Assets/Prefabs");
        ScenePlacement parent; parent.id="root"; parent.name="Original";
        parent.scripts.push_back({"spin",true,"Spin",{{"speed",30.0f}}});
        ScenePlacement child; child.id="child"; child.name="Child"; child.parentId="root"; child.position={1,0,0}; child.boxCollider.emplace();
        SceneLayout asset; asset.objects={parent,child};
        const std::filesystem::path path="Assets/Prefabs/Test.prefab";
        asset.Save(root/path);
        SceneLayout scene;
        const auto first=Prefab::Instantiate(scene,asset,path,{2,0,0});
        const auto second=Prefab::Instantiate(scene,asset,path,{6,0,0});
        Require(scene.objects.size()==4 && first!=second && scene.objects[1].parentId==first,"prefab hierarchy and unique instance IDs");
        Require(SceneLayout::Parse(scene.Serialize()).objects[0].prefab==scene.objects[0].prefab,"prefab link roundtrip");
        scene.objects[1].position={3,0,0};
        scene.objects[2].scripts[0].parameters["speed"]=45;
        scene.objects[3].boxCollider->center={0,1,0};
        asset.objects[0].name="Updated";
        asset.objects[0].scripts[0].parameters["speed"]=60;
        asset.objects[1].position={2,0,0};
        asset.objects[1].boxCollider->size={2,3,4};
        ScenePlacement added; added.id="new"; added.name="Added"; added.parentId="root";
        asset.objects.push_back(added); asset.Save(root/path);
        Prefab::Refresh(scene,root);
        Require(scene.objects.size()==6 && scene.objects[0].name=="Updated","prefab updates names and adds source children");
        Require(scene.objects[0].position==std::array<float,3>{2,0,0} && scene.objects[2].position==std::array<float,3>{6,0,0},"prefab updates preserve instance placement");
        Require(scene.objects[1].position==std::array<float,3>{3,0,0} && scene.objects[3].position==std::array<float,3>{2,0,0},"prefab updates preserve local transform overrides");
        Require(scene.objects[0].scripts[0].parameters.at("speed")==60 && scene.objects[2].scripts[0].parameters.at("speed")==45,"prefab updates preserve component overrides");
        Require(scene.objects[3].boxCollider->center==std::array<float,3>{0,1,0} && scene.objects[3].boxCollider->size==std::array<float,3>{2,3,4},"prefab property override keeps center while inheriting size in the same component");
        const auto saved=scene.Serialize();
        std::filesystem::rename(root/path,root/"Assets/Prefabs/Test.temp");
        bool failed=false; try { Prefab::Refresh(scene,root); } catch (const std::exception&) { failed=true; }
        std::filesystem::rename(root/"Assets/Prefabs/Test.temp",root/path);
        Require(failed && scene.Serialize()==saved,"missing prefab preserves entire scene");
        Prefab::Unpack(scene,first);
        Require(!scene.objects[0].prefab && !scene.objects[1].prefab && scene.objects[2].prefab,"unpack only detaches chosen instance");
        const auto extracted=Prefab::Extract(scene,second);
        Require(extracted.objects.size()==3 && extracted.objects[0].position==std::array<float,3>{0,0,0} && !extracted.objects[0].prefab,"prefab extraction flattens subtree and root position");
        Require(Editor::ProjectCatalog::Kind(path)==Editor::AssetKind::Prefab,"Project classifies prefab assets");
        Require(!Prefab::ValidPath("Assets/Prefabs/../Escape.prefab"),"prefab paths reject traversal");
        Advanced();
    }
}
