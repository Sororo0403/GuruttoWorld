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
        if (startSeconds_>=0)
        {
            const float duration=std::clamp(uiState_.Value("startDuration",0.32f),0.1f,10.0f);
            const float progress=std::clamp(startSeconds_/duration,0.0f,1.0f);
            uiState_.values["transition"]=progress;
            uiState_.values["transitionPink"]=std::min(1.0f,progress*1.25f);
        }
        AnimateCameraFocus(elapsed);
    }
    void SceneEnvironment::AnimateCameraFocus(float elapsed)
    {
        const auto* camera=SceneView::CameraObject(world_.Layout());
        const auto position=camera ? camera->position : std::array<float,3>{};
        const auto rotation=camera ? camera->rotation : std::array<float,3>{};
        const bool requested=uiState_.Value("cameraFocus")==1;
        if (requested!=focusRequested_)
        {
            focusRequested_=requested; focusEngaged_=true; focusSeconds_=0;
            focusPosition_=position; focusRotation_=rotation;
        }
        else focusSeconds_+=elapsed;
        if (!world_.Animate(uiState_.values)) Engine::Log::Warning("Animation rejected an invalid transform.");
        if (!focusEngaged_ || startSeconds_>=0) return;
        camera=SceneView::CameraObject(world_.Layout());
        if (!camera || !camera->animation || !camera->animation->enabled) return;
        auto targetPosition=camera->position,targetRotation=camera->rotation;
        float duration=0;
        const std::string clock=focusRequested_ ? "configFocusTime" : "homeFocusTime";
        for (auto track:camera->animation->tracks)
        {
            if (track.clock!=clock || track.keys.size()<2) continue;
            const auto& source=track.property=="position" ? focusPosition_ : focusRotation_;
            track.keys.front().value={source[0],source[1],source[2],0};
            const auto value=Animation::Sample(track,{{clock,focusSeconds_}});
            if (!value) continue;
            auto& target=track.property=="position" ? targetPosition : targetRotation;
            target={(*value)[0],(*value)[1],(*value)[2]};
            duration=std::max(duration,track.keys.back().time);
        }
        if (!world_.SetLocalTransform(camera->id,targetPosition,targetRotation,camera->scale))
            Engine::Log::Warning("Camera focus rejected an invalid transform.");
        if (!focusRequested_ && focusSeconds_>=duration) focusEngaged_=false;
    }
    void SceneEnvironment::SeekAnimation(float sceneSeconds,float motionSeconds,float startSeconds)
    {
        if (!std::isfinite(sceneSeconds) || !std::isfinite(motionSeconds) || !std::isfinite(startSeconds) ||
            sceneSeconds<0 || motionSeconds<0) return;
        sceneSeconds_=sceneSeconds; seconds_=motionSeconds; startSeconds_=startSeconds;
        focusRequested_=false; focusEngaged_=false; focusSeconds_=0;
        uiState_.values["sceneTime"]=sceneSeconds;
        uiState_.values["motionTime"]=motionSeconds;
        uiState_.values["startTime"]=startSeconds;
        const float duration=std::clamp(uiState_.Value("startDuration",0.32f),0.1f,10.0f);
        const float progress=std::clamp(startSeconds/duration,0.0f,1.0f);
        uiState_.values["transition"]=progress;
        uiState_.values["transitionPink"]=std::min(1.0f,progress*1.25f);
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
