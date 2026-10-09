#pragma once
#include "EnvironmentValidation.h"
#include "../Editor/src/GameSession.h"
#include "../Editor/src/EditHistory.h"
#include "../Editor/src/ObjectPanel.h"
#include <Engine/Graphics/Renderers/PostEffectRenderer.h>
#include <Engine/Graphics/Renderers/ParticleRenderer.h>
#include <Engine/Graphics/Resources/RenderTargetBinding.h>
#include <DirectXPackedVector.h>

namespace PostEffectValidation
{
    using EnvironmentValidation::Require;
    inline std::array<float,4> HdrPixel(Engine::DirectX12Renderer& renderer,const std::function<void(ID3D12GraphicsCommandList*)>& draw)
    {
        Engine::RenderTexture target; Require(target.Resize(renderer,64,32,Engine::RenderTexture::HdrFormat),"HDR target initializes");
        const auto original=target.GetResource();
        Require(!target.Resize(renderer,0,32) && target.GetResource()==original && target.GetFormat()==Engine::RenderTexture::HdrFormat,"invalid resize preserves HDR target");
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width=512*32; buffer.Height=1; buffer.DepthOrArraySize=buffer.MipLevels=1; buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        Require(SUCCEEDED(renderer.GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"HDR readback initializes");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) {
            Require(target.Begin(commands,{0,0,0,1}),"HDR target begins"); draw(commands); Require(target.End(commands),"HDR target ends");
            D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource=target.GetResource();
            barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
            commands->ResourceBarrier(1,&barrier);
            D3D12_TEXTURE_COPY_LOCATION source{},destination{};
            source.pResource=target.GetResource(); source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.pResource=readback.Get(); destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint.Footprint={Engine::RenderTexture::HdrFormat,64,32,1,512}; commands->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter); commands->ResourceBarrier(1,&barrier);
        })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"HDR rendering completes");
        void* pixels=nullptr; const D3D12_RANGE range{16*512+32*8,16*512+32*8+8};
        Require(SUCCEEDED(readback->Map(0,&range,&pixels)),"HDR pixels map");
        const auto* values=reinterpret_cast<const DirectX::PackedVector::HALF*>(static_cast<const unsigned char*>(pixels)+range.Begin);
        std::array<float,4> result{}; for (size_t index=0;index<4;++index) result[index]=DirectX::PackedVector::XMConvertHalfToFloat(values[index]);
        const D3D12_RANGE written{0,0}; readback->Unmap(0,&written); return result;
    }
    inline void Schema()
    {
        SceneRuntime::SceneLayout layout; layout.settings.postEffects.enabled=true; layout.settings.postEffects.exposure=2;
        layout.settings.postEffects.bloomRadius=2; layout.settings.postEffects.toneMapping=Engine::ToneMapping::Reinhard;
        layout.settings.postEffects.autoExposure=true; layout.settings.postEffects.exposureMinimum=-3;
        layout.settings.postEffects.exposureMaximum=4; layout.settings.postEffects.contrast=1.2f;
        layout.settings.postEffects.saturation=.7f; layout.settings.postEffects.colorFilter={1,.8f,.6f};
        Require(SceneRuntime::SceneLayout::Parse(layout.Serialize()).settings==layout.settings,"post effect settings roundtrip");
        auto legacy=Engine::Json::parse(layout.Serialize()); legacy["settings"].erase("postEffects");
        Require(!SceneRuntime::SceneLayout::Parse(legacy.dump()).settings.postEffects.enabled,"old scenes keep post effects disabled");
        for (const float invalid : {NAN,11.0f,-11.0f})
        {
            auto candidate=layout; candidate.settings.postEffects.exposure=invalid; bool rejected=false;
            try { candidate.Serialize(); } catch (const std::exception&) { rejected=true; } Require(rejected,"invalid exposure rejected");
        }
        layout.settings.postEffects.toneMapping=static_cast<Engine::ToneMapping>(42); bool rejected=false;
        try { layout.Serialize(); } catch (const std::exception&) { rejected=true; } Require(rejected,"unknown tone mapping rejected");
    }
    inline void Rendering(Engine::DirectX12Renderer& renderer)
    {
        Engine::MeshData data; data.vertices={{{-1,-1,0},{0,0,-1}},{{0,1,0},{0,0,-1}},{{1,-1,0},{0,0,-1}}}; data.indices={0,1,2};
        Engine::MeshRenderer mesh; Require(mesh.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),data,std::filesystem::absolute("Content/Shaders/Mesh.hlsl")),"HDR mesh initializes");
        DirectX::XMFLOAT4X4 identity; DirectX::XMStoreFloat4x4(&identity,DirectX::XMMatrixIdentity());
        auto world=identity;
        Engine::Material material; material.physicallyBased=true;
        Engine::DirectionalLight light; light.intensity=0; light.ambientIntensity=4;
        const auto draw=[&](auto* commands) { mesh.Draw(commands,world,identity,light,{0,0,-3.5f},{},&material); };
        Require(HdrPixel(renderer,draw)[0]>3.9f,"PBR retains values above one in linear HDR target");
        DirectX::XMStoreFloat4x4(&world,DirectX::XMMatrixScaling(-1,1,1));
        Require(HdrPixel(renderer,draw)[0]>3.9f,"mirrored HDR mesh pipeline preserves visibility");
        material.color={1,0,0,.5f}; material.transparent=true;
        const auto transparent=HdrPixel(renderer,draw);
        Require(std::abs(transparent[0]-2)<.02f && transparent[1]==0,"transparent mirrored HDR mesh blends linear radiance");
        DirectX::XMStoreFloat4x4(&world,DirectX::XMMatrixIdentity());
        Require(std::abs(HdrPixel(renderer,draw)[0]-2)<.02f,"transparent regular HDR pipeline blends linear radiance");
        auto white=std::make_shared<Engine::Texture2D>(); Require(white->Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),{}),"HDR white fixture initializes");
        Engine::SpriteRenderer sprite; Require(sprite.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),white,std::filesystem::absolute("Content/Shaders/Sprite.hlsl")),"HDR sprite initializes");
        Engine::SpriteDrawParameters parameters; parameters.size={64,32}; parameters.color={.5f,.5f,.5f,1};
        const auto gray=HdrPixel(renderer,[&](auto* commands) { sprite.Draw(commands,64,32,parameters); });
        Require(std::abs(gray[0]-.214041f)<.001f,"sprite converts authored sRGB into linear HDR");
        Engine::ParticleRenderer particles; Require(particles.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),white,std::filesystem::absolute("Content/Shaders/GlowParticle.hlsl")),"HDR particles initialize");
        DirectX::XMFLOAT4X4 particleMatrix; DirectX::XMStoreFloat4x4(&particleMatrix,DirectX::XMMatrixScaling(2,2,1));
        const auto glow=HdrPixel(renderer,[&](auto* commands) { particles.Draw(commands,particleMatrix,{1,0,0,1}); particles.Draw(commands,particleMatrix,{1,0,0,1}); });
        Require(glow[0]>1.9f && glow[1]==0,"additive glow particles retain HDR brightness");

        Engine::PostEffectRenderer post;
        Require(!post.Initialize(renderer.GetDevice(),"generated/tests/missing-post-shader.hlsl") && !post.Ready(),"post pipeline failure is retryable");
        Require(post.Initialize(renderer.GetDevice(),std::filesystem::absolute("Content/Shaders/PostEffects.hlsl")),"post pipeline initializes");
        Engine::PostEffectSettings settings; settings.enabled=true; settings.bloomEnabled=false; settings.toneMapping=Engine::ToneMapping::Reinhard;
        const auto constant=[&](float value) { return EnvironmentValidation::Pixel(renderer,[&](auto* commands) {
            Engine::RenderTargetBinding before,after; Require(Engine::RenderTargetBinding::Current(commands,before),"post caller target recorded");
            Require(!post.Begin(commands,0,32,{0,0,0,1}),"invalid post size rejected");
            Require(post.Begin(commands,64,32,{value,value,value,1}),"post processing begins");
            Require(!post.End(nullptr,settings),"wrong post command list rejected without destroying active pass");
            Require(post.End(commands,settings),"post processing completes");
            Require(Engine::RenderTargetBinding::Current(commands,after) && after.color.ptr==before.color.ptr && after.format==before.format,"post restores caller target and format");
        }); };
        const auto mapped=constant(4); Require(mapped[0]>=230 && mapped[0]<=232,"Reinhard preserves HDR input before mapping to sRGB");
        settings.exposure=1; Require(constant(4)[0]>mapped[0]+5,"positive EV brightens tone mapped image");
        settings.exposure=-2; Require(constant(4)[0]>=186 && constant(4)[0]<=189,"negative EV changes linear exposure before tone mapping");
        settings.exposure=0; settings.toneMapping=Engine::ToneMapping::Filmic;
        Require(constant(1)[0]>=230 && constant(1)[0]<=232,"filmic curve maps unit radiance using ACES approximation");
        settings.toneMapping=Engine::ToneMapping::None;
        Require(constant(.25f)[0]>=136 && constant(.25f)[0]<=138,"no curve keeps linear to sRGB conversion");
        settings.autoExposure=true;
        const auto meteredDark=constant(.01f),meteredBright=constant(16);
        Require(std::abs(static_cast<int>(meteredDark[0])-static_cast<int>(meteredBright[0]))<=1 &&
            meteredBright[0]>=116 && meteredBright[0]<=119,"GPU logarithmic metering normalizes dark and bright scenes to middle gray");
        settings.exposureMinimum=settings.exposureMaximum=0;
        Require(constant(.25f)[0]>=136 && constant(.25f)[0]<=138,"automatic exposure respects clamped EV limits");
        settings.autoExposure=false; settings.colorFilter={1,0,0};
        const auto filtered=constant(.25f); Require(filtered[0]>130 && filtered[1]==0 && filtered[2]==0,"color filter changes output channels");
        settings.saturation=0;
        const auto monochrome=constant(.25f); Require(monochrome[0]==monochrome[1] && monochrome[1]==monochrome[2],"zero saturation produces monochrome output");
        settings.colorFilter={1,1,1}; settings.saturation=1; settings.contrast=0;
        Require(constant(.8f)[0]>=116 && constant(.8f)[0]<=119,"zero contrast maps every luminance to middle gray");
        settings.contrast=1;
        const auto invalid=EnvironmentValidation::Pixel(renderer,[&](auto* commands) {
            auto bad=settings; bad.exposure=NAN;
            Require(post.Begin(commands,64,32,{.25f,.25f,.25f,1}) && !post.End(commands,bad),"invalid end settings restore target with safe conversion");
        });
        Require(invalid[0]>=136 && invalid[0]<=138,"invalid post settings cannot poison displayed pixels");
        const auto halo=[&] { return EnvironmentValidation::Pixel(renderer,[&](auto* commands) {
            Require(post.Begin(commands,64,32,{0,0,0,1}),"bloom scene begins");
            Engine::RenderTargetBinding current; Require(Engine::RenderTargetBinding::Current(commands,current),"bloom target recorded");
            const std::array<float,4> bright{8,8,8,1}; const D3D12_RECT rectangle{38,14,40,18};
            commands->ClearRenderTargetView(current.color,bright.data(),1,&rectangle);
            Require(post.End(commands,settings),"bloom scene ends");
        }); };
        Require(halo()[0]==0,"bright patch does not cover center before bloom");
        settings.bloomEnabled=true; settings.bloomIntensity=1; const auto spread=halo();
        Require(spread[0]>15,"bloom blurs HDR bright patch into surrounding pixels");
        settings.bloomThreshold=100; Require(halo()[0]==0,"bloom threshold rejects dimmer bright patch");
        settings.bloomThreshold=1; settings.bloomRadius=.25f; Require(halo()[0]<spread[0],"bloom radius controls halo spread");
        settings.bloomEnabled=false;
        for (int frame=0;frame<8;++frame)
            Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { Require(post.Begin(commands,31+frame,17+frame,{1,0,0,1}) && post.End(commands,settings),"post target safely resizes in flight"); })!=Engine::RenderResult::Failed,"post consecutive frames render");
        Require(renderer.WaitForIdle(),"post resources finish before release");
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        const auto root=std::filesystem::absolute("Content"); std::string error;
        auto layout=EnvironmentValidation::Layout(); layout.objects[0].sky->horizon={.5f,.5f,.5f}; layout.objects[0].sky->zenith={.5f,.5f,.5f};
        layout.settings.postEffects.enabled=true; layout.settings.postEffects.bloomEnabled=false;
        layout.settings.postEffects.exposure=1; layout.settings.postEffects.toneMapping=Engine::ToneMapping::Reinhard;
        SceneRuntime::SceneEnvironment environment; Require(environment.Initialize(renderer,root,layout,error),"runtime post settings prepare");
        const auto sky=EnvironmentValidation::Pixel(renderer,[&](auto* commands) { environment.Draw(commands,64,32); });
        Require(sky[0]>=147 && sky[0]<=151,"authored sky is linearized and processed in runtime");
        SceneRuntime::ScenePlacement canvas; canvas.id="canvas"; canvas.canvas.emplace(); canvas.canvas->referenceSize={64,32};
        SceneRuntime::ScenePlacement image; image.id="image"; image.parentId=canvas.id; image.rectTransform.emplace(); image.rectTransform->size={64,32}; image.image.emplace(); image.image->color={0,.5f,0,1};
        layout.objects.push_back(canvas); layout.objects.push_back(image);
        SceneRuntime::SceneEnvironment overlay; Require(overlay.Initialize(renderer,root,layout,error),"post UI overlay prepares");
        const auto pixel=EnvironmentValidation::Pixel(renderer,[&](auto* commands) { overlay.Draw(commands,64,32); });
        Require(pixel[0]==0 && pixel[1]>=127 && pixel[1]<=128 && pixel[2]==0,"UI is drawn after effects without exposure or tone mapping");
        layout=SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/PostEffectsPlayground.json",root); const auto saved=layout.Serialize();
        Editor::GameSession session; Require(session.Play(renderer,root,layout,error),"post playground starts in Editor");
        session.Update(.5,true);
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { session.Draw(commands,64,32); })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"post playground renders PBR, shadow, particles and UI");
        Require(session.Stop() && layout.Serialize()==saved,"post playback preserves authored settings");
        SceneRuntime::SceneWorld world; Require(world.Initialize(renderer,root,layout,root/"Shaders/Mesh.hlsl",&error),"post editing world initializes");
        auto settings=world.Layout().settings; settings.postEffects.exposure=NAN;
        Require(!world.SetSettings(settings,error) && world.Layout().Serialize()==saved,"invalid post edits preserve scene");
        Editor::EditHistory history; history.Reset({saved,"",{}});
        settings=world.Layout().settings; settings.postEffects.exposure=2; settings.postEffects.toneMapping=Engine::ToneMapping::Reinhard;
        Require(world.SetSettings(settings,error),"post settings edit applies");
        history.Observe({world.Layout().Serialize(),"",{}},{});
        Require(history.CanUndo() && world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(history.Target(false).json),root,error),"post Undo applies");
        history.Applied(false); Require(world.Layout().Serialize()==saved,"post Undo restores exposure curve and bloom");
        Require(world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(history.Target(true).json),root,error),"post Redo applies");
        history.Applied(true); Require(world.Layout().settings==settings,"post Redo restores complete settings");
        const auto folder=std::filesystem::absolute("generated/tests/post/"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        std::filesystem::create_directories(folder); world.Layout().Save(folder/"saved.json");
        Require(SceneRuntime::SceneLayout::Load(folder/"saved.json").settings==settings,"post save and reload preserve settings");
#if defined(_DEBUG)
        Editor::ObjectPanel inspector; Editor::EditState edit;
        Require(renderer.Render({0,0,0,1},[](auto*,float){},[&] { inspector.Draw(world,edit,true); })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"post settings Inspector keeps balanced UI stack");
#endif
    }
}
