#include <SceneRuntime/SceneWorld.h>
#include <iterator>
#include <Engine/Core/Validation.h>
#include <tuple>
#include <Engine/Graphics/Resources/GpuProfiler.h>
#include <SceneRuntime/SceneTransforms.h>
#include <SceneRuntime/SceneUi.h>
#include <SceneRuntime/Prefab.h>
#include <SceneRuntime/MaterialAsset.h>
#include <SceneRuntime/GenreGeometry.h>
#include <SceneRuntime/Navigation.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Log.h>
#include <format>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>
#include <unordered_map>
#include <Engine/Graphics/Renderers/ModelRenderer.h>
#include <Engine/Graphics/DirectX12/GpuSynchronization.h>
#include "SceneRenderPlan.h"

namespace
{
bool HasRenderAsset(const SceneRuntime::ScenePlacement &p)
{ return p.meshRenderer || p.material || p.terrain || p.tilemap; }
bool DifferentMeshMaterial(const SceneRuntime::ScenePlacement &a, const SceneRuntime::ScenePlacement &b)
{ return std::tie(a.meshRenderer, a.material) != std::tie(b.meshRenderer, b.material); }
bool DifferentAnimatedResources(const SceneRuntime::ScenePlacement &a, const SceneRuntime::ScenePlacement &b)
{ return std::tie(a.animator, a.terrain, a.tilemap) != std::tie(b.animator, b.terrain, b.tilemap) || (a.animator && DifferentMeshMaterial(a, b)); }
void CopySceneOwnership(SceneRuntime::SceneLayout& layout,const std::string& source,const std::string& copy)
{
    for(auto& [name,ids]:layout.sceneObjects) {
        static_cast<void>(name);
        if(std::find(ids.begin(),ids.end(),source)!=ids.end()) {ids.push_back(copy);return;}
    }
}
bool ValidPlayerInput(double seconds, float horizontal, float vertical)
{ return std::isfinite(seconds) && seconds > 0 && std::isfinite(horizontal) && std::isfinite(vertical); }
bool RenderableEnabled(const SceneRuntime::ScenePlacement &p)
{ return (p.meshRenderer && p.meshRenderer->enabled) || (p.terrain && p.terrain->enabled) || (p.tilemap && p.tilemap->enabled); }
bool HasPhysicalPose(const SceneRuntime::ScenePlacement &p)
{ return (p.playerController && p.playerController->enabled) || (p.rigidBody && p.rigidBody->enabled && p.rigidBody->motion != "static"); }
    void ApplySurfaceOverride(Engine::Object3D& object,const SceneRuntime::ScenePlacement& placement)
    {
        if(!placement.material || !placement.material->enabled || !placement.material->surfaceOverride) return;
        const auto& surface=*placement.material;
        const auto apply=[&](const std::shared_ptr<const Engine::Material>& original) -> std::shared_ptr<const Engine::Material> {
            if(original && original->color==surface.color && original->roughness==surface.roughness && original->metallic==surface.metallic) return original;
            auto material=original ? std::make_shared<Engine::Material>(*original) : std::make_shared<Engine::Material>();
            material->color=surface.color;material->roughness=surface.roughness;material->metallic=surface.metallic;
            return material;
        };
        object.SetMaterial(apply(object.GetMaterial()));
        auto slots=object.GetMaterialSlots(); for(auto& slot:slots) if(slot) slot=apply(slot);
        object.SetMaterialSlots(std::move(slots));
    }
    void PrepareProcedural(Engine::ModelManager& models,Engine::Object3D& object,const SceneRuntime::ScenePlacement& p,const std::filesystem::path& root,ID3D12Device* device,ID3D12CommandQueue* queue,
        const Engine::AssetDatabase* index=nullptr,std::map<std::filesystem::path,std::shared_ptr<const Engine::Material>>* materials=nullptr)
    {
        if(!p.terrain&&!p.tilemap) return;
        auto model=models.Procedural(p.id,root.generic_string()+SceneRuntime::GenreGeometry::Signature(p),[&] {
            return std::vector<Engine::MeshData>{p.terrain?SceneRuntime::GenreGeometry::Terrain(*p.terrain,root):SceneRuntime::GenreGeometry::Tilemap(*p.tilemap,root)};
        });
        object.SetModel(std::move(model));
        if(p.tilemap) {auto material=std::make_shared<Engine::Material>();material->transparent=true;object.SetMaterial(std::move(material));}
        if(!p.Material().empty()) {
            std::shared_ptr<const Engine::Material> material;
            if(materials) {
                auto found=materials->find(p.Material());
                if(found==materials->end()) found=materials->emplace(p.Material(),SceneRuntime::MaterialAsset::Load(root,p.Material(),index).Prepare(device,queue,root)).first;
                material=found->second;
            } else material=SceneRuntime::MaterialAsset::Load(root,p.Material(),index).Prepare(device,queue,root);
            object.SetMaterial(std::move(material));
        }
    }

