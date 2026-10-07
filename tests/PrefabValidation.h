#pragma once
#include <SceneRuntime/Prefab.h>
#include "../Editor/src/ProjectCatalog.h"
#include <stdexcept>
#include "../Editor/src/GameSession.h"

namespace PrefabValidation
{
    inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
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
        asset.objects[0].name="Updated";
        asset.objects[0].scripts[0].parameters["speed"]=60;
        asset.objects[1].position={2,0,0};
        ScenePlacement added; added.id="new"; added.name="Added"; added.parentId="root";
        asset.objects.push_back(added); asset.Save(root/path);
        Prefab::Refresh(scene,root);
        Require(scene.objects.size()==6 && scene.objects[0].name=="Updated","prefab updates names and adds source children");
        Require(scene.objects[0].position==std::array<float,3>{2,0,0} && scene.objects[2].position==std::array<float,3>{6,0,0},"prefab updates preserve instance placement");
        Require(scene.objects[1].position==std::array<float,3>{3,0,0} && scene.objects[3].position==std::array<float,3>{2,0,0},"prefab updates preserve local transform overrides");
        Require(scene.objects[0].scripts[0].parameters.at("speed")==60 && scene.objects[2].scripts[0].parameters.at("speed")==45,"prefab updates preserve component overrides");
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
    }
}
