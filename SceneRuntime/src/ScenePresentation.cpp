#include <SceneRuntime/ScenePresentation.h>
#include <Engine/Graphics/Resources/GpuProfiler.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <Engine/Core/Log.h>

namespace SceneRuntime
{
    bool ScenePresentation::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root, std::string& error)
    {
        auto texture=std::make_shared<Engine::Texture2D>();
        if (!texture->Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),{}) ||
            !sky_.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),texture,root/"Shaders/Sky.hlsl") ||
            !particles_.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),texture,root/"Shaders/GlowParticle.hlsl"))
        { error="Scene sky or particle renderer could not be initialized"; return false; }
        device_=renderer.GetDevice(); postShader_=root/"Shaders/PostEffects.hlsl";
        error.clear();
        return true;
    }
    void ScenePresentation::DrawSky(ID3D12GraphicsCommandList* commands, const SceneLayout& layout,
        unsigned int width, unsigned int height, double seconds) const
    {
        const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[](const auto& object) {
            return object.sky && object.sky->enabled;
        });
        if (found==layout.objects.end()) return;
        const auto& sky=*found->sky;
        Engine::SpriteDrawParameters parameters;
        parameters.size={static_cast<float>(width),static_cast<float>(height)};
        const float span=std::max(1.0f,sky.referenceAspect/(parameters.size[0]/parameters.size[1]));
        parameters.uvRect={0,0.5f-span*0.5f,1,0.5f+span*0.5f};
        auto& values=parameters.pixelConstants;
        const auto color=[&](size_t offset,const std::array<float,3>& rgb,float last) {
            std::copy(rgb.begin(),rgb.end(),values.begin()+offset); values[offset+3]=last;
        };
        color(0,sky.horizon,sky.horizonHeight); color(4,sky.zenith,sky.cloudOpacity);
        color(8,sky.sunColor,sky.sunStrength);
        values[12]=sky.sunCenter[0]; values[13]=sky.sunCenter[1];
        values[14]=sky.sunRadius[0]; values[15]=sky.sunRadius[1];
        color(16,sky.cloudLow,0); color(20,sky.cloudHigh,0);
        for (size_t index=0;index<sky.clouds.size();++index)
        {
            const auto offset=24+index*4;
            // Stationary clouds retain their exact authored center. Moving clouds wrap outside the screen.
            for (size_t axis=0;axis<2;++axis)
            {
                const auto center=sky.clouds[index].center[axis];
                const auto velocity=sky.cloudVelocity[axis];
                values[offset+axis]=velocity==0 ? center : static_cast<float>(
                    center+std::remainder(seconds*velocity,2.0));
            }
            values[offset+2]=sky.clouds[index].size;
        }
        sky_.Draw(commands,width,height,parameters);
    }
    std::array<float,4> ScenePresentation::Particle(const SceneWorld& world, const ScenePlacement& placement,
        unsigned int index, double seconds)
    {
        if (!placement.particleEmitter || index>=placement.particleEmitter->count || !std::isfinite(seconds)) return {};
        const auto& emitter=*placement.particleEmitter;
        const float phase=static_cast<float>(std::fmod(seconds/emitter.cycle+double(index)/emitter.count,1.0));
        const float wave=std::sin(phase*std::numbers::pi_v<float>);
        const float x=float((index*7)%emitter.count)/emitter.count;
        const float z=float((index*11)%emitter.count)/emitter.count;
        const std::array<float,3> local{
            x*emitter.extent[0]+phase*emitter.travel[0]+emitter.drift*wave,
            float((index*13)%emitter.count)/emitter.count*emitter.extent[1]+phase*emitter.travel[1],
            z*emitter.extent[2]+phase*emitter.travel[2]};
        DirectX::XMFLOAT4X4 transform;
        if (!world.WorldMatrix(placement.id,transform)) return {};
        DirectX::XMFLOAT3 position;
        DirectX::XMStoreFloat3(&position,DirectX::XMVector3TransformCoord(
            DirectX::XMVectorSet(local[0],local[1],local[2],1),DirectX::XMLoadFloat4x4(&transform)));
        return {position.x,position.y,position.z,emitter.color[3]*wave*wave};
    }
    void ScenePresentation::DrawParticles(ID3D12GraphicsCommandList* commands, const SceneWorld& world,
        const Engine::Camera& camera, double seconds) const
    {
        using namespace DirectX;
        auto billboard=XMMatrixInverse(nullptr,camera.GetViewMatrix());
        billboard.r[3]=XMVectorSet(0,0,0,1);
        const auto viewProjection=camera.GetViewMatrix()*camera.GetProjectionMatrix();
        for (const auto& placement : world.Layout().objects)
        {
            if (!placement.particleEmitter || !placement.particleEmitter->enabled) continue;
            const auto& emitter=*placement.particleEmitter;
            for (unsigned int index=0;index<emitter.count;++index)
            {
                const auto particle=Particle(world,placement,index,seconds);
                XMFLOAT4X4 matrix;
                XMStoreFloat4x4(&matrix,XMMatrixScaling(emitter.size,emitter.size,emitter.size)*billboard*
                    XMMatrixTranslation(particle[0],particle[1],particle[2])*viewProjection);
                particles_.Draw(commands,matrix,{emitter.color[0],emitter.color[1],emitter.color[2],particle[3]});
            }
        }
    }
    bool ScenePresentation::PrepareEffects(const SceneLayout& layout,std::string& error) const
    {
        if (!layout.settings.postEffects.enabled || postEffects_.Ready()) return true;
        if (!postEffects_.Initialize(device_.Get(),postShader_)) { error="Post effect pipeline could not be initialized"; return false; }
        return true;
    }
    void ScenePresentation::Draw(ID3D12GraphicsCommandList* commands, const SceneWorld& world,
        unsigned int width, unsigned int height, const Engine::Camera* sceneCamera, double seconds, bool motionEnabled, const UiState* uiState) const
    {
        if (!width || !height) return;
        bool processing=false;
        if (world.Layout().settings.postEffects.enabled)
        {
            std::string error; auto clear=world.Layout().settings.background;
            for (size_t channel=0;channel<3;++channel)
                clear[channel]=clear[channel]<=.04045f ? clear[channel]/12.92f : std::pow((clear[channel]+.055f)/1.055f,2.4f);
            processing=PrepareEffects(world.Layout(),error) && postEffects_.Begin(commands,width,height,clear);
            if (!processing) Engine::Log::Warning(error.empty() ? "Post effect target could not be prepared" : error);
        }
        { Engine::GpuScope gpu(commands,"Sky"); Engine::CpuScope cpu("Sky"); DrawSky(commands,world.Layout(),width,height,seconds); }
        Engine::Camera gameCamera;
        if (sceneCamera || SceneView::Camera(world,float(width)/height,seconds,gameCamera))
        {
            const auto& camera=sceneCamera ? *sceneCamera : gameCamera;
            world.Draw(commands,camera,SceneView::Light(world),uiState);
            if (motionEnabled) { Engine::GpuScope gpu(commands,"Particles"); Engine::CpuScope cpu("Particles"); DrawParticles(commands,world,camera,seconds); }
        }
        if (processing) { Engine::GpuScope gpu(commands,"Post effects"); Engine::CpuScope cpu("Post effects"); if (!postEffects_.End(commands,world.Layout().settings.postEffects)) Engine::Log::Warning("Post effect rendering failed"); }
        if (!sceneCamera) { Engine::GpuScope gpu(commands,"Game UI"); Engine::CpuScope cpu("Game UI"); DrawUi(commands,world.Layout(),width,height,uiState?*uiState:UiState{}); }
    }
}
