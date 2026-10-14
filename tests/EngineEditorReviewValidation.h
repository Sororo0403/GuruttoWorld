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
    /// <summary>保存競合・Shader改名・Prefab配置とScript複製の再発条件を検証します。</summary>
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

        std::filesystem::create_directories(root/"Shaders");
        {std::ofstream file(root/"Shaders/Original.hlsl");file<<"shader fixture\n";}
        const auto metadata=Engine::AssetDatabase::Ensure(root/"Shaders/Original.hlsl");
        Engine::AssetDatabase(root).Move("Shaders/Original.hlsl","Shaders/Renamed.hlsl");
        const Engine::AssetDatabase moved(root);
        Require(moved.Resolve("Shaders/Original.hlsl")=="Shaders/Renamed.hlsl" &&
            moved.Resolve("Shaders/Original.hlsl",metadata.id)=="Shaders/Renamed.hlsl" &&
            Engine::AssetDatabase::Read(root/"Shaders/Renamed.hlsl").id==metadata.id,"top-level shader rename preserves ID and old-path resolution");
        Require(Rejected([&] {Engine::AssetDatabase(root).Move("Shaders/Renamed.hlsl","Assets/Models/Renamed.hlsl");}) &&
            Rejected([&] {Engine::AssetDatabase(root).Move("Shaders/Renamed.hlsl","Shaders/../Escape.hlsl");}) &&
            Rejected([&] {Engine::AssetDatabase(root).Move("Assets/Scenes/C.json","Assets/Settings/C.json");}),
            "shader rename retains root, traversal and asset category restrictions");

        auto asset=MenuPrefab();SceneRuntime::SceneLayout instance;
        const auto id=SceneRuntime::Prefab::Instantiate(instance,asset,"Assets/Prefabs/Menu.prefab",{});
        CameraLinks(instance,0,1);CameraLinks(SceneRuntime::Prefab::Extract(instance,id),0,1);
        asset.objects.front().canvas->enabled=false;
        size_t next=1;SceneRuntime::ScriptScene api(asset,next);const auto copy=api.Instantiate("menu",{});api.Commit();
        Require(!copy.empty() && asset.objects.size()==4,"script duplicates complete menu subtree");CameraLinks(asset,2,3);
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
    /// <summary>テストモデルからスキン設定だけを取り除きます。</summary>
    inline void StripSkin(Engine::Json& gltf)
    {
        gltf.erase("skins");for(auto& node:gltf["nodes"]) node.erase("skin");
        for(auto& mesh:gltf["meshes"]) for(auto& primitive:mesh["primitives"]) {
            primitive["attributes"].erase("JOINTS_0");primitive["attributes"].erase("WEIGHTS_0");
        }
    }
    /// <summary>静的glTF/GLBのLODと、スキン・クリップを持つLODの拒否を検証します。</summary>
    inline void StaticLods(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        const auto models=Fixtures()/"Assets/Models";std::filesystem::create_directories(models);
        std::ifstream source(root/"Assets/Models/AnimatedBox.gltf");const auto original=Engine::Json::parse(source);
        auto rigid=original;StripSkin(rigid);rigid.erase("animations");
        {std::ofstream file(models/"Static.gltf");file<<rigid.dump();}
        auto binaryModel=rigid;const auto uri=binaryModel["buffers"][0]["uri"].get<std::string>();
        binaryModel["buffers"][0].erase("uri");std::vector<unsigned char> binary;
        constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::uint32_t accumulator=0;unsigned int bits=0;
        for(const char character:std::string_view(uri).substr(uri.find(',')+1)) {
            if(character=='=') break;const auto value=alphabet.find(character);Require(value!=std::string_view::npos,"fixture buffer is base64");
            accumulator=(accumulator<<6)|static_cast<std::uint32_t>(value);bits+=6;
            if(bits>=8) {bits-=8;binary.push_back(static_cast<unsigned char>(accumulator>>bits));}
        }
        while(binary.size()%4) binary.push_back(0);auto json=binaryModel.dump();while(json.size()%4) json+=' ';
        const std::array<std::uint32_t,5> header{0x46546c67,2,static_cast<std::uint32_t>(28+json.size()+binary.size()),static_cast<std::uint32_t>(json.size()),0x4e4f534a};
        const std::array<std::uint32_t,2> binaryHeader{static_cast<std::uint32_t>(binary.size()),0x004e4942};
        {std::ofstream file(models/"Static.glb",std::ios::binary);file.write(reinterpret_cast<const char*>(header.data()),sizeof(header));file<<json;
            file.write(reinterpret_cast<const char*>(binaryHeader.data()),sizeof(binaryHeader));file.write(reinterpret_cast<const char*>(binary.data()),binary.size());}
        auto skinned=original;skinned.erase("animations");{std::ofstream file(models/"SkinOnly.gltf");file<<skinned.dump();}
        auto animated=original;StripSkin(animated);{std::ofstream file(models/"ClipOnly.gltf");file<<animated.dump();}
        for(const auto* name:{"Static.gltf","Static.glb","SkinOnly.gltf","ClipOnly.gltf"}) {
            Engine::ModelRenderer model;
            Require(model.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),models/name,root/"Shaders/Mesh.hlsl"),"glTF and GLB fixtures load");
            Require(model.Rig() && model.IsAnimated()==(std::string_view(name).starts_with("Skin") || std::string_view(name).starts_with("Clip")),
                "static node hierarchy is distinct from skin or animation clips");
        }
        SceneRuntime::ScenePlacement model;model.id="static";model.SetModel("Assets/Models/Static.gltf");
        model.meshRenderer->lods={{20,"Assets/Models/Static.glb"}};
        SceneRuntime::SceneLayout layout;layout.objects={model};SceneRuntime::SceneWorld world;std::string error,created;
        Require(world.Initialize(renderer,Fixtures(),layout,root/"Shaders/Mesh.hlsl",&error),error.c_str());
        model.id="added";Require(world.AddObject(model,Fixtures(),created,error),error.c_str());
        auto runtimeModel=world.Layout().objects.front();runtimeModel.meshRenderer->lods.front().model="Assets/Models/Static.gltf";
        SceneRuntime::ScriptDefinition definition;
        definition.update=[runtimeModel](SceneRuntime::ScriptContext& context) {if(!context.state["applied"]) {context.scene->SetComponents(runtimeModel.id,runtimeModel);context.state["applied"]=1;}};
        Require(SceneRuntime::ScriptRegistry::Register("ReviewStaticLod",definition),"LOD script registers");
        SceneRuntime::ScenePlacement script;script.id="script";script.scripts.push_back({"lod",true,"ReviewStaticLod",{}});
        Require(world.AddObject(script,Fixtures(),created,error) && world.UpdateComponents(.016),"script component replacement loads static LOD");
        Require(world.Layout().objects.front().meshRenderer->lods.front().model=="Assets/Models/Static.gltf","script LOD edit commits");
        const auto saved=world.Layout().Serialize();
        for(const auto* name:{"SkinOnly.gltf","ClipOnly.gltf"}) {
            model.meshRenderer->lods.front().model=std::filesystem::path("Assets/Models")/name;
            Require(!world.AddObject(model,Fixtures(),created,error) && world.Layout().Serialize()==saved,"animated LOD remains rejected without scene mutation");
        }
        Engine::Camera camera;camera.SetPosition({0,0,-25});camera.SetPerspective(DirectX::XM_PIDIV4,1,.1f,100);
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) {world.Draw(commands,camera,{});})!=Engine::RenderResult::Failed &&
            renderer.WaitForIdle() && world.Telemetry().draws>0,"static glTF and GLB LOD scene renders");
    }
    /// <summary>マウス・キーボード・ゲームパッドから同じScriptイベントが一度ずつ届くことを確認します。</summary>
    inline void UiEvents(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        SceneRuntime::ScriptDefinition definition;definition.update=[](SceneRuntime::ScriptContext&) {};
        definition.onEvent=[](SceneRuntime::ScriptContext& context) {if(context.event->name=="activate") context.object.position[0]+=1;};
        Require(SceneRuntime::ScriptRegistry::Register("ReviewUiEvents",definition),"UI event receiver registers");
        SceneRuntime::ScenePlacement canvas;canvas.id="canvas";canvas.canvas.emplace();canvas.canvas->referenceSize={64,64};
        canvas.scripts.push_back({"events",true,"ReviewUiEvents",{}});
        SceneRuntime::ScenePlacement button;button.id="button";button.parentId="canvas";button.rectTransform.emplace();
        button.rectTransform->size={64,64};button.button.emplace();button.button->event="activate";button.button->inputAction="Confirm";
        SceneRuntime::SceneLayout layout;layout.objects={canvas,button};std::string error;SceneRuntime::SceneEnvironment environment;
        Require(environment.Initialize(renderer,root,layout,error),error.c_str());SceneRuntime::GamePlayback playback;
        playback.Initialize(environment,root,Fixtures()/"settings.json",false);
        playback.Pointer(environment,64,64,{30,30,false,false,false,0},true);
        playback.Pointer(environment,64,64,{30,30,true,false,false,0},true);
        playback.Pointer(environment,64,64,{30,30,false,false,false,0},true);
        environment.Update(.016,true,true);
        Require(environment.World().Layout().objects.front().position[0]==1,"pointer click delivers exactly one script event");
        Engine::InputActions actions;Engine::InputSnapshot snapshot;snapshot.active=true;actions.Update(snapshot);
        snapshot.down[DIK_RETURN]=snapshot.pressed[DIK_RETURN]=true;actions.Update(snapshot);
        playback.Shortcut(environment,actions,64,64,true);
        environment.Update(.016,true,true);
        Require(environment.World().Layout().objects.front().position[0]==2,"keyboard shortcut delivers same script event");
        snapshot={};snapshot.active=true;snapshot.gamepadConnected=true;actions.Update(snapshot);
        snapshot.buttons=snapshot.pressedButtons=XINPUT_GAMEPAD_A;actions.Update(snapshot);playback.Shortcut(environment,actions,64,64,true);
        environment.Update(.016,true,true);
        Require(environment.World().Layout().objects.front().position[0]==3,"gamepad shortcut delivers same script event");
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
    /// <summary>複製制約・LOD・入力経路・GPU資源寿命を実際の描画環境で検証します。</summary>
    inline void Runtime(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        DuplicateLimits(renderer,root);StaticLods(renderer,root);UiEvents(renderer,root);TextureLifetime(renderer,root);
    }
}
