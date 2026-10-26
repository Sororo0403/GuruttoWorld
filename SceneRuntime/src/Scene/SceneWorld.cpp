#include <Engine/Core/Profiler.h>
#include <SceneRuntime/Scene/SceneWorld.h>
#include "SceneWorldRuntime.h"
#include <SceneRuntime/Rendering/RenderScene.h>
#include <SceneRuntime/Rendering/SceneResourceChanges.h>
#include <SceneRuntime/Scene/SceneAssetReferences.h>
#include <iterator>
#include <Engine/Core/Validation.h>
#include <tuple>
#include <Engine/Graphics/Resources/GpuProfiler.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <SceneRuntime/Ui/SceneUi.h>
#include <SceneRuntime/Scene/Prefab.h>
#include <SceneRuntime/Rendering/MaterialAsset.h>
#include <SceneRuntime/Gameplay/GenreGeometry.h>
#include <SceneRuntime/Gameplay/Navigation.h>
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
#include "../Rendering/SceneRenderPlan.h"

namespace
{
    bool HasRenderAsset(const SceneRuntime::ScenePlacement& p)
    {
        return p.meshRenderer || p.material || p.terrain || p.tilemap;
    }
    bool RenderableEnabled(const SceneRuntime::ScenePlacement& p)
    {
        return (p.meshRenderer && p.meshRenderer->enabled) || (p.terrain && p.terrain->enabled) ||
               (p.tilemap && p.tilemap->enabled);
    }
    bool HasPhysicalPose(const SceneRuntime::ScenePlacement& p)
    {
        return (p.playerController && p.playerController->enabled) ||
               (p.rigidBody && p.rigidBody->enabled && p.rigidBody->motion != "static");
    }
    void ApplySurfaceOverride(Engine::Object3D& object, const SceneRuntime::ScenePlacement& placement)
    {
        if (!placement.material || !placement.material->enabled || !placement.material->surfaceOverride)
            return;
        const auto& surface = *placement.material;
        const auto apply =
            [&](const std::shared_ptr<const Engine::Material>& original) -> std::shared_ptr<const Engine::Material>
        {
            if (original && original->color == surface.color && original->roughness == surface.roughness &&
                original->metallic == surface.metallic && original->shaderParameters == surface.shaderParameters)
                return original;
            auto material =
                original ? std::make_shared<Engine::Material>(*original) : std::make_shared<Engine::Material>();
            material->color = surface.color;
            material->roughness = surface.roughness;
            material->metallic = surface.metallic;
            material->shaderParameters = surface.shaderParameters;
            return material;
        };
        object.SetMaterial(apply(object.GetMaterial()));
        auto slots = object.GetMaterialSlots();
        for (auto& slot : slots)
            if (slot)
                slot = apply(slot);
        object.SetMaterialSlots(std::move(slots));
    }
    void PrepareProcedural(
        Engine::ModelManager& models, Engine::Object3D& object, const SceneRuntime::ScenePlacement& p,
        const std::filesystem::path& root, ID3D12Device* device, ID3D12CommandQueue* queue,
        const Engine::AssetDatabase* index = nullptr,
        std::map<std::filesystem::path, std::shared_ptr<const Engine::Material>>* materials = nullptr)
    {
        if (!p.terrain && !p.tilemap)
            return;
        auto model = models.Procedural(p.id, root.generic_string() + SceneRuntime::GenreGeometry::Signature(p),
                                       [&]
                                       {
                                           return std::vector<Engine::MeshData>{
                                               p.terrain ? SceneRuntime::GenreGeometry::Terrain(*p.terrain, root)
                                                         : SceneRuntime::GenreGeometry::Tilemap(*p.tilemap, root)};
                                       });
        object.SetModel(std::move(model));
        if (p.tilemap)
        {
            auto material = std::make_shared<Engine::Material>();
            material->transparent = true;
            object.SetMaterial(std::move(material));
        }
        if (!p.Material().empty())
        {
            std::shared_ptr<const Engine::Material> material;
            if (materials)
            {
                auto found = materials->find(p.Material());
                if (found == materials->end())
                    found = materials
                                ->emplace(p.Material(),
                                          SceneRuntime::MaterialAsset::Load(root, p.Material(), index)
                                              .Prepare(device, queue, root,
                                                       index ? index->Content()
                                                             : std::shared_ptr<const Engine::ContentContext>{}))
                                .first;
                material = found->second;
            }
            else
                material = SceneRuntime::MaterialAsset::Load(root, p.Material(), index)
                               .Prepare(device, queue, root,
                                        index ? index->Content() : std::shared_ptr<const Engine::ContentContext>{});
            object.SetMaterial(std::move(material));
        }
    }

    Engine::MaterialSlots PrepareMaterialSlots(
        const SceneRuntime::ScenePlacement& placement, size_t meshes, ID3D12Device* device, ID3D12CommandQueue* queue,
        const std::filesystem::path& root,
        std::map<std::filesystem::path, std::shared_ptr<const Engine::Material>>& cache,
        const Engine::AssetDatabase* index = nullptr)
    {
        Engine::MaterialSlots result;
        if (!placement.material || !placement.material->enabled)
            return result;
        const auto& paths = placement.material->slots;
        result.resize(std::min(meshes, paths.size()));
        for (size_t i = 0; i < result.size(); ++i)
            if (!paths[i].empty())
            {
                auto found = cache.find(paths[i]);
                if (found == cache.end())
                    found =
                        cache
                            .emplace(paths[i], SceneRuntime::MaterialAsset::Load(root, paths[i], index)
                                                   .Prepare(device, queue, root,
                                                            index ? index->Content()
                                                                  : std::shared_ptr<const Engine::ContentContext>{}))
                            .first;
                result[i] = found->second;
            }
        return result;
    }
    bool HasTransformAnimation(const SceneRuntime::ScenePlacement& placement)
    {
        return placement.animation && placement.animation->enabled &&
               std::any_of(placement.animation->tracks.begin(), placement.animation->tracks.end(),
                           [](const auto& track)
                           {
                               return !track.property.starts_with("ui") && track.property != "opacity";
                           });
    }
    bool ApplyTransformAnimation(SceneRuntime::ScenePlacement& placement, const SceneRuntime::ScenePlacement& before,
                                 const std::map<std::string, float>& clocks)
    {
        if (!HasTransformAnimation(placement))
            return false;
        for (const auto& track : placement.animation->tracks)
        {
            if (track.property.starts_with("ui") || track.property == "opacity")
                continue;
            const auto clock = clocks.find(track.clock);
            if (clock == clocks.end() || clock->second < track.delay)
                continue;
            const auto value = SceneRuntime::Animation::Sample(track, clocks);
            if (!value)
                continue;
            SceneRuntime::Animation::Apply(placement, track.property, *value);
        }
        return before.position != placement.position || before.rotation != placement.rotation ||
               before.scale != placement.scale || !before.SameComponents(placement);
    }
    bool IsRotation(const DirectX::XMFLOAT4X4& rotation)
    {
        if (!SceneRuntime::SceneTransforms::IsUsable(rotation))
            return false;
        const auto matrix = DirectX::XMLoadFloat4x4(&rotation);
        DirectX::XMFLOAT4X4 product, identity;
        DirectX::XMStoreFloat4x4(&product, matrix * DirectX::XMMatrixTranspose(matrix));
        DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
        return DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(matrix)) > 0 &&
               SceneRuntime::SceneTransforms::Matches(product, identity);
    }

}

