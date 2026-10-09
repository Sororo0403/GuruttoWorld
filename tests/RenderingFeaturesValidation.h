#pragma once
#include "EnvironmentValidation.h"
#include "../SceneRuntime/src/SceneRenderPlan.h"
#include <Engine/Graphics/Renderers/OcclusionRenderer.h>
#include <SceneRuntime/LightmapBaker.h>
#include <fstream>

namespace RenderingFeaturesValidation
{
    using EnvironmentValidation::Require;
    /// <summary>インスタンス行列と現フレームの遮蔽結果がGPU描画へ反映されることを確認します。</summary>
    inline void Rendering(Engine::DirectX12Renderer& renderer)
    {
        const auto shader=std::filesystem::absolute("Content/Shaders/Mesh.hlsl");
        Engine::MeshData data; data.vertices={{{-1,-1,.1f},{0,0,-1}},{{0,1,.1f},{0,0,-1}},{{1,-1,.1f},{0,0,-1}}}; data.indices={0,1,2};
        Engine::MeshRenderer mesh; Require(mesh.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),data,shader),"instancing fixture initializes");
        DirectX::XMFLOAT4X4 identity,offscreen;
        DirectX::XMStoreFloat4x4(&identity,DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&offscreen,DirectX::XMMatrixTranslation(10,0,0));
        Engine::DirectionalLight light; light.enabled=false;
        Engine::Material material; material.color={1,0,0,1};
        const std::array<DirectX::XMFLOAT4X4,2> transforms{offscreen,identity};
        for (size_t frame=0;frame<4;++frame)
            Require(EnvironmentValidation::Pixel(renderer,[&](auto* commands) {
                mesh.Draw(commands,offscreen,identity,light,{0,0,-3.5f},{},&material,{},transforms);
            })==std::array<unsigned char,4>{255,0,0,255},"second GPU instance uses its own matrix across frame slots");
        Engine::OcclusionRenderer occlusion; Require(occlusion.Initialize(renderer.GetDevice(),shader),"occlusion pipeline initializes");
        const DirectX::BoundingBox bounds{{0,0,.6f},{.2f,.2f,.1f}};
        const auto predicate=[&](float depth) { return EnvironmentValidation::Pixel(renderer,[&](auto* commands) {
            Engine::RenderTargetBinding target; Require(Engine::RenderTargetBinding::Current(commands,target),"occlusion target tracked");
            commands->ClearDepthStencilView(target.depth,D3D12_CLEAR_FLAG_DEPTH,depth,0,0,nullptr);
            Require(occlusion.Begin(commands,bounds,identity,identity),"current-frame box query begins");
            mesh.Draw(commands,identity,identity,light,{0,0,-3.5f},{},&material);
            Engine::OcclusionRenderer::End(commands);
        }); };
        Require(predicate(.2f)[0]==0,"zero occlusion samples predicate the later mesh draw");
        Require(predicate(1)[0]==255,"newly visible bounds render immediately without stale frame results");
        Require(predicate(.2f)[0]==0,"occlusion query result is replaced for every invocation");
        Engine::ShadowMap localShadow; Require(localShadow.Initialize(renderer.GetDevice(),shader,true),"local shadow atlas initializes");
        Engine::LocalLight point; point.position={0,0,-1}; point.range=10; point.intensity=.5f;
        point.shadowFirst=0; point.shadowCount=6;
        Engine::DirectionalLight illuminated; illuminated.intensity=0; illuminated.ambientIntensity=0; illuminated.specularStrength=0;
        illuminated.localLights={point};
        DirectX::XMFLOAT4X4 blocker; DirectX::XMStoreFloat4x4(&blocker,DirectX::XMMatrixTranslation(0,0,-.6f));
        const auto local=[&](bool shadows,bool spot) { return EnvironmentValidation::Pixel(renderer,[&](auto* commands) {
            illuminated.localLights[0].spot=spot ? 1.0f : 0.0f;
            illuminated.localLights[0].shadowFirst=shadows ? 0.0f : -1.0f;
            if (shadows)
            {
                Require(localShadow.BeginLocal(commands,illuminated.localLights[0],spot ? 0U : 4U,spot ? 0U : 4U),"local source shadow face begins");
                mesh.DrawShadow(commands,blocker,localShadow); localShadow.End(commands); illuminated.localShadow=&localShadow;
            }
            else illuminated.localShadow=nullptr;
            mesh.Draw(commands,identity,identity,illuminated);
        }); };
        const auto pointLit=local(false,false),pointShadowed=local(true,false);
        Require(pointLit[0]>90 && pointShadowed[0]<pointLit[0]/4,"point cube face shadow blocks actual local lighting");
        const auto spotLit=local(false,true),spotShadowed=local(true,true);
        Require(spotLit[0]>90 && spotShadowed[0]<spotLit[0]/4,"spot perspective shadow blocks actual cone lighting");
        auto panorama=std::make_shared<Engine::Texture2D>(),lightmap=std::make_shared<Engine::Texture2D>();
        Require(panorama->InitializePixels(renderer.GetDevice(),renderer.GetCommandQueue(),1,1,{255,255,255,255}) &&
            lightmap->InitializePixels(renderer.GetDevice(),renderer.GetCommandQueue(),1,1,{0,128,0,255}),"IBL and baked lighting textures initialize");
        Engine::Material surface; surface.physicallyBased=true; surface.color={.5f,.5f,.5f,1}; surface.environmentTexture=panorama;
        Engine::DirectionalLight indirect; indirect.intensity=0; indirect.ambientIntensity=0;
        const auto imageLighting=[&] { return EnvironmentValidation::Pixel(renderer,[&](auto* commands) {
            mesh.Draw(commands,identity,identity,indirect,{0,0,-3.5f},{},&surface);
        }); };
        const auto environmentLit=imageLighting(); Require(environmentLit[0]>120 && environmentLit[0]==environmentLit[1],"panorama illuminates PBR material without any direct light");
        surface.environmentIntensity=2; Require(imageLighting()[0]>environmentLit[0]+25,"environment intensity changes image based lighting");
        surface.environmentTexture.reset(); surface.lightmap=lightmap;
        const auto baked=imageLighting(); Require(baked[0]==0 && baked[1]>50 && baked[2]==0,"baked irradiance illuminates original mesh UV independently of direct lights");

        const auto root=std::filesystem::absolute("generated/tests/rendering-features"); std::filesystem::create_directories(root/"Assets/Models");
        { std::ofstream file(root/"Assets/Models/Base.obj"); file<<"v -1 -1 0\nv 0 1 0\nv 1 -1 0\nvn 0 0 -1\nf 1//1 2//1 3//1\nf 1//1 2//1 3//1\n"; }
        { std::ofstream file(root/"Assets/Models/Lod.obj"); file<<"v -1 -1 0\nv 0 1 0\nv 1 -1 0\nvn 0 0 -1\nf 1//1 2//1 3//1\n"; }
        SceneRuntime::SceneLayout layout; SceneRuntime::ScenePlacement object; object.id="item"; object.name="LOD item";
        object.SetModel("Assets/Models/Base.obj"); object.meshRenderer->lods={{4,"Assets/Models/Lod.obj"}}; object.meshRenderer->instancing=true;
        layout.objects.push_back(object);
        Require(SceneRuntime::SceneLayout::Parse(layout.Serialize()).objects.front().meshRenderer==object.meshRenderer,"LOD and instancing settings roundtrip");
        SceneRuntime::SceneWorld world; std::string error;
        Require(world.Initialize(renderer,root,layout,shader,&error),"LOD models prepare atomically");
        Engine::Camera camera;
        const auto render=[&] { Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { world.Draw(commands,camera,light); })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"LOD world renders"); };
        render(); Require(world.Telemetry().triangles==2,"near camera selects original geometry");
        camera.SetPosition({0,0,-5}); render(); Require(world.Telemetry().triangles==1,"far camera selects lower detail geometry");
        std::string duplicate; Require(world.DuplicateObject("item",{2,0,0},duplicate,error),"LOD object duplication succeeds");
        render(); Require(world.Telemetry().draws==1 && world.Telemetry().triangles==2,"compatible duplicated static LOD meshes use one GPU draw");
        auto invalid=world.Layout(); invalid.objects.front().meshRenderer->lods.front().model="Assets/Models/Missing.obj";
        Require(!world.ReplaceLayout(invalid,root,error),"missing LOD resource fails before committing scene");
        render(); Require(world.Telemetry().draws==1 && world.Telemetry().triangles==2,"failed LOD edit preserves current batch and geometry");
        { std::ofstream file(root/"Assets/Models/Uv.obj"); file<<"v -1 -1 0\nv 0 1 0\nv 1 -1 0\nvn 0 0 1\nvt 0 1\nvt 0.5 0\nvt 1 1\nf 1/1/1 2/2/1 3/3/1\n"; }
        std::vector<Engine::MeshData> source; Require(Engine::ModelLoader::Load(root/"Assets/Models/Uv.obj",source),"bake UV fixture loads");
        const auto normal=source.front().vertices.front().normal;
        SceneRuntime::SceneLayout bakeLayout; object.meshRenderer->lods.clear(); object.SetModel("Assets/Models/Uv.obj"); bakeLayout.objects={object};
        SceneRuntime::ScenePlacement sun; sun.id="sun"; sun.directionalLight.emplace(); sun.directionalLight->intensity=1; sun.directionalLight->ambient=0;
        sun.directionalLight->direction={-normal[0],-normal[1],-normal[2]}; bakeLayout.objects.push_back(sun);
        SceneRuntime::SceneWorld bakeWorld; Require(bakeWorld.Initialize(renderer,root,bakeLayout,shader,&error),"lightmap world prepares");
        const auto lit=SceneRuntime::LightmapBaker::Bake(bakeWorld,root,"item",16);
        const size_t center=(8*16+8)*4; Require(lit[center]>240 && lit[center+3]==255,"UV baker records visible direct illumination");
        auto blockerObject=object; blockerObject.id="blocker"; blockerObject.position={normal[0]*.5f,normal[1]*.5f,normal[2]*.5f};
        std::string added; Require(bakeWorld.AddObject(blockerObject,root,added,error),"bake blocker placement adds");
        const auto shadowed=SceneRuntime::LightmapBaker::Bake(bakeWorld,root,"item",16);
        Require(shadowed[center]==0 && shadowed[center+3]==255,"UV baker traces actual scene geometry to produce baked shadow");
        const std::filesystem::path bakedPath="Assets/Textures/Baked/Test.bmp";
        SceneRuntime::LightmapBaker::Save(root,bakedPath,16,lit);
        Engine::Texture2D persisted; Require(persisted.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),root/bakedPath),"saved baked BMP loads as runtime texture");
        bool invalidOutput=false;
        try { SceneRuntime::LightmapBaker::Save(root,"../escape.bmp",16,lit); } catch (const std::exception&) { invalidOutput=true; }
        Require(invalidOutput,"baker rejects output outside Assets/Textures");
    }
}
