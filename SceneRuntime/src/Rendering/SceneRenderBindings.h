#pragma once
#include "SceneRenderResources.h"
#include <SceneRuntime/Rendering/SceneResourcePreparation.h>
#include <SceneRuntime/Scene/WorldState.h>
#include <algorithm>
#include <stdexcept>

namespace SceneRuntime
{
    struct SceneRenderBindingContext
    {
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        std::uint64_t generation = 0;
        std::shared_ptr<const RenderPacket> previous;
        std::shared_ptr<Engine::ShadowMap> shadow, localShadow;
        bool reuseBase = false;
    };
    class SceneRenderBindings final : public RenderResourceResolver
    {
        WorldState::ReadView view_;
        SceneResourceView prepared_;
        const std::vector<SceneAnimatorFrame>& poses_;
        std::shared_ptr<Engine::ShadowMap> shadow_, localShadow_;
        std::shared_ptr<const GpuRenderScene> scene_;
        std::vector<size_t> indices_;
        std::vector<const Engine::ModelRenderer::PreparedPose*> skins_;
        std::vector<const RenderObject*> previous_;
        SceneRenderBindingContext context_;

    public:
        /// <summary>明示的な資源世代と候補姿勢だけを参照し、Worldの可変状態を保持しません。</summary>
        SceneRenderBindings(WorldState::ReadView values, SceneResourceView resources,
                            const std::vector<SceneAnimatorFrame>& frames, SceneRenderBindingContext context)
            : view_(values), prepared_(resources), poses_(frames), shadow_(context.shadow),
              localShadow_(context.localShadow), context_(std::move(context))
        {
            Initialize();
        }
        /// <summary>個体と資源の対応、および共有できる前回の世代を準備します。</summary>
        void Initialize()
        {
            const auto entities = view_.Entities();
            size_t slots = 0;
            for (const auto entity : entities)
                slots = std::max(slots, static_cast<size_t>(entity.slot) + 1);
            indices_.assign(slots, static_cast<size_t>(-1));
            skins_.resize(slots);
            previous_.resize(slots);
            if (entities.size() != prepared_.layout.objects.size())
                throw std::runtime_error("Render definition and runtime counts differ");
            for (size_t index = 0; index < entities.size(); ++index)
                indices_[entities[index].slot] = index;
            for (const auto& pose : poses_)
                skins_[view_.Resolve(pose.id)->slot] = &pose.pose;
            const auto& previousPacket = context_.previous;
            if (previousPacket)
            {
                for (const auto& object : previousPacket->objects)
                    if (object.entity.slot < slots)
                        previous_[object.entity.slot] = &object;
                const auto oldScene = std::dynamic_pointer_cast<const GpuRenderScene>(previousPacket->sceneResources);
                if (oldScene && oldScene->shadow == shadow_ && oldScene->localShadow == localShadow_)
                    scene_ = oldScene;
            }
            if (!scene_)
            {
                auto candidate = std::make_shared<GpuRenderScene>();
                candidate->shadow = shadow_;
                candidate->localShadow = localShadow_;
                scene_ = std::move(candidate);
            }
        }
        /// <summary>影資源の不変世代を返します。</summary>
        std::shared_ptr<const RenderResource> Scene() const override
        {
            return scene_;
        }
        /// <summary>個体に対応するモデル・材質・骨格の描画資源を返します。</summary>
        std::optional<RenderBinding> Resolve(EntityHandle entity, std::string& error) override
        {
            if (entity.slot >= indices_.size() || indices_[entity.slot] == static_cast<size_t>(-1))
            {
                error = "Render object definition missing";
                return {};
            }
            const auto index = indices_[entity.slot];
            const auto& id = prepared_.layout.objects[index].id;
            const auto* old = previous_[entity.slot];
            const auto* previousResource =
                old ? dynamic_cast<const GpuRenderObject*>(old->binding.resource.get()) : nullptr;
            const bool sameOwner = old && old->entity == entity && previousResource &&
                                   previousResource->device.Get() == context_.device.Get();
            const auto& base = context_.reuseBase && sameOwner ? previousResource->object : prepared_.objects.at(index);
            if (!base.GetModel())
            {
                error = "Render model missing";
                return {};
            }
            std::optional<Engine::Object3D> modified;
            const auto surface = view_.ComponentLease<MaterialComponent>(entity);
            if (surface && surface->enabled && surface->surfaceOverride)
            {
                const auto matches = [&](const auto& material)
                {
                    return material && material->color == surface->color && material->roughness == surface->roughness &&
                           material->metallic == surface->metallic &&
                           material->shaderParameters == surface->shaderParameters;
                };
                const bool changed = !matches(base.GetMaterial()) ||
                                     std::any_of(base.GetMaterialSlots().begin(), base.GetMaterialSlots().end(),
                                                 [&](const auto& slot)
                                                 {
                                                     return slot && !matches(slot);
                                                 });
                if (changed)
                {
                    modified.emplace(base);
                    const auto apply = [&](const std::shared_ptr<const Engine::Material>& original)
                        -> std::shared_ptr<const Engine::Material>
                    {
                        if (matches(original))
                            return original;
                        auto material = original ? std::make_shared<Engine::Material>(*original)
                                                 : std::make_shared<Engine::Material>();
                        material->color = surface->color;
                        material->roughness = surface->roughness;
                        material->metallic = surface->metallic;
                        material->shaderParameters = surface->shaderParameters;
                        return material;
                    };
                    modified->SetMaterial(apply(base.GetMaterial()));
                    auto slots = base.GetMaterialSlots();
                    for (auto& slot : slots)
                        if (slot)
                            slot = apply(slot);
                    modified->SetMaterialSlots(std::move(slots));
                }
            }
            const auto& object = modified ? *modified : base;
            const auto levels = prepared_.lods.find(id);
            RenderBinding binding;
            if (old && old->entity == entity && previousResource &&
                previousResource->device.Get() == context_.device.Get() &&
                previousResource->object.GetModel() == object.GetModel() &&
                previousResource->object.GetMaterial() == object.GetMaterial() &&
                previousResource->object.GetMaterialSlots() == object.GetMaterialSlots() &&
                (levels == prepared_.lods.end() ? previousResource->levels.empty()
                                                : previousResource->levels == levels->second))
            {
                binding = old->binding;
                binding.skin.reset();
            }
            else
            {
                auto resource = std::make_shared<GpuRenderObject>();
                resource->object = object;
                resource->device = context_.device;
                if (levels != prepared_.lods.end())
                    resource->levels = levels->second;
                binding.device = reinterpret_cast<std::uintptr_t>(context_.device.Get());
                binding.generation = context_.generation;
                const auto bounds = object.GetModel()->Bounds();
                binding.bounds = {{bounds.Center.x, bounds.Center.y, bounds.Center.z},
                                  {bounds.Extents.x, bounds.Extents.y, bounds.Extents.z}};
                binding.resource = std::move(resource);
            }
            if (const auto* pose = skins_[entity.slot])
            {
                auto skin = std::make_shared<RenderSkin>();
                skin->palettes = pose->Palettes();
                const auto bounds = pose->Bounds();
                skin->bounds = {{bounds.Center.x, bounds.Center.y, bounds.Center.z},
                                {bounds.Extents.x, bounds.Extents.y, bounds.Extents.z}};
                binding.skin = std::move(skin);
            }
            return binding;
        }
    };
}