namespace SceneRuntime
{
    bool SceneWorld::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
                                const std::filesystem::path& layoutPath, const std::filesystem::path& shaderPath,
                                std::string* diagnostic, bool allowReferenceMigration)
    {
        if (!CanMutateScene(diagnostic))
            return false;
        try
        {
            if (!context_)
                context_ = Engine::ContentContext::Capture(assetsRoot);
            context_->RequireRoot(assetsRoot);
            document_->context_ = context_;
            if (execution_)
                execution_->values.scripts.SetContentContext(context_);
        }
        catch (const std::exception& failure)
        {
            if (diagnostic)
                *diagnostic = failure.what();
            return false;
        }
        std::string declarationError;
        const bool declarationCurrent = context_->CheckConfiguration(declarationError);
        if (!declarationCurrent && !IsDocumentView())
        {
            if (diagnostic)
                *diagnostic = declarationError;
            return false;
        }
        if (declarationCurrent &&
            !InitializeModels(renderer, context_->Resolve(std::filesystem::absolute(shaderPath)), diagnostic) &&
            !IsDocumentView())
            return false;
        std::string error;
        const bool loaded = Reload(assetsRoot, layoutPath, error, allowReferenceMigration);
        if (diagnostic)
            *diagnostic = error;
        return loaded;
    }

    bool SceneWorld::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
                                SceneLayout layout, const std::filesystem::path& shaderPath, std::string* diagnostic)
    {
        if (!CanMutateScene(diagnostic))
            return false;
        try
        {
            if (!context_)
                context_ = Engine::ContentContext::Capture(assetsRoot);
            context_->RequireRoot(assetsRoot);
            document_->context_ = context_;
            if (execution_)
                execution_->values.scripts.SetContentContext(context_);
        }
        catch (const std::exception& failure)
        {
            if (diagnostic)
                *diagnostic = failure.what();
            return false;
        }
        std::string declarationError;
        const bool declarationCurrent = context_->CheckConfiguration(declarationError);
        if (!declarationCurrent && !IsDocumentView())
        {
            if (diagnostic)
                *diagnostic = declarationError;
            return false;
        }
        if (declarationCurrent &&
            !InitializeModels(renderer, context_->Resolve(std::filesystem::absolute(shaderPath)), diagnostic) &&
            !IsDocumentView())
            return false;
        std::string error;
        bool loaded = false;
        try
        {
            if (!IsDocumentView())
            {
                layout.ResolveAssets(Engine::AssetDatabase(context_, true));
                Prefab::Refresh(layout, assetsRoot, {}, context_);
            }
            loaded = ReplaceLayout(std::move(layout), assetsRoot, error);
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
        }
        if (diagnostic)
            *diagnostic = error;
        return loaded;
    }

    bool SceneWorld::InitializeModels(const Engine::DirectX12Renderer& renderer,
                                      const std::filesystem::path& shaderPath, std::string* diagnostic)
    {
        Engine::CpuScope timing("Scene renderer initialization");
        materialDevice_ = renderer.GetDevice();
        materialQueue_ = renderer.GetCommandQueue();
        retirement_ = Engine::GpuRetirement::Shared(materialDevice_.Get(), materialQueue_.Get());
        if (!retirement_)
        {
            if (diagnostic)
                *diagnostic = "Cannot initialize resource retirement";
            return false;
        }
        meshShader_ = shaderPath;
        shadow_ = std::make_shared<Engine::ShadowMap>();
        localShadow_ = std::make_shared<Engine::ShadowMap>();
        modelsReady_ = models_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), shaderPath) &&
                       shadow_->Initialize(renderer.GetDevice(), shaderPath) &&
                       localShadow_->Initialize(renderer.GetDevice(), shaderPath, true) &&
                       occlusion_.Initialize(renderer.GetDevice(), shaderPath);
        if (!modelsReady_)
        {
            if (diagnostic)
                *diagnostic = "Scene renderer could not be initialized. Check shaders and restart.";
            return false;
        }
        return true;
    }
    bool SceneWorld::Animate(const std::map<std::string, float>& clocks)
    {
        if (runtime_)
        {
            if (!CanMutateScene())
                return false;
            ChangePreparation preparing(preparingChanges_);
            std::string error;
            const bool success = runtime_->session.Animate(clocks, error);
            if (success)
                DrainSession();
            else if (!error.empty())
                Engine::Log::Warning(error);
            return success;
        }
        Engine::CpuScope timing("Transform animation");
        if (!CanMutateScene())
            return false;
        if (std::none_of(layout_.objects.begin(), layout_.objects.end(), HasTransformAnimation))
        {
            animationScratch_.reset();
            return true;
        }
        auto& candidate = [&]() -> SceneLayout&
        {
            Engine::CpuScope scope("Animation layout clone");
            // 全内容を毎回同期し、外部編集と失敗後の候補を持ち越さず格納領域だけ再利用します。
            if (!animationScratch_)
                animationScratch_ = std::make_unique<SceneLayout>();
            *animationScratch_ = layout_;
            return *animationScratch_;
        }();
        std::vector<size_t> changed;
        {
            Engine::CpuScope scope("Animation evaluate placements");
            for (size_t index = 0; index < candidate.objects.size(); ++index)
                if (ApplyTransformAnimation(candidate.objects[index], layout_.objects[index], clocks))
                    changed.push_back(index);
        }
        if (changed.empty())
            return true;
        if (runtime_)
        {
            std::string error;
            return EditSession(ReplaceDocumentEdit{candidate}, error);
        }
        auto changes = changes_;
        changes.Merge(SceneChangeSet::Compare(layout_, candidate));
        try
        {
            Engine::CpuScope scope("Animation validate changes");
            candidate.ValidateChanges(layout_);
        }
        catch (const std::exception& error)
        {
            Engine::Log::Warning(error.what());
            return false;
        }
        {
            Engine::CpuScope scope("Animation commit transforms");
            if (!CommitTransforms(candidate))
                return false;
        }
        {
            Engine::CpuScope scope("Animation copy changed components");
            for (const auto i : changed)
            {
                layout_.objects[i].CopyComponents(candidate.objects[i]);
                ApplySurfaceOverride(objects_[i], layout_.objects[i]);
            }
        }
        changes_.Swap(changes);
        return true;
    }

    bool SceneWorld::Reload(const std::filesystem::path& assetsRoot, const std::filesystem::path& layoutPath,
                            std::string& error, bool allowReferenceMigration)
    {
        if (!CanMutateScene(&error))
            return false;
        try
        {
            return ReloadLayout(assetsRoot, SceneLayout::Load(layoutPath), error, allowReferenceMigration);
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool SceneWorld::ReloadLayout(const std::filesystem::path& assetsRoot, SceneLayout layout, std::string& error,
                                  bool allowReferenceMigration)
    {
        if (!CanMutateScene(&error))
            return false;
        try
        {
            if (!context_)
                context_ = Engine::ContentContext::Capture(assetsRoot);
            context_->RequireRoot(assetsRoot);
            std::string migration;
            try
            {
                if (IsDocumentView())
                    static_cast<void>(
                        ResolveSceneAssetReferencesForEditing(layout, Engine::AssetDatabase(context_, true)));
                else
                    layout.ResolveAssets(Engine::AssetDatabase(context_, true));
                Prefab::Refresh(layout, assetsRoot, {}, context_);
            }
            catch (const PrefabReferenceMigrationRequired& exception)
            {
                if (!allowReferenceMigration && !IsDocumentView())
                    throw;
                migration = exception.what();
            }
            catch (const std::exception& exception)
            {
                if (!IsDocumentView())
                    throw;
                migration = exception.what();
            }
            if (execution_)
                return ReplaceSession(std::move(layout), assetsRoot, false, error);
            PreparedLayout prepared;
            if (!PrepareLayout(std::move(layout), assetsRoot, error, false, prepared))
                return false;
            if (!migration.empty() && IsDocumentView())
                prepared.diagnostics.push_back(migration);
            CommitLayout(std::move(prepared), false);
            error.swap(migration);
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool SceneWorld::ReloadAssets(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
                                  const std::filesystem::path& shaderPath, std::string& error)
    {
        if (!CanMutateScene(&error))
            return false;
        ChangePreparation preparing(preparingChanges_);
        auto storage = std::make_shared<SceneDocumentData>(SceneLayout{}, context_);
        auto candidate = IsDocumentView() ? std::make_unique<SceneWorld>(storage) : std::make_unique<SceneWorld>();
        if (context_ && !candidate->Content())
            candidate->SetContentContext(context_);
        if (!candidate->Initialize(renderer, assetsRoot, layout_, shaderPath, &error))
            return false;
        auto prepared = candidate->RuntimeLayout();
        prepared.shadow = candidate->shadow_;
        prepared.localShadow = candidate->localShadow_;
        auto packet = runtime_ ? PrepareRuntimePacket(prepared, error) : candidate->documentPacket_;
        if (candidate->modelsReady_ && !packet)
            return false;
        auto changes = changes_;
        changes.Merge(SceneChangeSet::Compare(layout_, candidate->layout_));
        changes.fields.push_back({{}, {}, "resources.reload"});
        models_.Swap(candidate->models_);
        layout_ = std::move(candidate->layout_);
        animationScratch_.reset();
        objects_.swap(candidate->objects_);
        animatedModels_.swap(candidate->animatedModels_);
        animatorStates_.swap(candidate->animatorStates_);
        lodModels_.swap(candidate->lodModels_);
        shadow_.swap(candidate->shadow_);
        localShadow_.swap(candidate->localShadow_);
        std::swap(occlusion_, candidate->occlusion_);
        assetIndex_.swap(candidate->assetIndex_);
        materials_.swap(candidate->materials_);
        resourceDiagnostics_ = std::move(candidate->resourceDiagnostics_);
        modelsReady_ = candidate->modelsReady_;
        meshShader_ = candidate->meshShader_;
        if (runtime_)
            runtime_->packet.swap(packet);
        else
            documentPacket_.swap(packet);
        ++renderVersion_;
        changes_.Swap(changes);
        error.clear();
        return true;
    }

    bool SceneWorld::ReplaceLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error,
                                   bool preserveExecution)
    {
        if (execution_)
        {
            if (runtime_ && preserveExecution)
            {
                if (!CheckSessionRoot(assetsRoot, error))
                    return false;
                return EditSession(ReplaceDocumentEdit{std::move(layout)}, error);
            }
            return ReplaceSession(std::move(layout), assetsRoot, false, error);
        }
        if (!CanMutateScene(&error))
            return false;
        ChangePreparation preparing(preparingChanges_);
        try
        {
            PreparedLayout prepared;
            if (!PrepareLayout(std::move(layout), assetsRoot, error, preserveExecution, prepared))
                return false;
            std::optional<ScriptRuntime> runtime;
            if (preserveExecution && execution_)
            {
                runtime = execution_->values.scripts;
                auto stopped = prepared.layout;
                runtime->PrepareTransition(layout_, stopped);
                PreparedLayout candidate;
                if (!PrepareScriptLayout(std::move(stopped), &prepared, candidate, error) ||
                    !PrepareRuntimeCallbacks(candidate.layout, *runtime, error))
                    return false;
                if (!candidate.shadow)
                    candidate.shadow = prepared.shadow;
                if (!candidate.localShadow)
                    candidate.localShadow = prepared.localShadow;
                prepared = std::move(candidate);
            }
            else if (preserveExecution && prepareRuntime_ && !prepareRuntime_(prepared.layout, error))
                return false;
            auto changes = changes_;
            changes.Merge(SceneChangeSet::Compare(layout_, prepared.layout));
            if (execution_ && !preserveExecution)
                changes.fields.push_back({{}, {}, "runtime.reset"});
            if (runtime)
                changes.Merge(runtime->ChangesSince(execution_->values.scripts));
            CommitLayout(std::move(prepared), preserveExecution);
            if (runtime)
            {
                runtime->CommitStarts();
                execution_->values.scripts = std::move(*runtime);
            }
            changes_.Swap(changes);
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
        catch (...)
        {
            error = "Scene replacement preparation failed";
            return false;
        }
    }
    SceneWorld::LayoutView SceneWorld::BaselineView(const PreparedLayout* previous) const
    {
        if (previous)
            return {previous->layout,    previous->objects,  previous->assetsRoot, previous->assets,
                    previous->materials, previous->animated, previous->states,     previous->lods};
        return {layout_, objects_, assetsRoot_, assetIndex_, materials_, animatedModels_, animatorStates_, lodModels_};
    }
    Engine::Object3D SceneWorld::BuildLayoutObject(const ScenePlacement& placement, const SceneLayout& layout,
                                                   const DirectX::XMFLOAT4X4& matrix, PreparedLayout& prepared,
                                                   const LayoutView& baseline, bool preserveExecution)
    {
        Engine::Object3D object;
        if (placement.animator && !placement.meshRenderer)
            throw std::runtime_error("Animator needs a MeshRenderer");
        if (placement.meshRenderer)
        {
            const auto model = models_.Load(prepared.assetsRoot / placement.Model());
            if (!model)
                throw std::runtime_error("Model could not be loaded: " + placement.id);
            object.SetModel(model);
            for (const auto& lod : placement.meshRenderer->lods)
            {
                if (placement.animator)
                    throw std::runtime_error("Animated meshes cannot use static LOD models");
                auto loaded = models_.Load(prepared.assetsRoot / lod.model);
                if (!loaded || loaded->IsAnimated())
                    throw std::runtime_error("Static LOD model could not be loaded: " + placement.id);
                prepared.lods[placement.id].push_back(std::move(loaded));
            }
            InitializeAnimatorObject(object, placement, layout, matrix, prepared, baseline, preserveExecution);
            const auto& material = placement.Material();
            if (!material.empty())
            {
                auto found = prepared.materials.find(material);
                if (found == prepared.materials.end())
                    found =
                        prepared.materials
                            .emplace(material, MaterialAsset::Load(prepared.assetsRoot, material, prepared.assets.get())
                                                   .Prepare(materialDevice_.Get(), materialQueue_.Get(),
                                                            prepared.assetsRoot, context_))
                            .first;
                object.SetMaterial(found->second);
            }
            object.SetMaterialSlots(PrepareMaterialSlots(placement, model->MeshCount(), materialDevice_.Get(),
                                                         materialQueue_.Get(), prepared.assetsRoot, prepared.materials,
                                                         prepared.assets.get()));
        }
        PrepareProcedural(models_, object, placement, prepared.assetsRoot, materialDevice_.Get(), materialQueue_.Get(),
                          prepared.assets.get(), &prepared.materials);
        ApplySurfaceOverride(object, placement);
        if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
            throw std::runtime_error("Invalid transform: " + placement.id);
        if (!object.SetWorldMatrix(matrix))
            throw std::runtime_error("Invalid world matrix");
        return object;
    }
    void SceneWorld::InitializeAnimatorObject(Engine::Object3D& object, const ScenePlacement& placement,
                                              const SceneLayout& layout, const DirectX::XMFLOAT4X4& matrix,
                                              PreparedLayout& prepared, const LayoutView& baseline,
                                              bool preserveExecution)
    {
        if (!placement.animator)
            return;
        const auto model = object.GetModel();

        if (!model->Rig())
            throw std::runtime_error("Animator needs a glTF or GLB model");
        Animator::Validate(*placement.animator, model->Rig().get());
        Ragdoll::Validate(layout, placement, *model->Rig());
        const auto old = std::find_if(baseline.layout.objects.begin(), baseline.layout.objects.end(),
                                      [&](const auto& item)
                                      {
                                          return item.id == placement.id;
                                      });
        const bool reuse = preserveExecution && old != baseline.layout.objects.end() &&
                           old->meshRenderer == placement.meshRenderer && old->animator == placement.animator &&
                           baseline.animated.contains(placement.id);
        auto instance = reuse ? baseline.animated.at(placement.id)
                              : model->AnimatedCopy(materialDevice_.Get(), materialQueue_.Get());
        if (reuse)
            prepared.states[placement.id] = baseline.states.at(placement.id);
        if (!instance ||
            (!reuse && !instance->ApplyPose(Animator::Advance(*placement.animator, prepared.states[placement.id],
                                                              *model->Rig(), 0, {}, &matrix))))
            throw std::runtime_error("Animator initialization failed");
        object.SetModel(instance);
        prepared.animated[placement.id] = std::move(instance);
    }

    void SceneWorld::ApplyAnimatorOverrides(const std::string& id, const ScriptRuntime& scripts, AnimatorState& state)
    {
        if (const auto change = scripts.RootMotions().find(id); change != scripts.RootMotions().end())
            state.rootMotionOverride = change->second;
        if (const auto changes = scripts.AnimatorParameters().find(id); changes != scripts.AnimatorParameters().end())
            for (const auto& [name, value] : changes->second)
                state.parameterOverrides[name] = value;
        Animator::ValidateParameters(state.parameterOverrides);
        if (const auto changes = scripts.IkTargets().find(id); changes != scripts.IkTargets().end())
            for (const auto& [name, target] : changes->second)
            {
                if (target)
                    state.ikOverrides[name] = *target;
                else
                    state.ikOverrides.erase(name);
            }
    }

    std::map<std::string, float> SceneWorld::AnimatorInputs(const SceneLayout& layout, const std::string& id) const
    {
        if (!execution_)
            return {{"moveX", 0.0f}, {"moveY", 0.0f}, {"speed", 0.0f}, {"grounded", 1.0f}};
        auto parameters = execution_->values.inputValues;
        const auto input = [&](const std::string& name)
        {
            const auto found = execution_->values.inputValues.find(name);
            return found == execution_->values.inputValues.end() ? 0.0f : found->second;
        };
        const auto& mapping = layout.settings.playerInputs;
        parameters["moveX"] = input(mapping[0]) - input(mapping[1]);
        parameters["moveY"] = input(mapping[2]) - input(mapping[3]);
        parameters["speed"] = std::hypot(parameters["moveX"], parameters["moveY"]);
        const auto body = execution_->values.physics.find(id);
        parameters["grounded"] =
            body == execution_->values.physics.end() ? 1.0f : (body->second.grounded ? 1.0f : 0.0f);
        for (const auto& [name, pressed] : execution_->values.inputPressed)
            parameters["pressed:" + name] = pressed ? 1.0f : 0.0f;
        return parameters;
    }

    void SceneWorld::AdvanceAnimatorFrame(const SceneLayout& layout, const ScenePlacement& placement,
                                          AnimatorFrame& frame, double seconds,
                                          const std::map<std::string, float>& parameters,
                                          const DirectX::XMFLOAT4X4& matrix)
    {
        const bool ragdoll = placement.ragdoll && placement.ragdoll->enabled && placement.ragdoll->active &&
                             !placement.ragdoll->bones.empty();
        const auto pose = ragdoll
                              ? Ragdoll::Pose(layout, placement, *frame.model->Rig(),
                                              frame.state.basePose.empty() ? frame.state.pose : frame.state.basePose)
                              : Animator::Advance(*placement.animator, frame.state, *frame.model->Rig(), seconds,
                                                  parameters, &matrix);
        if (ragdoll)
        {
            frame.state.pose = pose;
            frame.state.events.clear();
            frame.state.rootDelta = {};
        }
        frame.applyPose = placement.animator->enabled || ragdoll;
        if (frame.applyPose && !frame.model->PreparePose(pose, frame.pose))
            throw std::runtime_error("Animator pose preparation failed");
    }
    bool SceneWorld::PrepareLayoutAssets(SceneLayout& layout, PreparedLayout& prepared, const PreparedLayout* previous,
                                         bool configurationCurrent, std::map<std::string, std::string>& unresolved)
    {
        bool indexAvailable = configurationCurrent;
        try
        {
            prepared.assets = previous && previous->assets && previous->assetsRoot == prepared.assetsRoot
                                  ? previous->assets
                                  : std::make_shared<Engine::AssetDatabase>(context_, true);
            if (IsDocumentView())
                unresolved = ResolveSceneAssetReferencesForEditing(layout, *prepared.assets);
            else
                layout.ResolveAssets(*prepared.assets);
        }
        catch (const std::exception& failure)
        {
            if (!IsDocumentView())
                throw;
            indexAvailable = false;
            prepared.diagnostics.push_back(failure.what());
        }
        for (const auto& [path, diagnostic] : unresolved)
            prepared.diagnostics.push_back(path + ": " + diagnostic);
        return indexAvailable;
    }
    void SceneWorld::ValidateEditingResources(const ScenePlacement& placement, bool indexAvailable,
                                              const std::map<std::string, std::string>& unresolved) const
    {
        if (IsDocumentView())
        {
            if (!modelsReady_ || !indexAvailable)
                throw std::runtime_error("Rendering resources are unavailable");
            auto references = placement;
            references.VisitAssetReferences(
                [&](const auto& value)
                {
                    const auto key = [&]()
                    {
                        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>)
                            return value;
                        else
                            return Engine::AssetDatabase::Text(value);
                    }();
                    if (unresolved.contains(key))
                        throw std::runtime_error(unresolved.at(key));
                });
        }
    }
    void SceneWorld::PrepareLayoutObjects(const SceneLayout& layout, const std::vector<DirectX::XMFLOAT4X4>& matrices,
                                          PreparedLayout& prepared, const LayoutView& baseline, bool preserveExecution,
                                          bool indexAvailable, const std::map<std::string, std::string>& unresolved)
    {
        Engine::CpuScope timing("Scene object preparation");
        prepared.objects.reserve(layout.objects.size());
        for (const auto& placement : layout.objects)
        {
            const auto& matrix = matrices[prepared.objects.size()];
            try
            {
                ValidateEditingResources(placement, indexAvailable, unresolved);
                prepared.objects.push_back(
                    BuildLayoutObject(placement, layout, matrix, prepared, baseline, preserveExecution));
            }
            catch (const std::exception& failure)
            {
                if (!IsDocumentView())
                    throw;
                prepared.diagnostics.push_back(placement.id + ": " + failure.what());
                prepared.animated.erase(placement.id);
                prepared.states.erase(placement.id);
                prepared.lods.erase(placement.id);
                Engine::Object3D placeholder;
                if (!placeholder.SetTransform(placement.position, placement.rotation, placement.scale) ||
                    !placeholder.SetWorldMatrix(matrix))
                    throw std::runtime_error("Invalid document transform");
                prepared.objects.push_back(std::move(placeholder));
            }
        }
    }
    bool SceneWorld::PrepareLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error,
                                   bool preserveExecution, PreparedLayout& result, const PreparedLayout* previous)
    {
        Engine::CpuScope timing("Scene layout preparation");
        if (!modelsReady_ && !IsDocumentView())
        {
            error = "Scene renderer is unavailable. Check shaders and restart.";
            return false;
        }
        try
        {
            layout.Validate();
            // 参照解決はパス・成分・Prefab 基準データ・参照表だけを変更し、ID や名前は生成しません。
            // 直前に全体を検証した同じ候補との差分を検証し、解決で生じた不正な値も拒否します。
            const auto validatedBeforeResolution = layout;
            if (!context_)
                context_ = Engine::ContentContext::Capture(assetsRoot);
            context_->RequireRoot(assetsRoot);
            const auto baseline = BaselineView(previous);
            const bool configurationCurrent = context_->CheckConfiguration(error);
            if (!configurationCurrent && !IsDocumentView())
                return false;
            PreparedLayout prepared;
            if (!configurationCurrent)
                prepared.diagnostics.push_back(error);
            if (!modelsReady_)
                prepared.diagnostics.push_back("Scene renderer is unavailable; editing data was retained");
            prepared.assetsRoot = context_->Root();
            std::map<std::string, std::string> unresolved;
            const bool indexAvailable =
                PrepareLayoutAssets(layout, prepared, previous, configurationCurrent, unresolved);
            layout.ValidateChanges(validatedBeforeResolution);
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(layout, matrices, error))
                throw std::runtime_error(error);
            prepared.materials = previous ? previous->materials : decltype(prepared.materials){};
            PrepareLayoutObjects(layout, matrices, prepared, baseline, preserveExecution, indexAvailable, unresolved);
            prepared.layout = std::move(layout);
            if (modelsReady_ && configurationCurrent &&
                !PrepareShadows(prepared.layout.settings, prepared.shadow, prepared.localShadow, error))
            {
                if (!IsDocumentView())
                    return false;
                prepared.diagnostics.push_back(error);
            }
            result = std::move(prepared);
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            error = std::string("Scene could not be loaded: ") + failure.what();
            Engine::Log::Error(error);
            return false;
        }
    }
    void SceneWorld::CommitLayout(PreparedLayout&& prepared, bool preserveExecution)
    {
        std::shared_ptr<const RenderPacket> packet;
        if (!execution_ && modelsReady_)
        {
            std::string error;
            packet = PrepareDocumentPacket(prepared, error);
            if (!packet)
                throw std::runtime_error(error);
        }
        // 旧SDKの直接書き込みは、確定境界でTransformだけを比較する互換経路で捕捉します。
        const bool publishChanges = !execution_ || !execution_->values.frameUpdating;
        auto changes = publishChanges ? changes_ : SceneChangeSet{};
        if (publishChanges)
            changes.Merge(SceneChangeSet::Compare(layout_, prepared.layout));
        if (publishChanges && execution_ && !preserveExecution)
            changes.fields.push_back({{}, {}, "runtime.reset"});
        if (!preserveExecution)
            animationScratch_.reset();
        if (!preserveExecution && execution_)
            execution_->values.scripts.Stop(layout_);
        if (prepared.shadow && prepared.shadow != shadow_)
        {
            if (retirement_)
                retirement_->Retire(shadow_);
            shadow_ = std::move(prepared.shadow);
        }
        if (prepared.localShadow && prepared.localShadow != localShadow_)
        {
            if (retirement_)
                retirement_->Retire(localShadow_);
            localShadow_ = std::move(prepared.localShadow);
        }
        resourceDiagnostics_ = std::move(prepared.diagnostics);
        layout_ = std::move(prepared.layout);
        assetsRoot_ = std::move(prepared.assetsRoot);
        assetIndex_ = std::move(prepared.assets);
        materials_ = std::move(prepared.materials);
        if (retirement_)
            retirement_->Collect();
        ++renderVersion_;
        if (execution_ && !preserveExecution)
        {
            execution_->values.previousPhysical.clear();
            execution_->values.currentPhysical.clear();
            execution_->values.physics.clear();
            execution_->physicsWorld.Reset();
            execution_->values.fixedSeconds = 0;
            execution_->values.fixedJump = false;
            execution_->values.fixedPressed.clear();
        }
        else if (execution_)
            std::erase_if(execution_->values.physics,
                          [&](const auto& pair)
                          {
                              return std::none_of(layout_.objects.begin(), layout_.objects.end(),
                                                  [&](const auto& item)
                                                  {
                                                      return item.id == pair.first;
                                                  });
                          });
        animatedModels_ = std::move(prepared.animated);
        animatorStates_ = std::move(prepared.states);
        objects_ = std::move(prepared.objects);
        lodModels_ = std::move(prepared.lods);
        if (!execution_)
            documentPacket_.swap(packet);
        if (publishChanges)
            changes_.Swap(changes);
    }

    bool SceneWorld::PrepareAnimators(const SceneLayout& layout, double seconds,
                                      const std::map<std::string, std::shared_ptr<Engine::ModelRenderer>>& models,
                                      const std::map<std::string, AnimatorState>& states, ScriptRuntime& scripts,
                                      std::vector<AnimatorFrame>& result, std::string& error) const
    {
        try
        {
            std::vector<AnimatorFrame> frames;
            if (std::none_of(layout.objects.begin(), layout.objects.end(),
                             [](const auto& p)
                             {
                                 return p.animator.has_value();
                             }))
            {
                result.clear();
                error.clear();
                return true;
            }
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(layout, matrices, error))
                throw std::runtime_error(error);
            for (size_t index = 0; index < layout.objects.size(); ++index)
            {
                const auto& placement = layout.objects[index];
                if (!placement.animator)
                    continue;
                const auto model = models.find(placement.id);
                if (model == models.end())
                    throw std::runtime_error("Animator model missing");
                AnimatorFrame frame;
                frame.id = placement.id;
                frame.index = index;
                frame.model = model->second;
                frame.state = states.at(placement.id);
                ApplyAnimatorOverrides(placement.id, scripts, frame.state);
                const auto parameters = AnimatorInputs(layout, placement.id);
                AdvanceAnimatorFrame(layout, placement, frame, seconds, parameters, matrices[index]);
                for (const auto& occurrence : frame.state.events)
                {
                    ScriptEvent event{occurrence.name, placement.id, placement.id, occurrence.value};
                    event.animation = occurrence;
                    scripts.QueueEvent(std::move(event));
                }
                frames.push_back(std::move(frame));
            }
            result = std::move(frames);
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    void SceneWorld::Draw(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
                          const Engine::DirectionalLight& light, const UiState* state) const
    {
        meshTelemetry_ = {};
        if (!modelsReady_)
            return;
        const auto defaults = state ? UiState{} : SceneUi::Defaults(layout_);
        if (!RenderScene::Draw(RenderSnapshot(), commands, camera, light, state ? *state : defaults, occlusion_,
                               meshTelemetry_))
            Engine::Log::Warning("Render packet could not be recorded");
    }

    bool SceneWorld::PrepareTransforms(const SceneLayout& layout, std::vector<Engine::Object3D>& objects,
                                       std::string& error, const SceneLayout* validatedBefore)
    {
        Engine::CpuScope scope("World transform preparation");
        std::vector<DirectX::XMFLOAT4X4> matrices;
        if (validatedBefore)
        {
            std::vector<DirectX::XMFLOAT4X4> previous;
            previous.reserve(objects.size());
            std::transform(objects.begin(), objects.end(), std::back_inserter(previous),
                           [](const auto& object)
                           {
                               return object.GetWorldMatrix();
                           });
            if (!SceneTransforms::ResolveChanges(layout, *validatedBefore, previous, matrices, error))
                return false;
        }
        else if (!SceneTransforms::Resolve(layout, matrices, error))
            return false;
        if (objects.size() != matrices.size())
        {
            error = "Placement and draw object counts differ";
            return false;
        }
        for (size_t index = 0; index < objects.size(); ++index)
        {
            const auto& placement = layout.objects[index];
            const bool sameLocal = objects[index].GetPosition() == placement.position &&
                                   objects[index].GetRotation() == placement.rotation &&
                                   objects[index].GetScale() == placement.scale;
            if (sameLocal &&
                std::memcmp(&objects[index].GetWorldMatrix(), &matrices[index], sizeof(matrices[index])) == 0)
                continue;
            if ((!sameLocal && !objects[index].SetTransform(placement.position, placement.rotation, placement.scale)) ||
                !objects[index].SetWorldMatrix(matrices[index]))
            {
                error = placement.id + ": invalid inherited transform";
                return false;
            }
        }
        error.clear();
        return true;
    }

    bool SceneWorld::CommitTransforms(const SceneLayout& candidate)
    {
        if (runtime_)
        {
            std::string error;
            return EditSession(ReplaceDocumentEdit{candidate}, error);
        }
        if (!CanMutateScene())
            return false;
        auto objects = objects_;
        std::string error;
        if (!PrepareTransforms(candidate, objects, error, &layout_))
            return false;
        for (size_t index = 0; index < candidate.objects.size(); ++index)
            if (candidate.objects[index].material != layout_.objects[index].material)
                ApplySurfaceOverride(objects[index], candidate.objects[index]);
        auto packet = PrepareDocumentPacket(candidate, objects, error);
        if (modelsReady_ && !packet)
            return false;
        auto changes = changes_;
        changes.Merge(SceneChangeSet::CompareTransforms(layout_, candidate));
        for (size_t index = 0; index < layout_.objects.size(); ++index)
        {
            auto& target = layout_.objects[index];
            const auto& source = candidate.objects[index];
            target.position = source.position;
            target.rotation = source.rotation;
            target.scale = source.scale;
            target.parentId = source.parentId;
        }
        objects_ = std::move(objects);
        documentPacket_.swap(packet);
        ++renderVersion_;
        changes_.Swap(changes);
        return true;
    }

    bool SceneWorld::SetParent(std::string_view id, const std::string& parentId, std::string& error)
    {
        if (!CanMutateScene(&error))
            return false;
        return SetParents({std::string(id)}, parentId, error);
    }

    bool SceneWorld::SetParents(const std::vector<std::string>& ids, const std::string& parentId, std::string& error)
    {
        if (runtime_)
            return EditSession(ParentEdit{ids, parentId}, error);
        if (!CanMutateScene(&error))
            return false;
        try
        {
            auto plan = SceneEdits::Prepare(layout_, ParentEdit{ids, parentId}, nextObjectId_, error);
            if (!plan)
                return false;
            if (!CommitTransforms(plan->layout))
                throw std::runtime_error("Invalid inherited transform");
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool SceneWorld::PrepareShadows(const SceneSettings& settings, std::shared_ptr<Engine::ShadowMap>& directional,
                                    std::shared_ptr<Engine::ShadowMap>& local, std::string& error) const
    {
        directional = shadow_;
        local = localShadow_;
        const auto prepare = [&](auto& map, unsigned int resolution, bool atlas)
        {
            if (map && map->Size() == resolution)
                return true;
            auto candidate = std::make_shared<Engine::ShadowMap>();
            if (!candidate->Initialize(materialDevice_.Get(), meshShader_, atlas, resolution))
            {
                error = "Shadow resources could not be initialized";
                return false;
            }
            map = std::move(candidate);
            return true;
        };
        return prepare(directional, settings.shadowResolution, false) &&
               prepare(local, settings.localShadowResolution, true);
    }
    bool SceneWorld::SetSettings(const SceneSettings& settings, std::string& error)
    {
        if (runtime_)
            return EditSession(SettingsEdit{settings}, error);
        if (!CanMutateScene(&error))
            return false;
        if (layout_.settings == settings)
        {
            error.clear();
            return true;
        }
        try
        {
            auto plan = SceneEdits::Prepare(layout_, SettingsEdit{settings}, nextObjectId_, error);
            if (!plan)
                return false;
            auto candidate = std::move(plan->layout);
            auto changes = changes_;
            changes.Merge(SceneChangeSet::Compare(layout_, candidate));
            if (IsDocumentView())
                return ReplaceLayout(std::move(candidate), assetsRoot_, error);
            ChangePreparation preparing(preparingChanges_);
            static_cast<void>(candidate.Serialize());
            std::shared_ptr<Engine::ShadowMap> directional, local;
            if (!PrepareShadows(settings, directional, local, error))
                return false;
            if (directional != shadow_)
            {
                if (retirement_ && !retirement_->Retire(shadow_))
                    return false;
            }
            if (local != localShadow_)
            {
                if (retirement_ && !retirement_->Retire(localShadow_))
                    return false;
            }
            if (execution_ && layout_.settings.fixedStep != settings.fixedStep)
            {
                execution_->values.fixedSeconds = 0;
                execution_->values.previousPhysical.clear();
                execution_->values.currentPhysical.clear();
            }
            shadow_ = std::move(directional);
            localShadow_ = std::move(local);
            layout_.settings = std::move(candidate.settings);
            ++renderVersion_;
            changes_.Swap(changes);
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool SceneWorld::SetComponents(std::string_view id, const ScenePlacement& settings,
                                   const std::filesystem::path& assetsRoot, std::string& error)
    {
        if (!CanMutateScene(&error))
            return false;
        auto owner = settings;
        owner.id = id;
        return SetComponentBatch({std::move(owner)}, assetsRoot, error);
    }
    bool SceneWorld::SetComponentBatch(const std::vector<ScenePlacement>& settings,
                                       const std::filesystem::path& assetsRoot, std::string& error)
    {
        if (runtime_)
        {
            if (!CheckSessionRoot(assetsRoot, error))
                return false;
            return EditSession(ComponentsEdit{settings}, error);
        }
        if (!CanMutateScene(&error))
            return false;
        try
        {
            auto plan = SceneEdits::Prepare(layout_, ComponentsEdit{settings}, nextObjectId_, error);
            if (!plan)
                return false;
            auto candidate = std::move(plan->layout);
            bool rebuild = false;
            for (const auto& setting : settings)
            {
                const auto found = std::find_if(candidate.objects.begin(), candidate.objects.end(),
                                                [&](const auto& placement)
                                                {
                                                    return placement.id == setting.id;
                                                });
                if (found == candidate.objects.end())
                    throw std::runtime_error("Component owner no longer exists");
                const auto index = static_cast<size_t>(found - candidate.objects.begin());
                rebuild |= SceneRuntime::SceneResourceChanges::MeshChanged(*found, layout_.objects[index]) ||
                           SceneRuntime::SceneResourceChanges::AnimationChanged(*found, layout_.objects[index]);
            }
            const auto batchChanges = SceneChangeSet::Compare(layout_, candidate);
            if (batchChanges.Empty())
            {
                error.clear();
                return true;
            }
            static_cast<void>(candidate.Serialize());
            if (IsDocumentView())
                return ReplaceLayout(std::move(candidate), assetsRoot, error);
            if (!rebuild)
            {
                auto changes = changes_;
                changes.Merge(batchChanges);
                for (const auto& object : candidate.objects)
                    if (object.ragdoll)
                    {
                        const auto model = animatedModels_.find(object.id);
                        if (model == animatedModels_.end())
                            throw std::runtime_error("Ragdoll requires a loaded Animator model");
                        Ragdoll::Validate(candidate, object, *model->second->Rig());
                    }
                for (size_t index = 0; index < candidate.objects.size(); ++index)
                    layout_.objects[index].CopyComponents(candidate.objects[index]);
                layout_.settings = candidate.settings;
                ++renderVersion_;
                changes_.Swap(changes);
                error.clear();
                return true;
            }
            return ReplaceLayout(std::move(candidate), assetsRoot, error, true);
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool SceneWorld::GenerateRagdoll(const std::string& id, const std::filesystem::path& root, std::string& error)
    {
        if (!CanMutateScene(&error))
            return false;
        const auto model = animatedModels_.find(id);
        const auto state = animatorStates_.find(id);
        if (model == animatedModels_.end() || state == animatorStates_.end())
        {
            error = "Ragdoll generation requires a loaded Animator model";
            return false;
        }
        auto candidate = layout_;
        if (!Ragdoll::Generate(candidate, id, *model->second->Rig(), state->second.pose, error))
            return false;
        return ReplaceLayout(std::move(candidate), root, error, true);
    }
    bool SceneWorld::AddImpulse(const std::string& id, const std::array<float, 3>& impulse)
    {
        if (runtime_)
        {
            if (!CanMutateScene())
                return false;
            const auto view = runtime_->session.Read();
            const auto entity = view.Resolve(id);
            const auto body = entity ? view.Instance<RigidBodyComponent>(*entity) : std::nullopt;
            if (!body)
                return false;
            RuntimeScriptEffects effects;
            effects.impulses.push_back({*entity, impulse, *body});
            return ApplySessionEffects(effects);
        }
        if (!CanMutateScene())
            return false;
        const auto object = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                         [&](const auto& item)
                                         {
                                             return item.id == id;
                                         });
        if (!execution_ || object == layout_.objects.end() || !object->rigidBody || !object->rigidBody->enabled ||
            object->rigidBody->motion != "dynamic")
            return false;
        auto changes = changes_;
        SceneChangeSet detail;
        if (impulse != std::array<float, 3>{})
            detail.fields.push_back({id, object->rigidBody->id, "runtime.physics.impulse"});
        changes.Merge(detail);
        if (!execution_->physicsWorld.AddImpulse(id, impulse))
            return false;
        changes_.Swap(changes);
        return true;
    }
    std::map<std::string, bool> SceneWorld::FixedRootOverrides(SceneLayout& layout, const ScriptRuntime& scripts) const
    {
        std::map<std::string, bool> savedRootMotion;
        for (auto& object : layout.objects)
            if (object.animator)
            {
                savedRootMotion[object.id] = object.animator->rootMotion;
                if (const auto state = animatorStates_.find(object.id); state != animatorStates_.end())
                    object.animator->rootMotion =
                        state->second.rootMotionOverride.value_or(object.animator->rootMotion);
                if (const auto change = scripts.RootMotions().find(object.id); change != scripts.RootMotions().end())
                    object.animator->rootMotion = change->second.value_or(savedRootMotion.at(object.id));
            }
        return savedRootMotion;
    }
    bool SceneWorld::SynchronizeRagdolls(SceneLayout& layout, std::string& error) const
    {
        try
        {
            std::vector<std::string> owners;
            for (const auto& object : layout.objects)
                if (object.ragdoll && !object.ragdoll->bones.empty())
                    owners.push_back(object.id);
            for (const auto& id : owners)
            {
                const auto model = animatedModels_.find(id);
                const auto state = animatorStates_.find(id);
                if (model == animatedModels_.end() || state == animatorStates_.end())
                    throw std::runtime_error("Ragdoll Animator is missing");
                Ragdoll::Synchronize(layout, id, *model->second->Rig(), state->second.pose);
            }
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
        return true;
    }
    std::map<std::string, std::array<bool, 3>> SceneWorld::DisableRagdollOwners(SceneLayout& layout,
                                                                                ScenePhysics::States& states)
    {
        std::map<std::string, std::array<bool, 3>> ragdollOwnerEnabled;
        for (auto& object : layout.objects)
            if (object.ragdoll && object.ragdoll->enabled && object.ragdoll->active && !object.ragdoll->bones.empty())
            {
                ragdollOwnerEnabled[object.id] = {object.playerController && object.playerController->enabled,
                                                  object.boxCollider && object.boxCollider->enabled,
                                                  object.rigidBody && object.rigidBody->enabled};
                if (object.playerController)
                    object.playerController->enabled = false;
                if (object.boxCollider)
                    object.boxCollider->enabled = false;
                if (object.rigidBody)
                {
                    object.rigidBody->enabled = false;
                    object.rigidBody->velocity = object.rigidBody->angularVelocity = {0, 0, 0};
                }
                states.erase(object.id);
            }
        return ragdollOwnerEnabled;
    }
    bool SceneWorld::AdvanceLegacyPlayers(SceneLayout& layout, ScenePhysics::States& states, float horizontal,
                                          float vertical, bool jump) const
    {
        if (std::any_of(layout.objects.begin(), layout.objects.end(),
                        [](const auto& p)
                        {
                            return p.playerController && p.playerController->enabled && !p.playerController->usePhysics;
                        }))
        {
            Engine::CpuScope legacyScope("Legacy player movement");
            if (!ScenePhysics::Advance(layout, states, FixedStep(), horizontal, vertical, jump))
                return false;
        }
        else
            std::erase_if(states,
                          [&](const auto& item)
                          {
                              return std::none_of(layout.objects.begin(), layout.objects.end(),
                                                  [&](const auto& p)
                                                  {
                                                      return p.id == item.first && p.playerController &&
                                                             p.playerController->enabled;
                                                  });
                          });
        return true;
    }
    void SceneWorld::RestoreRagdollOwners(SceneLayout& layout,
                                          const std::map<std::string, std::array<bool, 3>>& ragdollOwnerEnabled)
    {
        for (auto& object : layout.objects)
            if (const auto saved = ragdollOwnerEnabled.find(object.id); saved != ragdollOwnerEnabled.end())
            {
                if (object.playerController)
                    object.playerController->enabled = saved->second[0];
                if (object.boxCollider)
                    object.boxCollider->enabled = saved->second[1];
                if (object.rigidBody)
                    object.rigidBody->enabled = saved->second[2];
            }
    }
    void SceneWorld::RotateComponents(SceneLayout& candidate, double seconds)
    {
        for (auto& placement : candidate.objects)
        {
            if (!placement.rotator || !placement.rotator->enabled)
                continue;
            for (size_t axis = 0; axis < 3; ++axis)
            {
                const double speed = placement.rotator->angularVelocity[axis];
                if (speed == 0)
                    continue;
                const double elapsed = std::remainder(seconds, 360.0 / std::abs(speed));
                placement.rotation[axis] = static_cast<float>(std::remainder(
                    static_cast<double>(placement.rotation[axis]) + elapsed * speed * DirectX::XM_PI / 180.0,
                    static_cast<double>(DirectX::XM_2PI)));
            }
        }
    }
    bool SceneWorld::UpdateLateComponents(double seconds, PreparedLayout& prepared, ScriptRuntime& runtime,
                                          std::vector<AnimatorFrame>& frames,
                                          std::map<std::string, std::array<float, 3>>& impulses, std::string& error)
    {
        if (ScriptRuntime::HasPhase(prepared.layout, ScriptPhase::LateUpdate,
                                    &execution_->values.scripts.Definitions()))
        {
            auto late = prepared.layout;
            if (!runtime.UpdateCandidate(late, seconds, error, execution_->values.inputValues,
                                         execution_->values.inputPressed, &execution_->physicsWorld,
                                         ScriptPhase::LateUpdate, &layout_))
            {
                Engine::Log::Warning(error);
                return false;
            }
            PreparedLayout afterLate;
            if (!PrepareScriptLayout(std::move(late), &prepared, afterLate, error))
            {
                Engine::Log::Warning(error);
                return false;
            }
            prepared = std::move(afterLate);
            for (const auto& [id, impulse] : runtime.Impulses())
                for (size_t axis = 0; axis < 3; ++axis)
                    impulses[id][axis] += impulse[axis];
            if (!PrepareAnimators(prepared.layout, 0, prepared.animated, prepared.states, runtime, frames, error) ||
                !PrepareRootMotion(prepared, frames, error))
            {
                Engine::Log::Warning(error);
                return false;
            }
            for (const auto& frame : frames)
                prepared.states.at(frame.id) = frame.state;
        }
        return true;
    }
    bool SceneWorld::ApplyRootDelta(ScenePlacement& placement, const AnimatorFrame& frame)
    {
        if (placement.ragdoll && placement.ragdoll->enabled && placement.ragdoll->active)
            return false;
        if (!frame.applyPose || !frame.state.rootMotionOverride.value_or(placement.animator->rootMotion))
            return false;
        if (frame.state.rootDelta.position == std::array<float, 3>{} &&
            frame.state.rootDelta.rotation == std::array<float, 4>{0, 0, 0, 1})
            return false;
        const auto delta = Animator::RootDeltaMatrix(*placement.animator, frame.state, *frame.model->Rig());
        DirectX::XMFLOAT4X4 local, changed;
        if (!SceneTransforms::Compose(placement, local))
            throw std::runtime_error("Invalid root motion object transform");
        DirectX::XMStoreFloat4x4(&changed, DirectX::XMLoadFloat4x4(&delta) * DirectX::XMLoadFloat4x4(&local));
        ScenePlacement result;
        if (!SceneTransforms::ReadTransform(changed, placement, result))
            throw std::runtime_error("Root motion cannot be represented by object SRT");
        placement.position = result.position;
        placement.rotation = result.rotation;
        placement.scale = result.scale;
        return true;
    }
    void SceneWorld::ConstrainRootFrames(PreparedLayout& prepared, std::vector<AnimatorFrame>& frames,
                                         const std::vector<DirectX::XMFLOAT4X4>& matrices)
    {
        for (auto& frame : frames)
            if (frame.applyPose)
            {
                const auto& placement = prepared.layout.objects.at(frame.index);
                if (placement.ragdoll && placement.ragdoll->enabled && placement.ragdoll->active)
                    continue;
                frame.state.pose =
                    Animator::Constrain(*placement.animator, frame.state, *frame.model->Rig(), &matrices[frame.index]);
                if (!frame.model->PreparePose(frame.state.pose, frame.pose))
                    throw std::runtime_error("Root motion constraint pose failed");
            }
    }
    Engine::Object3D SceneWorld::BuildAddedObject(const ScenePlacement& placement, const SceneLayout& validation,
                                                  const DirectX::XMFLOAT4X4& matrix,
                                                  const std::filesystem::path& assetsRoot, PreparedLayout& prepared)
    {
        Engine::Object3D object;

        if (placement.animator && !placement.meshRenderer)
            throw std::runtime_error("Animator needs a MeshRenderer");
        if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
            throw std::runtime_error("Invalid transform");
        if (!object.SetWorldMatrix(matrix))
            throw std::runtime_error("Invalid world matrix");
        if (placement.meshRenderer)
        {
            const auto model = models_.Load(assetsRoot / placement.Model());
            if (!model)
                throw std::runtime_error("Model could not be loaded");
            object.SetModel(model);
            for (const auto& lod : placement.meshRenderer->lods)
            {
                if (placement.animator)
                    throw std::runtime_error("Animated meshes cannot use static LOD models");
                auto loaded = models_.Load(assetsRoot / lod.model);
                if (!loaded || loaded->IsAnimated())
                    throw std::runtime_error("Static LOD model could not be loaded");
                prepared.lods[placement.id].push_back(std::move(loaded));
            }
            InitializeAddedAnimator(object, placement, validation, matrix, model, prepared);
            if (!placement.Material().empty())
                object.SetMaterial(MaterialAsset::Load(assetsRoot, placement.Material())
                                       .Prepare(materialDevice_.Get(), materialQueue_.Get(), assetsRoot, context_));
            std::map<std::filesystem::path, std::shared_ptr<const Engine::Material>> materials;
            object.SetMaterialSlots(PrepareMaterialSlots(placement, model->MeshCount(), materialDevice_.Get(),
                                                         materialQueue_.Get(), assetsRoot, materials));
        }
        PrepareProcedural(models_, object, placement, assetsRoot, materialDevice_.Get(), materialQueue_.Get());
        ApplySurfaceOverride(object, placement);
        return object;
    }
    void SceneWorld::InitializeAddedAnimator(Engine::Object3D& object, const ScenePlacement& placement,
                                             const SceneLayout& validation, const DirectX::XMFLOAT4X4& matrix,
                                             const std::shared_ptr<const Engine::ModelRenderer>& model,
                                             PreparedLayout& prepared)
    {
        if (placement.animator)
        {
            if (!model->Rig())
                throw std::runtime_error("Animator needs a glTF or GLB model");
            Animator::Validate(*placement.animator, model->Rig().get());
            Ragdoll::Validate(validation, placement, *model->Rig());
            auto animated = model->AnimatedCopy(materialDevice_.Get(), materialQueue_.Get());
            if (!animated || !animated->ApplyPose(Animator::Advance(*placement.animator, prepared.states[placement.id],
                                                                    *model->Rig(), 0, {}, &matrix)))
                throw std::runtime_error("Animator initialization failed");
            object.SetModel(animated);
            prepared.animated[placement.id] = std::move(animated);
        }
    }
    void SceneWorld::CommitAnimatorFrames(std::vector<AnimatorFrame>& frames)
    {
        for (auto& frame : frames)
        {
            if (frame.applyPose)
                frame.model->ApplyPreparedPose(std::move(frame.pose));
            animatorStates_.at(frame.id) = std::move(frame.state);
        }
    }
    bool SceneWorld::SameRenderResources(const SceneLayout& layout, const SceneLayout& before)
    {
        return layout.objects.size() == before.objects.size() &&
               std::equal(layout.objects.begin(), layout.objects.end(), before.objects.begin(),
                          [](const auto& a, const auto& b)
                          {
                              return std::tie(a.id, a.meshRenderer, a.material, a.animator, a.terrain, a.tilemap) ==
                                     std::tie(b.id, b.meshRenderer, b.material, b.animator, b.terrain, b.tilemap);
                          });
    }
    void SceneWorld::ValidateAnimatorModels(const PreparedLayout& candidate)
    {
        for (const auto& p : candidate.layout.objects)
            if (p.animator)
            {
                const auto instance = candidate.animated.find(p.id);
                if (instance == candidate.animated.end() || !instance->second->Rig())
                    throw std::runtime_error("Animator model missing: " + p.id);
                Ragdoll::Validate(candidate.layout, p, *instance->second->Rig());
            }
    }
    SceneWorld::ResourceChanges SceneWorld::DetectResourceChanges(const SceneLayout& layout, const SceneLayout& before,
                                                                  const std::unordered_map<std::string, size_t>& old)
    {
        ResourceChanges changes;
        for (const auto& p : layout.objects)
        {
            const auto found = old.find(p.id);
            if (found == old.end())
            {
                changes.resolveAssets = changes.resolveAssets || HasRenderAsset(p);
                changes.animatedChange = changes.animatedChange || p.animator.has_value();
                continue;
            }
            const auto& original = before.objects[found->second];
            changes.resolveAssets =
                changes.resolveAssets || SceneRuntime::SceneResourceChanges::MeshChanged(p, original);
            changes.animatedChange =
                changes.animatedChange || SceneRuntime::SceneResourceChanges::AnimationChanged(p, original);
        }
        return changes;
    }
    Engine::Object3D SceneWorld::ReuseRuntimeObject(const ScenePlacement& p, PreparedLayout& candidate,
                                                    const LayoutView& baseline, size_t index)
    {
        auto object = baseline.objects[index];
        if (p.animator)
        {
            const auto instance = baseline.animated.find(p.id);
            if (instance == baseline.animated.end() || !instance->second->Rig())
                throw std::runtime_error("Animator model missing: " + p.id);
            Ragdoll::Validate(candidate.layout, p, *instance->second->Rig());
            candidate.animated.emplace(*instance);
        }
        if (const auto i = baseline.states.find(p.id); i != baseline.states.end())
            candidate.states.emplace(*i);
        if (const auto i = baseline.lods.find(p.id); i != baseline.lods.end())
            candidate.lods.emplace(*i);
        return object;
    }
    Engine::Object3D SceneWorld::BuildStaticRuntimeObject(const ScenePlacement& p, PreparedLayout& candidate)
    {
        Engine::Object3D object;
        if (p.meshRenderer)
        {
            const auto model = models_.Load(assetsRoot_ / p.Model());
            if (!model)
                throw std::runtime_error("Model could not be loaded: " + p.id);
            object.SetModel(model);
            for (const auto& lod : p.meshRenderer->lods)
            {
                auto loaded = models_.Load(assetsRoot_ / lod.model);
                if (!loaded || loaded->IsAnimated())
                    throw std::runtime_error("Invalid static LOD: " + p.id);
                candidate.lods[p.id].push_back(std::move(loaded));
            }
            const auto& material = p.Material();
            if (!material.empty())
            {
                auto i = candidate.materials.find(material);
                if (i == candidate.materials.end())
                    i = candidate.materials
                            .emplace(material,
                                     MaterialAsset::Load(assetsRoot_, material, candidate.assets.get())
                                         .Prepare(materialDevice_.Get(), materialQueue_.Get(), assetsRoot_, context_))
                            .first;
                object.SetMaterial(i->second);
            }
            object.SetMaterialSlots(PrepareMaterialSlots(p, model->MeshCount(), materialDevice_.Get(),
                                                         materialQueue_.Get(), assetsRoot_, candidate.materials,
                                                         candidate.assets.get()));
        }
        PrepareProcedural(models_, object, p, assetsRoot_, materialDevice_.Get(), materialQueue_.Get(),
                          candidate.assets.get(), &candidate.materials);
        return object;
    }
    bool SceneWorld::PrepareRuntimeObjects(PreparedLayout& candidate, const LayoutView& baseline,
                                           const std::unordered_map<std::string, size_t>& old)
    {
        bool replaced = false;
        for (const auto& p : candidate.layout.objects)
        {
            const auto found = old.find(p.id);
            const bool reuse = found != old.end() &&
                               p.meshRenderer == baseline.layout.objects[found->second].meshRenderer &&
                               p.material == baseline.layout.objects[found->second].material;
            Engine::Object3D object;
            if (reuse)
            {
                object = ReuseRuntimeObject(p, candidate, baseline, found->second);
            }
            else
            {
                replaced = true;
                object = BuildStaticRuntimeObject(p, candidate);
            }
            ApplySurfaceOverride(object, p);
            candidate.objects.push_back(std::move(object));
        }
        return replaced;
    }

    static void AccumulatePressed(const std::map<std::string, bool>& inputPressed, std::map<std::string, bool>& pressed)
    {
        for (const auto& [name, value] : inputPressed)
            if (value)
                pressed[name] = true;
    }
    static double DropFixedBacklog(double pending, double step)
    {
        if (pending + 1e-10 >= step)
        {
            pending = std::fmod(pending, step);
            Engine::CpuProfiler::Add("Fixed backlog dropped", 0);
        }
        return pending;
    }
    bool SceneWorld::MovePlayers(double seconds, float horizontal, float vertical, bool jump)
    {
        return ExecuteUpdate(seconds, horizontal, vertical, jump, true, false);
    }
    bool SceneWorld::PrepareMovement(SceneLayout& candidate, double seconds, float horizontal, float vertical,
                                     bool jump)
    {
        Engine::CpuScope scope("Physics and player movement");
        auto& values = execution_->values;
        std::string error;
        auto pending = std::min(seconds, 0.1) + values.fixedSeconds;
        auto pressed = values.fixedPressed;
        AccumulatePressed(values.inputPressed, pressed);
        bool pendingJump = values.fixedJump || jump;
        if (pending + 1e-10 < FixedStep())
        {
            values.fixedSeconds = pending;
            values.fixedPressed = std::move(pressed);
            values.fixedJump = pendingJump;
            return true;
        }
        unsigned int steps = 0;
        const auto MaximumFixedSteps = layout_.settings.maximumFixedSteps;
        while (pending + 1e-10 >= FixedStep() && steps < MaximumFixedSteps)
        {
            values.previousPhysical = PhysicalPoses(candidate);
            if (!FixedTick(candidate, values.physics, values.scripts, horizontal, vertical, pendingJump, pressed,
                           error))
            {
                Engine::Log::Warning(error);
                return false;
            }
            values.currentPhysical = PhysicalPoses(candidate);
            ++steps;
            pending = std::max(0.0, pending - FixedStep());
            pendingJump = false;
            pressed.clear();
        }
        pending = DropFixedBacklog(pending, FixedStep());
        values.fixedSeconds = pending;
        values.fixedJump = pendingJump;
        values.fixedPressed = std::move(pressed);
        return true;
    }
    bool SceneWorld::FixedTick(SceneLayout& layout, ScenePhysics::States& states, ScriptRuntime& scripts,
                               float horizontal, float vertical, bool jump, const std::map<std::string, bool>& pressed,
                               std::string& error)
    {
        const bool hasFixedUpdate =
            ScriptRuntime::HasPhase(layout, ScriptPhase::FixedUpdate, &execution_->values.scripts.Definitions());
        if (hasFixedUpdate &&
            !scripts.UpdateCandidate(layout, FixedStep(), error, execution_->values.inputValues, pressed,
                                     &execution_->physicsWorld, ScriptPhase::FixedUpdate, &layout_))
            return false;
        if (std::any_of(layout.objects.begin(), layout.objects.end(),
                        [](const auto& p)
                        {
                            return p.navAgent && p.navAgent->enabled && p.navAgent->moving;
                        }))
        {
            Engine::CpuScope navigationScope("Navigation update");
            if (!Navigation::Advance(layout, FixedStep(), error))
                return false;
        }
        const auto savedRootMotion = FixedRootOverrides(layout, scripts);
        if (hasFixedUpdate)
            for (const auto& [id, impulse] : scripts.Impulses())
                if (!execution_->physicsWorld.AddImpulse(id, impulse))
                {
                    error = "FixedUpdate impulse rejected";
                    return false;
                }
        if (!SynchronizeRagdolls(layout, error))
            return false;
        const auto ragdollOwnerEnabled = DisableRagdollOwners(layout, states);
        if (!AdvanceLegacyPlayers(layout, states, horizontal, vertical, jump))
            return false;
        if (!execution_->physicsWorld.Advance(layout, states, FixedStep(), horizontal, vertical, jump, assetsRoot_,
                                              error))
            return false;
        RestoreRagdollOwners(layout, ragdollOwnerEnabled);
        for (auto& object : layout.objects)
            if (object.animator)
                object.animator->rootMotion = savedRootMotion.at(object.id);
        return QueueContacts(layout, scripts, error);
    }
    bool SceneWorld::QueueContacts(const SceneLayout& layout, ScriptRuntime& scripts, std::string& error) const
    {
        std::set<std::string> listeners;
        for (const auto& object : layout.objects)
            for (const auto& script : object.scripts)
                if (script.enabled)
                {
                    const auto definition = scripts.Definitions().find(script.behaviour);
                    if (definition != scripts.Definitions().end() && definition->second.onEvent)
                        listeners.insert(object.id);
                }
        for (const auto& contact : execution_->physicsWorld.Events())
        {
            const auto name = std::string(contact.trigger ? "trigger" : "collision") + contact.phase;
            try
            {
                if (listeners.contains(contact.second))
                    scripts.QueueEvent({name, contact.first, contact.second, 0});
                if (listeners.contains(contact.first))
                    scripts.QueueEvent({name, contact.second, contact.first, 0});
            }
            catch (const std::exception& exception)
            {
                error = exception.what();
                return false;
            }
        }
        return true;
    }

    SceneWorld::PreparedLayout SceneWorld::RuntimeLayout() const
    {
        PreparedLayout result;
        result.assetsRoot = assetsRoot_;
        result.layout = layout_;
        result.objects = objects_;
        result.assets = assetIndex_;
        result.materials = materials_;
        result.animated = animatedModels_;
        result.states = animatorStates_;
        result.lods = lodModels_;
        result.shadow = shadow_;
        result.localShadow = localShadow_;
        return result;
    }
    bool SceneWorld::PrepareCachedLayout(SceneLayout layout, const LayoutView& baseline, PreparedLayout& result,
                                         std::string& error)
    {
        PreparedLayout candidate;
        candidate.assetsRoot = baseline.root;
        candidate.assets = baseline.assets;
        candidate.materials = baseline.materials;
        candidate.objects = baseline.objects;
        candidate.animated = baseline.animated;
        candidate.states = baseline.states;
        candidate.lods = baseline.lods;
        candidate.layout = std::move(layout);
        ValidateAnimatorModels(candidate);
        if (!PrepareTransforms(candidate.layout, candidate.objects, error, &baseline.layout))
            return false;
        result = std::move(candidate);
        error.clear();
        return true;
    }
    bool SceneWorld::PrepareScriptLayout(SceneLayout layout, const PreparedLayout* previous, PreparedLayout& result,
                                         std::string& error)
    {
        Engine::CpuScope scope("Script render resource preparation");
        try
        {
            const auto baseline = BaselineView(previous);
            const bool unchangedResources = SameRenderResources(layout, baseline.layout);
            if (unchangedResources)
            {
                return PrepareCachedLayout(std::move(layout), baseline, result, error);
            }
            std::unordered_map<std::string, size_t> old;
            for (size_t i = 0; i < baseline.layout.objects.size(); ++i)
                old.emplace(baseline.layout.objects[i].id, i);
            const auto changes = DetectResourceChanges(layout, baseline.layout, old);
            if (changes.resolveAssets || changes.animatedChange)
                context_->RequireConfiguration();
            if (changes.animatedChange)
            {
                if (!retirement_->Retire(std::make_shared<std::vector<Engine::Object3D>>(baseline.objects)))
                    throw std::runtime_error("Script resource retirement failed");
                if (previous)
                    return PrepareLayout(std::move(layout), assetsRoot_, error, true, result, previous);
                const auto snapshot = RuntimeLayout();
                return PrepareLayout(std::move(layout), assetsRoot_, error, true, result, &snapshot);
            }
            if (changes.resolveAssets)
            {
                if (!baseline.assets)
                    throw std::runtime_error("Runtime asset index missing");
                layout.ResolveAssets(*baseline.assets);
            }
            PreparedLayout candidate;
            candidate.assetsRoot = baseline.root;
            candidate.assets = baseline.assets;
            candidate.materials = baseline.materials;
            candidate.layout = std::move(layout);
            candidate.objects.reserve(candidate.layout.objects.size());
            const bool replaced = PrepareRuntimeObjects(candidate, baseline, old);
            if (!PrepareTransforms(candidate.layout, candidate.objects, error))
                return false;
            if (replaced || candidate.layout.objects.size() != baseline.layout.objects.size())
            {
                if (!retirement_->Retire(std::make_shared<std::vector<Engine::Object3D>>(baseline.objects)))
                    throw std::runtime_error("Script resource retirement failed");
            }
            result = std::move(candidate);
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool SceneWorld::QueueScriptEvent(ScriptEvent event, std::string& error)
    {
        if (runtime_)
            return CanMutateScene(&error) && runtime_->session.QueueEvent(std::move(event), error);
        if (!CanMutateScene(&error))
            return false;
        try
        {
            if (!execution_)
                throw std::runtime_error("Editing document cannot receive runtime events");
            execution_->values.scripts.QueueEvent(std::move(event));
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool SceneWorld::StartScripts(std::string& error)
    {
        if (runtime_)
        {
            if (!CanMutateScene(&error))
                return false;
            ChangePreparation preparing(preparingChanges_);
            const bool success = runtime_->session.Start(error);
            if (success)
                DrainSession();
            return success;
        }
        if (!CanMutateScene(&error))
            return false;
        ChangePreparation preparing(preparingChanges_);
        if (!execution_)
        {
            error = "Editing documents cannot start scripts";
            return false;
        }
        auto layout = layout_;
        auto runtime = execution_->values.scripts;
        if (!runtime.StartCandidate(layout, error))
            return false;
        PreparedLayout prepared;
        if (!PrepareScriptLayout(std::move(layout), nullptr, prepared, error))
            return false;
        if (prepareRuntime_ && !prepareRuntime_(prepared.layout, error))
            return false;
        if (prepareUi_ && !prepareUi_(prepared.layout, runtime.UiCommands(), error))
            return false;
        if (!ScriptRuntime::ValidateForPlay(prepared.layout, error, &execution_->values.scripts.Definitions()))
            return false;
        auto changes = changes_;
        changes.Merge(SceneChangeSet::Compare(layout_, prepared.layout));
        changes.Merge(runtime.ChangesSince(execution_->values.scripts));
        CommitLayout(std::move(prepared), true);
        runtime.CommitStarts();
        execution_->values.scripts = std::move(runtime);
        changes_.Swap(changes);
        return true;
    }
    bool SceneWorld::UpdateComponents(double seconds)
    {
        return ExecuteUpdate(seconds, 0, 0, false, false, true);
    }
    bool SceneWorld::PrepareComponents(SceneLayout candidate, double seconds, PreparedLayout& prepared,
                                       std::vector<AnimatorFrame>& frames,
                                       std::map<std::string, std::array<float, 3>>& impulses, std::string& error)
    {
        Engine::CpuScope scope("Components update");
        RotateComponents(candidate, seconds);
        auto& runtime = execution_->values.scripts;
        if (!runtime.UpdateCandidate(candidate, seconds, error, execution_->values.inputValues,
                                     execution_->values.inputPressed, &execution_->physicsWorld, ScriptPhase::Update,
                                     &layout_))
        {
            Engine::Log::Warning(error);
            return false;
        }
        if (!PrepareScriptLayout(std::move(candidate), nullptr, prepared, error))
        {
            Engine::Log::Warning(error);
            return false;
        }
        if (!PrepareAnimators(prepared.layout, seconds, prepared.animated, prepared.states, runtime, frames, error))
        {
            Engine::Log::Warning(error);
            return false;
        }
        if (!PrepareRootMotion(prepared, frames, error))
        {
            Engine::Log::Warning(error);
            return false;
        }
        for (const auto& frame : frames)
            prepared.states.at(frame.id) = frame.state;
        impulses = runtime.Impulses();
        if (!UpdateLateComponents(seconds, prepared, runtime, frames, impulses, error))
            return false;
        if (!PrepareRuntimeCallbacks(prepared.layout, runtime, error))
        {
            Engine::Log::Warning(error);
            return false;
        }
        if (!ScriptRuntime::ValidateForPlay(prepared.layout, error, &execution_->values.scripts.Definitions()))
            return false;
        return true;
    }

    bool SceneWorld::PrepareRootMotion(PreparedLayout& prepared, std::vector<AnimatorFrame>& frames,
                                       std::string& error) const
    {
        Engine::CpuScope scope("Root motion and IK");
        if (frames.empty())
        {
            error.clear();
            return true;
        }
        try
        {
            const auto before = prepared.layout;
            std::vector<size_t> actors;
            for (const auto& frame : frames)
            {
                auto& placement = prepared.layout.objects.at(frame.index);
                if (!ApplyRootDelta(placement, frame))
                    continue;
                actors.push_back(frame.index);
            }
            if (actors.empty())
                return true;
            if (!execution_->physicsWorld.ConstrainRootMotion(before, prepared.layout, actors, assetsRoot_, error))
                return false;
            if (!PrepareTransforms(prepared.layout, prepared.objects, error))
                return false;
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(prepared.layout, matrices, error))
                return false;
            ConstrainRootFrames(prepared, frames, matrices);
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
    bool SceneWorld::SetAnimatorParameter(const std::string& id, const std::string& name, float value)
    {
        if (runtime_)
        {
            RuntimeScriptEffects effects;
            effects.animatorParameters[id][name] = value;
            return ApplySessionEffects(effects);
        }
        if (!CanMutateScene())
            return false;
        const auto found = animatorStates_.find(id);
        if (found == animatorStates_.end())
            return false;
        auto values = found->second.parameterOverrides;
        values[name] = value;
        try
        {
            Animator::ValidateParameters(values);
        }
        catch (const std::exception&)
        {
            return false;
        }
        if (found->second.parameterOverrides == values)
            return true;
        auto changes = changes_;
        SceneChangeSet detail;
        detail.fields.push_back({id, {}, "runtime.animatorParameters." + name});
        changes.Merge(detail);
        found->second.parameterOverrides = std::move(values);
        changes_.Swap(changes);
        return true;
    }
    bool SceneWorld::ClearAnimatorParameter(const std::string& id, const std::string& name)
    {
        if (runtime_)
        {
            RuntimeScriptEffects effects;
            effects.clearAnimatorParameters[id].push_back(name);
            return ApplySessionEffects(effects);
        }
        if (!CanMutateScene())
            return false;
        const auto found = animatorStates_.find(id);
        if (found == animatorStates_.end() || !found->second.parameterOverrides.contains(name))
            return false;
        auto changes = changes_;
        SceneChangeSet detail;
        detail.fields.push_back({id, {}, "runtime.animatorParameters." + name});
        changes.Merge(detail);
        found->second.parameterOverrides.erase(name);
        changes_.Swap(changes);
        return true;
    }

    bool SceneWorld::RenameObject(std::string_view id, std::string name)
    {
        if (runtime_)
        {
            std::string error;
            return EditSession(NameEdit{std::string(id), std::move(name)}, error);
        }
        if (!CanMutateScene())
            return false;
        std::string error;
        auto plan = SceneEdits::Prepare(layout_, NameEdit{std::string(id), std::move(name)}, nextObjectId_, error);
        if (!plan)
            return false;
        auto changes = changes_;
        changes.Merge(plan->changes);
        layout_ = std::move(plan->layout);
        changes_.Swap(changes);
        return true;
    }

    bool SceneWorld::SetPlacementTransform(std::string_view id, const ScenePlacement& placement)
    {
        if (runtime_)
        {
            std::string error;
            return EditSession(
                TransformEdit{std::string(id), {placement.position, placement.rotation, placement.scale}}, error);
        }
        Engine::CpuScope timing("Placement transform update");
        if (!CanMutateScene())
            return false;
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                        [&](const auto& value)
                                        {
                                            return value.id == id;
                                        });
        if (found == layout_.objects.end())
            return false;
        if (found->position == placement.position && found->rotation == placement.rotation &&
            found->scale == placement.scale)
            return true;
        std::string error;
        auto plan = SceneEdits::Prepare(
            layout_, TransformEdit{std::string(id), {placement.position, placement.rotation, placement.scale}},
            nextObjectId_, error);
        return plan && CommitTransforms(plan->layout);
    }

    bool SceneWorld::SetLocalTransform(std::string_view id, const std::array<float, 3>& position,
                                       const std::array<float, 3>& rotation, const std::array<float, 3>& scale)
    {
        return ApplyLocalTransform(id, {position, rotation, scale}).succeeded;
    }

    WorldReadView SceneWorld::Read() const noexcept
    {
        return WorldReadView(layout_);
    }

    SceneEditResult SceneWorld::Edit(const SceneEdit& edit, std::string& error)
    {
        if (runtime_)
        {
            if (!CanMutateScene(&error))
                return {};
            auto plan = SceneEdits::Prepare(layout_, edit, nextObjectId_, error);
            if (!plan)
                return {};
            SceneEditResult result{true, std::move(plan->changes)};
            if (result.changes.Empty())
                return result;
            if (!EditSession(edit, error))
                return {};
            nextObjectId_ = plan->nextId;
            return result;
        }
        if (!CanMutateScene(&error))
            return {};
        auto plan = SceneEdits::Prepare(layout_, edit, nextObjectId_, error);
        if (!plan)
            return {};
        SceneEditResult result{true, std::move(plan->changes)};
        if (result.changes.Empty())
            return result;
        const bool success = std::visit(
            [&](const auto& operation)
            {
                using T = std::decay_t<decltype(operation)>;
                if constexpr (std::is_same_v<T, TransformEdit> || std::is_same_v<T, WorldTransformEdit> ||
                              std::is_same_v<T, TranslationEdit> || std::is_same_v<T, RotationEdit> ||
                              std::is_same_v<T, ScaleEdit> || std::is_same_v<T, ParentEdit>)
                    return CommitTransforms(plan->layout);
                else if constexpr (std::is_same_v<T, NameEdit>)
                    return RenameObject(operation.id, operation.name);
                else if constexpr (std::is_same_v<T, ComponentsEdit>)
                    return SetComponentBatch(operation.values, assetsRoot_, error);
                else if constexpr (std::is_same_v<T, SettingsEdit>)
                    return SetSettings(operation.value, error);
                else if constexpr (std::is_same_v<T, AddObjectEdit>)
                {
                    std::string created;
                    return AddObject(operation.value, assetsRoot_, created, error);
                }
                else if constexpr (std::is_same_v<T, DuplicateEdit>)
                {
                    std::vector<std::string> created;
                    return DuplicateObjects(operation.ids, operation.offset, created, error);
                }
                else if constexpr (std::is_same_v<T, RemoveObjectsEdit>)
                    return RemoveObjects(operation.ids, error);
                else
                    return ReplaceLayout(std::move(plan->layout), assetsRoot_, error);
            },
            edit);
        if (!success)
            return {};
        error.clear();
        return result;
    }

    SceneEditResult SceneWorld::ApplyComponents(std::string_view id, const ScenePlacement& components,
                                                const std::filesystem::path& assetsRoot, std::string& error)
    {
        if (!CanMutateScene(&error))
            return {};
        const auto before = Read().Object(id);
        if (!before)
        {
            error = "Component owner no longer exists";
            return {};
        }
        auto candidate = *before;
        candidate.CopyComponents(components);
        SceneEditResult result{true, {}};
        result.changes.RecordObject(*before, candidate);
        if (result.changes.Empty())
        {
            error.clear();
            return result;
        }
        return SetComponents(id, candidate, assetsRoot, error) ? std::move(result) : SceneEditResult{};
    }

    SceneChangeSet SceneWorld::TakeChanges() noexcept
    {
        SceneChangeSet changes;
        if (CanMutateScene())
            changes_.Swap(changes);
        return changes;
    }

    SceneEditResult SceneWorld::ApplyLocalTransform(std::string_view id, const LocalTransform& transform)
    {
        if (!CanMutateScene())
            return {};
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                        [&](const auto& value)
                                        {
                                            return value.id == id;
                                        });
        if (found == layout_.objects.end())
            return {};
        ScenePlacement placement;
        placement.id = found->id;
        placement.parentId = found->parentId;
        placement.position = found->position;
        placement.rotation = found->rotation;
        placement.scale = found->scale;
        auto result = SceneTransformEdit::Apply(placement, transform);
        if (!result.succeeded || result.changes.Empty())
            return result;
        if (!SetPlacementTransform(id, placement))
            return {};
        return result;
    }

    bool SceneWorld::SetWorldTransform(std::string_view id, const std::array<float, 3>& position,
                                       const std::array<float, 3>& rotation, const std::array<float, 3>& scale)
    {
        if (!CanMutateScene())
            return false;
        ScenePlacement placement;
        placement.position = position;
        placement.rotation = rotation;
        placement.scale = scale;
        DirectX::XMFLOAT4X4 matrix;
        if (!SceneTransforms::Compose(placement, matrix))
            return false;
        return SetWorldTransform(id, matrix);
    }

    bool SceneWorld::SetWorldTransform(std::string_view id, const DirectX::XMFLOAT4X4& matrix)
    {
        if (!CanMutateScene())
            return false;
        ScenePlacement placement;
        if (!LocalTransformFromWorld(id, matrix, placement))
            return false;
        return SetPlacementTransform(id, placement);
    }

    bool SceneWorld::TranslateObjectsWorld(const std::vector<std::string>& ids, const std::array<float, 3>& delta)
    {
        if (!CanMutateScene())
            return false;
        std::string error;
        auto plan = SceneEdits::Prepare(layout_, TranslationEdit{ids, delta}, nextObjectId_, error);
        return plan && CommitTransforms(plan->layout);
    }

    bool SceneWorld::RotateObjectsWorld(const std::vector<std::string>& ids, const std::array<float, 3>& pivot,
                                        const DirectX::XMFLOAT4X4& rotation)
    {
        if (!CanMutateScene())
            return false;
        if (ids.empty() || !IsRotation(rotation) ||
            !std::all_of(pivot.begin(), pivot.end(),
                         [](float value)
                         {
                             return std::isfinite(value);
                         }))
            return false;
        const auto delta = DirectX::XMMatrixTranslation(-pivot[0], -pivot[1], -pivot[2]) *
                           DirectX::XMLoadFloat4x4(&rotation) *
                           DirectX::XMMatrixTranslation(pivot[0], pivot[1], pivot[2]);
        DirectX::XMFLOAT4X4 matrix;
        DirectX::XMStoreFloat4x4(&matrix, delta);
        return TransformObjectsWorld(ids, matrix);
    }

    bool SceneWorld::ScaleObjectsWorld(const std::vector<std::string>& ids, const std::array<float, 3>& pivot,
                                       const DirectX::XMFLOAT4X4& axes, const std::array<float, 3>& factors)
    {
        if (!CanMutateScene())
            return false;
        if (!IsRotation(axes) ||
            !std::all_of(pivot.begin(), pivot.end(),
                         [](float value)
                         {
                             return std::isfinite(value);
                         }) ||
            !std::all_of(factors.begin(), factors.end(),
                         [](float value)
                         {
                             return std::isfinite(value) && value > 0;
                         }))
            return false;
        auto scaling = DirectX::XMMatrixScaling(factors[0], factors[1], factors[2]);
        if (factors[0] != factors[1] || factors[1] != factors[2])
        {
            const auto frame = DirectX::XMLoadFloat4x4(&axes);
            scaling = DirectX::XMMatrixTranspose(frame) * scaling * frame;
        }
        DirectX::XMFLOAT4X4 matrix;
        DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixTranslation(-pivot[0], -pivot[1], -pivot[2]) * scaling *
                                              DirectX::XMMatrixTranslation(pivot[0], pivot[1], pivot[2]));
        return TransformObjectsWorld(ids, matrix);
    }

    bool SceneWorld::TransformObjectsWorld(const std::vector<std::string>& ids, const DirectX::XMFLOAT4X4& delta)
    {
        if (!CanMutateScene())
            return false;
        std::string error;
        auto plan = SceneEdits::Prepare(layout_, WorldTransformEdit{ids, delta}, nextObjectId_, error);
        return plan && CommitTransforms(plan->layout);
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
        if (runtime_)
        {
            if (!CanMutateScene(&error))
                return false;
            auto plan = SceneEdits::Prepare(layout_, AddObjectEdit{std::move(placement)}, nextObjectId_, error);
            if (!plan)
                return false;
            auto id = plan->createdIds.front();
            if (!EditSession(ReplaceDocumentEdit{std::move(plan->layout)}, error))
                return false;
            nextObjectId_ = plan->nextId;
            createdId.swap(id);
            return true;
        }
        if (!CanMutateScene(&error))
            return false;
        createdId.clear();
        try
        {
            if (!modelsReady_ && !IsDocumentView())
                throw std::runtime_error("Scene renderer is unavailable");
            auto plan = SceneEdits::Prepare(layout_, AddObjectEdit{std::move(placement)}, nextObjectId_, error);
            if (!plan)
                return false;
            auto nextCounter = plan->nextId;
            auto validation = std::move(plan->layout);
            placement = validation.objects.back();
            if (IsDocumentView())
            {
                const auto id = placement.id;
                if (!ReplaceLayout(std::move(validation), assetsRoot, error))
                    return false;
                nextObjectId_ = nextCounter;
                createdId = id;
                return true;
            }
            ChangePreparation preparing(preparingChanges_);
            validation.ResolveAssets(assetsRoot);
            placement = validation.objects.back();
            static_cast<void>(validation.Serialize());
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(validation, matrices, error))
                throw std::runtime_error(error);
            PreparedLayout prepared;
            auto object = BuildAddedObject(placement, validation, matrices.back(), assetsRoot, prepared);
            const auto id = placement.id;
            auto changes = changes_;
            changes.Merge(SceneChangeSet::Compare(layout_, validation));
            Append(std::move(placement), std::move(object));
            layout_.assetReferences = std::move(validation.assetReferences);
            animatedModels_.merge(prepared.animated);
            animatorStates_.merge(prepared.states);
            lodModels_.merge(prepared.lods);
            nextObjectId_ = nextCounter;
            createdId = id;
            changes_.Swap(changes);
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool SceneWorld::DuplicateObject(std::string_view id, const std::array<float, 3>& offset, std::string& createdId,
                                     std::string& error)
    {
        if (!CanMutateScene(&error))
            return false;
        std::vector<std::string> created;
        const bool success = DuplicateObjects({std::string(id)}, offset, created, error);
        createdId = success ? created.front() : std::string{};
        return success;
    }

    static bool FindDuplicateIndices(const SceneLayout& layout, const std::vector<std::string>& requested,
                                     std::vector<size_t>& indices, std::string& error)
    {
        if (requested.empty())
        {
            error = "No objects selected";
            return false;
        }

        for (const auto& id : requested)
        {
            const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                            [&](const auto& placement)
                                            {
                                                return placement.id == id;
                                            });
            if (found == layout.objects.end())
            {
                error = id + ": object no longer exists";
                return false;
            }
            const auto index = static_cast<size_t>(found - layout.objects.begin());
            if (std::find(indices.begin(), indices.end(), index) == indices.end())
                indices.push_back(index);
        }
        return true;
    }

    void SceneWorld::DuplicateAnimator(size_t index, const std::string& id, Engine::Object3D& object,
                                       std::map<std::string, std::shared_ptr<Engine::ModelRenderer>>& animated,
                                       std::map<std::string, AnimatorState>& states)
    {
        auto model = objects_[index].GetModel()->AnimatedCopy(materialDevice_.Get(), materialQueue_.Get());
        auto state = animatorStates_.at(layout_.objects[index].id);
        if (!model || !model->ApplyPose(state.pose))
            throw std::runtime_error("Animator duplication failed");
        object.SetModel(model);
        animated[id] = std::move(model);
        states[id] = std::move(state);
    }
    bool SceneWorld::DuplicateObjects(const std::vector<std::string>& ids, const std::array<float, 3>& offset,
                                      std::vector<std::string>& createdIds, std::string& error)
    {
        if (runtime_)
        {
            if (!CanMutateScene(&error))
                return false;
            auto plan = SceneEdits::Prepare(layout_, DuplicateEdit{ids, offset}, nextObjectId_, error);
            if (!plan || !EditSession(ReplaceDocumentEdit{std::move(plan->layout)}, error))
                return false;
            nextObjectId_ = plan->nextId;
            createdIds.swap(plan->createdIds);
            return true;
        }
        if (!CanMutateScene(&error))
            return false;
        const auto requested = ids;
        createdIds.clear();
        std::vector<size_t> indices;
        if (!FindDuplicateIndices(layout_, requested, indices, error))
            return false;
        auto plan = SceneEdits::Prepare(layout_, DuplicateEdit{requested, offset}, nextObjectId_, error);
        if (!plan)
            return false;
        auto candidate = std::move(plan->layout);
        auto objects = objects_;
        auto nextCounter = plan->nextId;
        auto animated = animatedModels_;
        auto states = animatorStates_;
        auto lods = lodModels_;
        auto created = std::move(plan->createdIds);
        for (size_t copyIndex = 0; copyIndex < indices.size(); ++copyIndex)
        {
            const auto index = indices[copyIndex];
            const auto& placement = candidate.objects[layout_.objects.size() + copyIndex];
            if (const auto levels = lodModels_.find(layout_.objects[index].id); levels != lodModels_.end())
                lods[placement.id] = levels->second;
            auto object = objects_[index];
            if (placement.animator && !IsDocumentView())
            {
                try
                {
                    DuplicateAnimator(index, placement.id, object, animated, states);
                }
                catch (const std::exception& exception)
                {
                    error = exception.what();
                    return false;
                }
            }
            objects.push_back(std::move(object));
        }
        try
        {
            candidate.Validate();
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
        if (IsDocumentView())
        {
            if (!ReplaceLayout(std::move(candidate), assetsRoot_, error))
                return false;
            nextObjectId_ = nextCounter;
            createdIds = std::move(created);
            return true;
        }
        if (!PrepareTransforms(candidate, objects, error))
            return false;
        auto changes = changes_;
        changes.Merge(SceneChangeSet::Compare(layout_, candidate));
        layout_ = std::move(candidate);
        objects_ = std::move(objects);
        ++renderVersion_;
        animatedModels_ = std::move(animated);
        animatorStates_ = std::move(states);
        lodModels_ = std::move(lods);
        nextObjectId_ = nextCounter;
        createdIds = std::move(created);
        changes_.Swap(changes);
        error.clear();
        return true;
    }

    bool SceneWorld::RemoveObjects(const std::vector<std::string>& ids, std::string& error)
    {
        if (runtime_)
            return EditSession(RemoveObjectsEdit{ids}, error);
        if (!CanMutateScene(&error))
            return false;
        ChangePreparation preparing(preparingChanges_);
        const std::unordered_set<std::string> removed(ids.begin(), ids.end());
        auto plan = SceneEdits::Prepare(layout_, RemoveObjectsEdit{ids}, nextObjectId_, error);
        if (!plan)
            return false;
        auto candidate = std::move(plan->layout);
        std::vector<Engine::Object3D> objects;
        objects.reserve(objects_.size());
        for (size_t index = 0; index < layout_.objects.size(); ++index)
        {
            if (removed.contains(layout_.objects[index].id))
                continue;
            objects.push_back(objects_[index]);
        }
        if (!PrepareTransforms(candidate, objects, error))
            return false;
        std::optional<ScriptRuntime> runtime;
        if (execution_)
        {
            runtime = execution_->values.scripts;
            try
            {
                runtime->PrepareTransition(layout_, candidate);
                if (!PrepareRuntimeCallbacks(candidate, *runtime, error))
                    return false;
            }
            catch (const std::exception& failure)
            {
                error = failure.what();
                return false;
            }
            catch (...)
            {
                error = "Script removal preparation failed";
                return false;
            }
        }
        auto changes = changes_;
        changes.Merge(SceneChangeSet::Compare(layout_, candidate));
        if (runtime)
            changes.Merge(runtime->ChangesSince(execution_->values.scripts));
        if (retirement_ && !retirement_->Retire(std::make_shared<std::vector<Engine::Object3D>>(objects_)))
        {
            error = "Cannot retire removed scene resources";
            return false;
        }
        auto packet = PrepareDocumentPacket(candidate, objects, error);
        if (modelsReady_ && !packet)
            return false;
        documentPacket_.swap(packet);
        layout_ = std::move(candidate);
        objects_ = std::move(objects);
        ++renderVersion_;
        for (const auto& id : removed)
        {
            animatedModels_.erase(id);
            animatorStates_.erase(id);
            lodModels_.erase(id);
            ForgetExecutionObject(id);
        }
        if (runtime)
        {
            runtime->CommitStarts();
            execution_->values.scripts = std::move(*runtime);
        }
        changes_.Swap(changes);
        error.clear();
        return true;
    }
    std::optional<std::string> SceneWorld::PickRay(const std::array<float, 3>& origin,
                                                   const std::array<float, 3>& direction, float maxDistance) const
    {
        const auto finite = [](const auto& vector)
        {
            return std::all_of(vector.begin(), vector.end(),
                               [](float value)
                               {
                                   return std::isfinite(value);
                               });
        };
        if (!finite(origin) || !finite(direction) || !std::isfinite(maxDistance) || maxDistance <= 0)
            return {};
        using namespace DirectX;
        const auto start = XMVectorSet(origin[0], origin[1], origin[2], 1);
        auto ray = XMVectorSet(direction[0], direction[1], direction[2], 0);
        const float length = XMVectorGetX(XMVector3Length(ray));
        if (!std::isfinite(length) || length <= 0)
            return {};
        ray /= length;
        float closest = maxDistance;
        std::optional<std::string> selected;
        for (size_t index = 0; index < objects_.size(); ++index)
        {
            const auto& object = objects_[index];
            const auto& placement = layout_.objects[index];
            const bool enabled = RenderableEnabled(placement);
            if (!object.GetModel() || !enabled)
                continue;
            const auto inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&object.GetWorldMatrix()));
            const auto localStart = XMVector3TransformCoord(start, inverse);
            auto localRay = XMVector3TransformNormal(ray, inverse);
            const float factor = XMVectorGetX(XMVector3Length(localRay));
            if (!std::isfinite(factor) || factor <= 0)
                continue;
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

    std::map<std::string, SceneWorld::PhysicalPose> SceneWorld::PhysicalPoses(const SceneLayout& layout)
    {
        std::map<std::string, PhysicalPose> result;
        for (const auto& p : layout.objects)
            if (HasPhysicalPose(p))
                result.emplace(p.id, PhysicalPose{p.position, p.rotation, p.scale, p.parentId});
        return result;
    }
    bool SceneWorld::InterpolatedLocalTransform(const ScenePlacement& p, DirectX::XMFLOAT4X4& transform,
                                                DirectX::XMFLOAT4X4& rotation) const
    {
        if (!execution_ || !HasPhysicalPose(p))
            return false;
        const auto before = execution_->values.previousPhysical.find(p.id),
                   now = execution_->values.currentPhysical.find(p.id);
        if (before == execution_->values.previousPhysical.end() || now == execution_->values.currentPhysical.end())
            return false;
        const auto& a = before->second;
        const auto& b = now->second;
        if (p.position != b.position || p.rotation != b.rotation || p.scale != b.scale || p.parentId != a.parent ||
            p.parentId != b.parent)
            return false;
        float distance = 0;
        for (size_t i = 0; i < 3; ++i)
            distance += (b.position[i] - a.position[i]) * (b.position[i] - a.position[i]);
        if (distance > 4)
            return false;
        // 物理姿勢を SRT に戻す際の微小なスケール誤差は許容します。保存された現在姿勢と異なる直接編集は上の判定で補間から除外します。
        for (size_t i = 0; i < 3; ++i)
            if (std::abs(a.scale[i] - b.scale[i]) >
                1e-5f * std::max(1.0f, std::max(std::abs(a.scale[i]), std::abs(b.scale[i]))))
                return false;
        const float alpha = static_cast<float>(std::clamp(execution_->values.fixedSeconds / FixedStep(), 0.0, 1.0));
        std::array<float, 3> position;
        for (size_t i = 0; i < 3; ++i)
            position[i] = a.position[i] + (p.position[i] - a.position[i]) * alpha;
        using namespace DirectX;
        const auto localRotation = [](const auto& angles)
        {
            return XMMatrixRotationX(angles[0]) * XMMatrixRotationY(angles[1]) * XMMatrixRotationZ(angles[2]);
        };
        const auto orientation = XMQuaternionSlerp(XMQuaternionRotationMatrix(localRotation(a.rotation)),
                                                   XMQuaternionRotationMatrix(localRotation(p.rotation)), alpha);
        const auto interpolatedRotation = XMMatrixRotationQuaternion(orientation);
        XMStoreFloat4x4(&rotation, interpolatedRotation);
        XMStoreFloat4x4(&transform, XMMatrixScaling(p.scale[0], p.scale[1], p.scale[2]) * interpolatedRotation *
                                        XMMatrixTranslation(position[0], position[1], position[2]));
        return true;
    }
    bool SceneWorld::PrepareRenderTransforms() const
    {
        if (preparedRenderVersion_ == renderVersion_)
            return true;
        std::map<std::string, DirectX::XMFLOAT4X4> overrides;
        for (const auto& p : layout_.objects)
        {
            DirectX::XMFLOAT4X4 matrix, rotation;
            if (InterpolatedLocalTransform(p, matrix, rotation))
                overrides.emplace(p.id, matrix);
        }
        if (overrides.empty())
        {
            renderMatrices_.clear();
            renderMatrices_.reserve(objects_.size());
            std::transform(objects_.begin(), objects_.end(), std::back_inserter(renderMatrices_),
                           [](const auto& object)
                           {
                               return object.GetWorldMatrix();
                           });
        }
        else
        {
            std::string error;
            if (!SceneTransforms::Resolve(layout_, overrides, renderMatrices_, error))
            {
                Engine::Log::Warning(error);
                return false;
            }
        }
        preparedRenderVersion_ = renderVersion_;
        return true;
    }
    bool SceneWorld::RenderWorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        if (!PrepareRenderTransforms())
            return false;
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                        [&](const auto& p)
                                        {
                                            return p.id == id;
                                        });
        if (found == layout_.objects.end())
            return false;
        matrix = renderMatrices_[static_cast<size_t>(found - layout_.objects.begin())];
        return true;
    }

    bool SceneWorld::WorldRotation(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        auto rotation = DirectX::XMMatrixIdentity();
        auto current = std::string(id);
        size_t remaining = layout_.objects.size();
        do
        {
            const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                            [&](const auto& placement)
                                            {
                                                return placement.id == current;
                                            });
            if (found == layout_.objects.end() || remaining-- == 0)
                return false;
            const auto& angles = found->rotation;
            rotation = rotation * DirectX::XMMatrixRotationX(angles[0]) * DirectX::XMMatrixRotationY(angles[1]) *
                       DirectX::XMMatrixRotationZ(angles[2]);
            current = found->parentId;
        } while (!current.empty());
        DirectX::XMFLOAT4X4 candidate;
        DirectX::XMStoreFloat4x4(&candidate, rotation);
        if (!SceneTransforms::IsUsable(candidate))
            return false;
        matrix = candidate;
        return true;
    }

    bool SceneWorld::RenderWorldRotation(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        auto rotation = DirectX::XMMatrixIdentity();
        auto current = std::string(id);
        size_t remaining = layout_.objects.size();
        do
        {
            const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                            [&](const auto& placement)
                                            {
                                                return placement.id == current;
                                            });
            if (found == layout_.objects.end() || remaining-- == 0)
                return false;
            DirectX::XMFLOAT4X4 transform, local;
            if (InterpolatedLocalTransform(*found, transform, local))
                rotation = rotation * DirectX::XMLoadFloat4x4(&local);
            else
            {
                const auto& angles = found->rotation;
                rotation = rotation * DirectX::XMMatrixRotationX(angles[0]) * DirectX::XMMatrixRotationY(angles[1]) *
                           DirectX::XMMatrixRotationZ(angles[2]);
            }
            current = found->parentId;
        } while (!current.empty());
        DirectX::XMFLOAT4X4 candidate;
        DirectX::XMStoreFloat4x4(&candidate, rotation);
        if (!SceneTransforms::IsUsable(candidate))
            return false;
        matrix = candidate;
        return true;
    }

    bool SceneWorld::WorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                        [&](const auto& placement)
                                        {
                                            return placement.id == id;
                                        });
        if (found == layout_.objects.end())
            return false;
        matrix = objects_[static_cast<size_t>(found - layout_.objects.begin())].GetWorldMatrix();
        return true;
    }

    bool SceneWorld::SkeletonWorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                        [&](const auto& placement)
                                        {
                                            return placement.id == id;
                                        });
        if (found == layout_.objects.end())
            return false;
        const auto& object = objects_[static_cast<size_t>(found - layout_.objects.begin())];
        const auto model = object.GetModel();
        if (!model || !model->Rig())
            return false;
        const auto& rig = *model->Rig();
        if (!std::isfinite(rig.importScale) || rig.importScale <= 0)
            return false;
        DirectX::XMFLOAT4X4 candidate;
        DirectX::XMStoreFloat4x4(&candidate,
                                 DirectX::XMLoadFloat4x4(&rig.inverseRoot) *
                                     DirectX::XMMatrixScaling(rig.importScale, rig.importScale, rig.importScale) *
                                     DirectX::XMLoadFloat4x4(&object.GetWorldMatrix()));
        if (!SceneTransforms::IsUsable(candidate))
            return false;
        matrix = candidate;
        return true;
    }

    bool SceneWorld::LocalTransformFromWorld(std::string_view id, const DirectX::XMFLOAT4X4& world,
                                             ScenePlacement& placement) const
    {
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                        [&](const auto& value)
                                        {
                                            return value.id == id;
                                        });
        if (found == layout_.objects.end())
            return false;
        auto matrix = world;
        if (!found->parentId.empty())
        {
            DirectX::XMFLOAT4X4 parent;
            if (!WorldMatrix(found->parentId, parent) || !SceneTransforms::WorldToLocal(world, parent, matrix))
                return false;
        }
        return SceneTransforms::ReadTransform(matrix, *found, placement);
    }

    bool SceneWorld::WorldBounds(std::string_view id, std::array<std::array<float, 3>, 8>& corners) const
    {
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
                                        [id](const auto& placement)
                                        {
                                            return placement.id == id;
                                        });
        if (found == layout_.objects.end())
            return false;
        const auto& object = objects_[static_cast<size_t>(found - layout_.objects.begin())];
        DirectX::XMFLOAT3 localCorners[8];
        if (object.GetModel())
            object.GetModel()->Bounds().GetCorners(localCorners);
        else
            DirectX::BoundingBox(DirectX::XMFLOAT3{}, DirectX::XMFLOAT3{0.25f, 0.25f, 0.25f}).GetCorners(localCorners);
        const auto matrix = DirectX::XMLoadFloat4x4(&object.GetWorldMatrix());
        for (size_t i = 0; i < 8; ++i)
        {
            DirectX::XMFLOAT3 position;
            DirectX::XMStoreFloat3(&position,
                                   DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&localCorners[i]), matrix));
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
                return false;
            corners[i] = {position.x, position.y, position.z};
        }
        return true;
    }
}