    Engine::MaterialSlots PrepareMaterialSlots(const SceneRuntime::ScenePlacement& placement,size_t meshes,
        ID3D12Device* device,ID3D12CommandQueue* queue,const std::filesystem::path& root,
        std::map<std::filesystem::path,std::shared_ptr<const Engine::Material>>& cache,const Engine::AssetDatabase* index=nullptr)
    {
        Engine::MaterialSlots result;
        if(!placement.material || !placement.material->enabled) return result;
        const auto& paths=placement.material->slots;result.resize(std::min(meshes,paths.size()));
        for(size_t i=0;i<result.size();++i) if(!paths[i].empty()) {
            auto found=cache.find(paths[i]);
            if(found==cache.end()) found=cache.emplace(paths[i],SceneRuntime::MaterialAsset::Load(root,paths[i],index).Prepare(device,queue,root)).first;
            result[i]=found->second;
        }
        return result;
    }
    bool HasTransformAnimation(const SceneRuntime::ScenePlacement& placement)
    {
        return placement.animation && placement.animation->enabled &&
            std::any_of(placement.animation->tracks.begin(),placement.animation->tracks.end(),[](const auto& track) {
                return !track.property.starts_with("ui") && track.property!="opacity";
            });
    }
    bool ApplyTransformAnimation(SceneRuntime::ScenePlacement& placement,const std::map<std::string,float>& clocks)
    {
        if (!HasTransformAnimation(placement)) return false;
        const auto before=placement;
        for (const auto& track:placement.animation->tracks)
        {
            if (track.property.starts_with("ui") || track.property=="opacity") continue;
            const auto clock=clocks.find(track.clock);
            if (clock==clocks.end() || clock->second<track.delay) continue;
            const auto value=SceneRuntime::Animation::Sample(track,clocks);
            if (!value) continue;
            SceneRuntime::Animation::Apply(placement,track.property,*value);
        }
        return before.position!=placement.position || before.rotation!=placement.rotation || before.scale!=placement.scale || !before.SameComponents(placement);
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
        retirement_=Engine::GpuRetirement::Shared(materialDevice_.Get(),materialQueue_.Get());
        if(!retirement_) { if(diagnostic) *diagnostic="Cannot initialize resource retirement"; return false; }
        meshShader_=shaderPath;shadow_=std::make_shared<Engine::ShadowMap>();localShadow_=std::make_shared<Engine::ShadowMap>();
        modelsReady_ = models_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), shaderPath) &&
            shadow_->Initialize(renderer.GetDevice(),shaderPath) && localShadow_->Initialize(renderer.GetDevice(),shaderPath,true) && occlusion_.Initialize(renderer.GetDevice(),shaderPath);
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
        if(!changed) return true;
        try {candidate.ValidateChanges(layout_);} catch(const std::exception& error) {Engine::Log::Warning(error.what());return false;}
        if(!CommitTransforms(candidate)) return false;
        for(size_t i=0;i<candidate.objects.size();++i) {
            layout_.objects[i].CopyComponents(candidate.objects[i]);
            ApplySurfaceOverride(objects_[i],layout_.objects[i]);
        }
        return true;
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
        layout_=std::move(candidate.layout_); objects_.swap(candidate.objects_); animatedModels_.swap(candidate.animatedModels_); animatorStates_.swap(candidate.animatorStates_); lodModels_.swap(candidate.lodModels_);
        shadow_.swap(candidate.shadow_);
        localShadow_.swap(candidate.localShadow_);
        std::swap(occlusion_,candidate.occlusion_);
        assetIndex_.swap(candidate.assetIndex_); materials_.swap(candidate.materials_);
        ++renderVersion_;
        error.clear();
        return true;
    }

    bool SceneWorld::ReplaceLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error,bool preserveExecution)
    {
        PreparedLayout prepared;
        if (!PrepareLayout(std::move(layout),assetsRoot,error,preserveExecution,prepared)) return false;
        if (preserveExecution && prepareRuntime_ && !prepareRuntime_(prepared.layout,error)) return false;
        if(preserveExecution) scripts_.StopRemoved(layout_,prepared.layout);
        CommitLayout(std::move(prepared),preserveExecution); error.clear(); return true;
    }
    SceneWorld::LayoutView SceneWorld::BaselineView(const PreparedLayout *previous) const {
        if (previous) return {previous->layout, previous->objects, previous->assetsRoot, previous->assets, previous->materials, previous->animated, previous->states, previous->lods};
        return {layout_, objects_, assetsRoot_, assetIndex_, materials_, animatedModels_, animatorStates_, lodModels_};
    }
    Engine::Object3D SceneWorld::BuildLayoutObject(const ScenePlacement &placement, const SceneLayout &layout, const DirectX::XMFLOAT4X4 &matrix, PreparedLayout &prepared, const LayoutView &baseline, bool preserveExecution)
    {
        Engine::Object3D object;
        if (placement.animator && !placement.meshRenderer) throw std::runtime_error("Animator needs a MeshRenderer");
        if (placement.meshRenderer)
        {
            const auto model = models_.Load(prepared.assetsRoot / placement.Model());
            if (!model) throw std::runtime_error("Model could not be loaded: " + placement.id);
            object.SetModel(model);
            for (const auto &lod : placement.meshRenderer->lods)
            {
                if (placement.animator) throw std::runtime_error("Animated meshes cannot use static LOD models");
                auto loaded = models_.Load(prepared.assetsRoot / lod.model);
                if (!loaded || loaded->Rig()) throw std::runtime_error("Static LOD model could not be loaded: " + placement.id);
                prepared.lods[placement.id].push_back(std::move(loaded));
            }
            InitializeAnimatorObject(object, placement, layout, matrix, prepared, baseline, preserveExecution);
            const auto &material = placement.Material();
            if (!material.empty())
            {
                auto found = prepared.materials.find(material);
                if (found == prepared.materials.end()) found = prepared.materials.emplace(material, MaterialAsset::Load(prepared.assetsRoot, material, prepared.assets.get()).Prepare(materialDevice_.Get(), materialQueue_.Get(), prepared.assetsRoot)).first;
                object.SetMaterial(found->second);
            }
            object.SetMaterialSlots(PrepareMaterialSlots(placement, model->MeshCount(), materialDevice_.Get(), materialQueue_.Get(), prepared.assetsRoot, prepared.materials, prepared.assets.get()));
        }
        PrepareProcedural(models_, object, placement, prepared.assetsRoot, materialDevice_.Get(), materialQueue_.Get(), prepared.assets.get(), &prepared.materials);
        ApplySurfaceOverride(object, placement);
        if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
            throw std::runtime_error("Invalid transform: " + placement.id);
        if (!object.SetWorldMatrix(matrix)) throw std::runtime_error("Invalid world matrix");
        return object;
    }
    void SceneWorld::InitializeAnimatorObject(Engine::Object3D &object, const ScenePlacement &placement, const SceneLayout &layout, const DirectX::XMFLOAT4X4 &matrix, PreparedLayout &prepared, const LayoutView &baseline, bool preserveExecution)
    {
        if (!placement.animator) return;
        const auto model = object.GetModel();

        if (!model->Rig()) throw std::runtime_error("Animator needs a glTF or GLB model");
        Animator::Validate(*placement.animator, model->Rig().get());
        Ragdoll::Validate(layout, placement, *model->Rig());
        const auto old = std::find_if(baseline.layout.objects.begin(), baseline.layout.objects.end(), [&](const auto &item) { return item.id == placement.id; });
        const bool reuse = preserveExecution && old != baseline.layout.objects.end() && old->meshRenderer == placement.meshRenderer && old->animator == placement.animator && baseline.animated.contains(placement.id);
        auto instance = reuse ? baseline.animated.at(placement.id) : model->AnimatedCopy(materialDevice_.Get(), materialQueue_.Get());
        if (reuse) prepared.states[placement.id] = baseline.states.at(placement.id);
        if (!instance || (!reuse && !instance->ApplyPose(Animator::Advance(*placement.animator, prepared.states[placement.id], *model->Rig(), 0, {}, &matrix)))) throw std::runtime_error("Animator initialization failed");
        object.SetModel(instance); prepared.animated[placement.id] = std::move(instance);

    }

    void SceneWorld::ApplyAnimatorOverrides(const std::string &id, const ScriptRuntime &scripts, AnimatorState &state)
    {
        if (const auto change = scripts.RootMotions().find(id); change != scripts.RootMotions().end()) state.rootMotionOverride = change->second;
        if (const auto changes = scripts.AnimatorParameters().find(id); changes != scripts.AnimatorParameters().end())
            for (const auto &[name, value] : changes->second) state.parameterOverrides[name] = value;
        Animator::ValidateParameters(state.parameterOverrides);
        if (const auto changes = scripts.IkTargets().find(id); changes != scripts.IkTargets().end())
            for (const auto &[name, target] : changes->second)
            { if (target) state.ikOverrides[name] = *target; else state.ikOverrides.erase(name); }
    }

    std::map<std::string, float> SceneWorld::AnimatorInputs(const SceneLayout &layout, const std::string &id) const
    {
        auto parameters = inputValues_;
        const auto input = [&](const std::string &name) { const auto found=inputValues_.find(name); return found==inputValues_.end() ? 0.0f : found->second; };
        const auto &mapping = layout.settings.playerInputs;
        parameters["moveX"] = input(mapping[0]) - input(mapping[1]); parameters["moveY"] = input(mapping[2]) - input(mapping[3]);
        parameters["speed"] = std::hypot(parameters["moveX"], parameters["moveY"]);
        const auto body = physics_.find(id); parameters["grounded"] = body == physics_.end() ? 1.0f : (body->second.grounded ? 1.0f : 0.0f);
        for (const auto &[name, pressed] : inputPressed_) parameters["pressed:" + name] = pressed ? 1.0f : 0.0f;
        return parameters;
    }

    void SceneWorld::AdvanceAnimatorFrame(const SceneLayout &layout, const ScenePlacement &placement, AnimatorFrame &frame, double seconds, const std::map<std::string, float> &parameters, const DirectX::XMFLOAT4X4 &matrix)
    {
        const bool ragdoll = placement.ragdoll && placement.ragdoll->enabled && placement.ragdoll->active && !placement.ragdoll->bones.empty();
        const auto pose = ragdoll ? Ragdoll::Pose(layout, placement, *frame.model->Rig(), frame.state.basePose.empty() ? frame.state.pose : frame.state.basePose) : Animator::Advance(*placement.animator, frame.state, *frame.model->Rig(), seconds, parameters, &matrix);
        if (ragdoll) {frame.state.pose = pose; frame.state.events.clear(); frame.state.rootDelta = {};}
        frame.applyPose = placement.animator->enabled || ragdoll;
        if (frame.applyPose && !frame.model->PreparePose(pose, frame.pose)) throw std::runtime_error("Animator pose preparation failed");
    }
    bool SceneWorld::PrepareLayout(SceneLayout layout,const std::filesystem::path& assetsRoot,std::string& error,bool preserveExecution,PreparedLayout& result,const PreparedLayout* previous)
    {
        if (!modelsReady_)
        {
            error = "Scene renderer is unavailable. Check shaders and restart.";
            return false;
        }
        try
        {
            const auto baseline=BaselineView(previous);
            auto index=previous && previous->assets && previous->assetsRoot==assetsRoot ? previous->assets : std::make_shared<Engine::AssetDatabase>(assetsRoot,true);
            { Engine::CpuScope scope("Scene asset resolution"); layout.ResolveAssets(*index); }
            static_cast<void>(layout.Serialize());
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(layout,matrices,error)) throw std::runtime_error(error);
            PreparedLayout prepared; prepared.assetsRoot=assetsRoot; prepared.assets=std::move(index);
            prepared.materials=previous?previous->materials:decltype(prepared.materials){};
            prepared.objects.reserve(layout.objects.size());
            for (const auto& placement : layout.objects)
            {
                prepared.objects.push_back(BuildLayoutObject(placement,layout,matrices[prepared.objects.size()],prepared,baseline,preserveExecution));
            }
            prepared.layout=std::move(layout);
            if(!PrepareShadows(prepared.layout.settings,prepared.shadow,prepared.localShadow,error)) return false;
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
        if(prepared.shadow && prepared.shadow!=shadow_) {if(retirement_) retirement_->Retire(shadow_);shadow_=std::move(prepared.shadow);}
        if(prepared.localShadow && prepared.localShadow!=localShadow_) {if(retirement_) retirement_->Retire(localShadow_);localShadow_=std::move(prepared.localShadow);}
        layout_=std::move(prepared.layout); assetsRoot_=std::move(prepared.assetsRoot);
        assetIndex_=std::move(prepared.assets); materials_=std::move(prepared.materials);
        if(retirement_) retirement_->Collect();
        ++renderVersion_;
        if (!preserveExecution) { previousPhysical_.clear(); currentPhysical_.clear(); physics_.clear(); physicsWorld_.Reset(); fixedSeconds_=0; fixedJump_=false; fixedPressed_.clear(); }
        else std::erase_if(physics_,[&](const auto& pair) { return std::none_of(layout_.objects.begin(),layout_.objects.end(),[&](const auto& item) { return item.id==pair.first; }); });
        animatedModels_=std::move(prepared.animated); animatorStates_=std::move(prepared.states); objects_=std::move(prepared.objects);
        lodModels_=std::move(prepared.lods);
    }

    bool SceneWorld::PrepareAnimators(const SceneLayout& layout,double seconds,const std::map<std::string,std::shared_ptr<Engine::ModelRenderer>>& models,
        const std::map<std::string,AnimatorState>& states,ScriptRuntime& scripts,std::vector<AnimatorFrame>& result,std::string& error) const
    {
        try
        {
            std::vector<AnimatorFrame> frames;
            if(std::none_of(layout.objects.begin(),layout.objects.end(),[](const auto& p){return p.animator.has_value();})) {result.clear();error.clear();return true;}
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(layout,matrices,error)) throw std::runtime_error(error);
            for (size_t index=0;index<layout.objects.size();++index)
            {
                const auto& placement=layout.objects[index];
                if (!placement.animator) continue;
                const auto model=models.find(placement.id); if (model==models.end()) throw std::runtime_error("Animator model missing");
                AnimatorFrame frame; frame.id=placement.id; frame.index=index; frame.model=model->second; frame.state=states.at(placement.id);
                ApplyAnimatorOverrides(placement.id,scripts,frame.state);
                const auto parameters=AnimatorInputs(layout,placement.id);
                AdvanceAnimatorFrame(layout,placement,frame,seconds,parameters,matrices[index]);
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
        const auto defaults=state ? UiState{} : SceneUi::Defaults(layout_);
        const auto& values=state ? *state : defaults;
        std::vector<RenderPlan::Item> items;
        {
            Engine::CpuScope cpu("Render item preparation");
            const bool interpolated=PrepareRenderTransforms();
            items=RenderPlan::Items(layout_,objects_,lodModels_,camera,values,
                interpolated ? std::span<const DirectX::XMFLOAT4X4>(renderMatrices_) : std::span<const DirectX::XMFLOAT4X4>{});
        }
        auto lighting=light;
        lighting.shadow=nullptr;
        lighting.localShadow=nullptr;
        if (light.enabled && light.shadowsEnabled && shadow_->Begin(commands,camera,light))
        {
            Engine::GpuScope gpu(commands,"Shadows"); Engine::CpuScope cpu("Shadows");
            for (const auto& item:items) if (item.object.HasMaterialPass(Engine::MaterialPass::Opaque))
                {
                    RenderPlan::Count(item.object,Engine::MaterialPass::Opaque,1,meshTelemetry_);
                    item.object.GetModel()->DrawShadow(commands,item.object.GetWorldMatrix(),*shadow_,item.object.GetMaterial().get(),item.object.GetMaterialSlots());
                }
            shadow_->End(commands);
            lighting.shadow=shadow_.get();
        }
        if (RenderPlan::LocalShadows(commands,lighting,items,*localShadow_,meshTelemetry_)) lighting.localShadow=localShadow_.get();
        {
        Engine::GpuScope gpu(commands,"Opaque meshes"); Engine::CpuScope cpu("Opaque meshes");
        std::stable_sort(items.begin(),items.end(),[](const auto& first,const auto& second) { return first.distance<second.distance; });
        RenderPlan::Opaque(commands,camera,lighting,items,meshTelemetry_,occlusion_);
        }
        Engine::GpuScope gpu(commands,"Transparent meshes"); Engine::CpuScope cpu("Transparent meshes");
        std::stable_sort(items.begin(),items.end(),[](const auto& first,const auto& second) { return first.distance>second.distance; });
        for (const auto& item : items)
            if (item.object.HasMaterialPass(Engine::MaterialPass::Transparent))
            { RenderPlan::Count(item.object,Engine::MaterialPass::Transparent,1,meshTelemetry_); item.object.Draw(commands,camera,lighting,{},Engine::MaterialPass::Transparent); }
    }

    bool SceneWorld::PrepareTransforms(const SceneLayout& layout, std::vector<Engine::Object3D>& objects, std::string& error,const SceneLayout* validatedBefore)
    {
        Engine::CpuScope scope("World transform preparation");
        std::vector<DirectX::XMFLOAT4X4> matrices;
        if(validatedBefore) {
            std::vector<DirectX::XMFLOAT4X4> previous; previous.reserve(objects.size());
            std::transform(objects.begin(),objects.end(),std::back_inserter(previous),[](const auto& object) {return object.GetWorldMatrix();});
            if(!SceneTransforms::ResolveChanges(layout,*validatedBefore,previous,matrices,error)) return false;
        } else if (!SceneTransforms::Resolve(layout,matrices,error)) return false;
        if (objects.size()!=matrices.size()) { error="Placement and draw object counts differ"; return false; }
        for (size_t index=0;index<objects.size();++index)
        {
            const auto& placement=layout.objects[index];
            const bool sameLocal=objects[index].GetPosition()==placement.position && objects[index].GetRotation()==placement.rotation && objects[index].GetScale()==placement.scale;
            if(sameLocal && std::memcmp(&objects[index].GetWorldMatrix(),&matrices[index],sizeof(matrices[index]))==0) continue;
            if ((!sameLocal && !objects[index].SetTransform(placement.position,placement.rotation,placement.scale)) ||
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
        objects_=std::move(objects); ++renderVersion_;
        return true;
    }

    bool SceneWorld::SetParent(std::string_view id, const std::string& parentId, std::string& error)
    {
        return SetParents({std::string(id)},parentId,error);
    }

    bool SceneWorld::SetParents(const std::vector<std::string>& ids,const std::string& parentId,std::string& error)
    {
        try
        {
            if(ids.empty()) throw std::runtime_error("No objects selected");
            auto candidate=layout_;
            for(const auto& id:ids) {
                const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
                    [&](const auto& object) { return object.id==id; });
                if(found==candidate.objects.end()) throw std::runtime_error("Object no longer exists: "+id);
                if(found->parentId!=parentId && !ReparentPlacement(*found,parentId,error)) return false;
            }
            // Validate the final graph and commit once so a failed member cannot move the rest.
            static_cast<void>(candidate.Serialize());
            if (!CommitTransforms(std::move(candidate))) throw std::runtime_error("Invalid inherited transform");
            error.clear();
            return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }

    bool SceneWorld::PrepareShadows(const SceneSettings& settings,std::shared_ptr<Engine::ShadowMap>& directional,std::shared_ptr<Engine::ShadowMap>& local,std::string& error) const
    {
        directional=shadow_;local=localShadow_;
        const auto prepare=[&](auto& map,unsigned int resolution,bool atlas) {
            if(map && map->Size()==resolution) return true;
            auto candidate=std::make_shared<Engine::ShadowMap>();
            if(!candidate->Initialize(materialDevice_.Get(),meshShader_,atlas,resolution)) {error="Shadow resources could not be initialized";return false;}
            map=std::move(candidate);return true;
        };
        return prepare(directional,settings.shadowResolution,false) && prepare(local,settings.localShadowResolution,true);
    }
    bool SceneWorld::SetSettings(const SceneSettings& settings, std::string& error)
    {
        try
        {
            auto candidate=layout_;
            candidate.settings=settings;
            static_cast<void>(candidate.Serialize());
            std::shared_ptr<Engine::ShadowMap> directional,local;
            if(!PrepareShadows(settings,directional,local,error)) return false;
            if(directional!=shadow_) {if(retirement_ && !retirement_->Retire(shadow_)) return false;shadow_=std::move(directional);}
            if(local!=localShadow_) {if(retirement_ && !retirement_->Retire(localShadow_)) return false;localShadow_=std::move(local);}
            if(layout_.settings.fixedStep!=settings.fixedStep) {fixedSeconds_=0;previousPhysical_.clear();currentPhysical_.clear();}
            layout_.settings=settings;
            ++renderVersion_;
            error.clear();
            return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }

    bool SceneWorld::SetComponents(std::string_view id, const ScenePlacement& settings,
        const std::filesystem::path& assetsRoot, std::string& error)
    {
        auto owner=settings; owner.id=id;
        return SetComponentBatch({std::move(owner)},assetsRoot,error);
    }
    bool SceneWorld::SetComponentBatch(const std::vector<ScenePlacement>& settings,const std::filesystem::path& assetsRoot,std::string& error)
    {
        try
        {
            auto candidate=layout_;
            bool rebuild=false;
            for(const auto& setting:settings) {
            const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
                [&](const auto& placement) { return placement.id==setting.id; });
            if (found==candidate.objects.end()) throw std::runtime_error("Component owner no longer exists");
            found->CopyComponents(setting);
            if (!found->camera && candidate.settings.mainCamera==setting.id) candidate.settings.mainCamera.clear();
            const auto index=static_cast<size_t>(found-candidate.objects.begin());
            rebuild |= DifferentMeshMaterial(*found,layout_.objects[index]) || DifferentAnimatedResources(*found,layout_.objects[index]);
            }
            static_cast<void>(candidate.Serialize());
            if (!rebuild)
            {
                for (const auto& object:candidate.objects) if (object.ragdoll) {
                    const auto model=animatedModels_.find(object.id);
                    if(model==animatedModels_.end()) throw std::runtime_error("Ragdoll requires a loaded Animator model");
                    Ragdoll::Validate(candidate,object,*model->second->Rig());
                }
                for(size_t index=0;index<candidate.objects.size();++index) layout_.objects[index].CopyComponents(candidate.objects[index]);
                layout_.settings=candidate.settings;
                ++renderVersion_;
                error.clear();
                return true;
            }
            return ReplaceLayout(std::move(candidate),assetsRoot,error,true);
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }

    bool SceneWorld::GenerateRagdoll(const std::string& id,const std::filesystem::path& root,std::string& error)
    {
        const auto model=animatedModels_.find(id); const auto state=animatorStates_.find(id);
        if(model==animatedModels_.end() || state==animatorStates_.end()) {error="Ragdoll generation requires a loaded Animator model"; return false;}
        auto candidate=layout_;
        if(!Ragdoll::Generate(candidate,id,*model->second->Rig(),state->second.pose,error)) return false;
        return ReplaceLayout(std::move(candidate),root,error,true);
    }
    bool SceneWorld::AddImpulse(const std::string& id,const std::array<float,3>& impulse)
    {
        const auto object=std::find_if(layout_.objects.begin(),layout_.objects.end(),[&](const auto& item) { return item.id==id; });
        return object!=layout_.objects.end() && object->rigidBody && object->rigidBody->enabled && object->rigidBody->motion=="dynamic" && physicsWorld_.AddImpulse(id,impulse);
    }
    std::map<std::string, bool> SceneWorld::FixedRootOverrides(SceneLayout &layout, const ScriptRuntime &scripts) const
    {
        std::map<std::string, bool> savedRootMotion;
        for (auto &object : layout.objects) if (object.animator) {
                savedRootMotion[object.id] = object.animator->rootMotion;
                if (const auto state = animatorStates_.find(object.id); state != animatorStates_.end())
                    object.animator->rootMotion = state->second.rootMotionOverride.value_or(object.animator->rootMotion);
                if (const auto change = scripts.RootMotions().find(object.id); change != scripts.RootMotions().end())
                    object.animator->rootMotion = change->second.value_or(savedRootMotion.at(object.id));
            }
        return savedRootMotion;
    }
    bool SceneWorld::SynchronizeRagdolls(SceneLayout &layout, std::string &error) const
    {
        try {
            std::vector<std::string> owners; for (const auto &object : layout.objects) if (object.ragdoll && !object.ragdoll->bones.empty()) owners.push_back(object.id);
            for (const auto &id : owners) {
                const auto model = animatedModels_.find(id); const auto state = animatorStates_.find(id);
                if (model == animatedModels_.end() || state == animatorStates_.end()) throw std::runtime_error("Ragdoll Animator is missing");
                Ragdoll::Synchronize(layout, id, *model->second->Rig(), state->second.pose);
            }
        } catch (const std::exception &exception) {error = exception.what(); return false;}
        return true;
    }
    std::map<std::string, std::array<bool, 3>> SceneWorld::DisableRagdollOwners(SceneLayout &layout, ScenePhysics::States &states)
    {
        std::map<std::string, std::array<bool, 3>> ragdollOwnerEnabled;
        for (auto &object : layout.objects) if (object.ragdoll && object.ragdoll->enabled && object.ragdoll->active && !object.ragdoll->bones.empty()) {
                ragdollOwnerEnabled[object.id] = {object.playerController && object.playerController->enabled, object.boxCollider && object.boxCollider->enabled, object.rigidBody && object.rigidBody->enabled};
                if (object.playerController) object.playerController->enabled = false;
                if (object.boxCollider) object.boxCollider->enabled = false;
                if (object.rigidBody) {object.rigidBody->enabled = false; object.rigidBody->velocity = object.rigidBody->angularVelocity = {0, 0, 0};}
                states.erase(object.id);
            }
        return ragdollOwnerEnabled;
    }
    bool SceneWorld::AdvanceLegacyPlayers(SceneLayout &layout, ScenePhysics::States &states, float horizontal, float vertical, bool jump) const
    {
        if (std::any_of(layout.objects.begin(), layout.objects.end(), [](const auto &p){ return p.playerController && p.playerController->enabled && !p.playerController->usePhysics; })) {
            Engine::CpuScope legacyScope("Legacy player movement");
            if (!ScenePhysics::Advance(layout, states, FixedStep(), horizontal, vertical, jump)) return false;
        } else std::erase_if(states, [&](const auto &item){ return std::none_of(layout.objects.begin(), layout.objects.end(), [&](const auto &p){ return p.id == item.first && p.playerController && p.playerController->enabled; }); });
        return true;
    }
    void SceneWorld::RestoreRagdollOwners(SceneLayout &layout, const std::map<std::string, std::array<bool, 3>> &ragdollOwnerEnabled)
    {
        for (auto &object : layout.objects) if (const auto saved = ragdollOwnerEnabled.find(object.id); saved != ragdollOwnerEnabled.end()) {
                if (object.playerController) object.playerController->enabled = saved->second[0];
                if (object.boxCollider) object.boxCollider->enabled = saved->second[1];
                if (object.rigidBody) object.rigidBody->enabled = saved->second[2];
            }

    }
    void SceneWorld::RotateComponents(SceneLayout &candidate, double seconds)
    {
        for (auto &placement : candidate.objects)
        {
            if (!placement.rotator || !placement.rotator->enabled) continue;
            for (size_t axis = 0; axis < 3; ++axis)
            {
                const double speed = placement.rotator->angularVelocity[axis];
                if (speed == 0) continue;
                const double elapsed = std::remainder(seconds, 360.0 / std::abs(speed));
                placement.rotation[axis] = static_cast<float>(std::remainder(static_cast<double>(placement.rotation[axis]) +
                                                                                 elapsed * speed * DirectX::XM_PI / 180.0,static_cast<double>(DirectX::XM_2PI)));
            }
        }

    }
    bool SceneWorld::UpdateLateComponents(double seconds, PreparedLayout &prepared, ScriptRuntime &runtime, std::vector<AnimatorFrame> &frames, std::map<std::string, std::array<float, 3>> &impulses, std::string &error)
    {
        if (ScriptRuntime::HasPhase(prepared.layout, ScriptPhase::LateUpdate)) {
            auto late = prepared.layout;
            if (!runtime.UpdateCandidate(late, seconds, error, inputValues_, inputPressed_, &physicsWorld_, ScriptPhase::LateUpdate, &layout_)) { Engine::Log::Warning(error); return false; }
            PreparedLayout afterLate;
            if (!PrepareScriptLayout(std::move(late), &prepared, afterLate, error)) { Engine::Log::Warning(error); return false; }
            prepared = std::move(afterLate);
            for (const auto &[id, impulse] : runtime.Impulses()) for (size_t axis = 0; axis < 3; ++axis) impulses[id][axis] += impulse[axis];
            if (!PrepareAnimators(prepared.layout, 0, prepared.animated, prepared.states, runtime, frames, error) || !PrepareRootMotion(prepared, frames, error))
            { Engine::Log::Warning(error); return false; }
            for (const auto &frame : frames) prepared.states.at(frame.id) = frame.state;
        }
        return true;
    }
    bool SceneWorld::ApplyRootDelta(ScenePlacement &placement, const AnimatorFrame &frame)
    {
        if (placement.ragdoll && placement.ragdoll->enabled && placement.ragdoll->active) return false;
        if (!frame.applyPose || !frame.state.rootMotionOverride.value_or(placement.animator->rootMotion)) return false;
        if (frame.state.rootDelta.position == std::array<float, 3>{} && frame.state.rootDelta.rotation == std::array<float, 4>{0, 0, 0, 1}) return false;
        const auto delta = Animator::RootDeltaMatrix(*placement.animator, frame.state, *frame.model->Rig());
        DirectX::XMFLOAT4X4 local, changed;
        if (!SceneTransforms::Compose(placement, local)) throw std::runtime_error("Invalid root motion object transform");
        DirectX::XMStoreFloat4x4(&changed, DirectX::XMLoadFloat4x4(&delta) * DirectX::XMLoadFloat4x4(&local));
        ScenePlacement result;
        if (!SceneTransforms::ReadTransform(changed, placement, result)) throw std::runtime_error("Root motion cannot be represented by object SRT");
        placement.position = result.position; placement.rotation = result.rotation; placement.scale = result.scale;
        return true;
    }
    void SceneWorld::ConstrainRootFrames(PreparedLayout &prepared, std::vector<AnimatorFrame> &frames, const std::vector<DirectX::XMFLOAT4X4> &matrices)
    {
        for (auto &frame : frames) if (frame.applyPose)
            {
                const auto &placement = prepared.layout.objects.at(frame.index);
                if (placement.ragdoll && placement.ragdoll->enabled && placement.ragdoll->active) continue;
                frame.state.pose = Animator::Constrain(*placement.animator, frame.state, *frame.model->Rig(), &matrices[frame.index]);
                if (!frame.model->PreparePose(frame.state.pose, frame.pose)) throw std::runtime_error("Root motion constraint pose failed");
            }

    }
    Engine::Object3D SceneWorld::BuildAddedObject(const ScenePlacement &placement, const SceneLayout &validation, const DirectX::XMFLOAT4X4 &matrix, const std::filesystem::path &assetsRoot, PreparedLayout &prepared)
    {
        Engine::Object3D object;


            if (placement.animator && !placement.meshRenderer) throw std::runtime_error("Animator needs a MeshRenderer");
        if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
            throw std::runtime_error("Invalid transform");
        if (!object.SetWorldMatrix(matrix)) throw std::runtime_error("Invalid world matrix");
        if (placement.meshRenderer)
        {
            const auto model = models_.Load(assetsRoot / placement.Model());
            if (!model) throw std::runtime_error("Model could not be loaded");
            object.SetModel(model);
            for (const auto &lod : placement.meshRenderer->lods)
            {
                if (placement.animator) throw std::runtime_error("Animated meshes cannot use static LOD models");
                auto loaded = models_.Load(assetsRoot / lod.model);
                if (!loaded || loaded->Rig()) throw std::runtime_error("Static LOD model could not be loaded");
                prepared.lods[placement.id].push_back(std::move(loaded));
            }
            InitializeAddedAnimator(object, placement, validation, matrix, model, prepared);
            if (!placement.Material().empty()) object.SetMaterial(MaterialAsset::Load(assetsRoot, placement.Material()).Prepare(materialDevice_.Get(), materialQueue_.Get(), assetsRoot));
            std::map<std::filesystem::path, std::shared_ptr<const Engine::Material>> materials;
            object.SetMaterialSlots(PrepareMaterialSlots(placement, model->MeshCount(), materialDevice_.Get(), materialQueue_.Get(), assetsRoot, materials));
        }
        PrepareProcedural(models_, object, placement, assetsRoot, materialDevice_.Get(), materialQueue_.Get());
        ApplySurfaceOverride(object, placement);
        return object;
    }
    void SceneWorld::RemapDuplicateLinks(ScenePlacement &placement, const std::unordered_map<std::string, std::string> &copies)
    {
        for (auto &script : placement.scripts) script.Remap(copies);
        if (placement.navAgent) placement.navAgent->Remap(copies);
        if (placement.ragdoll) placement.ragdoll->Remap(copies);
        if (placement.joint) {const auto found = copies.find(placement.joint->target);if (found != copies.end()) placement.joint->target = found->second;}
        if (placement.button) {
            const auto remap = [&](std::string &id) {const auto found=copies.find(id); if(found!=copies.end()) id=found->second; };
            if (!Engine::OneOf(placement.button->action, {"loadScene", "loadSceneAdditive", "unloadScene", "setState"})) remap(placement.button->target);
            remap(placement.button->sound);
        }
        if (placement.prefab)
        {
            const auto root = copies.find(placement.prefab->rootId);
            if (root != copies.end()) placement.prefab->rootId = root->second;
            else placement.prefab.reset();
        }
    }

    void SceneWorld::InitializeAddedAnimator(Engine::Object3D &object, const ScenePlacement &placement, const SceneLayout &validation, const DirectX::XMFLOAT4X4 &matrix, const std::shared_ptr<const Engine::ModelRenderer> &model, PreparedLayout &prepared)
    {
        if (placement.animator) {
            if (!model->Rig()) throw std::runtime_error("Animator needs a glTF or GLB model");
            Animator::Validate(*placement.animator, model->Rig().get());
            Ragdoll::Validate(validation, placement, *model->Rig());
            auto animated = model->AnimatedCopy(materialDevice_.Get(), materialQueue_.Get());
            if (!animated || !animated->ApplyPose(Animator::Advance(*placement.animator, prepared.states[placement.id], *model->Rig(), 0, {}, &matrix))) throw std::runtime_error("Animator initialization failed");
            object.SetModel(animated); prepared.animated[placement.id] = std::move(animated);
        }

    }
    void SceneWorld::CommitAnimatorFrames(std::vector<AnimatorFrame> &frames)
    {
        for (auto &frame : frames)
        {
            if (frame.applyPose) frame.model->ApplyPreparedPose(std::move(frame.pose));
            animatorStates_.at(frame.id) = std::move(frame.state);
        }

    }
    bool SceneWorld::SameRenderResources(const SceneLayout &layout, const SceneLayout &before)
    {
        return layout.objects.size() == before.objects.size() && std::equal(layout.objects.begin(), layout.objects.end(), before.objects.begin(), [](const auto &a, const auto &b) {
            return std::tie(a.id, a.meshRenderer, a.material, a.animator, a.terrain, a.tilemap) == std::tie(b.id, b.meshRenderer, b.material, b.animator, b.terrain, b.tilemap);
        });
    }
    void SceneWorld::ValidateAnimatorModels(const PreparedLayout &candidate)
    {
        for (const auto &p : candidate.layout.objects) if (p.animator) {
                const auto instance = candidate.animated.find(p.id);
                if (instance == candidate.animated.end() || !instance->second->Rig()) throw std::runtime_error("Animator model missing: " + p.id);
                Ragdoll::Validate(candidate.layout, p, *instance->second->Rig());
            }

    }
    SceneWorld::ResourceChanges SceneWorld::DetectResourceChanges(const SceneLayout &layout, const SceneLayout &before, const std::unordered_map<std::string, size_t> &old)
    {
        ResourceChanges changes;
        for (const auto &p : layout.objects) {
            const auto found = old.find(p.id);
            if (found == old.end()) {changes.resolveAssets = changes.resolveAssets || HasRenderAsset(p); changes.animatedChange = changes.animatedChange || p.animator.has_value(); continue;}
            const auto &original = before.objects[found->second];
            changes.resolveAssets = changes.resolveAssets || DifferentMeshMaterial(p, original);
            changes.animatedChange = changes.animatedChange || DifferentAnimatedResources(p, original);

            }
        return changes;
    }
    Engine::Object3D SceneWorld::ReuseRuntimeObject(const ScenePlacement &p, PreparedLayout &candidate, const LayoutView &baseline, size_t index)
    {
        auto object = baseline.objects[index];
        if (p.animator) {
            const auto instance = baseline.animated.find(p.id);
            if (instance == baseline.animated.end() || !instance->second->Rig()) throw std::runtime_error("Animator model missing: " + p.id);
            Ragdoll::Validate(candidate.layout, p, *instance->second->Rig());
            candidate.animated.emplace(*instance);
        }
        if (const auto i = baseline.states.find(p.id); i != baseline.states.end()) candidate.states.emplace(*i);
        if (const auto i = baseline.lods.find(p.id); i != baseline.lods.end()) candidate.lods.emplace(*i);
        return object;
    }
    Engine::Object3D SceneWorld::BuildStaticRuntimeObject(const ScenePlacement &p, PreparedLayout &candidate)
    {
        Engine::Object3D object;
        if (p.meshRenderer) {
            const auto model = models_.Load(assetsRoot_ / p.Model());
            if (!model) throw std::runtime_error("Model could not be loaded: " + p.id);
            object.SetModel(model);
            for (const auto &lod : p.meshRenderer->lods) {
                auto loaded = models_.Load(assetsRoot_ / lod.model);
                if (!loaded || loaded->Rig()) throw std::runtime_error("Invalid static LOD: " + p.id);
                candidate.lods[p.id].push_back(std::move(loaded));
            }
            const auto &material = p.Material();
            if (!material.empty()) {
                auto i = candidate.materials.find(material);
                if (i == candidate.materials.end()) i = candidate.materials.emplace(material, MaterialAsset::Load(assetsRoot_, material, candidate.assets.get()).Prepare(materialDevice_.Get(), materialQueue_.Get(), assetsRoot_)).first;
                object.SetMaterial(i->second);
            }
            object.SetMaterialSlots(PrepareMaterialSlots(p, model->MeshCount(), materialDevice_.Get(), materialQueue_.Get(), assetsRoot_, candidate.materials, candidate.assets.get()));
        }
        PrepareProcedural(models_, object, p, assetsRoot_, materialDevice_.Get(), materialQueue_.Get(), candidate.assets.get(), &candidate.materials);
        return object;
    }
    bool SceneWorld::PrepareRuntimeObjects(PreparedLayout &candidate, const LayoutView &baseline, const std::unordered_map<std::string, size_t> &old)
    {
        bool replaced = false;
        for (const auto &p : candidate.layout.objects) {
            const auto found = old.find(p.id);
            const bool reuse = found != old.end() && p.meshRenderer == baseline.layout.objects[found->second].meshRenderer && p.material == baseline.layout.objects[found->second].material;
            Engine::Object3D object;
            if (reuse) {
                object = ReuseRuntimeObject(p, candidate, baseline, found->second);
            } else {
                replaced = true;
                object = BuildStaticRuntimeObject(p, candidate);
            }
            ApplySurfaceOverride(object, p);
            candidate.objects.push_back(std::move(object));
        }
        return replaced;
    }

    bool SceneWorld::MovePlayers(double seconds, float horizontal, float vertical, bool jump)
    {
        Engine::CpuScope scope("Physics and player movement");
        if (!ValidPlayerInput(seconds,horizontal,vertical)) return false;
        auto pending=std::min(seconds,0.1)+fixedSeconds_;
        auto pressed=fixedPressed_; for (const auto& [name,value]:inputPressed_) if (value) pressed[name]=true;
        bool pendingJump=fixedJump_ || jump;
        if (pending+1e-10<FixedStep()) {
            fixedSeconds_=pending; ++renderVersion_; fixedPressed_=std::move(pressed); fixedJump_=pendingJump; return true;
        }
        try {
            auto transaction=physicsWorld_.BeginTransaction();
            auto candidate=layout_; auto states=physics_; auto runtime=scripts_;
            std::string error;
            auto previousPoses=previousPhysical_,currentPoses=currentPhysical_;
            unsigned int steps=0;
            const auto MaximumFixedSteps=layout_.settings.maximumFixedSteps;
            while (pending+1e-10>=FixedStep() && steps<MaximumFixedSteps) {
                previousPoses=PhysicalPoses(candidate);
                if (!FixedTick(candidate,states,runtime,horizontal,vertical,pendingJump,pressed,error)) {
                    Engine::Log::Warning(error); return false;
                }
                currentPoses=PhysicalPoses(candidate); ++steps;
                pending=std::max(0.0,pending-FixedStep()); pendingJump=false; pressed.clear();
            }
            if(pending+1e-10>=FixedStep()) { pending=std::fmod(pending,FixedStep()); Engine::CpuProfiler::Add("Fixed backlog dropped",0); }
            PreparedLayout prepared;
            if (!PrepareScriptLayout(std::move(candidate),nullptr,prepared,error) ||
                (prepareRuntime_ && !prepareRuntime_(prepared.layout,error))) { Engine::Log::Warning(error); return false; }
            CommitLayout(std::move(prepared),true); physics_=std::move(states); scripts_=std::move(runtime);
            previousPhysical_=std::move(previousPoses); currentPhysical_=std::move(currentPoses);
            fixedSeconds_=pending; ++renderVersion_; fixedJump_=pendingJump; fixedPressed_=std::move(pressed); transaction.Commit(); return true;
        } catch (const std::exception& error) { Engine::Log::Warning(error.what()); return false; }
    }
    bool SceneWorld::FixedTick(SceneLayout& layout,ScenePhysics::States& states,ScriptRuntime& scripts,float horizontal,float vertical,bool jump,
        const std::map<std::string,bool>& pressed,std::string& error)
    {
        const bool hasFixedUpdate=ScriptRuntime::HasPhase(layout,ScriptPhase::FixedUpdate);
        if (hasFixedUpdate &&
            !scripts.UpdateCandidate(layout,FixedStep(),error,inputValues_,pressed,&physicsWorld_,ScriptPhase::FixedUpdate,&layout_)) return false;
        if(std::any_of(layout.objects.begin(),layout.objects.end(),[](const auto& p){return p.navAgent && p.navAgent->enabled && p.navAgent->moving;})) {
            Engine::CpuScope navigationScope("Navigation update");
            if(!Navigation::Advance(layout,FixedStep(),error)) return false;
        }
        const auto savedRootMotion=FixedRootOverrides(layout,scripts);
        if (hasFixedUpdate)
            for (const auto& [id,impulse]:scripts.Impulses()) if (!physicsWorld_.AddImpulse(id,impulse)) { error="FixedUpdate impulse rejected"; return false; }
        if(!SynchronizeRagdolls(layout,error)) return false;
        const auto ragdollOwnerEnabled=DisableRagdollOwners(layout,states);
        if(!AdvanceLegacyPlayers(layout,states,horizontal,vertical,jump)) return false;
        if(!physicsWorld_.Advance(layout,states,FixedStep(),horizontal,vertical,jump,assetsRoot_,error)) return false;
        RestoreRagdollOwners(layout,ragdollOwnerEnabled);
        for (auto& object:layout.objects) if (object.animator) object.animator->rootMotion=savedRootMotion.at(object.id);
        return QueueContacts(layout,scripts,error);
    }
    bool SceneWorld::QueueContacts(const SceneLayout& layout,ScriptRuntime& scripts,std::string& error) const
    {
        std::set<std::string> listeners;
        for (const auto& object : layout.objects) for (const auto& script:object.scripts) if (script.enabled)
        {
            const auto definition=ScriptRegistry::Definitions().find(script.behaviour);
            if (definition!=ScriptRegistry::Definitions().end() && definition->second.onEvent) listeners.insert(object.id);
        }
        for (const auto& contact : physicsWorld_.Events())
        {
            const auto name=std::string(contact.trigger ? "trigger" : "collision")+contact.phase;
            try
            {
                if (listeners.contains(contact.second)) scripts.QueueEvent({name,contact.first,contact.second,0});
                if (listeners.contains(contact.first)) scripts.QueueEvent({name,contact.second,contact.first,0});
            }
            catch (const std::exception& exception) { error=exception.what(); return false; }
        }
        return true;
    }

    SceneWorld::PreparedLayout SceneWorld::RuntimeLayout() const
    {
        PreparedLayout result; result.assetsRoot=assetsRoot_; result.layout=layout_; result.objects=objects_;
        result.assets=assetIndex_; result.materials=materials_;
        result.animated=animatedModels_; result.states=animatorStates_; result.lods=lodModels_; result.shadow=shadow_;result.localShadow=localShadow_;return result;
    }
    bool SceneWorld::PrepareScriptLayout(SceneLayout layout,const PreparedLayout* previous,PreparedLayout& result,std::string& error)
    {
        Engine::CpuScope scope("Script render resource preparation");
        try {
            const auto baseline=BaselineView(previous);
            const bool unchangedResources=SameRenderResources(layout,baseline.layout);
            if(unchangedResources) {
                PreparedLayout candidate; candidate.assetsRoot=baseline.root; candidate.assets=baseline.assets; candidate.materials=baseline.materials;
                candidate.objects=baseline.objects; candidate.animated=baseline.animated; candidate.states=baseline.states; candidate.lods=baseline.lods; candidate.layout=std::move(layout);
                ValidateAnimatorModels(candidate);
                if(!PrepareTransforms(candidate.layout,candidate.objects,error,&baseline.layout)) return false;
                result=std::move(candidate); error.clear(); return true;
            }
            std::unordered_map<std::string,size_t> old;
            for(size_t i=0;i<baseline.layout.objects.size();++i) old.emplace(baseline.layout.objects[i].id,i);
            const auto changes=DetectResourceChanges(layout,baseline.layout,old);
            if(changes.animatedChange) {
                if(!retirement_->Retire(std::make_shared<std::vector<Engine::Object3D>>(baseline.objects))) throw std::runtime_error("Script resource retirement failed");
                if(previous) return PrepareLayout(std::move(layout),assetsRoot_,error,true,result,previous);
                const auto snapshot=RuntimeLayout(); return PrepareLayout(std::move(layout),assetsRoot_,error,true,result,&snapshot);
            }
            if(changes.resolveAssets) {
                if(!baseline.assets) throw std::runtime_error("Runtime asset index missing");
                layout.ResolveAssets(*baseline.assets);
            }
            PreparedLayout candidate; candidate.assetsRoot=baseline.root; candidate.assets=baseline.assets;
            candidate.materials=baseline.materials; candidate.layout=std::move(layout);
            candidate.objects.reserve(candidate.layout.objects.size());
            const bool replaced=PrepareRuntimeObjects(candidate,baseline,old);
            if(!PrepareTransforms(candidate.layout,candidate.objects,error)) return false;
            if(replaced || candidate.layout.objects.size()!=baseline.layout.objects.size()) {
                if(!retirement_->Retire(std::make_shared<std::vector<Engine::Object3D>>(baseline.objects))) throw std::runtime_error("Script resource retirement failed");
            }
            result=std::move(candidate); error.clear(); return true;
        } catch(const std::exception& exception) {error=exception.what();return false;}
    }

    bool SceneWorld::QueueScriptEvent(ScriptEvent event,std::string& error)
    {
        try { scripts_.QueueEvent(std::move(event)); error.clear(); return true; }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }

    bool SceneWorld::UpdateComponents(double seconds)
    {
        Engine::CpuScope scope("Components update");
        if (!std::isfinite(seconds) || seconds<=0) return false;
        auto candidate=layout_;
        RotateComponents(candidate,seconds);
        auto runtime=scripts_;
        std::string error;
        if (!runtime.UpdateCandidate(candidate,seconds,error,inputValues_,inputPressed_,&physicsWorld_,ScriptPhase::Update,&layout_)) { Engine::Log::Warning(error); return false; }
        PreparedLayout prepared;
        if (!PrepareScriptLayout(std::move(candidate),nullptr,prepared,error)) { Engine::Log::Warning(error); return false; }
        std::vector<AnimatorFrame> frames;
        if (!PrepareAnimators(prepared.layout,seconds,prepared.animated,prepared.states,runtime,frames,error))
        { Engine::Log::Warning(error); return false; }
        if (!PrepareRootMotion(prepared,frames,error)) { Engine::Log::Warning(error); return false; }
        for (const auto& frame:frames) prepared.states.at(frame.id)=frame.state;
        auto impulses=runtime.Impulses();
        if(!UpdateLateComponents(seconds,prepared,runtime,frames,impulses,error)) return false;
        if (prepareRuntime_ && !prepareRuntime_(prepared.layout,error)) { Engine::Log::Warning(error); return false; }
        if(prepareUi_ && !prepareUi_(prepared.layout,runtime.UiCommands(),error)) {Engine::Log::Warning(error); return false;}
        CommitLayout(std::move(prepared),true);
        CommitAnimatorFrames(frames);
        for (const auto& [id,impulse] : impulses) physicsWorld_.AddImpulse(id,impulse);
        scripts_=std::move(runtime);
        return true;
    }

    bool SceneWorld::PrepareRootMotion(PreparedLayout& prepared,std::vector<AnimatorFrame>& frames,std::string& error) const
    {
        Engine::CpuScope scope("Root motion and IK");
        if(frames.empty()) {error.clear();return true;}
        try
        {
            const auto before=prepared.layout; std::vector<size_t> actors;
            for (const auto& frame : frames)
            {
                auto& placement=prepared.layout.objects.at(frame.index);
                if(!ApplyRootDelta(placement,frame)) continue;
                actors.push_back(frame.index);
            }
            if (actors.empty()) return true;
            if (!physicsWorld_.ConstrainRootMotion(before,prepared.layout,actors,assetsRoot_,error)) return false;
            if (!PrepareTransforms(prepared.layout,prepared.objects,error)) return false;
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(prepared.layout,matrices,error)) return false;
            ConstrainRootFrames(prepared,frames,matrices);
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
        ++renderVersion_;
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
            PreparedLayout prepared;
            auto object=BuildAddedObject(placement,validation,matrices.back(),assetsRoot,prepared);
            const auto id = placement.id;
            Append(std::move(placement), std::move(object));
            layout_.assetReferences=std::move(validation.assetReferences);
            animatedModels_.merge(prepared.animated); animatorStates_.merge(prepared.states); lodModels_.merge(prepared.lods);
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
        auto animated=animatedModels_; auto states=animatorStates_; auto lods=lodModels_;
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
            RemapDuplicateLinks(placement,copies);
            const auto parent=copies.find(placement.parentId);
            if (parent!=copies.end()) placement.parentId=parent->second;
            else if (!TranslatePlacement(placement,offset))
            { error=placement.id+": invalid world duplicate offset"; return false; }
            placement.id=copies.at(placement.id); placement.name+=" copy";
            if (const auto levels=lodModels_.find(layout_.objects[index].id);levels!=lodModels_.end()) lods[placement.id]=levels->second;
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
            CopySceneOwnership(candidate,layout_.objects[index].id,copies.at(layout_.objects[index].id));
            objects.push_back(std::move(object));
        }
        if (!PrepareTransforms(candidate,objects,error)) return false;
        layout_=std::move(candidate); objects_=std::move(objects); ++renderVersion_;
        animatedModels_=std::move(animated); animatorStates_=std::move(states);
        lodModels_=std::move(lods);
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
        auto candidate=layout_;candidate.objects.clear();
        for(auto& [name,members]:candidate.sceneObjects) {
            static_cast<void>(name);
            std::erase_if(members,[&](const auto& id){return removed.contains(id);});
        }
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
        layout_=std::move(candidate); objects_=std::move(objects); ++renderVersion_;
        for (const auto& id : removed) { animatedModels_.erase(id); animatorStates_.erase(id); physics_.erase(id); lodModels_.erase(id); previousPhysical_.erase(id); currentPhysical_.erase(id); }
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
            const auto& placement=layout_.objects[index];
            const bool enabled=RenderableEnabled(placement);
            if(!object.GetModel()||!enabled) continue;
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

    std::map<std::string,SceneWorld::PhysicalPose> SceneWorld::PhysicalPoses(const SceneLayout& layout)
    {
        std::map<std::string,PhysicalPose> result;
        for(const auto& p:layout.objects)
            if(HasPhysicalPose(p))
                result.emplace(p.id,PhysicalPose{p.position,p.rotation,p.scale,p.parentId});
        return result;
    }
    bool SceneWorld::InterpolatedLocalTransform(const ScenePlacement& p,DirectX::XMFLOAT4X4& transform,DirectX::XMFLOAT4X4& rotation) const
    {
        if (!HasPhysicalPose(p)) return false;
        const auto before=previousPhysical_.find(p.id),now=currentPhysical_.find(p.id);
        if(before==previousPhysical_.end() || now==currentPhysical_.end()) return false;
        const auto& a=before->second; const auto& b=now->second;
        if(p.position!=b.position || p.rotation!=b.rotation || p.scale!=b.scale || p.parentId!=a.parent || p.parentId!=b.parent) return false;
        float distance=0;
        for(size_t i=0;i<3;++i) distance+=(b.position[i]-a.position[i])*(b.position[i]-a.position[i]);
        if(distance>4) return false;
        // Saving a simulated SRT can introduce a few ULPs of scale error; genuine resize edits still snap above.
        for(size_t i=0;i<3;++i)
            if(std::abs(a.scale[i]-b.scale[i])>1e-5f*std::max(1.0f,std::max(std::abs(a.scale[i]),std::abs(b.scale[i])))) return false;
        const float alpha=static_cast<float>(std::clamp(fixedSeconds_/FixedStep(),0.0,1.0));
        std::array<float,3> position;
        for(size_t i=0;i<3;++i) position[i]=a.position[i]+(p.position[i]-a.position[i])*alpha;
        using namespace DirectX;
        const auto localRotation=[](const auto& angles) {return XMMatrixRotationX(angles[0])*XMMatrixRotationY(angles[1])*XMMatrixRotationZ(angles[2]);};
        const auto orientation=XMQuaternionSlerp(XMQuaternionRotationMatrix(localRotation(a.rotation)),XMQuaternionRotationMatrix(localRotation(p.rotation)),alpha);
        const auto interpolatedRotation=XMMatrixRotationQuaternion(orientation);
        XMStoreFloat4x4(&rotation,interpolatedRotation);
        XMStoreFloat4x4(&transform,XMMatrixScaling(p.scale[0],p.scale[1],p.scale[2])*interpolatedRotation*XMMatrixTranslation(position[0],position[1],position[2]));
        return true;
    }
    bool SceneWorld::PrepareRenderTransforms() const
    {
        if(preparedRenderVersion_==renderVersion_) return true;
        std::map<std::string,DirectX::XMFLOAT4X4> overrides;
        for(const auto& p:layout_.objects) {
            DirectX::XMFLOAT4X4 matrix,rotation;
            if(InterpolatedLocalTransform(p,matrix,rotation)) overrides.emplace(p.id,matrix);
        }
        if(overrides.empty()) {
            renderMatrices_.clear(); renderMatrices_.reserve(objects_.size());
            std::transform(objects_.begin(),objects_.end(),std::back_inserter(renderMatrices_),[](const auto& object) {return object.GetWorldMatrix();});
        } else {
            std::string error;
            if(!SceneTransforms::Resolve(layout_,overrides,renderMatrices_,error)) {Engine::Log::Warning(error);return false;}
        }
        preparedRenderVersion_=renderVersion_; return true;
    }
    bool SceneWorld::RenderWorldMatrix(std::string_view id,DirectX::XMFLOAT4X4& matrix) const
    {
        if(!PrepareRenderTransforms()) return false;
        const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),[&](const auto& p){return p.id==id;});
        if(found==layout_.objects.end()) return false;
        matrix=renderMatrices_[static_cast<size_t>(found-layout_.objects.begin())]; return true;
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

    bool SceneWorld::RenderWorldRotation(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        auto rotation=DirectX::XMMatrixIdentity();
        auto current=std::string(id);
        size_t remaining=layout_.objects.size();
        do
        {
            const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
                [&](const auto& placement) { return placement.id==current; });
            if (found==layout_.objects.end() || remaining--==0) return false;
            DirectX::XMFLOAT4X4 transform,local;
            if (InterpolatedLocalTransform(*found,transform,local)) rotation=rotation*DirectX::XMLoadFloat4x4(&local);
            else {
                const auto& angles=found->rotation;
                rotation=rotation*DirectX::XMMatrixRotationX(angles[0])*DirectX::XMMatrixRotationY(angles[1])*DirectX::XMMatrixRotationZ(angles[2]);
            }
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
