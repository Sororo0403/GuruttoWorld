#include <SceneRuntime/SceneEnvironment.h>
#include <SceneRuntime/Navigation.h>
#include <Engine/Core/Log.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Resources/Texture2D.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    struct CameraPose
    {
        std::array<float,3> position,rotation;
        float duration=0;
    };
    CameraPose SampleCameraFocus(const SceneRuntime::ScenePlacement& camera,const std::string& clock,float seconds,
        const std::array<float,3>& position,const std::array<float,3>& rotation)
    {
        CameraPose pose{camera.position,camera.rotation};
        for (auto track:camera.animation->tracks)
        {
            if (track.clock!=clock || track.keys.size()<2 ||
                (track.property!="position" && track.property!="rotation")) continue;
            const auto& source=track.property=="position" ? position : rotation;
            track.keys.front().value={source[0],source[1],source[2],0};
            const auto value=SceneRuntime::Animation::Sample(track,{{clock,seconds}});
            if (!value) continue;
            auto& target=track.property=="position" ? pose.position : pose.rotation;
            target={(*value)[0],(*value)[1],(*value)[2]};
            pose.duration=std::max(pose.duration,track.keys.back().time);
        }
        return pose;
    }
}

namespace SceneRuntime
{
    bool SceneEnvironment::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
        const std::filesystem::path& scenePath, std::string& error)
    {
        try { if(!Initialize(renderer,root,SceneLayout::Load(scenePath,root),error)) return false; scenes_.Reset(world_.Layout(),scenePath.is_absolute() ? scenePath.lexically_relative(root).generic_string() : scenePath.generic_string()); return true; }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }
    bool SceneEnvironment::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
        SceneLayout layout, std::string& error)
    {
        if (!presentation_.Initialize(renderer,root,error) ||
            !world_.Initialize(renderer,root,std::move(layout),root/"Shaders/Mesh.hlsl",&error) || !presentation_.PrepareUi(renderer,root,world_.Layout(),error)) return false;
        seconds_=0; motionEnabled_=true; uiState_=SceneUi::Defaults(world_.Layout());
        renderer_=&renderer; root_=root; scenes_.Reset(world_.Layout());
        uiSceneCommands_.clear();
        pendingUiResources_.reset();
        world_.SetRuntimePreparation([this](const SceneLayout& candidate,std::string& diagnostic) {
            return PrepareRuntimeUi(candidate,{},diagnostic);
        });
        world_.SetUiPreparation([this](const SceneLayout& candidate,const ScriptUiCommands& commands,std::string& diagnostic) {
            return PrepareRuntimeUi(candidate,commands,diagnostic);
        });
        SeekAnimation(0,0);
        error.clear();
        return true;
    }
    bool SceneEnvironment::PrepareRuntimeUi(const SceneLayout& layout,const ScriptUiCommands& commands,std::string& error)
    {
        if(!renderer_) {error="UI renderer is not initialized";return false;}
        auto candidate=uiPreparationOverride_?*uiPreparationOverride_:uiState_;
        for(const auto& [key,value]:commands.values) candidate.values[key]=value;
        for(const auto& [key,text]:commands.texts) candidate.strings[key]=text;
        auto resources=pendingUiResources_?*pendingUiResources_:presentation_.CaptureUi();
        if(!presentation_.PrepareUiCandidate(*renderer_,root_,layout,error,candidate,resources)) return false;
        pendingUiResources_=std::move(resources); return true;
    }
    void SceneEnvironment::ApplyUiCommands(ScriptUiCommands commands)
    {
        for(const auto& [key,value]:commands.values) uiState_.values[key]=value;
        for(const auto& [key,text]:commands.texts) uiState_.strings[key]=text;
        if(pendingUiResources_) presentation_.RestoreUi(std::move(*pendingUiResources_));
        pendingUiResources_.reset();
    }
    void SceneEnvironment::DiscardPreparedUi()
    {
        world_.DiscardUiCommands(); pendingUiResources_.reset();
    }
    void SceneEnvironment::Update(double deltaSeconds, bool enabled, bool active)
    {
        motionEnabled_=enabled;
        if (!active) DiscardPendingInput();
        if (!active || !std::isfinite(deltaSeconds) || deltaSeconds<=0) return;
        const bool physicsReady=physicsAttempted_ ? physicsSucceeded_ :
            !RequiresFixedUpdate(world_.Layout()) || world_.MovePlayers(deltaSeconds,0,0);
        physicsAttempted_=false;
        if (!physicsReady) { DiscardPreparedUi(); Engine::Log::Warning("FixedUpdate failed; frame update was skipped."); return; }
        if (!world_.UpdateComponents(deltaSeconds)) { DiscardPreparedUi(); Engine::Log::Warning("Component update failed; runtime clocks were preserved."); return; }
        ApplyUiCommands(world_.TakeUiCommands());
        ProcessSceneCommands();
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
            SetTransitionState(progress);
        }
        AnimateCameraFocus(elapsed);
    }
    void SceneEnvironment::SetTransitionState(float progress) {
        for(const auto& object:world_.Layout().objects) if(object.canvas && object.canvas->enabled && object.canvas->menu) {
            const auto defaults=uiState_.values;
            for(const auto& binding:object.canvas->menu->bindings) if(binding.source=="transition")
                uiState_.values[binding.key]=EvaluateMenuBinding(binding,progress,defaults);
            return;
        }
        uiState_.values["transition"]=progress;
        uiState_.values["transitionPink"]=std::min(1.0f,progress*uiState_.Value("transitionPinkScale",1.25f));
    }
    void SceneEnvironment::AnimateCameraFocus(float elapsed)
    {
        const auto* camera=SceneView::CameraObject(world_.Layout());
        const auto position=camera ? camera->position : std::array<float,3>{};
        const auto rotation=camera ? camera->rotation : std::array<float,3>{};
        const MenuConfiguration* menu=nullptr;
        for(const auto& object:world_.Layout().objects) if(object.canvas && object.canvas->enabled && object.canvas->menu) {menu=&*object.canvas->menu; break;}
        const float requested=uiState_.Value(menu?menu->focusState:"cameraFocus");
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
        if(!menu) return;
        const auto entry=std::find_if(menu->entries.begin(),menu->entries.end(),[&](const auto& e){return e.focus==focusRequested_;});
        if(entry==menu->entries.end() || entry->focusClock.empty()) return;
        const auto& clock=entry->focusClock;
        const auto pose=SampleCameraFocus(*camera,clock,focusSeconds_,focusPosition_,focusRotation_);
        if (!world_.SetLocalTransform(camera->id,pose.position,pose.rotation,camera->scale))
            Engine::Log::Warning("Camera focus rejected an invalid transform.");
        if (entry->view==0 && focusSeconds_>=pose.duration) focusEngaged_=false;
    }
    void SceneEnvironment::SeekAnimation(float sceneSeconds,float motionSeconds,float startSeconds)
    {
        if (!std::isfinite(sceneSeconds) || !std::isfinite(motionSeconds) || !std::isfinite(startSeconds) ||
            sceneSeconds<0 || motionSeconds<0) return;
        sceneSeconds_=sceneSeconds; seconds_=motionSeconds; startSeconds_=startSeconds;
        focusRequested_=0; focusEngaged_=false; focusSeconds_=0;
        uiState_.values["sceneTime"]=sceneSeconds;
        uiState_.values["motionTime"]=motionSeconds;
        uiState_.values["startTime"]=startSeconds;
        const float duration=std::clamp(uiState_.Value("startDuration",0.32f),0.1f,10.0f);
        const float progress=std::clamp(startSeconds/duration,0.0f,1.0f);
        SetTransitionState(progress);
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
        if(uiSceneCommands_.size()<64)
        {
            if(event.action=="loadSceneAdditive") uiSceneCommands_.push_back({event.target,true,false});
            if(event.action=="unloadScene") uiSceneCommands_.push_back({event.target,false,true});
        }
        return event;
    }
    bool SceneEnvironment::RequiresFixedUpdate(const SceneLayout& layout)
    {
        return ScriptRuntime::HasPhase(layout,ScriptPhase::FixedUpdate) || Navigation::HasAgents(layout) ||
            std::any_of(layout.objects.begin(),layout.objects.end(),[](const auto& object){
                return (object.ragdoll && !object.ragdoll->bones.empty()) || (object.rigidBody && object.rigidBody->enabled) ||
                    (object.playerController && object.playerController->enabled);
            });
    }
    void SceneEnvironment::ProcessSceneCommands()
    {
        auto commands=world_.TakeSceneCommands();
        commands.insert(commands.end(),uiSceneCommands_.begin(),uiSceneCommands_.end());uiSceneCommands_.clear();
        for(const auto& command:commands)
        {
            std::string error;
            const bool success=command.unload ? UnloadScene(command.scene,error) : LoadScene(command.scene,command.additive,error);
            if(!success) Engine::Log::Warning(error);
        }
    }
    bool SceneEnvironment::ApplySceneLayout(SceneLayout layout,SceneCollection scenes,std::string& error,bool resetUi)
    {
        if(!renderer_) {error="Scene environment is not initialized";return false;}
        Engine::Texture2D fenceTexture;
        if(!fenceTexture.Initialize(renderer_->GetDevice(),renderer_->GetCommandQueue(),{})) {error="Cannot synchronize scene resource release";return false;}
        auto candidateUi=SceneUi::SceneState(world_.Layout(),layout,uiState_,resetUi);
        const auto previousOverride=uiPreparationOverride_;
        struct RestorePreparation {const UiState*& target; const UiState* previous; ~RestorePreparation() {target=previous;}} restore{uiPreparationOverride_,previousOverride};
        uiPreparationOverride_=&candidateUi;
        auto previousResources=presentation_.CaptureUi();
        if(!presentation_.PrepareUi(*renderer_,root_,layout,error,candidateUi)) return false;
        if(!world_.ReplaceLayout(std::move(layout),root_,error,true))
        {
            pendingUiResources_.reset(); presentation_.RestoreUi(std::move(previousResources)); return false;
        }
        uiPreparationOverride_=previousOverride;
        scenes_=std::move(scenes); uiState_=std::move(candidateUi);
        ApplyUiCommands({});
        if(resetUi) world_.DiscardUiCommands();
        if(resetUi) {seconds_=0;sceneSeconds_=0;startSeconds_=-1;focusRequested_=0;focusEngaged_=false;focusSeconds_=0;}
        if(!audio_.Reconcile(root_,world_.Layout(),error)) Engine::Log::Warning(error);
        error.clear(); return true;
    }
    bool SceneEnvironment::LoadScene(const std::filesystem::path& scene,bool additive,std::string& error)
    {
        try
        {
            const auto relative=scene.lexically_normal();
            if(relative.is_absolute() || relative.has_root_name() || relative.extension()!=".json" ||
                !relative.generic_string().starts_with("Assets/Scenes/") ||
                std::any_of(relative.begin(),relative.end(),[](const auto& part){return part=="..";}))
                throw std::runtime_error("Invalid scene path");
            auto incoming=SceneLayout::Load(root_/relative,root_); auto collection=scenes_; auto layout=world_.Layout();
            std::set<std::string> reserved;
            if(!additive)
            {
                for(const auto& object:layout.objects) reserved.insert(object.id);
                const auto entries=collection.Entries(); for(const auto& entry:entries) layout=collection.Unload(layout,entry.name);
            }
            layout=collection.Add(layout,std::move(incoming),relative.generic_string(),!additive,reserved);
            return ApplySceneLayout(std::move(layout),std::move(collection),error,!additive);
        }
        catch(const std::exception& exception) {error=exception.what();return false;}
    }
    bool SceneEnvironment::UnloadScene(const std::string& name,std::string& error)
    {
        try {auto collection=scenes_; auto layout=collection.Unload(world_.Layout(),name);return ApplySceneLayout(std::move(layout),std::move(collection),error);}
        catch(const std::exception& exception) {error=exception.what();return false;}
    }
    UiEvent SceneEnvironment::UiPointer(unsigned int width,unsigned int height,float x,float y,bool down,bool pressed,bool released,float wheel)
    {
        const auto pressedObject=uiState_.pressed;
        const auto found=std::find_if(world_.Layout().objects.begin(),world_.Layout().objects.end(),[&](const auto& p){return p.id==pressedObject;});
        const bool button=found!=world_.Layout().objects.end() && found->button && found->button->enabled && !found->toggle && !found->inputField;
        auto event=SceneUi::Pointer(world_.Layout(),width,height,x,y,down,pressed,released && !button,wheel,uiState_);
        if(released && button) {
            if(pressedObject==uiState_.hovered) event=Click(pressedObject);
            uiState_.pressed.clear();
        }
        if(!event.event.empty()) {
            ScriptEvent scriptEvent; scriptEvent.name=event.event; scriptEvent.sender=event.object; scriptEvent.value=event.value; scriptEvent.text=event.text;
            std::string error; if(!QueueScriptEvent(std::move(scriptEvent),error)) Engine::Log::Warning(error);
        }
        return event;
    }
    UiEvent SceneEnvironment::UiTextInput(char32_t character)
    {
        auto event=SceneUi::TextInput(world_.Layout(),character,uiState_);
        if(!event.event.empty()) {
            ScriptEvent scriptEvent; scriptEvent.name=event.event; scriptEvent.sender=event.object; scriptEvent.value=event.value; scriptEvent.text=event.text;
            std::string error; if(!QueueScriptEvent(std::move(scriptEvent),error)) Engine::Log::Warning(error);
        }
        return event;
    }
    void SceneEnvironment::Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height, const Engine::Camera* sceneCamera, bool showSceneUi) const
    {
        presentation_.Draw(commands,world_,width,height,sceneCamera,seconds_,motionEnabled_,&uiState_,showSceneUi);
    }
}