namespace SceneRuntime
{
    SceneWorld::SceneWorld()
        : execution_(std::make_unique<SceneExecutionState>()), document_(std::make_shared<SceneDocumentData>()),
          layout_(document_->layout_)
    {
    }
    SceneWorld::SceneWorld(std::shared_ptr<SceneDocumentData> document)
        : document_(document ? std::move(document) : throw std::invalid_argument("Document is null")),
          context_(document_->Content()), layout_(document_->layout_)
    {
        if (document_->view_)
            throw std::runtime_error("Document already has an editing projection");
        document_->view_ = this;
    }

    auto SceneWorld::SetContentContext(std::shared_ptr<const Engine::ContentContext> context) -> void
    {
        if (!CanMutateScene())
            return;
        if (!context || (context_ && context_ != context))
            throw std::runtime_error("Content context is already owned");
        context_ = std::move(context);
        document_->context_ = context_;
    }

    SceneWorld::~SceneWorld()
    {
        ChangePreparation preparing(preparingChanges_);
        runtime_.reset();
        if (execution_)
            execution_->values.scripts.Stop(layout_);
        else if (document_->view_ == this)
            document_->view_ = nullptr;
    }

    auto SceneWorld::PauseScripts(bool paused) -> void
    {
        std::string error;
        if (!SetScriptsPaused(paused, error) && !error.empty())
            Engine::Log::Warning(error);
    }

