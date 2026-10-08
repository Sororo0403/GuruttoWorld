#include <SceneRuntime/SceneWorld.h>
#include <SceneRuntime/SceneTransforms.h>
#include <SceneRuntime/SceneUi.h>
#include <SceneRuntime/Prefab.h>
#include <SceneRuntime/MaterialAsset.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Log.h>
#include <format>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <unordered_map>
#include <Engine/Graphics/Renderers/ModelRenderer.h>
#include <Engine/Graphics/DirectX12/GpuSynchronization.h>

namespace
{
    bool HasTransformAnimation(const SceneRuntime::ScenePlacement& placement)
    {
        return placement.animation && placement.animation->enabled &&
            std::any_of(placement.animation->tracks.begin(),placement.animation->tracks.end(),[](const auto& track) {
                return track.property=="position" || track.property=="rotation";
            });
    }
    bool ApplyTransformAnimation(SceneRuntime::ScenePlacement& placement,const std::map<std::string,float>& clocks)
    {
        if (!HasTransformAnimation(placement)) return false;
        const auto position=placement.position,rotation=placement.rotation;
        for (const auto& track:placement.animation->tracks)
        {
            if (track.property!="position" && track.property!="rotation") continue;
            const auto clock=clocks.find(track.clock);
            if (clock==clocks.end() || clock->second<track.delay) continue;
            const auto value=SceneRuntime::Animation::Sample(track,clocks);
            if (!value) continue;
            auto& target=track.property=="position" ? placement.position : placement.rotation;
            target={(*value)[0],(*value)[1],(*value)[2]};
        }
        return position!=placement.position || rotation!=placement.rotation;
    }
    bool IsRotation(const DirectX::XMFLOAT4X4& rotation)
    {
        if (!SceneRuntime::SceneTransforms::IsUsable(rotation)) return false;
        const auto matrix=DirectX::XMLoadFloat4x4(&rotation);
        DirectX::XMFLOAT4X4 product,identity;
        DirectX::XMStoreFloat4x4(&product,matrix*DirectX::XMMatrixTranspose(matrix));
        DirectX::XMStoreFloat4x4(&identity,DirectX::XMMatrixIdentity());
        return DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(matrix))>0 &&
            SceneRuntime::SceneTransforms::Matches(product,identity);
    }

    bool IsIdentity(const DirectX::XMFLOAT4X4& matrix)
    {
        for (size_t row=0;row<4;++row)
            for (size_t column=0;column<4;++column)
                if (matrix.m[row][column]!=(row==column ? 1.0f : 0.0f)) return false;
        return true;
    }

    bool HasSelectedAncestor(const SceneRuntime::SceneLayout& layout, std::string parent,
        const std::vector<std::string>& ids)
    {
        while (!parent.empty())
        {
            if (std::find(ids.begin(),ids.end(),parent)!=ids.end()) return true;
            const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),
                [&](const auto& placement) { return placement.id==parent; });
            if (found==layout.objects.end()) return false;
            parent=found->parentId;
        }
        return false;
    }
}

