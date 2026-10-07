#pragma once
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Graphics/Models/ModelLoader.h>
#include <Engine/Animation/Skeleton.h>
#include "../Editor/src/AssetChanges.h"
#include <SceneRuntime/SceneLayout.h>
#include <SceneRuntime/MaterialAsset.h>
namespace AssetDatabaseValidation {
inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
inline void Run() {
    using namespace Engine; using namespace SceneRuntime;
    const auto root=std::filesystem::absolute("generated/tests/asset-ids/"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(root/"Assets/Models"); std::filesystem::create_directories(root/"Assets/Scenes");
    const std::filesystem::path source="Assets/Models/Original.obj",destination="Assets/Models/Moved/Renamed.obj";
    { std::ofstream output(root/"Assets/Models/Original.mtl"); output<<"newmtl test\nKd 1 1 1\n"; }
    { std::ofstream output(root/source); output<<"mtllib Original.mtl\nusemtl test\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0.25 0.25\nf 1/1 2/1 3/1\n"; }
    auto metadata=AssetDatabase::Ensure(root/source); Require(AssetDatabase::Ensure(root/source).id==metadata.id,"stable metadata ID");
    SceneLayout scene; ScenePlacement actor; actor.id="actor"; actor.name="Actor"; actor.SetModel(source); scene.objects={actor};
    scene.Save(root/"Assets/Scenes/Test.json");
    auto saved=SceneLayout::Load(root/"Assets/Scenes/Test.json",root); Require(saved.assetReferences.at(AssetDatabase::Text(source))==metadata.id,"scene captures referenced ID");
    AssetDatabase(root).Move(source,destination);
    Require(AssetDatabase::Read(root/destination).id==metadata.id && !std::filesystem::exists(root/source),"rename preserves sidecar and ID");
    Require(AssetDatabase(root).Resolve(source)==destination && AssetDatabase(root).Resolve(source,metadata.id)==destination,"old paths and IDs resolve moved asset");
    saved=SceneLayout::Load(root/"Assets/Scenes/Test.json",root); Require(saved.objects[0].Model()==destination,"saved scene follows model rename");
    scene.ResolveAssets(root); Require(scene.objects[0].Model()==destination,"legacy in-memory layout follows rename");
    std::filesystem::create_directories(root/"Shaders");
    Editor::AssetChanges watcher; watcher.Observe(Editor::AssetChanges::Capture(root));
    metadata=AssetDatabase::Read(root/destination); metadata.scale=2; metadata.flipV=true; AssetDatabase::Write(root/destination,metadata);
    watcher.Observe(Editor::AssetChanges::Capture(root));
    Require(watcher.Pending(),"saved import metadata requests reload");
    watcher.Observe(Editor::AssetChanges::Capture(root)); Require(watcher.TakeReady(true),"stable import metadata reloads once");
    const auto skeletal=root/"Assets/Models/Skeleton.gltf";
    std::filesystem::copy_file("Content/Assets/Models/AnimatedBox.gltf",skeletal);
    auto skeletalSettings=AssetDatabase::Ensure(skeletal); skeletalSettings.scale=2; AssetDatabase::Write(skeletal,skeletalSettings);
    std::string error; const auto rig=Skeleton::Load(skeletal,error); Require(rig!=nullptr,"skeletal import settings load");
    const auto skinned=Skeleton::Skin(*rig,rig->meshes[0],Skeleton::Matrices(*rig,Skeleton::Sample(*rig,"",0,false)));
    float height=0; for (const auto& vertex : skinned.vertices) height=std::max(height,vertex.position[1]);
    Require(std::abs(height-4)<0.001f,"skeletal import scale applies after skinning");
    std::vector<MeshData> meshes; Require(ModelLoader::Load(root/destination,meshes),"model reimports with metadata");
    float maximum=0; for (const auto& vertex : meshes[0].vertices) maximum=std::max(maximum,vertex.position[0]);
    Require(maximum==2 && std::abs(meshes[0].vertices[0].uv[1]-0.25f)<0.001f,"import scale and UV flip apply");
    bool rejected=false; try { AssetDatabase(root).Move(destination,"Assets/Models/../../Escape.obj"); } catch (...) { rejected=true; }
    Require(rejected && std::filesystem::exists(root/destination),"traversal refused without moving source");
    { std::ofstream output(root/"Assets/Models/Exists.obj"); output<<"existing"; }
    rejected=false; try { AssetDatabase(root).Move(destination,"Assets/Models/Exists.obj"); } catch (...) { rejected=true; }
    Require(rejected,"rename cannot overwrite existing asset");
    metadata.scale=0; rejected=false; try { AssetDatabase::Write(root/destination,metadata); } catch (...) { rejected=true; }
    Require(rejected && AssetDatabase::Read(root/destination).scale==2,"invalid settings preserve metadata");
    std::filesystem::remove(AssetDatabase::Sidecar(root/destination)); rejected=false;
    try { SceneLayout::Load(root/"Assets/Scenes/Test.json",root); } catch (...) { rejected=true; }
    Require(rejected,"missing stable ID reported instead of substituting asset");
}
}
