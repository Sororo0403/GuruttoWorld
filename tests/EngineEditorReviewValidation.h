#pragma once
#include "../Editor/src/SceneDocument.h"
#include "EnvironmentValidation.h"
#include <SceneRuntime/Prefab.h>
#include <SceneRuntime/GamePlayback.h>
#include <Engine/Graphics/Renderers/ModelRenderer.h>
#include <algorithm>
#include <cstdint>
#include <fstream>

namespace EngineEditorReviewValidation
{
    /// <summary>回帰条件を満たさない場合に検証を終了します。</summary>
    inline void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    /// <summary>検証プロセスごとに独立した生成物の保存先を取得します。</summary>
    inline const std::filesystem::path& Fixtures()
    {
        static const auto root=std::filesystem::absolute("generated/tests/engine-editor-review")/
            (std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        return root;
    }
    /// <summary>不正な操作が例外として拒否されることを確認します。</summary>
    template<class Action> inline bool Rejected(Action action)
    {
        try {action();return false;} catch(const std::exception&) {return true;}
    }
    /// <summary>内部と外部のカメラ参照を持つメニューのテスト配置を生成します。</summary>
    inline SceneRuntime::SceneLayout MenuPrefab()
    {
        SceneRuntime::ScenePlacement menu;menu.id="menu";menu.canvas.emplace();menu.canvas->menu.emplace();
        auto& configuration=*menu.canvas->menu;
        configuration.configTransition.cameraTarget="camera";
        configuration.configTransition.returnCameraTarget="external-camera";
        configuration.entries.front().transitionCameraTarget="camera";
        SceneRuntime::ScenePlacement camera;camera.id="camera";camera.parentId="menu";camera.camera.emplace();
        SceneRuntime::SceneLayout layout;layout.objects={menu,camera};return layout;
    }
    /// <summary>複製先のカメラ参照と外部参照の保持を確認します。</summary>
    inline void CameraLinks(const SceneRuntime::SceneLayout& layout,size_t menuIndex,size_t cameraIndex)
    {
        const auto& menu=*layout.objects.at(menuIndex).canvas->menu;
        Require(menu.configTransition.cameraTarget==layout.objects.at(cameraIndex).id &&
            menu.entries.front().transitionCameraTarget==layout.objects.at(cameraIndex).id &&
            menu.configTransition.returnCameraTarget=="external-camera","internal menu cameras remap while external references remain intact");
    }
    /// <summary>編集データの再発条件を検証します。</summary>
    inline void Data()
    {
        const auto& root=Fixtures();std::filesystem::create_directories(root/"Assets/Scenes");
        const auto source=root/"Assets/Scenes/A.json",destination=root/"Assets/Scenes/B.json";
        SceneRuntime::SceneLayout initial,external;SceneRuntime::ScenePlacement item;item.id="external";external.objects={item};
        initial.Save(source);Editor::SceneDocument document(source);external.Save(source);
        Require(Rejected([&] {document.Save(initial);}),"external edits prevent saving before rename");
        Engine::AssetDatabase(root).Move("Assets/Scenes/A.json","Assets/Scenes/B.json");document.AssetMoved(source,destination);
        Require(Rejected([&] {document.Save(initial);}) && SceneRuntime::SceneLayout::Load(destination).Serialize()==external.Serialize(),
            "rename must preserve conflict baseline and external file contents");
        initial.Save(source);Editor::SceneDocument normal(source);
        Engine::AssetDatabase(root).Move("Assets/Scenes/A.json","Assets/Scenes/C.json");normal.AssetMoved(source,root/"Assets/Scenes/C.json");
        normal.Save(external);Require(SceneRuntime::SceneLayout::Load(normal.Path()).objects.size()==1,"uncontested renamed document still saves");

    }
    /// <summary>メニューと光源の上限を超える複製が変更前に拒否されることを確認します。</summary>
    inline void DuplicateLimits(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        auto layout=MenuPrefab();SceneRuntime::SceneWorld world;std::string error;
        Require(world.Initialize(renderer,root,layout,root/"Shaders/Mesh.hlsl",&error),error.c_str());
        const auto saved=world.Layout().Serialize();const auto version=world.RenderVersion();std::vector<std::string> created;
        Require(!world.DuplicateObjects({"menu"},{},created,error) && !error.empty() && created.empty() &&
            world.Layout().Serialize()==saved && world.RenderVersion()==version,"duplicate menu rejection preserves scene, history snapshot and render version");
        layout.objects.front().canvas->enabled=false;Require(world.ReplaceLayout(layout,root,error),error.c_str());
        Require(world.DuplicateObjects({"menu","camera"},{},created,error) && created.size()==2,"disabled menu subtree can be duplicated");
        CameraLinks(world.Layout(),2,3);
        layout.objects.clear();for(int i=0;i<32;++i) {
            SceneRuntime::ScenePlacement light;light.id="light"+std::to_string(i);light.pointLight.emplace();layout.objects.push_back(light);
        }
        Require(world.ReplaceLayout(layout,root,error),error.c_str());const auto lights=world.Layout().Serialize();
        const auto lightsVersion=world.RenderVersion();created.clear();
        Require(!world.DuplicateObjects({"light0"},{},created,error) && !error.empty() && created.empty() &&
            world.Layout().Serialize()==lights && world.RenderVersion()==lightsVersion,"33rd light duplication is rejected atomically");
    }
    /// <summary>GPU実行中の参照保持とフレーム枠再利用時のテクスチャ解放を検証します。</summary>
    inline void TextureLifetime(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        Engine::MeshData data;data.vertices={{{-1,-1,.1f},{0,0,-1}},{{0,1,.1f},{0,0,-1}},{{1,-1,.1f},{0,0,-1}}};data.indices={0,1,2};
        Engine::MeshRenderer mesh;Require(mesh.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),data,root/"Shaders/Mesh.hlsl"),"texture lifetime mesh initializes");
        DirectX::XMFLOAT4X4 identity;DirectX::XMStoreFloat4x4(&identity,DirectX::XMMatrixIdentity());
        const auto texture=[&](std::vector<unsigned char> pixels) {
            auto result=std::make_shared<Engine::Texture2D>();Require(result->InitializePixels(renderer.GetDevice(),renderer.GetCommandQueue(),1,1,pixels),"test texture initializes");return result;
        };
        std::vector<std::weak_ptr<Engine::Texture2D>> previous;
        for(int i=0;i<8;++i) {
            auto image=texture({255,255,255,255});previous.push_back(image);Engine::Material material;material.texture=std::move(image);
            Require(renderer.Render({0,0,0,1},[&](auto* commands,float) {
                mesh.Draw(commands,identity,identity,{}, {},{},&material);material.texture.reset();
                Require(!previous.back().expired(),"recorded frame retains texture after caller drops it");
            })!=Engine::RenderResult::Failed,"texture replacement frame submits");
        }
        Require(renderer.WaitForIdle(),"replacement frames complete");
        Require(std::count_if(previous.begin(),previous.end(),[](const auto& value) {return !value.expired();})<=Engine::RenderFrameContext::SlotCount,
            "discarded material textures are bounded by in-flight frame slots");
        for(unsigned int i=0;i<Engine::RenderFrameContext::SlotCount;++i)
            Require(renderer.Render({0,0,0,1},[&](auto* commands,float) {mesh.Draw(commands,identity,identity);})!=Engine::RenderResult::Failed,"neutral frame submits");
        Require(renderer.WaitForIdle() && std::all_of(previous.begin(),previous.end(),[](const auto& value) {return value.expired();}),"recycled frame slots release all discarded textures");
        Engine::Material red,blue;red.texture=texture({255,0,0,255});blue.texture=texture({0,0,255,255});
        const std::weak_ptr<const Engine::Texture2D> redWeak=red.texture,blueWeak=blue.texture;
        Engine::DirectionalLight unlit;unlit.enabled=false;
        const auto pixel=EnvironmentValidation::Pixel(renderer,[&](auto* commands) {
            const D3D12_RECT left{0,0,32,32},right{32,0,64,32};commands->RSSetScissorRects(1,&left);
            mesh.Draw(commands,identity,identity,unlit,{}, {},&red);red.texture.reset();commands->RSSetScissorRects(1,&right);
            mesh.Draw(commands,identity,identity,unlit,{}, {},&blue);blue.texture.reset();
            Require(!redWeak.expired() && !blueWeak.expired(),"multiple draws retain independent immutable descriptors");
        });
        Require(pixel[2]>240 && pixel[0]<10 && pixel[1]<10,"GPU samples last draw texture after caller releases both textures");
    }
    /// <summary>追加済みの実行時の回帰条件を検証します。</summary>
    inline void Runtime(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        DuplicateLimits(renderer,root);TextureLifetime(renderer,root);
    }
}
