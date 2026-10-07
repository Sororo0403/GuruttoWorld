#pragma once
#include <SceneRuntime/MaterialAsset.h>
#include "EnvironmentValidation.h"
#include "../Editor/src/ProjectCatalog.h"
#include <limits>

namespace MaterialValidation
{
    using EnvironmentValidation::Require;
    inline void Schema()
    {
        const auto root=std::filesystem::absolute("generated/tests/materials");
        const std::filesystem::path path="Assets/Materials/Test.mat";
        SceneRuntime::MaterialAsset asset; asset.values.color={0.2f,0.4f,0.6f,0.5f}; asset.values.transparent=true;
        asset.values.roughness=0.3f; asset.values.metallic=0.7f; asset.values.uv.scale={2,3}; asset.values.uv.rotation=0.5f;
        asset.Save(root,path); const auto restored=SceneRuntime::MaterialAsset::Load(root,path);
        Require(restored.values.color==asset.values.color && restored.values.roughness==0.3f && restored.values.transparent && restored.values.uv.scale==asset.values.uv.scale,"material properties roundtrip");
        const auto saved=restored.values.color;
        asset.values.roughness=std::numeric_limits<float>::quiet_NaN();
        bool rejected=false; try { asset.Save(root,path); } catch (const std::exception&) { rejected=true; }
        Require(rejected && SceneRuntime::MaterialAsset::Load(root,path).values.color==saved,"invalid material save preserves prior asset");
        Require(!SceneRuntime::MaterialAsset::ValidPath("Assets/Materials/../Bad.mat"),"material path rejects traversal");
        Require(Editor::ProjectCatalog::Kind(path)==Editor::AssetKind::Material,"Project classifies materials");
        SceneRuntime::ScenePlacement object; object.id="mesh"; object.SetModel("Assets/Models/Cube.obj"); object.meshRenderer->material=path;
        SceneRuntime::SceneLayout scene; scene.objects={object};
        Require(SceneRuntime::SceneLayout::Parse(scene.Serialize()).objects[0].meshRenderer->material==path,"Mesh material reference roundtrip");
    }
    inline void Rendering(Engine::DirectX12Renderer& renderer)
    {
        Engine::MeshData data;
        data.vertices={{{-1,-1,0},{0,0,-1},{0,0}},{{0,1,0},{0,0,-1},{0,0}},{{1,-1,0},{0,0,-1},{0,0}}}; data.indices={0,1,2};
        Engine::MeshRenderer mesh;
        Require(mesh.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),data,std::filesystem::absolute("Content/Shaders/Mesh.hlsl")),"material mesh fixture initializes");
        Engine::DirectionalLight light; light.enabled=false;
        DirectX::XMFLOAT4X4 identity; DirectX::XMStoreFloat4x4(&identity,DirectX::XMMatrixIdentity());
        Engine::Material material; material.color={1,0,0,1};
        const auto sample=[&] { return EnvironmentValidation::Pixel(renderer,[&](auto* commands) { mesh.Draw(commands,identity,identity,light,{0,0,-3.5f},material.uv,&material); }); };
        Require(sample()==std::array<unsigned char,4>{255,0,0,255},"material tint reaches GPU without affecting shared geometry");
        material.color[3]=0.5f; material.transparent=true;
        const auto blended=sample();
        Require(blended[0]>=126 && blended[0]<=129 && blended[1]==0 && blended[2]==0,"material opacity blends against background");
        const auto root=std::filesystem::absolute("generated/tests/materials"); std::filesystem::create_directories(root/"Assets/Textures");
        std::array<unsigned char,58> bmp{}; bmp[0]='B'; bmp[1]='M'; bmp[2]=58; bmp[10]=54; bmp[14]=40; bmp[18]=1; bmp[22]=1; bmp[26]=1; bmp[28]=24; bmp[55]=255;
        { std::ofstream image(root/"Assets/Textures/green.bmp",std::ios::binary); image.write(reinterpret_cast<const char*>(bmp.data()),bmp.size()); }
        SceneRuntime::MaterialAsset asset; asset.texture="Assets/Textures/green.bmp";
        const auto prepared=asset.Prepare(renderer.GetDevice(),renderer.GetCommandQueue(),root); material=*prepared;
        Require(sample()==std::array<unsigned char,4>{0,255,0,255},"material texture overrides model texture on GPU");
        Require(renderer.WaitForIdle(),"material GPU complete before resource release");
    }
}