    bool SceneWorld::SetScriptsPaused(bool paused, std::string& error)
    {
        if (runtime_)
        {
            if (!CanMutateScene(&error))
                return false;
            if (runtime_->session.Paused() == paused)
            {
                error.clear();
                return true;
            }
            ChangePreparation preparing(preparingChanges_);
            ChangePreparation pause(runtime_->pauseRequest);
            const bool success = runtime_->session.Pause(paused, error);
            if (success)
                DrainSession();
            return success;
        }
        if (!CanMutateScene(&error))
            return false;
        if (!execution_ || execution_->values.scripts.Paused() == paused)
        {
            error.clear();
            return true;
        }
        ChangePreparation preparing(preparingChanges_);
        try
        {
            auto layout = layout_;
            auto runtime = execution_->values.scripts;
            if (!runtime.Pause(layout, paused))
            {
                error = "Script pause preparation failed";
                return false;
            }
            if (SceneChangeSet::Compare(layout_, layout).Empty())
            {
                auto changes = changes_;
                changes.Merge(runtime.ChangesSince(execution_->values.scripts));
                execution_->values.scripts = std::move(runtime);
                changes_.Swap(changes);
                error.clear();
                return true;
            }
            PreparedLayout candidate;
            if (!PrepareScriptLayout(std::move(layout), nullptr, candidate, error) ||
                !PrepareRuntimeCallbacks(candidate.layout, runtime, error))
                return false;
            auto changes = changes_;
            changes.Merge(SceneChangeSet::Compare(layout_, candidate.layout));
            changes.Merge(runtime.ChangesSince(execution_->values.scripts));
            CommitLayout(std::move(candidate), true);
            execution_->values.scripts = std::move(runtime);
            changes_.Swap(changes);
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
        catch (...)
        {
            error = "Script pause preparation failed";
            return false;
        }
    }

    auto SceneWorld::DiscardAudioCommands() -> void
    {
        if (runtime_)
        {
            if (CanMutateScene())
                runtime_->effects.audio.clear();
            return;
        }
        if (!CanMutateScene())
            return;
        if (execution_)
            execution_->values.scripts.DiscardAudioCommands();
    }

    auto SceneWorld::DiscardUiCommands() -> void
    {
        if (runtime_)
        {
            if (CanMutateScene())
            {
                runtime_->effects.ui.values.clear();
                runtime_->effects.ui.texts.clear();
            }
            return;
        }
        if (!CanMutateScene())
            return;
        if (execution_)
            execution_->values.scripts.DiscardUiCommands();
    }

    auto SceneWorld::SetRuntimePreparation(std::function<bool(const SceneLayout&, std::string&)> prepare) -> void
    {
        if (!CanMutateScene())
            return;
        prepareRuntime_ = std::move(prepare);
    }

    auto SceneWorld::SetUiPreparation(
        std::function<bool(const SceneLayout&, const ScriptUiCommands&, std::string&)> prepare) -> void
    {
        if (!CanMutateScene())
            return;
        prepareUi_ = std::move(prepare);
    }

    auto SceneWorld::SetInputActions(std::map<std::string, float> values, std::map<std::string, bool> pressed) -> void
    {
        if (runtime_)
        {
            if (CanMutateScene())
            {
                runtime_->input.values.swap(values);
                runtime_->input.pressed.swap(pressed);
            }
            return;
        }
        if (!CanMutateScene())
            return;
        if (!execution_)
            return;
        execution_->values.inputValues = std::move(values);
        execution_->values.inputPressed = std::move(pressed);
    }

    auto SceneWorld::DiscardPendingInput() -> void
    {
        if (runtime_)
        {
            if (!CanMutateScene())
                return;
            runtime_->session.DiscardPendingInput();
            runtime_->input.values.clear();
            runtime_->input.pressed.clear();
            runtime_->effects.ui.values.clear();
            runtime_->effects.ui.texts.clear();
            runtime_->effects.audio.clear();
            return;
        }
        if (!CanMutateScene())
            return;
        if (!execution_)
            return;
        execution_->values.fixedPressed.clear();
        execution_->values.fixedJump = false;
        execution_->values.inputValues.clear();
        execution_->values.inputPressed.clear();
        if (execution_)
            execution_->values.scripts.DiscardUiCommands();
        if (execution_)
            execution_->values.scripts.DiscardAudioCommands();
    }
}
