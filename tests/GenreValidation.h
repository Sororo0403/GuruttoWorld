#pragma once
#include <SceneRuntime/Navigation.h>
#include <SceneRuntime/GenreGeometry.h>
#include <SceneRuntime/SceneEnvironment.h>
#include "../Editor/src/ComponentEditResources.h"
#include <Engine/Core/Json.h>
#include <cmath>
namespace GenreValidation {
/// <summary>ジャンル支援のテスト条件を確認します。</summary>
inline void Require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
/// <summary>地形とタイルの最小シーンを作ります。</summary>
inline SceneRuntime::SceneLayout Layout() {
    SceneRuntime::SceneLayout layout;SceneRuntime::ScenePlacement terrain;terrain.id="ground";terrain.terrain.emplace();terrain.terrain->columns=5;terrain.terrain->rows=5;terrain.terrain->heights.assign(25,0);terrain.navMesh.emplace();
    terrain.boxCollider.emplace();terrain.boxCollider->shape="terrain";
    SceneRuntime::ScenePlacement tiles;tiles.id="tiles";tiles.position={0,0,6};tiles.tilemap.emplace();tiles.tilemap->columns=2;tiles.tilemap->rows=2;tiles.tilemap->tiles={0,-1,1,0};tiles.tilemap->atlasColumns=2;
    layout.objects={terrain,tiles};return layout;
}
/// <summary>保存・メッシュ・アトラス・衝突形状・Undo資源判定を確認します。</summary>
inline void SchemaGeometry() {
    auto layout=Layout();std::string error;Require(SceneRuntime::Navigation::Bake(layout,layout.objects[0],error),"terrain navigation bake");
    const auto restored=SceneRuntime::SceneLayout::Parse(layout.Serialize());for(size_t i=0;i<layout.objects.size();++i) Require(layout.objects[i].SameComponents(restored.objects[i]),"genre component roundtrip");
    const auto terrain=SceneRuntime::GenreGeometry::Terrain(*layout.objects[0].terrain);Require(terrain.vertices.size()==25&&terrain.indices.size()==96,"terrain indexed heightfield geometry");
    Require(terrain.vertices[12].normal[1]==1,"flat heightfield upward normals");
    const auto tiles=SceneRuntime::GenreGeometry::Tilemap(*layout.objects[1].tilemap);Require(tiles.vertices.size()==12&&tiles.indices.size()==18,"empty tile skips quad");
    Require(tiles.vertices[4].uv[0]==.5f,"tile atlas selects correct cell");
    Require(SceneRuntime::GenreGeometry::TileCollision(*layout.objects[1].tilemap).indices.size()==3*36,"tile collision extrudes occupied cells");
    const auto before=layout.objects[0];layout.objects[0].terrain->heights[12]=2;
    Require(Editor::ComponentEditResources::Changed(before,layout.objects[0]),"terrain edits require GPU idle");
    const auto raised=SceneRuntime::GenreGeometry::Terrain(*layout.objects[0].terrain);Require(raised.vertices[12].position[1]==2,"height brush changes geometry");
    layout.objects[0].terrain->heights.pop_back();bool rejected=false;try {static_cast<void>(layout.Serialize());}catch(const std::exception&) {rejected=true;}Require(rejected,"malformed height grid rejected");
    layout=Layout();layout.objects[1].tilemap->tiles[0]=2;rejected=false;try {static_cast<void>(layout.Serialize());}catch(const std::exception&) {rejected=true;}Require(rejected,"invalid atlas tile rejected");
}
/// <summary>障害物・傾斜Bake、A*経路、親座標と固定時間Agentを確認します。</summary>
inline void NavigationPaths() {
    auto layout=Layout();SceneRuntime::ScenePlacement obstacle;obstacle.id="wall";obstacle.position={1.5f,.5f,1.5f};obstacle.boxCollider.emplace();obstacle.boxCollider->size={.8f,1,.8f};layout.objects.push_back(obstacle);
    std::string error;Require(SceneRuntime::Navigation::Bake(layout,layout.objects[0],error),"obstacle navmesh bake");
    Require(!layout.objects[0].navMesh->walkable[5],"obstacle cell blocks navigation");
    const auto path=SceneRuntime::Navigation::Path(layout,"ground",{.5f,0,1.5f},{3.5f,0,1.5f});Require(path.size()>4,"A star detours around obstacle");
    for(const auto& point:path) Require(!(point[0]>1&&point[0]<2&&point[2]>1&&point[2]<2),"path avoids blocked cell");
    SceneRuntime::ScenePlacement parent;parent.id="parent";parent.position={.5f,0,1.5f};layout.objects.push_back(parent);
    SceneRuntime::ScenePlacement agent;agent.id="agent";agent.parentId="parent";agent.navAgent.emplace();agent.navAgent->mesh="ground";agent.navAgent->destination={3.5f,0,1.5f};agent.navAgent->speed=2;layout.objects.push_back(agent);
    auto thirty=layout,oneTwenty=layout;for(int i=0;i<120;++i) Require(SceneRuntime::Navigation::Advance(thirty,1.0/30,error),"30 Hz navigation");for(int i=0;i<480;++i) Require(SceneRuntime::Navigation::Advance(oneTwenty,1.0/120,error),"120 Hz navigation");
    for(size_t axis=0;axis<3;++axis) Require(std::abs(thirty.objects.back().position[axis]-oneTwenty.objects.back().position[axis])<.02f,"navigation frame-rate equivalent parent-local movement");
    Require(std::abs(thirty.objects.back().position[0]-3)<.1f,"agent reaches world destination under parent");
    auto steep=Layout();for(unsigned int z=0;z<5;++z) for(unsigned int x=0;x<5;++x) steep.objects[0].terrain->heights[z*5+x]=static_cast<float>(x)*10;
    Require(SceneRuntime::Navigation::Bake(steep,steep.objects[0],error),"slope bake");Require(std::none_of(steep.objects[0].navMesh->walkable.begin(),steep.objects[0].navMesh->walkable.end(),[](bool v){return v;}),"steep surface excluded");
    std::map<std::string,std::string> remap{{"ground","copy-ground"}};agent.navAgent->Remap(remap);Require(agent.navAgent->mesh=="copy-ground","NavMesh reference remap");
}
/// <summary>実GPU描画、ピッキング、地形Jolt衝突、SceneEnvironmentのAgent更新を確認します。</summary>
inline void Runtime(Engine::DirectX12Renderer& renderer) {
    auto layout=Layout();std::string error;Require(SceneRuntime::Navigation::Bake(layout,layout.objects[0],error),"runtime bake");
    SceneRuntime::ScenePlacement agent;agent.id="agent";agent.position={.5f,0,.5f};agent.navAgent.emplace();agent.navAgent->mesh="ground";agent.navAgent->destination={3.5f,0,.5f};layout.objects.push_back(agent);
    SceneRuntime::SceneEnvironment scene;Require(scene.Initialize(renderer,"Content",layout,error),"procedural scene GPU resources");
    const auto pick=scene.World().PickRay({2,5,2},{0,-1,0});Require(pick=="ground","procedural terrain triangle picking");
    scene.Update(1.0/30,true,true);Require(scene.World().Layout().objects.back().position[0]>.5f,"SceneEnvironment advances NavAgent without player input");
    SceneRuntime::PhysicsWorld physics;SceneRuntime::ScenePhysics::States states;Require(physics.Advance(layout,states,1.0/60,0,0,false,"Content",error),"Jolt heightfield collider build");
    const auto hit=physics.Raycast({2,5,2},{0,-1,0},10);Require(hit&&hit->object=="ground"&&std::abs(hit->position[1])<.01f,"Jolt terrain raycast hits generated triangles");
    Engine::Camera camera;camera.SetPosition({2,4,-6});camera.SetRotation(0,-.35f);
    Require(renderer.Render({0,0,0,1},[&](auto* commands,float){scene.World().Draw(commands,camera,{});})!=Engine::RenderResult::Failed&&renderer.WaitForIdle(),"procedural GPU frame submits");
    Require(scene.World().Telemetry().draws>=2,"terrain and tilemap actual draw calls");
}
/// <summary>ジャンル支援回帰を実行します。</summary>
inline void Schema() {SchemaGeometry();NavigationPaths();}
}
