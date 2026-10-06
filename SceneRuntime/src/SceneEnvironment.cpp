#include <SceneRuntime/SceneEnvironment.h>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace SceneRuntime
{
    bool SceneEnvironment::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
        const std::filesystem::path& scenePath, std::string& error)
    {
        try { return Initialize(renderer,root,SceneLayout::Load(scenePath),error); }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }
    bool SceneEnvironment::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
        SceneLayout layout, std::string& error)
    {
        if (!presentation_.Initialize(renderer,root,error) ||
            !world_.Initialize(renderer,root,std::move(layout),root/"Shaders/Mesh.hlsl",&error) || !presentation_.PrepareUi(renderer,root,world_.Layout(),error)) return false;
        seconds_=0; motionEnabled_=true; uiState_=SceneUi::Defaults(world_.Layout());
        SeekAnimation(0,0);
        error.clear();
        return true;
    }
    void SceneEnvironment::Update(double deltaSeconds, bool enabled, bool active)
    {
        motionEnabled_=enabled;
        if (!active || !std::isfinite(deltaSeconds) || deltaSeconds<=0) return;
        if (!world_.UpdateComponents(deltaSeconds)) Engine::Log::Warning("Component update rejected an invalid inherited transform.");
        if (enabled) seconds_+=std::min(deltaSeconds,0.1);
        const float elapsed=static_cast<float>(std::min(deltaSeconds,0.1));
        sceneSeconds_+=elapsed;
        if (uiState_.Value("startRequested")==1 && startSeconds_<0) startSeconds_=0;
        else if (startSeconds_>=0) startSeconds_+=elapsed;
        uiState_.values["sceneTime"]=sceneSeconds_;
        uiState_.values["motionTime"]=static_cast<float>(seconds_);
        uiState_.values["startTime"]=startSeconds_;
        if (!world_.Animate(uiState_.values)) Engine::Log::Warning("Animation rejected an invalid transform.");
    }
    void SceneEnvironment::SeekAnimation(float sceneSeconds,float motionSeconds,float startSeconds)
    {
        if (!std::isfinite(sceneSeconds) || !std::isfinite(motionSeconds) || !std::isfinite(startSeconds) ||
            sceneSeconds<0 || motionSeconds<0) return;
        sceneSeconds_=sceneSeconds; seconds_=motionSeconds; startSeconds_=startSeconds;
        uiState_.values["sceneTime"]=sceneSeconds;
        uiState_.values["motionTime"]=motionSeconds;
        uiState_.values["startTime"]=startSeconds;
        world_.Animate(uiState_.values);
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
    UiEvent SceneEnvironment::Click(const std::string& object)
    {
        const auto event=SceneUi::Activate(world_.Layout(),object,uiState_);
        if(!event.sound.empty()) audio_.Play(event.sound);
        if(event.action=="playAudio") audio_.Play(event.target);
        return event;
    }
    void SceneEnvironment::Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height) const
    {
        presentation_.Draw(commands,world_,width,height,nullptr,seconds_,motionEnabled_,&uiState_);
    }
}