namespace SceneRuntime
{
    bool SceneWorld::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
        const std::filesystem::path& layoutPath, const std::filesystem::path& shaderPath, std::string* diagnostic)
    {
        if (!InitializeModels(renderer,shaderPath,diagnostic)) return false;
        std::string error;
        const bool loaded = Reload(assetsRoot, layoutPath, error);
        if (diagnostic) *diagnostic = error;
        return loaded;
    }

    bool SceneWorld::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
        SceneLayout layout, const std::filesystem::path& shaderPath, std::string* diagnostic)
    {
        if (!InitializeModels(renderer,shaderPath,diagnostic)) return false;
        std::string error;
        bool loaded=false;
        try { layout.ResolveAssets(assetsRoot); Prefab::Refresh(layout,assetsRoot); loaded=ReplaceLayout(std::move(layout),assetsRoot,error); }
        catch (const std::exception& exception) { error=exception.what(); }
        if (diagnostic) *diagnostic=error;
        return loaded;
    }

    bool SceneWorld::InitializeModels(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& shaderPath,
        std::string* diagnostic)
    {
        materialDevice_=renderer.GetDevice(); materialQueue_=renderer.GetCommandQueue();
        modelsReady_ = models_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), shaderPath) &&
            shadow_.Initialize(renderer.GetDevice(),shaderPath);
        if (!modelsReady_)
        {
            if (diagnostic) *diagnostic = "Scene renderer could not be initialized. Check shaders and restart.";
            return false;
        }
        return true;
    }
    bool SceneWorld::Animate(const std::map<std::string, float>& clocks)
    {
        if (std::none_of(layout_.objects.begin(),layout_.objects.end(),HasTransformAnimation)) return true;
        auto candidate=layout_;
        bool changed=false;
        for (auto& placement:candidate.objects) changed=ApplyTransformAnimation(placement,clocks) || changed;
        return !changed || CommitTransforms(std::move(candidate));
    }

    bool SceneWorld::Reload(const std::filesystem::path& assetsRoot, const std::filesystem::path& layoutPath,
        std::string& error)
    {
        try { auto layout=SceneLayout::Load(layoutPath,assetsRoot); Prefab::Refresh(layout,assetsRoot); return ReplaceLayout(std::move(layout), assetsRoot, error); }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool SceneWorld::ReloadAssets(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
        const std::filesystem::path& shaderPath, std::string& error)
    {
        SceneWorld candidate;
        if (!candidate.InitializeModels(renderer,shaderPath,&error) || !candidate.ReplaceLayout(layout_,assetsRoot,error)) return false;
        models_.Swap(candidate.models_);
        layout_=std::move(candidate.layout_); objects_.swap(candidate.objects_); animatedModels_.swap(candidate.animatedModels_); animatorStates_.swap(candidate.animatorStates_);
        std::swap(shadow_,candidate.shadow_);
        error.clear();
        return true;
    }

    bool SceneWorld::ReplaceLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error,bool preserveExecution)
    {
        PreparedLayout prepared;
        if (!PrepareLayout(std::move(layout),assetsRoot,error,preserveExecution,prepared)) return false;
        if (preserveExecution && prepareRuntime_ && !prepareRuntime_(prepared.layout,error)) return false;
        CommitLayout(std::move(prepared),preserveExecution); error.clear(); return true;
    }
    bool SceneWorld::PrepareLayout(SceneLayout layout,const std::filesystem::path& assetsRoot,std::string& error,bool preserveExecution,PreparedLayout& result)
    {
        if (!modelsReady_)
        {
            error = "Scene renderer is unavailable. Check shaders and restart.";
            return false;
        }
        try
        {
            layout.ResolveAssets(assetsRoot);
            static_cast<void>(layout.Serialize());
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(layout,matrices,error)) throw std::runtime_error(error);
            std::vector<Engine::Object3D> objects;
            objects.reserve(layout.objects.size());
            std::map<std::filesystem::path,std::shared_ptr<const Engine::Material>> materials;
            std::map<std::string,std::shared_ptr<Engine::ModelRenderer>> animated;
            std::map<std::string,AnimatorState> animatorStates;
            for (const auto& placement : layout.objects)
            {
                Engine::Object3D object;
                if (placement.animator && !placement.meshRenderer) throw std::runtime_error("Animator needs a MeshRenderer");
                if (placement.meshRenderer)
                {
                    const auto model = models_.Load(assetsRoot / placement.Model());
                    if (!model) throw std::runtime_error("Model could not be loaded: " + placement.id);
                    object.SetModel(model);
                    if (placement.animator)
                    {
                        if (!model->Rig()) throw std::runtime_error("Animator needs a glTF or GLB model");
                        Animator::Validate(*placement.animator,model->Rig().get());
                        const auto old=std::find_if(layout_.objects.begin(),layout_.objects.end(),[&](const auto& item) { return item.id==placement.id; });
                        const bool reuse=preserveExecution && old!=layout_.objects.end() && old->meshRenderer==placement.meshRenderer && old->animator==placement.animator && animatedModels_.contains(placement.id);
                        auto instance=reuse ? animatedModels_.at(placement.id) : model->AnimatedCopy(materialDevice_.Get(),materialQueue_.Get());
                        if (reuse) animatorStates[placement.id]=animatorStates_.at(placement.id);
                        if (!instance || (!reuse && !instance->ApplyPose(Animator::Advance(*placement.animator,animatorStates[placement.id],*model->Rig(),0,{},&matrices[objects.size()])))) throw std::runtime_error("Animator initialization failed");
                        object.SetModel(instance); animated[placement.id]=std::move(instance);
                    }
                    const auto& material=placement.meshRenderer->material;
                    if (!material.empty())
                    {
                        auto found=materials.find(material);
                        if (found==materials.end()) found=materials.emplace(material,MaterialAsset::Load(assetsRoot,material).Prepare(materialDevice_.Get(),materialQueue_.Get(),assetsRoot)).first;
                        object.SetMaterial(found->second);
                    }
                }
                if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
                    throw std::runtime_error("Invalid transform: " + placement.id);
                if (!object.SetWorldMatrix(matrices[objects.size()])) throw std::runtime_error("Invalid world matrix");
                objects.push_back(std::move(object));
            }
            PreparedLayout prepared; prepared.assetsRoot=assetsRoot; prepared.layout=std::move(layout);
            prepared.animated=std::move(animated); prepared.states=std::move(animatorStates); prepared.objects=std::move(objects);
            result=std::move(prepared);
            error.clear();
            return true;
        }
        catch (const std::exception& errorException)
        {
            error = std::string("Scene could not be loaded: ") + errorException.what();
            Engine::Log::Error(error);
            return false;
        }
    }
    void SceneWorld::CommitLayout(PreparedLayout&& prepared,bool preserveExecution)
    {
        if (!preserveExecution) scripts_.Stop(layout_);
        layout_=std::move(prepared.layout); assetsRoot_=std::move(prepared.assetsRoot);
        if (!preserveExecution) { physics_.clear(); physicsWorld_.Reset(); }
        else std::erase_if(physics_,[&](const auto& pair) { return std::none_of(layout_.objects.begin(),layout_.objects.end(),[&](const auto& item) { return item.id==pair.first; }); });
        animatedModels_=std::move(prepared.animated); animatorStates_=std::move(prepared.states); objects_=std::move(prepared.objects);
    }

    bool SceneWorld::PrepareAnimators(const SceneLayout& layout,double seconds,const std::map<std::string,std::shared_ptr<Engine::ModelRenderer>>& models,
        const std::map<std::string,AnimatorState>& states,ScriptRuntime& scripts,std::vector<AnimatorFrame>& result,std::string& error) const
    {
        try
        {
            std::vector<AnimatorFrame> frames;
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(layout,matrices,error)) throw std::runtime_error(error);
            for (size_t index=0;index<layout.objects.size();++index)
            {
                const auto& placement=layout.objects[index];
                if (!placement.animator) continue;
                const auto model=models.find(placement.id); if (model==models.end()) throw std::runtime_error("Animator model missing");
                AnimatorFrame frame; frame.id=placement.id; frame.index=index; frame.model=model->second; frame.state=states.at(placement.id);
                if (const auto changes=scripts.AnimatorParameters().find(placement.id);changes!=scripts.AnimatorParameters().end())
                    for (const auto& [name,value] : changes->second) frame.state.parameterOverrides[name]=value;
                Animator::ValidateParameters(frame.state.parameterOverrides);
                if (const auto changes=scripts.IkTargets().find(placement.id);changes!=scripts.IkTargets().end())
                    for (const auto& [name,target] : changes->second)
                    { if (target) frame.state.ikOverrides[name]=*target; else frame.state.ikOverrides.erase(name); }
                auto parameters=inputValues_;
                const auto input=[&](const char* name) { const auto found=inputValues_.find(name); return found==inputValues_.end() ? 0.0f : found->second; };
                parameters["moveX"]=input("MoveRight")-input("MoveLeft"); parameters["moveY"]=input("MoveForward")-input("MoveBack");
                parameters["speed"]=std::hypot(parameters["moveX"],parameters["moveY"]);
                const auto body=physics_.find(placement.id); parameters["grounded"]=body==physics_.end() ? 1.0f : (body->second.grounded ? 1.0f : 0.0f);
                for (const auto& [name,pressed] : inputPressed_) parameters["pressed:"+name]=pressed ? 1.0f : 0.0f;
                const auto pose=Animator::Advance(*placement.animator,frame.state,*frame.model->Rig(),seconds,parameters,&matrices[index]);
                frame.applyPose=placement.animator->enabled;
                if (frame.applyPose && !frame.model->PreparePose(pose,frame.pose)) throw std::runtime_error("Animator pose preparation failed");
                for (const auto& occurrence : frame.state.events)
                {
                    ScriptEvent event{occurrence.name,placement.id,placement.id,occurrence.value}; event.animation=occurrence;
                    scripts.QueueEvent(std::move(event));
                }
                frames.push_back(std::move(frame));
            }
            result=std::move(frames); error.clear(); return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }

    void SceneWorld::Draw(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
        const Engine::DirectionalLight& light, const UiState* state) const
    {
        meshTelemetry_={};
        const auto count=[&](size_t index) { const auto& model=objects_[index].GetModel(); if (model) { meshTelemetry_.draws+=model->MeshCount(); meshTelemetry_.triangles+=model->TriangleCount(); } };
        const auto defaults=state ? UiState{} : SceneUi::Defaults(layout_);
        const auto& values=state ? *state : defaults;
        const auto visible=[&](size_t index) {
            const auto& mesh=layout_.objects[index].meshRenderer;
            return mesh && mesh->enabled && values.Matches(mesh->visibleWhen);
        };
        const auto transparent=[&](size_t index) { const auto& material=objects_[index].GetMaterial(); return material && (material->transparent || material->color[3]<1); };
        auto lighting=light;
        lighting.shadow=nullptr;
        if (light.enabled && light.shadowsEnabled && shadow_.Begin(commands,camera,light))
        {
            for (size_t index=0;index<objects_.size();++index)
                if (visible(index) && !transparent(index) && objects_[index].GetModel())
                    { count(index); objects_[index].GetModel()->DrawShadow(commands,objects_[index].GetWorldMatrix(),shadow_); }
            shadow_.End(commands);
            lighting.shadow=&shadow_;
        }
        std::vector<size_t> blended;
        for (size_t index=0;index<objects_.size();++index)
            if (visible(index))
            {
                if (transparent(index)) blended.push_back(index);
                else { count(index); objects_[index].Draw(commands,camera,lighting); }
            }
        const auto distance=[&](size_t index) {
            const auto& world=objects_[index].GetWorldMatrix(); const auto& eye=camera.GetPosition();
            const float x=world._41-eye[0],y=world._42-eye[1],z=world._43-eye[2]; return x*x+y*y+z*z;
        };
        std::stable_sort(blended.begin(),blended.end(),[&](size_t first,size_t second) { return distance(first)>distance(second); });
        for (const auto index : blended) { count(index); objects_[index].Draw(commands,camera,lighting); }
    }

    bool SceneWorld::PrepareTransforms(const SceneLayout& layout, std::vector<Engine::Object3D>& objects, std::string& error)
    {
        std::vector<DirectX::XMFLOAT4X4> matrices;
        if (!SceneTransforms::Resolve(layout,matrices,error)) return false;
        if (objects.size()!=matrices.size()) { error="Placement and draw object counts differ"; return false; }
        for (size_t index=0;index<objects.size();++index)
        {
            const auto& placement=layout.objects[index];
            if (!objects[index].SetTransform(placement.position,placement.rotation,placement.scale) ||
                !objects[index].SetWorldMatrix(matrices[index]))
            { error=placement.id+": invalid inherited transform"; return false; }
        }
        error.clear();
        return true;
    }

    bool SceneWorld::ReparentPlacement(ScenePlacement& placement, std::string parentId, std::string& error) const
    {
        DirectX::XMFLOAT4X4 matrix;
        if (!WorldMatrix(placement.id,matrix)) { error=placement.id+": object no longer exists"; return false; }
        if (!parentId.empty())
        {
            DirectX::XMFLOAT4X4 parent;
            if (!WorldMatrix(parentId,parent) || !SceneTransforms::WorldToLocal(matrix,parent,matrix))
            { error=placement.id+": invalid parent transform"; return false; }
        }
        auto candidate=placement;
        if (!SceneTransforms::ReadTransform(matrix,placement,candidate))
        { error=placement.id+": preserving world placement requires shear that cannot be stored as SRT"; return false; }
        candidate.parentId=std::move(parentId);
        placement=std::move(candidate);
        error.clear();
        return true;
    }

    bool SceneWorld::TranslatePlacement(ScenePlacement& placement, const std::array<float,3>& delta) const
    {
        if (!std::all_of(delta.begin(),delta.end(),[](float value) { return std::isfinite(value); })) return false;
        auto movement=DirectX::XMVectorSet(delta[0],delta[1],delta[2],0);
        if (!placement.parentId.empty())
        {
            DirectX::XMFLOAT4X4 parent;
            if (!WorldMatrix(placement.parentId,parent)) return false;
            movement=DirectX::XMVector3TransformNormal(movement,
                DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&parent)));
        }
        DirectX::XMFLOAT3 local;
        DirectX::XMStoreFloat3(&local,movement);
        placement.position[0]+=local.x; placement.position[1]+=local.y; placement.position[2]+=local.z;
        DirectX::XMFLOAT4X4 matrix;
        return SceneTransforms::Compose(placement,matrix);
    }

    bool SceneWorld::CommitTransforms(SceneLayout candidate)
    {
        auto objects=objects_;
        std::string error;
        if (!PrepareTransforms(candidate,objects,error)) return false;
        for (size_t index=0;index<layout_.objects.size();++index)
        {
            auto& target=layout_.objects[index];
            const auto& source=candidate.objects[index];
            target.position=source.position; target.rotation=source.rotation; target.scale=source.scale;
            target.parentId=std::move(candidate.objects[index].parentId);
        }
        objects_=std::move(objects);
        return true;
    }

    bool SceneWorld::SetParent(std::string_view id, std::string parentId, std::string& error)
    {
        try
        {
            auto candidate=layout_;
            const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
                [&](const auto& object) { return object.id==id; });
            if (found==candidate.objects.end()) throw std::runtime_error("Object no longer exists");
            if (found->parentId==parentId) { error.clear(); return true; }
            auto validation=candidate;
            validation.objects[static_cast<size_t>(found-candidate.objects.begin())].parentId=parentId;
            static_cast<void>(validation.Serialize());
            if (!ReparentPlacement(*found,std::move(parentId),error)) return false;
            if (!CommitTransforms(std::move(candidate))) throw std::runtime_error("Invalid inherited transform");
            error.clear();
            return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }

    bool SceneWorld::SetSettings(const SceneSettings& settings, std::string& error)
    {
        try
        {
            auto candidate=layout_;
            candidate.settings=settings;
            static_cast<void>(candidate.Serialize());
            layout_.settings=settings;
            error.clear();
            return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }

    bool SceneWorld::SetComponents(std::string_view id, const ScenePlacement& settings,
        const std::filesystem::path& assetsRoot, std::string& error)
    {
        try
        {
            auto candidate=layout_;
            const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
                [&](const auto& placement) { return placement.id==id; });
            if (found==candidate.objects.end()) throw std::runtime_error("Component owner no longer exists");
            found->CopyComponents(settings);
            if (!found->camera && candidate.settings.mainCamera==id) candidate.settings.mainCamera.clear();
            static_cast<void>(candidate.Serialize());
            const auto index=static_cast<size_t>(found-candidate.objects.begin());
            if (found->meshRenderer==layout_.objects[index].meshRenderer && found->animator==layout_.objects[index].animator)
            {
                layout_.objects[index].CopyComponents(*found);
                layout_.settings=candidate.settings;
                error.clear();
                return true;
            }
            return ReplaceLayout(std::move(candidate),assetsRoot,error,true);
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }

    bool SceneWorld::AddImpulse(const std::string& id,const std::array<float,3>& impulse)
    {
        const auto object=std::find_if(layout_.objects.begin(),layout_.objects.end(),[&](const auto& item) { return item.id==id; });
        return object!=layout_.objects.end() && object->rigidBody && object->rigidBody->enabled && object->rigidBody->motion=="dynamic" && physicsWorld_.AddImpulse(id,impulse);
    }
    bool SceneWorld::MovePlayers(double seconds, float horizontal, float vertical, bool jump)
    {
        auto candidate=layout_;
        auto states=physics_;
        if (!ScenePhysics::Advance(candidate,states,seconds,horizontal,vertical,jump)) return false;
        std::string error;
        if (!physicsWorld_.Advance(candidate,states,seconds,horizontal,vertical,jump,assetsRoot_,error))
        { Engine::Log::Warning(error); return false; }
        if (!CommitTransforms(std::move(candidate))) return false;
        physics_=std::move(states);
        std::set<std::string> listeners;
        for (const auto& object : layout_.objects) for (const auto& script : object.scripts) if (script.enabled)
        {
            const auto definition=ScriptRegistry::Definitions().find(script.behaviour);
            if (definition!=ScriptRegistry::Definitions().end() && definition->second.onEvent) listeners.insert(object.id);
        }
        for (const auto& contact : physicsWorld_.Events())
        {
            const auto name=std::string(contact.trigger ? "trigger" : "collision")+contact.phase;
            try
            {
                if (listeners.contains(contact.second)) scripts_.QueueEvent({name,contact.first,contact.second,0});
                if (listeners.contains(contact.first)) scripts_.QueueEvent({name,contact.second,contact.first,0});
            }
            catch (const std::exception& exception) { Engine::Log::Warning(exception.what()); break; }
        }
        return true;
    }

    bool SceneWorld::QueueScriptEvent(ScriptEvent event,std::string& error)
    {
        try { scripts_.QueueEvent(std::move(event)); error.clear(); return true; }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }

    bool SceneWorld::UpdateComponents(double seconds)
    {
        if (!std::isfinite(seconds) || seconds<=0) return false;
        auto candidate=layout_;
        for (auto& placement : candidate.objects)
        {
            if (!placement.rotator || !placement.rotator->enabled) continue;
            for (size_t axis=0;axis<3;++axis)
            {
                const double speed=placement.rotator->angularVelocity[axis];
                if (speed==0) continue;
                const double elapsed=std::remainder(seconds,360.0/std::abs(speed));
                placement.rotation[axis]=static_cast<float>(std::remainder(static_cast<double>(placement.rotation[axis])+
                    elapsed*speed*DirectX::XM_PI/180.0,static_cast<double>(DirectX::XM_2PI)));
            }
        }
        auto runtime=scripts_;
        std::string error;
        if (!runtime.Update(candidate,seconds,error,inputValues_,inputPressed_,&physicsWorld_)) { Engine::Log::Warning(error); return false; }
        const bool rebuild=candidate.objects.size()!=layout_.objects.size() ||
            !std::equal(candidate.objects.begin(),candidate.objects.end(),layout_.objects.begin(),[](const auto& a,const auto& b) {
                return a.id==b.id && a.meshRenderer==b.meshRenderer && a.animator==b.animator;
            });
        PreparedLayout prepared;
        if (rebuild)
        {
            Microsoft::WRL::ComPtr<ID3D12Fence> fence;
            if (FAILED(materialDevice_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))) ||
                !Engine::SignalGpuFence(materialDevice_.Get(),materialQueue_.Get(),fence.Get(),1) ||
                !Engine::WaitForGpuFence(materialDevice_.Get(),fence.Get(),1,nullptr)) return false;
            if (!PrepareLayout(std::move(candidate),assetsRoot_,error,true,prepared)) { Engine::Log::Warning(error); return false; }
        }
        else
        {
            prepared.layout=std::move(candidate); prepared.objects=objects_;
            if (!PrepareTransforms(prepared.layout,prepared.objects,error)) return false;
        }
        std::vector<AnimatorFrame> frames;
        if (!PrepareAnimators(prepared.layout,seconds,rebuild ? prepared.animated : animatedModels_,rebuild ? prepared.states : animatorStates_,runtime,frames,error))
        { Engine::Log::Warning(error); return false; }
        if (!PrepareRootMotion(prepared,frames,error)) { Engine::Log::Warning(error); return false; }
        if (prepareRuntime_ && !prepareRuntime_(prepared.layout,error)) { Engine::Log::Warning(error); return false; }
        if (rebuild) CommitLayout(std::move(prepared),true);
        else { layout_=std::move(prepared.layout); objects_=std::move(prepared.objects); }
        for (auto& frame : frames)
        {
            if (frame.applyPose) frame.model->ApplyPreparedPose(std::move(frame.pose));
            animatorStates_.at(frame.id)=std::move(frame.state);
        }
        for (const auto& [id,impulse] : runtime.Impulses()) physicsWorld_.AddImpulse(id,impulse);
        scripts_=std::move(runtime);
        return true;
    }

    bool SceneWorld::PrepareRootMotion(PreparedLayout& prepared,std::vector<AnimatorFrame>& frames,std::string& error) const
    {
        try
        {
            bool moved=false;
            for (const auto& frame : frames)
            {
                auto& placement=prepared.layout.objects.at(frame.index);
                if (!frame.applyPose || !placement.animator->rootMotion) continue;
                if (frame.state.rootDelta.position==std::array<float,3>{} && frame.state.rootDelta.rotation==std::array<float,4>{0,0,0,1}) continue;
                const auto delta=Animator::RootDeltaMatrix(*placement.animator,frame.state,*frame.model->Rig());
                DirectX::XMFLOAT4X4 local,changed;
                if (!SceneTransforms::Compose(placement,local)) throw std::runtime_error("Invalid root motion object transform");
                DirectX::XMStoreFloat4x4(&changed,DirectX::XMLoadFloat4x4(&delta)*DirectX::XMLoadFloat4x4(&local));
                ScenePlacement result;
                if (!SceneTransforms::ReadTransform(changed,placement,result)) throw std::runtime_error("Root motion cannot be represented by object SRT");
                placement.position=result.position; placement.rotation=result.rotation; placement.scale=result.scale; moved=true;
            }
            if (!moved) return true;
            if (!PrepareTransforms(prepared.layout,prepared.objects,error)) return false;
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(prepared.layout,matrices,error)) return false;
            for (auto& frame : frames) if (frame.applyPose)
            {
                const auto& placement=prepared.layout.objects.at(frame.index);
                frame.state.pose=Animator::Constrain(*placement.animator,frame.state,*frame.model->Rig(),&matrices[frame.index]);
                if (!frame.model->PreparePose(frame.state.pose,frame.pose)) throw std::runtime_error("Root motion constraint pose failed");
            }
            error.clear(); return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }
    bool SceneWorld::SetAnimatorParameter(const std::string& id,const std::string& name,float value)
    {
        const auto found=animatorStates_.find(id); if (found==animatorStates_.end()) return false;
        auto values=found->second.parameterOverrides; values[name]=value;
        try { Animator::ValidateParameters(values); }
        catch (const std::exception&) { return false; }
        found->second.parameterOverrides=std::move(values); return true;
    }
    bool SceneWorld::ClearAnimatorParameter(const std::string& id,const std::string& name)
    {
        const auto found=animatorStates_.find(id); return found!=animatorStates_.end() && found->second.parameterOverrides.erase(name)>0;
    }

    bool SceneWorld::RenameObject(std::string_view id, std::string name)
    {
        if (name.find_first_not_of(" \t\r\n")==std::string::npos || name.find('\0')!=std::string::npos) return false;
        const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
            [id](const ScenePlacement& object) { return object.id==id; });
        if (found==layout_.objects.end()) return false;
        found->name=std::move(name);
        return true;
    }

    bool SceneWorld::SetPlacementTransform(std::string_view id, const ScenePlacement& placement)
    {
        auto candidate=layout_;
        const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
            [&](const auto& value) { return value.id==id; });
        if (found==candidate.objects.end()) return false;
        found->position=placement.position;
        found->rotation=placement.rotation;
        found->scale=placement.scale;
        return CommitTransforms(std::move(candidate));
    }

    bool SceneWorld::SetLocalTransform(std::string_view id, const std::array<float,3>& position,
        const std::array<float,3>& rotation, const std::array<float,3>& scale)
    {
        const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
            [&](const auto& value) { return value.id==id; });
        if (found==layout_.objects.end()) return false;
        auto placement=*found;
        placement.position=position; placement.rotation=rotation; placement.scale=scale;
        return SetPlacementTransform(id,placement);
    }

    bool SceneWorld::SetWorldTransform(std::string_view id, const std::array<float,3>& position,
        const std::array<float,3>& rotation, const std::array<float,3>& scale)
    {
        ScenePlacement placement;
        placement.position=position; placement.rotation=rotation; placement.scale=scale;
        DirectX::XMFLOAT4X4 matrix;
        if (!SceneTransforms::Compose(placement,matrix)) return false;
        return SetWorldTransform(id,matrix);
    }

    bool SceneWorld::SetWorldTransform(std::string_view id, const DirectX::XMFLOAT4X4& matrix)
    {
        ScenePlacement placement;
        if (!LocalTransformFromWorld(id,matrix,placement)) return false;
        return SetPlacementTransform(id,placement);
    }

    bool SceneWorld::TranslateObjectsWorld(const std::vector<std::string>& ids, const std::array<float,3>& delta)
    {
        if (ids.empty() || !std::all_of(delta.begin(),delta.end(),[](float value) { return std::isfinite(value); })) return false;
        auto candidate=layout_;
        std::vector<std::string> seen;
        for (const auto& id : ids)
        {
            const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
                [&](const auto& placement) { return placement.id==id; });
            if (found==candidate.objects.end() || std::find(seen.begin(),seen.end(),id)!=seen.end()) return false;
            seen.push_back(id);
            if (HasSelectedAncestor(layout_,found->parentId,ids)) continue;

            if (!TranslatePlacement(*found,delta)) return false;
        }
        return CommitTransforms(std::move(candidate));
    }

    bool SceneWorld::RotateObjectsWorld(const std::vector<std::string>& ids, const std::array<float,3>& pivot,
        const DirectX::XMFLOAT4X4& rotation)
    {
        if (ids.empty() || !IsRotation(rotation) ||
            !std::all_of(pivot.begin(),pivot.end(),[](float value) { return std::isfinite(value); })) return false;
        const auto delta=DirectX::XMMatrixTranslation(-pivot[0],-pivot[1],-pivot[2])*
            DirectX::XMLoadFloat4x4(&rotation)*DirectX::XMMatrixTranslation(pivot[0],pivot[1],pivot[2]);
        DirectX::XMFLOAT4X4 matrix;
        DirectX::XMStoreFloat4x4(&matrix,delta);
        return TransformObjectsWorld(ids,matrix);
    }

    bool SceneWorld::ScaleObjectsWorld(const std::vector<std::string>& ids, const std::array<float,3>& pivot,
        const DirectX::XMFLOAT4X4& axes, const std::array<float,3>& factors)
    {
        if (!IsRotation(axes) || !std::all_of(pivot.begin(),pivot.end(),[](float value) { return std::isfinite(value); }) ||
            !std::all_of(factors.begin(),factors.end(),[](float value) { return std::isfinite(value) && value>0; })) return false;
        auto scaling=DirectX::XMMatrixScaling(factors[0],factors[1],factors[2]);
        if (factors[0]!=factors[1] || factors[1]!=factors[2])
        {
            const auto frame=DirectX::XMLoadFloat4x4(&axes);
            scaling=DirectX::XMMatrixTranspose(frame)*scaling*frame;
        }
        DirectX::XMFLOAT4X4 matrix;
        DirectX::XMStoreFloat4x4(&matrix,DirectX::XMMatrixTranslation(-pivot[0],-pivot[1],-pivot[2])*
            scaling*DirectX::XMMatrixTranslation(pivot[0],pivot[1],pivot[2]));
        return TransformObjectsWorld(ids,matrix);
    }

    bool SceneWorld::TransformObjectsWorld(const std::vector<std::string>& ids, const DirectX::XMFLOAT4X4& delta)
    {
        if (ids.empty() || !SceneTransforms::IsUsable(delta)) return false;
        auto candidate=layout_;
        std::vector<std::string> seen;
        for (const auto& id : ids)
        {
            const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
                [&](const auto& placement) { return placement.id==id; });
            if (found==candidate.objects.end() || std::find(seen.begin(),seen.end(),id)!=seen.end()) return false;
            seen.push_back(id);
            if (IsIdentity(delta) || HasSelectedAncestor(layout_,found->parentId,ids)) continue;
            DirectX::XMFLOAT4X4 matrix;
            if (!WorldMatrix(id,matrix)) return false;
            DirectX::XMStoreFloat4x4(&matrix,DirectX::XMLoadFloat4x4(&matrix)*DirectX::XMLoadFloat4x4(&delta));
            ScenePlacement placement;
            if (!LocalTransformFromWorld(id,matrix,placement)) return false;
            *found=std::move(placement);
        }
        return CommitTransforms(std::move(candidate));
    }

    std::string SceneWorld::NewId(size_t& nextCounter) const
    {
        for (;;)
        {
            const auto id = "object-" + std::to_string(nextCounter++);
            if (std::none_of(layout_.objects.begin(), layout_.objects.end(),
                [&](const auto& placement) { return placement.id == id; })) return id;
        }
    }

    void SceneWorld::Append(ScenePlacement placement, Engine::Object3D object)
    {
        layout_.objects.reserve(layout_.objects.size() + 1);
        objects_.reserve(objects_.size() + 1);
        layout_.objects.push_back(std::move(placement));
        objects_.push_back(std::move(object));
    }

    bool SceneWorld::AddObject(ScenePlacement placement, const std::filesystem::path& assetsRoot,
        std::string& createdId, std::string& error)
    {
        createdId.clear();
        try
        {
            if (!modelsReady_) throw std::runtime_error("Scene renderer is unavailable");
            auto nextCounter=nextObjectId_;
            if (placement.id.empty()) placement.id=NewId(nextCounter);
            if (std::any_of(layout_.objects.begin(), layout_.objects.end(),
                [&](const auto& existing) { return existing.id == placement.id; }))
                throw std::runtime_error("Object ID already exists");
            if (placement.name.empty()) placement.name = placement.meshRenderer ? placement.Model().stem().string() : "Empty object";
            auto validation=layout_;
            validation.objects.push_back(placement);
            validation.ResolveAssets(assetsRoot); placement=validation.objects.back();
            static_cast<void>(validation.Serialize());
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(validation,matrices,error)) throw std::runtime_error(error);
            Engine::Object3D object;
            std::shared_ptr<Engine::ModelRenderer> animated; AnimatorState animatorState;
            if (placement.animator && !placement.meshRenderer) throw std::runtime_error("Animator needs a MeshRenderer");
            if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
                throw std::runtime_error("Invalid transform");
            if (!object.SetWorldMatrix(matrices.back())) throw std::runtime_error("Invalid world matrix");
            if (placement.meshRenderer)
            {
                const auto model = models_.Load(assetsRoot / placement.Model());
                if (!model) throw std::runtime_error("Model could not be loaded");
                object.SetModel(model);
                if (placement.animator) {
                    if (!model->Rig()) throw std::runtime_error("Animator needs a glTF or GLB model");
                    Animator::Validate(*placement.animator,model->Rig().get());
                    animated=model->AnimatedCopy(materialDevice_.Get(),materialQueue_.Get());
                    if (!animated || !animated->ApplyPose(Animator::Advance(*placement.animator,animatorState,*model->Rig(),0,{},&matrices.back()))) throw std::runtime_error("Animator initialization failed");
                    object.SetModel(animated);
                }
                if (!placement.meshRenderer->material.empty()) object.SetMaterial(MaterialAsset::Load(assetsRoot,placement.meshRenderer->material).Prepare(materialDevice_.Get(),materialQueue_.Get(),assetsRoot));
            }
            const auto id = placement.id;
            Append(std::move(placement), std::move(object));
            layout_.assetReferences=std::move(validation.assetReferences);
            if (animated) { animatedModels_[id]=std::move(animated); animatorStates_[id]=std::move(animatorState); }
            nextObjectId_=nextCounter;
            createdId = id;
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool SceneWorld::DuplicateObject(std::string_view id, const std::array<float,3>& offset,
        std::string& createdId, std::string& error)
    {
        std::vector<std::string> created;
        const bool success=DuplicateObjects({std::string(id)},offset,created,error);
        createdId=success ? created.front() : std::string{};
        return success;
    }

    bool SceneWorld::DuplicateObjects(const std::vector<std::string>& ids, const std::array<float,3>& offset,
        std::vector<std::string>& createdIds, std::string& error)
    {
        const auto requested=ids;
        createdIds.clear();
        if (requested.empty()) { error="No objects selected"; return false; }
        std::vector<size_t> indices;
        for (const auto& id : requested)
        {
            const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
                [&](const auto& placement) { return placement.id==id; });
            if (found==layout_.objects.end()) { error=id+": object no longer exists"; return false; }
            const auto index=static_cast<size_t>(found-layout_.objects.begin());
            if (std::find(indices.begin(),indices.end(),index)==indices.end()) indices.push_back(index);
        }
        auto candidate=layout_;
        auto objects=objects_;
        auto nextCounter=nextObjectId_;
        auto animated=animatedModels_; auto states=animatorStates_;
        std::unordered_map<std::string,std::string> copies;
        std::vector<std::string> created;
        for (const auto index : indices)
        {
            auto id=NewId(nextCounter);
            copies.emplace(layout_.objects[index].id,id);
            created.push_back(std::move(id));
        }
        for (const auto index : indices)
        {
            auto placement=layout_.objects[index];
            const auto parent=copies.find(placement.parentId);
            if (parent!=copies.end()) placement.parentId=parent->second;
            else if (!TranslatePlacement(placement,offset))
            { error=placement.id+": invalid world duplicate offset"; return false; }
            if(placement.button) {
                const auto remap=[&](std::string& id) {const auto found=copies.find(id); if(found!=copies.end()) id=found->second;};
                if(placement.button->action!="loadScene" && placement.button->action!="setState") remap(placement.button->target);
                remap(placement.button->sound);
            }
            if (placement.prefab)
            {
                const auto root=copies.find(placement.prefab->rootId);
                if (root!=copies.end()) placement.prefab->rootId=root->second;
                else placement.prefab.reset();
            }
            placement.id=copies.at(placement.id); placement.name+=" copy";
            auto object=objects_[index];
            if (placement.animator) {
                try {
                    auto model=objects_[index].GetModel()->AnimatedCopy(materialDevice_.Get(),materialQueue_.Get());
                    auto state=animatorStates_.at(layout_.objects[index].id);
                    if (!model || !model->ApplyPose(state.pose)) throw std::runtime_error("Animator duplication failed");
                    object.SetModel(model); animated[placement.id]=std::move(model); states[placement.id]=std::move(state);
                } catch (const std::exception& exception) { error=exception.what(); return false; }
            }
            candidate.objects.push_back(std::move(placement));
            objects.push_back(std::move(object));
        }
        if (!PrepareTransforms(candidate,objects,error)) return false;
        layout_=std::move(candidate); objects_=std::move(objects);
        animatedModels_=std::move(animated); animatorStates_=std::move(states);
        nextObjectId_=nextCounter;
        createdIds=std::move(created);
        error.clear();
        return true;
    }

    bool SceneWorld::RemoveObjects(const std::vector<std::string>& ids, std::string& error)
    {
        if (ids.empty()) { error="No objects selected"; return false; }
        const std::unordered_set<std::string> removed(ids.begin(),ids.end());
        const auto missing=std::find_if(removed.begin(),removed.end(),[&](const auto& id) {
            return std::none_of(layout_.objects.begin(),layout_.objects.end(),[&](const auto& object) { return object.id==id; });
        });
        if (missing!=removed.end()) { error=*missing+": object no longer exists"; return false; }
        SceneLayout candidate;
        candidate.settings=layout_.settings; candidate.assetReferences=layout_.assetReferences;
        if (removed.contains(candidate.settings.mainCamera)) candidate.settings.mainCamera.clear();
        std::vector<Engine::Object3D> objects;
        candidate.objects.reserve(layout_.objects.size()); objects.reserve(objects_.size());
        for (size_t index=0;index<layout_.objects.size();++index)
        {
            auto placement=layout_.objects[index];
            if (removed.contains(placement.id)) continue;
            if (placement.prefab && removed.contains(placement.prefab->rootId)) placement.prefab.reset();
            if (removed.contains(placement.parentId) && !ReparentPlacement(placement,{},error)) return false;
            candidate.objects.push_back(std::move(placement));
            objects.push_back(objects_[index]);
        }
        if (!PrepareTransforms(candidate,objects,error)) return false;
        layout_=std::move(candidate); objects_=std::move(objects);
        for (const auto& id : removed) { animatedModels_.erase(id); animatorStates_.erase(id); physics_.erase(id); }
        error.clear();
        return true;
    }
    std::optional<std::string> SceneWorld::PickRay(const std::array<float, 3>& origin,
        const std::array<float, 3>& direction, float maxDistance) const
    {
        const auto finite = [](const auto& vector) {
            return std::all_of(vector.begin(), vector.end(), [](float value) { return std::isfinite(value); });
        };
        if (!finite(origin) || !finite(direction) || !std::isfinite(maxDistance) || maxDistance <= 0) return {};
        using namespace DirectX;
        const auto start = XMVectorSet(origin[0], origin[1], origin[2], 1);
        auto ray = XMVectorSet(direction[0], direction[1], direction[2], 0);
        const float length = XMVectorGetX(XMVector3Length(ray));
        if (!std::isfinite(length) || length <= 0) return {};
        ray /= length;
        float closest = maxDistance;
        std::optional<std::string> selected;
        for (size_t index = 0; index < objects_.size(); ++index)
        {
            const auto& object = objects_[index];
            if (!object.GetModel() || !layout_.objects[index].meshRenderer->enabled) continue;
            const auto inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&object.GetWorldMatrix()));
            const auto localStart = XMVector3TransformCoord(start, inverse);
            auto localRay = XMVector3TransformNormal(ray, inverse);
            const float factor = XMVectorGetX(XMVector3Length(localRay));
            if (!std::isfinite(factor) || factor <= 0) continue;
            localRay /= factor;
            float hit;
            if (object.GetModel()->IntersectRay(localStart, localRay, hit) && hit / factor < closest)
            {
                closest = hit / factor;
                selected = layout_.objects[index].id;
            }
        }
        return selected;
    }

    bool SceneWorld::WorldRotation(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        auto rotation=DirectX::XMMatrixIdentity();
        auto current=std::string(id);
        size_t remaining=layout_.objects.size();
        do
        {
            const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
                [&](const auto& placement) { return placement.id==current; });
            if (found==layout_.objects.end() || remaining--==0) return false;
            const auto& angles=found->rotation;
            rotation=rotation*DirectX::XMMatrixRotationX(angles[0])*DirectX::XMMatrixRotationY(angles[1])*
                DirectX::XMMatrixRotationZ(angles[2]);
            current=found->parentId;
        } while (!current.empty());
        DirectX::XMFLOAT4X4 candidate;
        DirectX::XMStoreFloat4x4(&candidate,rotation);
        if (!SceneTransforms::IsUsable(candidate)) return false;
        matrix=candidate;
        return true;
    }

    bool SceneWorld::WorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
            [&](const auto& placement) { return placement.id==id; });
        if (found==layout_.objects.end()) return false;
        matrix=objects_[static_cast<size_t>(found-layout_.objects.begin())].GetWorldMatrix();
        return true;
    }

    bool SceneWorld::LocalTransformFromWorld(std::string_view id, const DirectX::XMFLOAT4X4& world, ScenePlacement& placement) const
    {
        const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
            [&](const auto& value) { return value.id==id; });
        if (found==layout_.objects.end()) return false;
        auto matrix=world;
        if (!found->parentId.empty())
        {
            DirectX::XMFLOAT4X4 parent;
            if (!WorldMatrix(found->parentId,parent) || !SceneTransforms::WorldToLocal(world,parent,matrix)) return false;
        }
        return SceneTransforms::ReadTransform(matrix,*found,placement);
    }

    bool SceneWorld::WorldBounds(std::string_view id, std::array<std::array<float, 3>, 8>& corners) const
    {
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
            [id](const auto& placement) { return placement.id == id; });
        if (found == layout_.objects.end()) return false;
        const auto& object = objects_[static_cast<size_t>(found - layout_.objects.begin())];
        DirectX::XMFLOAT3 localCorners[8];
        if (object.GetModel()) object.GetModel()->Bounds().GetCorners(localCorners);
        else DirectX::BoundingBox(DirectX::XMFLOAT3{},DirectX::XMFLOAT3{0.25f,0.25f,0.25f}).GetCorners(localCorners);
        const auto matrix = DirectX::XMLoadFloat4x4(&object.GetWorldMatrix());
        for (size_t i = 0; i < 8; ++i)
        {
            DirectX::XMFLOAT3 position;
            DirectX::XMStoreFloat3(&position, DirectX::XMVector3TransformCoord(
                DirectX::XMLoadFloat3(&localCorners[i]), matrix));
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return false;
            corners[i] = { position.x, position.y, position.z };
        }
        return true;
    }
}
