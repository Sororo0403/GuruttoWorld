#include <SceneRuntime/SceneEnvironment.h>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace SceneRuntime
{
    bool SceneEnvironment::Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
        const std::filesystem::path& scenePath, std::string& error)
    {
        try { return Initialize(renderer,root,SceneLayout::Load(scenePath),error); }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }
    bool SceneEnvironment::Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
        SceneLayout layout, std::string& error)
    {
        if (!presentation_.Initialize(renderer,root,error) ||
            !world_.Initialize(renderer,root,std::move(layout),root/"Shaders/TitleMesh.hlsl",&error)) return false;
        seconds_=0; motionEnabled_=true;
        error.clear();
        return true;
    }
    void SceneEnvironment::Update(double deltaSeconds, bool enabled, bool active)
    {
        motionEnabled_=enabled;
        if (!active || !std::isfinite(deltaSeconds) || deltaSeconds<=0) return;
        if (!world_.UpdateComponents(deltaSeconds)) Engine::Log::Warning("Component update rejected an invalid inherited transform.");
        if (enabled) seconds_+=std::min(deltaSeconds,0.1);
    }
    std::array<float,3> SceneEnvironment::CameraPosition() const
    {
        Engine::Camera camera;
        return SceneView::Camera(world_,16.0f/9.0f,seconds_,camera) ? camera.GetPosition() : std::array<float,3>{};
    }
    std::array<float,4> SceneEnvironment::Particle(unsigned int index) const
    {
        const auto& objects=world_.Layout().objects;
        const auto found=std::find_if(objects.begin(),objects.end(),[](const auto& placement) {
            return placement.particleEmitter && placement.particleEmitter->enabled;
        });
        return found==objects.end() ? std::array<float,4>{} : ScenePresentation::Particle(world_,*found,index,seconds_);
    }
    void SceneEnvironment::Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height) const
    {
        presentation_.Draw(commands,world_,width,height,nullptr,seconds_,motionEnabled_);
    }
}
