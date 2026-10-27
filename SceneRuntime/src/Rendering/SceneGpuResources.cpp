#include <SceneRuntime/Rendering/SceneGpuResources.h>
#include <SceneRuntime/Scene/SceneAssetReferences.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <SceneRuntime/Rendering/MaterialAsset.h>
#include <SceneRuntime/Gameplay/GenreGeometry.h>
#include <SceneRuntime/Physics/Ragdoll.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Profiler.h>
#include <Engine/Core/Log.h>
#include <stdexcept>
#include <algorithm>

namespace
{
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

}
namespace SceneRuntime
{
    void SceneGpuResources::ApplySurfaceOverride(Engine::Object3D& object,
                                                 const SceneRuntime::ScenePlacement& placement)
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
    bool SceneGpuResources::Initialize(const Engine::DirectX12Renderer& renderer,
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
    Engine::Object3D SceneGpuResources::BuildLayoutObject(const ScenePlacement& placement, const SceneLayout& layout,
                                                          const DirectX::XMFLOAT4X4& matrix, PreparedLayout& prepared,
                                                          const LayoutView& baseline, bool preserveExecution,
                                                          const std::shared_ptr<const Engine::ContentContext>& context)
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
                                                            prepared.assetsRoot, context))
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
    void SceneGpuResources::InitializeAnimatorObject(Engine::Object3D& object, const ScenePlacement& placement,
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
    bool SceneGpuResources::PrepareLayoutAssets(SceneLayout& layout, PreparedLayout& prepared,
                                                const PreparedLayout* previous, bool configurationCurrent,
                                                std::map<std::string, std::string>& unresolved, bool editing,
                                                const std::shared_ptr<const Engine::ContentContext>& context)
    {
        bool indexAvailable = configurationCurrent;
        try
        {
            prepared.assets = previous && previous->assets && previous->assetsRoot == prepared.assetsRoot
                                  ? previous->assets
                                  : std::make_shared<Engine::AssetDatabase>(context, true);
            if (editing)
                unresolved = ResolveSceneAssetReferencesForEditing(layout, *prepared.assets);
            else
                layout.ResolveAssets(*prepared.assets);
        }
        catch (const std::exception& failure)
        {
            if (!editing)
                throw;
            indexAvailable = false;
            prepared.diagnostics.push_back(failure.what());
        }
        for (const auto& [path, diagnostic] : unresolved)
            prepared.diagnostics.push_back(path + ": " + diagnostic);
        return indexAvailable;
    }
    void SceneGpuResources::ValidateEditingResources(const ScenePlacement& placement, bool indexAvailable,
                                                     const std::map<std::string, std::string>& unresolved,
                                                     bool editing) const
    {
        if (editing)
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
    void SceneGpuResources::PrepareLayoutObjects(const SceneLayout& layout,
                                                 const std::vector<DirectX::XMFLOAT4X4>& matrices,
                                                 PreparedLayout& prepared, const LayoutView& baseline,
                                                 bool preserveExecution, bool indexAvailable,
                                                 const std::map<std::string, std::string>& unresolved, bool editing,
                                                 const std::shared_ptr<const Engine::ContentContext>& context)
    {
        Engine::CpuScope timing("Scene object preparation");
        prepared.objects.reserve(layout.objects.size());
        for (const auto& placement : layout.objects)
        {
            const auto& matrix = matrices[prepared.objects.size()];
            try
            {
                ValidateEditingResources(placement, indexAvailable, unresolved, editing);
                prepared.objects.push_back(
                    BuildLayoutObject(placement, layout, matrix, prepared, baseline, preserveExecution, context));
            }
            catch (const std::exception& failure)
            {
                if (!editing)
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
    bool SceneGpuResources::PrepareLayout(SceneLayout layout, const std::filesystem::path& assetsRoot,
                                          std::string& error, bool preserveExecution, PreparedLayout& result,
                                          const PreparedLayout* previous, const LayoutView& baseline, bool editing,
                                          std::shared_ptr<const Engine::ContentContext>& context)
    {
        Engine::CpuScope timing("Scene layout preparation");
        if (!modelsReady_ && !editing)
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
            if (!context)
                context = Engine::ContentContext::Capture(assetsRoot);
            context->RequireRoot(assetsRoot);
            const bool configurationCurrent = context->CheckConfiguration(error);
            if (!configurationCurrent && !editing)
                return false;
            PreparedLayout prepared;
            if (!configurationCurrent)
                prepared.diagnostics.push_back(error);
            if (!modelsReady_)
                prepared.diagnostics.push_back("Scene renderer is unavailable; editing data was retained");
            prepared.assetsRoot = context->Root();
            std::map<std::string, std::string> unresolved;
            const bool indexAvailable =
                PrepareLayoutAssets(layout, prepared, previous, configurationCurrent, unresolved, editing, context);
            layout.ValidateChanges(validatedBeforeResolution);
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(layout, matrices, error))
                throw std::runtime_error(error);
            prepared.materials = previous ? previous->materials : decltype(prepared.materials){};
            PrepareLayoutObjects(layout, matrices, prepared, baseline, preserveExecution, indexAvailable, unresolved,
                                 editing, context);
            prepared.layout = std::move(layout);
            if (modelsReady_ && configurationCurrent &&
                !PrepareShadows(prepared.layout.settings, prepared.shadow, prepared.localShadow, error))
            {
                if (!editing)
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
    bool SceneGpuResources::PrepareShadows(const SceneSettings& settings,
                                           std::shared_ptr<Engine::ShadowMap>& directional,
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
    Engine::Object3D SceneGpuResources::BuildAddedObject(const ScenePlacement& placement, const SceneLayout& validation,
                                                         const DirectX::XMFLOAT4X4& matrix,
                                                         const std::filesystem::path& assetsRoot,
                                                         PreparedLayout& prepared,
                                                         const std::shared_ptr<const Engine::ContentContext>& context)
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
                                       .Prepare(materialDevice_.Get(), materialQueue_.Get(), assetsRoot, context));
            std::map<std::filesystem::path, std::shared_ptr<const Engine::Material>> materials;
            object.SetMaterialSlots(PrepareMaterialSlots(placement, model->MeshCount(), materialDevice_.Get(),
                                                         materialQueue_.Get(), assetsRoot, materials));
        }
        PrepareProcedural(models_, object, placement, assetsRoot, materialDevice_.Get(), materialQueue_.Get());
        ApplySurfaceOverride(object, placement);
        return object;
    }
    void SceneGpuResources::InitializeAddedAnimator(Engine::Object3D& object, const ScenePlacement& placement,
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
    Engine::Object3D SceneGpuResources::BuildStaticRuntimeObject(
        const ScenePlacement& p, PreparedLayout& candidate, const std::filesystem::path& assetsRoot,
        const std::shared_ptr<const Engine::ContentContext>& context)
    {
        Engine::Object3D object;
        if (p.meshRenderer)
        {
            const auto model = models_.Load(assetsRoot / p.Model());
            if (!model)
                throw std::runtime_error("Model could not be loaded: " + p.id);
            object.SetModel(model);
            for (const auto& lod : p.meshRenderer->lods)
            {
                auto loaded = models_.Load(assetsRoot / lod.model);
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
                                     MaterialAsset::Load(assetsRoot, material, candidate.assets.get())
                                         .Prepare(materialDevice_.Get(), materialQueue_.Get(), assetsRoot, context))
                            .first;
                object.SetMaterial(i->second);
            }
            object.SetMaterialSlots(PrepareMaterialSlots(p, model->MeshCount(), materialDevice_.Get(),
                                                         materialQueue_.Get(), assetsRoot, candidate.materials,
                                                         candidate.assets.get()));
        }
        PrepareProcedural(models_, object, p, assetsRoot, materialDevice_.Get(), materialQueue_.Get(),
                          candidate.assets.get(), &candidate.materials);
        return object;
    }
    void SceneGpuResources::DuplicateAnimator(const Engine::Object3D& source, const AnimatorState& sourceState,
                                              const std::string& id, Engine::Object3D& object,
                                              std::map<std::string, std::shared_ptr<Engine::ModelRenderer>>& animated,
                                              std::map<std::string, AnimatorState>& states)
    {
        auto model = source.GetModel()->AnimatedCopy(materialDevice_.Get(), materialQueue_.Get());
        auto state = sourceState;
        if (!model || !model->ApplyPose(state.pose))
            throw std::runtime_error("Animator duplication failed");
        object.SetModel(model);
        animated[id] = std::move(model);
        states[id] = std::move(state);
    }
    void SceneGpuResources::SwapModels(SceneGpuResources& candidate) noexcept
    {
        models_.Swap(candidate.models_);
        std::swap(modelsReady_, candidate.modelsReady_);
        meshShader_.swap(candidate.meshShader_);
    }
}
