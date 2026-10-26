#include <SceneRuntime/Rendering/RenderScene.h>
#include "SceneRenderResources.h"
#include "SceneRenderPlan.h"
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include <Engine/Graphics/Resources/GpuProfiler.h>
#include <Engine/Core/Profiler.h>
#include <algorithm>
#include <cmath>

namespace SceneRuntime
{
    bool RenderScene::Draw(const std::shared_ptr<const RenderPacket>& packet, ID3D12GraphicsCommandList* commands,
                           const Engine::Camera& camera, const Engine::DirectionalLight& light, const UiState& state,
                           Engine::OcclusionRenderer& occlusion, MeshTelemetry& telemetry)
    {
        telemetry = {};
        if (!packet || !commands)
            return false;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(commands->GetDevice(IID_PPV_ARGS(&device))) ||
            packet->device != reinterpret_cast<std::uintptr_t>(device.Get()))
            return false;
        const auto* scene = dynamic_cast<const GpuRenderScene*>(packet->sceneResources.get());
        if (!scene || !scene->shadow || !scene->localShadow)
            return false;
        std::vector<RenderPlan::Item> items;
        {
            Engine::CpuScope scope("Packet render item preparation");
            items.reserve(packet->objects.size());
            const auto& eye = camera.GetPosition();
            for (const auto& object : packet->objects)
            {
                if (!state.Matches(object.visibleWhen))
                    continue;
                const auto* binding = dynamic_cast<const GpuRenderObject*>(object.binding.resource.get());
                if (!binding || binding->device.Get() != device.Get() || !binding->object.GetModel())
                    return false;
                RenderPlan::Item item;
                item.object = binding->object;
                if (!item.object.SetWorldMatrix(object.matrix))
                    return false;
                item.instancing = object.instancing;
                item.occlusion = object.occlusion;
                item.skin = object.binding.skin;
                item.distance =
                    std::hypot(object.matrix._41 - eye[0], object.matrix._42 - eye[1], object.matrix._43 - eye[2]);
                if (binding->levels.size() > object.lodDistances.size())
                    return false;
                for (size_t level = 0; level < binding->levels.size(); ++level)
                    if (item.distance >= object.lodDistances[level])
                        item.object.SetModel(binding->levels[level]);
                if (item.skin && item.skin->palettes.size() != item.object.GetModel()->MeshCount())
                    return false;
                items.push_back(std::move(item));
            }
        }
        if (!Engine::RenderFrameContext::Retain(commands, packet))
            return false;
        auto lighting = light;
        lighting.shadow = nullptr;
        lighting.localShadow = nullptr;
        if (light.enabled && light.shadowsEnabled && scene->shadow->Begin(commands, camera, light))
        {
            Engine::GpuScope gpu(commands, "Shadows");
            Engine::CpuScope cpu("Shadows");
            for (const auto& item : items)
                if (item.object.HasMaterialPass(Engine::MaterialPass::Opaque))
                {
                    RenderPlan::Count(item.object, Engine::MaterialPass::Opaque, 1, telemetry);
                    item.object.GetModel()->DrawShadow(
                        commands, item.object.GetWorldMatrix(), *scene->shadow, item.object.GetMaterial().get(),
                        item.object.GetMaterialSlots(),
                        item.skin ? std::span<const std::vector<Engine::SkinMatrix>>(item.skin->palettes)
                                  : std::span<const std::vector<Engine::SkinMatrix>>{});
                }
            scene->shadow->End(commands);
            lighting.shadow = scene->shadow.get();
        }
        if (RenderPlan::LocalShadows(commands, lighting, items, *scene->localShadow, telemetry))
            lighting.localShadow = scene->localShadow.get();
        {
            Engine::GpuScope gpu(commands, "Opaque meshes");
            Engine::CpuScope cpu("Opaque meshes");
            std::stable_sort(items.begin(), items.end(),
                             [](const auto& first, const auto& second)
                             {
                                 return first.distance < second.distance;
                             });
            RenderPlan::Opaque(commands, camera, lighting, items, telemetry, occlusion);
        }
        {
            Engine::GpuScope gpu(commands, "Transparent meshes");
            Engine::CpuScope cpu("Transparent meshes");
            std::stable_sort(items.begin(), items.end(),
                             [](const auto& first, const auto& second)
                             {
                                 return first.distance > second.distance;
                             });
            for (const auto& item : items)
                if (item.object.HasMaterialPass(Engine::MaterialPass::Transparent))
                {
                    RenderPlan::Count(item.object, Engine::MaterialPass::Transparent, 1, telemetry);
                    item.object.GetModel()->Draw(
                        commands, item.object.GetWorldMatrix(), camera.GetViewProjectionMatrix(), lighting,
                        camera.GetPosition(), {}, item.object.GetMaterial().get(),
                        {item.object.GetMaterialSlots(),
                         Engine::MaterialPass::Transparent,
                         {},
                         item.skin ? std::span<const std::vector<Engine::SkinMatrix>>(item.skin->palettes)
                                   : std::span<const std::vector<Engine::SkinMatrix>>{}});
                }
        }
        return true;
    }
}
