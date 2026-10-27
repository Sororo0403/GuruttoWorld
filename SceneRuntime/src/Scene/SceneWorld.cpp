#include <SceneRuntime/Rendering/SceneRenderState.h>
#include <SceneRuntime/Rendering/EditorSceneProjection.h>
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
#include <SceneRuntime/Gameplay/Navigation.h>
#include <SceneRuntime/Physics/Ragdoll.h>
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
            if (editorProjection_)
                editorProjection_->SetContentContext(context_);
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
            if (editorProjection_)
                editorProjection_->SetContentContext(context_);
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
        return renderState_->Initialize(renderer, shaderPath, diagnostic);
    }
    bool SceneWorld::PrepareLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error,
                                   bool preserveExecution, PreparedLayout& result, const PreparedLayout* previous)
    {
        return renderState_->Gpu().PrepareLayout(std::move(layout), assetsRoot, error, preserveExecution, result,
                                                 previous, BaselineView(previous), IsDocumentView(), context_);
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
            renderState_->ResetPreview();
            return true;
        }
        auto& candidate = [&]() -> SceneLayout&
        {
            Engine::CpuScope scope("Animation layout clone");
            // 全内容を毎回同期し、外部編集と失敗後の候補を持ち越さず格納領域だけ再利用します。
            return renderState_->AnimationScratch(layout_);
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
                renderState_->ApplySurface(i, layout_.objects[i]);
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
            if (runtimeOwner_)
                return ReplaceSession(std::move(layout), assetsRoot, false, error);
            if (DocumentEditor())
                return ForwardDocumentEdit(ReplaceDocumentEdit{std::move(layout)}, assetsRoot, error).succeeded;
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
        if (DocumentEditor())
            return editorProjection_->ReloadAssets(renderer, assetsRoot, shaderPath, error);
        ChangePreparation preparing(preparingChanges_);
        auto storage = std::make_shared<SceneDocumentData>(SceneLayout{}, context_);
        auto candidate = std::make_unique<SceneWorld>(storage);
        if (context_ && !candidate->Content())
            candidate->SetContentContext(context_);
        if (!candidate->Initialize(renderer, assetsRoot, layout_, shaderPath, &error))
            return false;
        if (runtime_)
        {
            struct ReloadScope
            {
                RuntimeData& data;
                ~ReloadScope()
                {
                    data.reloading = nullptr;
                }
            } scope{*runtime_};
            runtime_->reloading = candidate.get();
            const SceneEdit resolved = ReplaceDocumentEdit{candidate->layout_};
            if (!runtime_->session.ReloadAssets(error, &resolved))
                return false;
            DrainSession();
            return true;
        }
        auto prepared = candidate->RuntimeLayout();
        auto packet = candidate->RenderSnapshot();
        if (candidate->renderState_->Gpu().Ready() && !packet)
            return false;
        auto changes = changes_;
        changes.Merge(SceneChangeSet::Compare(layout_, candidate->layout_));
        changes.fields.push_back({{}, {}, "resources.reload"});
        if (!renderState_->PrepareRetirement(prepared, error))
            return false;
        layout_.Swap(candidate->layout_);
        packet = renderState_->CommitReload(*candidate->renderState_, std::move(packet));
        changes_.Swap(changes);
        error.clear();
        return true;
    }

    bool SceneWorld::ReplaceLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error,
                                   bool preserveExecution)
    {
        if (DocumentEditor())
            return ForwardDocumentEdit(ReplaceDocumentEdit{std::move(layout)}, assetsRoot, error).succeeded;

        if (runtimeOwner_)
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
            if (!PrepareLayout(std::move(layout), assetsRoot, error, false, prepared))
                return false;
            CommitLayout(std::move(prepared), false);
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }
    SceneWorld::LayoutView SceneWorld::BaselineView(const PreparedLayout* previous) const
    {
        if (previous)
            return {previous->layout,    previous->objects,    previous->assetsRoot, previous->assets,
                    previous->materials, previous->animated,   previous->states,     previous->lods,
                    previous->shadow,    previous->localShadow};
        return renderState_->View(layout_);
    }
    void SceneWorld::CommitLayout(PreparedLayout&& prepared, bool preserveExecution)
    {
        std::string error;
        auto packet = PrepareDocumentPacket(prepared, error);
        if (renderState_->Gpu().Ready() && !packet)
            throw std::runtime_error(error);
        auto changes = changes_;
        changes.Merge(SceneChangeSet::Compare(layout_, prepared.layout));
        if (!renderState_->PrepareRetirement(prepared, error))
            throw std::runtime_error(error);
        layout_.Swap(prepared.layout);
        packet = renderState_->CommitPrepared(prepared, std::move(packet), preserveExecution);
        changes_.Swap(changes);
    }

    void SceneWorld::Draw(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
                          const Engine::DirectionalLight& light, const UiState* state) const
    {
        const auto defaults = state ? UiState{} : SceneUi::Defaults(layout_);
        if (!renderState_->DrawScene(commands, camera, light, state ? *state : defaults))
            Engine::Log::Warning("Render packet could not be recorded");
    }

    bool SceneWorld::PrepareTransforms(const SceneLayout& layout, std::vector<Engine::Object3D>& objects,
                                       std::string& error, const SceneLayout* validatedBefore)
    {
        return SceneRenderState::PrepareTransforms(layout, objects, error, validatedBefore);
    }

    bool SceneWorld::CommitTransforms(const SceneLayout& candidate)
    {
        if (DocumentEditor())
        {
            std::string error;
            return ForwardDocumentEdit(ReplaceDocumentEdit{candidate}, {}, error).succeeded;
        }

        if (runtime_)
        {
            std::string error;
            return EditSession(ReplaceDocumentEdit{candidate}, error);
        }
        if (!CanMutateScene())
            return false;
        auto objects = renderState_->Read().objects;
        std::string error;
        if (!PrepareTransforms(candidate, objects, error, &layout_))
            return false;
        for (size_t index = 0; index < candidate.objects.size(); ++index)
            if (candidate.objects[index].material != layout_.objects[index].material)
                SceneGpuResources::ApplySurfaceOverride(objects[index], candidate.objects[index]);
        auto packet = PrepareDocumentPacket(candidate, objects, error);
        if (renderState_->Gpu().Ready() && !packet)
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
        packet = renderState_->CommitTransforms(objects, std::move(packet));
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
        if (DocumentEditor())
            return ForwardDocumentEdit(ParentEdit{ids, parentId}, {}, error).succeeded;

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

    bool SceneWorld::SetSettings(const SceneSettings& settings, std::string& error)
    {
        if (DocumentEditor())
            return ForwardDocumentEdit(SettingsEdit{settings}, {}, error).succeeded;

        if (runtime_)
            return EditSession(SettingsEdit{settings}, error);
        if (!CanMutateScene(&error))
            return false;
        auto plan = SceneEdits::Prepare(layout_, SettingsEdit{settings}, nextObjectId_, error);
        if (!plan)
            return false;
        if (plan->changes.Empty())
        {
            error.clear();
            return true;
        }
        return ReplaceLayout(std::move(plan->layout), renderState_->Read().assetsRoot, error);
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
        if (DocumentEditor())
            return ForwardDocumentEdit(ComponentsEdit{settings}, assetsRoot, error).succeeded;

        if (runtime_)
        {
            if (!CheckSessionRoot(assetsRoot, error))
                return false;
            return EditSession(ComponentsEdit{settings}, error);
        }
        if (!CanMutateScene(&error))
            return false;
        auto plan = SceneEdits::Prepare(layout_, ComponentsEdit{settings}, nextObjectId_, error);
        if (!plan)
            return false;
        if (plan->changes.Empty())
        {
            error.clear();
            return true;
        }
        return ReplaceLayout(std::move(plan->layout), assetsRoot, error);
    }

    bool SceneWorld::GenerateRagdoll(const std::string& id, const std::filesystem::path& root, std::string& error)
    {
        if (!CanMutateScene(&error))
            return false;
        const auto model = renderState_->Read().animated.find(id);
        const auto state = renderState_->Read().states.find(id);
        if (model == renderState_->Read().animated.end() || state == renderState_->Read().states.end())
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
        return false;
    }
    bool SceneWorld::SameRenderResources(const SceneLayout& layout, const SceneLayout& before)
    {
        return SceneRenderState::SameResources(layout, before);
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
                object = renderState_->Gpu().BuildStaticRuntimeObject(p, candidate, renderState_->Read().assetsRoot,
                                                                      context_);
            }
            SceneGpuResources::ApplySurfaceOverride(object, p);
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
    SceneWorld::PreparedLayout SceneWorld::RuntimeLayout() const
    {
        return renderState_->Capture(layout_);
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
                if (!renderState_->RetireObjects(baseline.objects))
                    throw std::runtime_error("Script resource retirement failed");
                if (previous)
                    return PrepareLayout(std::move(layout), renderState_->Read().assetsRoot, error, true, result,
                                         previous);
                const auto snapshot = RuntimeLayout();
                return PrepareLayout(std::move(layout), renderState_->Read().assetsRoot, error, true, result,
                                     &snapshot);
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
                if (!renderState_->RetireObjects(baseline.objects))
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
        if (runtime_ && CanMutateScene(&error))
            return runtime_->session.QueueEvent(std::move(event), error);
        error = "Runtime session is not initialized";
        return false;
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
        error = "Runtime session is not initialized";
        return false;
    }
    bool SceneWorld::UpdateComponents(double seconds)
    {
        return ExecuteUpdate(seconds, 0, 0, false, false, true);
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
        const auto found = renderState_->Read().states.find(id);
        if (found == renderState_->Read().states.end())
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
        renderState_->CommitAnimatorParameters(id, values);
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
        const auto found = renderState_->Read().states.find(id);
        if (found == renderState_->Read().states.end() || !found->second.parameterOverrides.contains(name))
            return false;
        auto changes = changes_;
        SceneChangeSet detail;
        detail.fields.push_back({id, {}, "runtime.animatorParameters." + name});
        changes.Merge(detail);
        renderState_->EraseAnimatorParameter(id, name);
        changes_.Swap(changes);
        return true;
    }

    bool SceneWorld::RenameObject(std::string_view id, std::string name)
    {
        if (DocumentEditor())
        {
            std::string error;
            return ForwardDocumentEdit(NameEdit{std::string(id), std::move(name)}, {}, error).succeeded;
        }

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
        if (DocumentEditor())
        {
            std::string error;
            return ForwardDocumentEdit(
                       TransformEdit{std::string(id), {placement.position, placement.rotation, placement.scale}}, {},
                       error)
                .succeeded;
        }

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
        if (DocumentEditor())
            return ForwardDocumentEdit(edit, {}, error);

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
            const bool creates =
                std::holds_alternative<AddObjectEdit>(edit) || std::holds_alternative<DuplicateEdit>(edit);
            const bool committed =
                creates ? EditSession(ReplaceDocumentEdit{std::move(plan->layout), std::move(plan->duplicates)}, error)
                        : EditSession(edit, error);
            if (!committed)
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
                    return SetComponentBatch(operation.values, renderState_->Read().assetsRoot, error);
                else if constexpr (std::is_same_v<T, SettingsEdit>)
                    return SetSettings(operation.value, error);
                else if constexpr (std::is_same_v<T, AddObjectEdit>)
                {
                    std::string created;
                    return AddObject(operation.value, renderState_->Read().assetsRoot, created, error);
                }
                else if constexpr (std::is_same_v<T, DuplicateEdit>)
                {
                    std::vector<std::string> created;
                    return DuplicateObjects(operation.ids, operation.offset, created, error);
                }
                else if constexpr (std::is_same_v<T, RemoveObjectsEdit>)
                    return RemoveObjects(operation.ids, error);
                else
                    return ReplaceLayout(std::move(plan->layout), renderState_->Read().assetsRoot, error);
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
        if (DocumentEditor())
        {
            std::string error;
            return ForwardDocumentEdit(TransformEdit{std::string(id), transform}, {}, error);
        }

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
        renderState_->ReserveAdditionalObject();
        layout_.objects.push_back(std::move(placement));
        renderState_->AppendObject(std::move(object));
    }

    bool SceneWorld::AddObject(ScenePlacement placement, const std::filesystem::path& assetsRoot,
                               std::string& createdId, std::string& error)
    {
        if (DocumentEditor())
        {
            createdId.clear();
            auto result = ForwardDocumentEdit(AddObjectEdit{std::move(placement)}, assetsRoot, error);
            if (!result.succeeded)
                return false;
            for (auto& object : result.changes.objects)
                if (object.kind == SceneChangeKind::Added)
                {
                    createdId.swap(object.objectId);
                    return true;
                }
            return false;
        }

        if (runtime_)
        {
            if (!CanMutateScene(&error))
                return false;
            createdId.clear();
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
            if (!renderState_->Gpu().Ready() && !IsDocumentView())
                throw std::runtime_error("Scene renderer is unavailable");
            createdId.clear();
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
            auto object = renderState_->Gpu().BuildAddedObject(placement, validation, matrices.back(), assetsRoot,
                                                               prepared, context_);
            const auto id = placement.id;
            auto changes = changes_;
            changes.Merge(SceneChangeSet::Compare(layout_, validation));
            Append(std::move(placement), std::move(object));
            layout_.assetReferences = std::move(validation.assetReferences);
            renderState_->MergeAnimation(prepared);
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
        renderState_->Gpu().DuplicateAnimator(renderState_->Read().objects[index],
                                              renderState_->Read().states.at(layout_.objects[index].id), id, object,
                                              animated, states);
    }
    bool SceneWorld::DuplicateObjects(const std::vector<std::string>& ids, const std::array<float, 3>& offset,
                                      std::vector<std::string>& createdIds, std::string& error)
    {
        if (DocumentEditor())
        {
            const auto requested = ids;
            createdIds.clear();
            createdIds.reserve(layout_.objects.size());
            auto result = ForwardDocumentEdit(DuplicateEdit{requested, offset}, {}, error);
            if (!result.succeeded)
                return false;
            for (auto& object : result.changes.objects)
                if (object.kind == SceneChangeKind::Added)
                    createdIds.push_back(std::move(object.objectId));
            return true;
        }

        if (runtime_)
        {
            if (!CanMutateScene(&error))
                return false;
            const auto requested = ids;
            createdIds.clear();
            auto plan = SceneEdits::Prepare(layout_, DuplicateEdit{requested, offset}, nextObjectId_, error);
            if (!plan || !EditSession(ReplaceDocumentEdit{std::move(plan->layout), std::move(plan->duplicates)}, error))
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
        auto objects = renderState_->Read().objects;
        auto nextCounter = plan->nextId;
        auto animated = renderState_->Read().animated;
        auto states = renderState_->Read().states;
        auto lods = renderState_->Read().lods;
        auto created = std::move(plan->createdIds);
        for (size_t copyIndex = 0; copyIndex < indices.size(); ++copyIndex)
        {
            const auto index = indices[copyIndex];
            const auto& placement = candidate.objects[layout_.objects.size() + copyIndex];
            if (const auto levels = renderState_->Read().lods.find(layout_.objects[index].id);
                levels != renderState_->Read().lods.end())
                lods[placement.id] = levels->second;
            auto object = renderState_->Read().objects[index];
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
            if (!ReplaceLayout(std::move(candidate), renderState_->Read().assetsRoot, error))
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
        renderState_->CommitDuplicates(objects, animated, states, lods);
        nextObjectId_ = nextCounter;
        createdIds = std::move(created);
        changes_.Swap(changes);
        error.clear();
        return true;
    }

    bool SceneWorld::RemoveObjects(const std::vector<std::string>& ids, std::string& error)
    {
        if (DocumentEditor())
            return ForwardDocumentEdit(RemoveObjectsEdit{ids}, {}, error).succeeded;

        if (runtime_)
            return EditSession(RemoveObjectsEdit{ids}, error);
        if (!CanMutateScene(&error))
            return false;
        auto plan = SceneEdits::Prepare(layout_, RemoveObjectsEdit{ids}, nextObjectId_, error);
        if (!plan)
            return false;
        if (plan->changes.Empty())
        {
            error.clear();
            return true;
        }
        return ReplaceLayout(std::move(plan->layout), renderState_->Read().assetsRoot, error);
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
        for (size_t index = 0; index < renderState_->Read().objects.size(); ++index)
        {
            const auto& object = renderState_->Read().objects[index];
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

    bool SceneWorld::InterpolatedLocalTransform(const ScenePlacement& p, DirectX::XMFLOAT4X4& transform,
                                                DirectX::XMFLOAT4X4& rotation) const
    {
        if (!runtime_ || !HasPhysicalPose(p))
            return false;
        const auto before = runtime_->previousPhysical.find(p.id), now = runtime_->currentPhysical.find(p.id);
        if (before == runtime_->previousPhysical.end() || now == runtime_->currentPhysical.end())
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
        const float alpha = static_cast<float>(std::clamp(runtime_->fixedSeconds / FixedStep(), 0.0, 1.0));
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
        if (renderState_->MatricesCurrent())
            return true;
        std::map<std::string, DirectX::XMFLOAT4X4> overrides;
        for (const auto& p : layout_.objects)
        {
            DirectX::XMFLOAT4X4 matrix, rotation;
            if (InterpolatedLocalTransform(p, matrix, rotation))
                overrides.emplace(p.id, matrix);
        }
        std::string error;
        if (!renderState_->PrepareMatrices(layout_, overrides, error))
        {
            Engine::Log::Warning(error);
            return false;
        }
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
        matrix = renderState_->Matrices()[static_cast<size_t>(found - layout_.objects.begin())];
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
        matrix = renderState_->Read().objects[static_cast<size_t>(found - layout_.objects.begin())].GetWorldMatrix();
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
        const auto& object = renderState_->Read().objects[static_cast<size_t>(found - layout_.objects.begin())];
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
        const auto& object = renderState_->Read().objects[static_cast<size_t>(found - layout_.objects.begin())];
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
        : renderState_(std::make_unique<SceneRenderState>()), nextObjectId_(ownedNextId_), layout_(ownedLayout_),
          changes_(ownedChanges_)
    {
    }
    SceneWorld::SceneWorld(std::shared_ptr<SceneDocumentData> document)
        : renderState_(std::make_unique<SceneRenderState>()), runtimeOwner_(false),
          document_(document ? std::move(document) : throw std::invalid_argument("Document is null")),
          context_(document_->Content()),
          editorProjection_(std::make_unique<EditorSceneProjection>(document_, *renderState_)),
          nextObjectId_(editorProjection_->BorrowNextId()), layout_(editorProjection_->BorrowLayout()),
          changes_(editorProjection_->BorrowChanges())
    {
    }

    auto SceneWorld::SetContentContext(std::shared_ptr<const Engine::ContentContext> context) -> void
    {
        if (!CanMutateScene())
            return;
        if (!context || (context_ && context_ != context))
            throw std::runtime_error("Content context is already owned");
        context_ = std::move(context);
        if (editorProjection_)
            editorProjection_->SetContentContext(context_);
    }

    SceneWorld::~SceneWorld()
    {
        ChangePreparation preparing(preparingChanges_);
        runtime_.reset();
        editorProjection_.reset();
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
        error.clear();
        return true;
    }

    auto SceneWorld::DiscardAudioCommands() -> void
    {
        if (runtime_)
        {
            if (CanMutateScene())
                runtime_->effects.audio.clear();
            return;
        }
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
    }
}

namespace SceneRuntime
{
    const std::vector<std::string>& SceneWorld::ResourceDiagnostics() const
    {
        return renderState_->Read().diagnostics;
    }
}

namespace SceneRuntime
{
    const MeshTelemetry& SceneWorld::Telemetry() const
    {
        return renderState_->Telemetry();
    }
}

namespace SceneRuntime
{
    size_t SceneWorld::RenderVersion() const
    {
        return renderState_->Version();
    }
}

namespace SceneRuntime
{
    SceneEditTarget* SceneWorld::DocumentEditor() const noexcept
    {
        return IsDocumentView() ? document_->Editor() : nullptr;
    }
    SceneEditResult SceneWorld::ForwardDocumentEdit(const SceneEdit& edit, const std::filesystem::path& root,
                                                    std::string& error)
    {
        if (!CanMutateScene(&error))
            return {};
        try
        {
            if (!root.empty() && context_)
                context_->RequireRoot(root);
            auto* editor = DocumentEditor();
            return editor ? editor->Edit(edit, error) : SceneEditResult{};
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return {};
        }
    }

}
