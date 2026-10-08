#pragma once
#include <SceneRuntime/MaterialAsset.h>
#include "EnvironmentValidation.h"
#include "../Editor/src/ProjectCatalog.h"
#include "../Editor/src/GameSession.h"
#include <limits>

namespace MaterialValidation
{
    using EnvironmentValidation::Require;
    inline void Schema()
    {
        const auto root=std::filesystem::absolute("generated/tests/materials");
        const std::filesystem::path path="Assets/Materials/Test.mat";
        SceneRuntime::MaterialAsset asset; asset.values.color={0.2f,0.4f,0.6f,0.5f}; asset.values.transparent=true;
        asset.values.physicallyBased=true; asset.values.normalFlipY=true;
        asset.values.roughness=0.3f; asset.values.metallic=0.7f; asset.values.uv.scale={2,3}; asset.values.uv.rotation=0.5f;
        asset.Save(root,path); const auto restored=SceneRuntime::MaterialAsset::Load(root,path);
        Require(restored.values.color==asset.values.color && restored.values.roughness==0.3f && restored.values.transparent && restored.values.uv.scale==asset.values.uv.scale,"material properties roundtrip");
        Require(restored.values.physicallyBased && restored.values.normalFlipY,"PBR and normal convention roundtrip");
        auto legacy=Engine::Json::parse(std::ifstream(root/path));
        legacy.erase("physicallyBased"); legacy.erase("normalTexture"); legacy.erase("normalFlipY");
        { std::ofstream output(root/"Assets/Materials/Legacy.mat"); output<<legacy.dump(); }
        const auto old=SceneRuntime::MaterialAsset::Load(root,"Assets/Materials/Legacy.mat");
        Require(!old.values.physicallyBased && old.normalTexture.empty(),"legacy materials retain original lighting without normal maps");
        std::filesystem::create_directories(root/"Assets/Textures");
        { std::ofstream image(root/"Assets/Textures/normal.bmp"); image<<"schema fixture"; }
        Engine::AssetDatabase::Ensure(root/"Assets/Textures/normal.bmp");
        asset.normalTexture="Assets/Textures/normal.bmp"; asset.Save(root,path);
        std::filesystem::remove(root/"Assets/Textures/normal-renamed.bmp");
        std::filesystem::remove(root/"Assets/Textures/normal-renamed.bmp.meta");
        Engine::AssetDatabase(root).Move(asset.normalTexture,"Assets/Textures/normal-renamed.bmp");
        Require(SceneRuntime::MaterialAsset::Load(root,path).normalTexture=="Assets/Textures/normal-renamed.bmp","normal map GUID survives asset rename");
        asset.normalTexture="Assets/Textures/../outside.bmp";
        bool invalidNormal=false; try { asset.Validate(); } catch (const std::exception&) { invalidNormal=true; }
        Require(invalidNormal,"normal map rejects path traversal"); asset.normalTexture.clear();
        const auto saved=restored.values.color;
        asset.values.roughness=std::numeric_limits<float>::quiet_NaN();
        bool rejected=false; try { asset.Save(root,path); } catch (const std::exception&) { rejected=true; }
        Require(rejected && SceneRuntime::MaterialAsset::Load(root,path).values.color==saved,"invalid material save preserves prior asset");
        Require(!SceneRuntime::MaterialAsset::ValidPath("Assets/Materials/../Bad.mat"),"material path rejects traversal");
        Require(Editor::ProjectCatalog::Kind(path)==Editor::AssetKind::Material,"Project classifies materials");
        SceneRuntime::ScenePlacement object; object.id="mesh"; object.SetModel("Assets/Models/Cube.obj"); object.material=SceneRuntime::MaterialComponent{"material",true,path};
        SceneRuntime::SceneLayout scene; scene.objects={object};
        Require(SceneRuntime::SceneLayout::Parse(scene.Serialize()).objects[0].Material()==path,"Independent material component reference roundtrip");
        object.material->slots={path,{},"Assets/Materials/Legacy.mat"};scene.objects[0]=object;
        Require(SceneRuntime::SceneLayout::Parse(scene.Serialize()).objects[0].material->slots==object.material->slots,"per-mesh material slots roundtrip with empty fallback");
        const auto invalidSlots=Engine::Json::parse(scene.Serialize());auto malformedSlots=invalidSlots;
        malformedSlots["objects"][0]["components"][1]["slots"]={"Assets/Materials/../invalid.mat"};
        bool badSlot=false;try {SceneRuntime::SceneLayout::Parse(malformedSlots.dump());}catch(...) {badSlot=true;}
        Require(badSlot,"material slot rejects traversal");
        object.material->slots.clear();scene.objects[0]=object;
        const auto serialized=Engine::Json::parse(scene.Serialize());
        auto legacyScene=serialized;
        legacyScene["objects"][0]["components"].erase(legacyScene["objects"][0]["components"].begin()+1);
        legacyScene["objects"][0]["components"][0]["material"]=Engine::AssetDatabase::Text(path);
        const auto migrated=SceneRuntime::SceneLayout::Parse(legacyScene.dump());
        Require(migrated.objects[0].material && migrated.objects[0].Material()==path &&
            !Engine::Json::parse(migrated.Serialize())["objects"][0]["components"][0].contains("material"),
            "legacy mesh material migrates to independent component without data loss");
        object.meshRenderer.reset(); scene.objects[0]=object;
        const auto withoutMesh=SceneRuntime::SceneLayout::Parse(scene.Serialize());
        Require(!withoutMesh.objects[0].meshRenderer && withoutMesh.objects[0].Material()==path,
            "material assignment survives removal of mesh renderer");
        object.material->enabled=false; scene.objects[0]=object;
        Require(SceneRuntime::SceneLayout::Parse(scene.Serialize()).objects[0].Material().empty(),
            "disabled material binding falls back to model materials");
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
        const auto slotRoot=std::filesystem::absolute("generated/tests/material-slots");std::filesystem::create_directories(slotRoot);
        {std::ofstream file(slotRoot/"Slots.mtl");file<<"newmtl First\nKd 1 1 1\nnewmtl Second\nKd 1 1 1\n";}
        {std::ofstream file(slotRoot/"Slots.obj");file<<"mtllib Slots.mtl\nv -1 -1 -0.3\nv 0 1 -0.3\nv 1 -1 -0.3\nv -1 -1 -0.5\nv 0 1 -0.5\nv 1 -1 -0.5\nvn 0 0 1\no First\nusemtl First\nf 1//1 3//1 2//1\no Second\nusemtl Second\nf 4//1 6//1 5//1\n";}
        auto slotModel=std::make_shared<Engine::ModelRenderer>();
        Require(slotModel->Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),slotRoot/"Slots.obj",std::filesystem::absolute("Content/Shaders/Mesh.hlsl")) && slotModel->MeshCount()==2,"two-mesh material-slot fixture initializes");
        auto fallback=std::make_shared<Engine::Material>();fallback->color={1,0,0,1};
        auto slotOverride=std::make_shared<Engine::Material>();slotOverride->color={0,1,0,1};slotOverride->transparent=true;
        Engine::MaterialSlots slots{nullptr,slotOverride};
        const auto slotSample=[&](Engine::MaterialPass pass) {return EnvironmentValidation::Pixel(renderer,[&](auto* commands){slotModel->Draw(commands,identity,identity,light,{0,0,-3.5f},{},fallback.get(),slots,pass);});};
        Require(slotSample(Engine::MaterialPass::Opaque)==std::array<unsigned char,4>{255,0,0,255},"opaque pass renders default material and excludes transparent slot");
        Require(slotSample(Engine::MaterialPass::Transparent)==std::array<unsigned char,4>{0,255,0,255},"transparent pass renders only the per-mesh override");
        Engine::Object3D first;first.SetModel(slotModel);first.SetMaterial(fallback);first.SetMaterialSlots(slots);
        Engine::Object3D independent;independent.SetModel(slotModel);independent.SetMaterial(fallback);
        Require(first.MaterialForMesh(1)==slotOverride.get() && independent.MaterialForMesh(1)==fallback.get() && first.HasMaterialPass(Engine::MaterialPass::Opaque) && first.HasMaterialPass(Engine::MaterialPass::Transparent),"slot overrides belong to the instance without mutating shared model");
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
        material=Engine::Material{}; material.physicallyBased=true; material.color={.8f,.2f,.05f,1}; material.roughness=.7f;
        light.enabled=true; light.direction={0,0,1}; light.intensity=.5f; light.ambientIntensity=0;
        const auto dielectric=sample();
        // 正面照明の解析値でGGXの反射と線形色空間の計算を確認します。
        const auto encode=[](float linear) { return linear<=.0031308f ? linear*12.92f : 1.055f*std::pow(linear,1/2.4f)-.055f; };
        for (size_t channel=0;channel<3;++channel)
        {
            const float srgb=material.color[channel];
            const float base=srgb<=.04045f ? srgb/12.92f : std::pow((srgb+.055f)/1.055f,2.4f);
            const float linear=.5f*(.96f*base/DirectX::XM_PI+.04f/(4*DirectX::XM_PI*std::pow(.7f,4.0f)));
            Require(std::abs(static_cast<int>(dielectric[channel])-std::lround(255*encode(linear)))<=3,"PBR dielectric matches normal-incidence GGX with sRGB conversion");
        }
        material.metallic=1; const auto metal=sample();
        Require(metal[1]<dielectric[1] && metal[2]<dielectric[2],"metal highlights take base color and remove dielectric diffuse");
        material.roughness=.35f; const auto smooth=sample();
        Require(smooth[0]>metal[0]+30,"PBR roughness changes highlight lobe");
        auto grayImage=std::make_shared<Engine::Texture2D>();
        Require(grayImage->InitializePixels(renderer.GetDevice(),renderer.GetCommandQueue(),1,1,{128,128,128,255}),"PBR gray texture initializes");
        material.texture=grayImage; material.color={.5f,.5f,.5f,1}; light.intensity=0; light.ambientIntensity=1;
        const auto multiplied=sample();
        const float product=std::pow((128/255.0f+.055f)/1.055f,2.4f)*std::pow((.5f+.055f)/1.055f,2.4f);
        Require(std::abs(static_cast<int>(multiplied[0])-std::lround(255*encode(product)))<=1,
            "PBR texture and tint multiply after independent sRGB decoding");

        Engine::MeshRenderer mappedMesh;
        data.vertices[0].uv={0,1}; data.vertices[1].uv={.5f,0}; data.vertices[2].uv={1,1};
        Require(mappedMesh.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),data,std::filesystem::absolute("Content/Shaders/Mesh.hlsl")),"normal mapped mesh initializes");
        auto normalImage=std::make_shared<Engine::Texture2D>();
        Require(normalImage->InitializePixels(renderer.GetDevice(),renderer.GetCommandQueue(),1,1,{128,218,218,255}),"linear tangent-space normal initializes");
        material=Engine::Material{}; material.color={.5f,.5f,.5f,1}; material.normalTexture=normalImage;
        light.direction={0,.70710678f,.70710678f}; light.intensity=1; light.ambientIntensity=0; light.specularStrength=0;
        // 従来材質も金属度からハイライトを持つので、視線を外して法線の差を確認します。
        const auto normalSample=[&](Engine::MeshRenderer& target) { return EnvironmentValidation::Pixel(renderer,[&](auto* commands) {
            target.Draw(commands,identity,identity,light,{3,0,-3.5f},material.uv,&material);
        }); };
        const auto front=normalSample(mappedMesh);
        material.normalFlipY=true; const auto reversed=normalSample(mappedMesh);
        Require(front[0]>reversed[0]+60,"normal map and Y convention change actual lighting");
        material.normalTexture.reset(); const auto geometric=normalSample(mappedMesh);
        material.normalTexture=normalImage; const auto degenerate=normalSample(mesh);
        Require(std::abs(static_cast<int>(degenerate[0])-geometric[0])<=1,"degenerate UV falls back to geometric normal");
        light.enabled=false;
        const auto unlit=normalSample(mappedMesh);
        Require(unlit[0]>=127 && unlit[0]<=128 && unlit[1]==unlit[0] && unlit[2]==unlit[0] && unlit[3]==255,
            "normal flag does not accidentally enable disabled lighting");
        Require(renderer.WaitForIdle(),"material GPU complete before resource release");
        const auto content=std::filesystem::absolute("Content");
        const auto layout=SceneRuntime::SceneLayout::Load(content/"Assets/Scenes/RenderingPlayground.json",content);
        const auto saved=layout.Serialize(); std::string error;
        SceneRuntime::SceneEnvironment environment;
        Require(environment.Initialize(renderer,content,layout,error),"PBR scene prepares through App shared environment");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { environment.Draw(commands,64,32); })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),
            "App environment renders PBR and normal mapped sample with shadows");
        Editor::GameSession session;
        Require(session.Play(renderer,content,layout,error),"Editor playback prepares PBR scene");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { session.Draw(commands,64,32); })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),
            "Editor playback renders PBR and normal maps");
        Require(session.Stop() && layout.Serialize()==saved,"stopping PBR playback preserves authored scene");
    }
}
