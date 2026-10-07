#pragma once
#include "EnvironmentValidation.h"
#include "../Editor/src/GameSession.h"
#include "../Editor/src/ComponentPanel.h"
#include <SceneRuntime/SceneView.h>
#include <Engine/Graphics/Resources/LocalLightBuffer.h>

namespace LocalLightValidation
{
    using EnvironmentValidation::Require;
    inline void Schema()
    {
        SceneRuntime::ScenePlacement object; object.id="light"; object.pointLight.emplace(); object.spotLight.emplace();
        object.pointLight->intensity=8; object.spotLight->outerAngle=80;
        SceneRuntime::SceneLayout layout; layout.objects={object};
        const auto parsed=SceneRuntime::SceneLayout::Parse(layout.Serialize());
        Require(parsed.objects[0].SameComponents(object),"Point/Spot properties roundtrip");
        SceneRuntime::ScenePlacement copy; copy.CopyComponents(object);
        Require(copy.SameComponents(object) && copy.HasComponentId("pointLight") && copy.HasComponentId("spotLight"),"light IDs and component copying participate in scene operations");
        const auto rejects=[&](const auto& candidate) { bool rejected=false; try { candidate.Serialize(); } catch (const std::exception&) { rejected=true; } Require(rejected,"invalid local light rejected"); };
        auto invalid=layout; invalid.objects[0].pointLight->range=0; rejects(invalid);
        invalid=layout; invalid.objects[0].spotLight->innerAngle=80; rejects(invalid);
        invalid=layout; invalid.objects[0].spotLight->color[1]=NAN; rejects(invalid);
        invalid=layout; invalid.objects[0].pointLight->intensity=-1; rejects(invalid);
        layout.objects.clear(); object.spotLight.reset();
        for (size_t index=0;index<32;++index) { object.id="light"+std::to_string(index); layout.objects.push_back(object); }
        Require(SceneRuntime::SceneLayout::Parse(layout.Serialize()).objects.size()==32,"32 enabled local lights supported");
        object.id="extra"; layout.objects.push_back(object); rejects(layout);
        layout.objects.back().pointLight->enabled=false;
        Require(SceneRuntime::SceneLayout::Parse(layout.Serialize()).objects.size()==33,"disabled lights do not consume GPU capacity");
    }
    inline void Rendering(Engine::DirectX12Renderer& renderer)
    {
        Engine::MeshData data; data.vertices={{{-1,-1,0},{0,0,-1}},{{0,1,0},{0,0,-1}},{{1,-1,0},{0,0,-1}}}; data.indices={0,1,2};
        Engine::MeshRenderer mesh;
        Require(mesh.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),data,std::filesystem::absolute("Content/Shaders/Mesh.hlsl")),"local light GPU fixture initializes");
        DirectX::XMFLOAT4X4 identity; DirectX::XMStoreFloat4x4(&identity,DirectX::XMMatrixIdentity());
        Engine::DirectionalLight lighting; lighting.intensity=0; lighting.ambientIntensity=0; lighting.specularStrength=0;
        Engine::LocalLight point; point.position={0,0,-1}; point.color={1,0,0}; point.intensity=.5f; point.range=10;
        lighting.localLights={point};
        const auto capture=[&] { return EnvironmentValidation::Pixel(renderer,[&](auto* commands) { mesh.Draw(commands,identity,identity,lighting); }); };
        const auto closePixel=capture(); Require(closePixel[0]>=125 && closePixel[0]<=129 && closePixel[1]==0,"point light lights surface without directional or ambient light");
        lighting.localLights[0].position[2]=-2; const auto distantPixel=capture();
        Require(distantPixel[0]>=30 && distantPixel[0]<=33 && closePixel[0]>distantPixel[0]*3,"point light follows inverse square attenuation");
        lighting.localLights[0].range=1; Require(capture()[0]==0,"range smoothly cuts off distant light");
        lighting.localLights[0]=point; lighting.localLights[0].position[2]=1;
        Require(capture()[0]==0,"point behind surface does not illuminate front normal");
        lighting.localLights[0]=point; lighting.localLights[0].spot=1; const auto inside=capture();
        lighting.localLights[0].direction={1,0,0}; Require(capture()[0]==0,"spot cone excludes surfaces outside outer angle");
        lighting.localLights[0].direction={std::sin(.4f),0,std::cos(.4f)}; const auto edge=capture();
        Require(inside[0]>edge[0]+10 && edge[0]>5,"spot smoothly fades between inner and outer cone");
        lighting.localLights={point,point}; lighting.localLights[1].color={0,1,0}; const auto combined=capture();
        Require(combined[0]>=125 && combined[1]>=125 && combined[2]==0,"multiple colored point lights add on GPU");
        lighting.localLights[1].position={1e30f,0,0}; const auto distantSource=capture();
        Require(distantSource[0]>=125 && distantSource[1]==0,"far finite light cannot poison other lighting through distance overflow");
        lighting.enabled=false; Require(capture()==std::array<unsigned char,4>{255,255,255,255},"local lights respect global unlit mode"); lighting.enabled=true;
        lighting.localLights.assign(32,point); for (auto& light : lighting.localLights) light.intensity=1.0f/64;
        Require(capture()[0]>=125 && capture()[0]<=129,"all 32 local lights reach shader buffer");
        Engine::Material pbr; pbr.physicallyBased=true; pbr.roughness=.7f; pbr.color={.6f,.6f,.6f,1};
        lighting.localLights={point};
        const auto pbrPixel=EnvironmentValidation::Pixel(renderer,[&](auto* commands) { mesh.Draw(commands,identity,identity,lighting,{0,0,-3.5f},{},&pbr); });
        Require(pbrPixel[0]>40 && pbrPixel[1]==0,"local light evaluates PBR reflection in linear space");
        // 同一フレームで同じメッシュに異なる光を使い、最初の描画を後の書込から保護します。
        const auto retained=EnvironmentValidation::Pixel(renderer,[&](auto* commands) {
            lighting.localLights={point}; mesh.Draw(commands,identity,identity,lighting);
            D3D12_RECT hidden{0,0,1,1}; commands->RSSetScissorRects(1,&hidden);
            lighting.localLights[0].color={0,1,0}; mesh.Draw(commands,identity,identity,lighting);
        });
        Require(retained[0]>=125 && retained[1]==0,"later light upload does not overwrite earlier draw in same frame");
        // フェンス待機なしで複数フレームを送信し、フレーム枠とSRVの寿命も検証します。
        for (int frame=0;frame<8;++frame)
        {
            lighting.localLights[0].color={frame%2 ? 1.0f : 0.0f,frame%2 ? 0.0f : 1.0f,0};
            Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { mesh.Draw(commands,identity,identity,lighting); })!=Engine::RenderResult::Failed,"local lights support consecutive in-flight frames");
        }
        Require(renderer.WaitForIdle(),"local light buffers finish before release");
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        const auto root=std::filesystem::absolute("Content"); std::string error;
        auto layout=SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/LocalLightPlayground.json",root);
        const auto saved=layout.Serialize();
        Editor::GameSession session; Require(session.Play(renderer,root,layout,error),"local light scene enters Editor play");
        auto* environment=session.Runtime(); const auto before=SceneRuntime::SceneView::Light(environment->World());
        Require(before.intensity==0 && before.localLights.size()==3,"local lights collected without enabled directional light");
        const auto& spot=before.localLights.back();
        Require(std::abs(spot.position[1]-6)<.001f && spot.direction[1]<-.2f && spot.direction[2]>.9f,"spot inherits parent camera position and rotation");
        Require(session.Update(.25,true),"scripted local light advances in play");
        const auto after=SceneRuntime::SceneView::Light(environment->World());
        Require(after.localLights[0].position[1]!=before.localLights[0].position[1],"runtime scripts move local light source");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { session.Draw(commands,64,32); })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"Editor draws local light sample");
#if defined(_DEBUG)
        Editor::EditState state; const auto& object=environment->World().Layout().objects.back();
        Require(renderer.Render({0,0,0,1},[](auto*,float){},[&] { ImGui::Begin("Local light inspector"); Editor::ComponentPanel::Draw(state,object,nullptr); ImGui::End(); })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"spot Inspector keeps balanced ImGui stack");
#endif
        Require(session.Stop() && layout.Serialize()==saved,"local light playback preserves authored transforms");
        SceneRuntime::SceneEnvironment app; Require(app.Initialize(renderer,root,layout,error),"App common environment prepares local light scene");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { app.Draw(commands,64,32); })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"App common path renders Point/Spot and PBR");
        auto candidate=layout.objects.back(); candidate.spotLight->range=-1;
        SceneRuntime::SceneWorld world; Require(world.Initialize(renderer,root,layout,root/"Shaders/Mesh.hlsl",&error),"editable local light world initializes");
        const auto original=world.Layout().Serialize();
        Require(!world.SetComponents(candidate.id,candidate,root,error) && world.Layout().Serialize()==original,"invalid Inspector light settings preserve world");
    }
}
