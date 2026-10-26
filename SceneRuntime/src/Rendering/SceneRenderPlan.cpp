#include "SceneRenderPlan.h"

namespace SceneRuntime::RenderPlan
{
    bool Compatible(const Item& first, const Item& second)
    {
        return first.instancing && second.instancing && !first.occlusion && !second.occlusion &&
               !first.object.GetModel()->Rig() && first.object.GetModel() == second.object.GetModel() &&
               first.object.GetMaterial() == second.object.GetMaterial() &&
               first.object.GetMaterialSlots() == second.object.GetMaterialSlots() &&
               (DirectX::XMVectorGetX(
                    DirectX::XMMatrixDeterminant(DirectX::XMLoadFloat4x4(&first.object.GetWorldMatrix()))) < 0) ==
                   (DirectX::XMVectorGetX(
                        DirectX::XMMatrixDeterminant(DirectX::XMLoadFloat4x4(&second.object.GetWorldMatrix()))) < 0);
    }

    void Count(const Engine::Object3D& object, Engine::MaterialPass pass, size_t instances, MeshTelemetry& telemetry)
    {
        const auto& model = object.GetModel();
        if (!model)
            return;
        for (size_t mesh = 0; mesh < model->MeshCount(); ++mesh)
            if (Engine::MatchesMaterialPass(object.MaterialForMesh(mesh), pass))
            {
                ++telemetry.draws;
                telemetry.triangles += model->MeshTriangleCount(mesh) * instances;
            }
    }

    bool LocalShadows(ID3D12GraphicsCommandList* commands, Engine::DirectionalLight& lighting,
                      const std::vector<Item>& items, Engine::ShadowMap& atlas, MeshTelemetry& telemetry)
    {
        UINT next = 0;
        for (auto& light : lighting.localLights)
        {
            light.shadowFirst = -1;
            const UINT count = light.shadowCount > 0 ? (light.spot ? 1U : 6U) : 0U;
            if (!count || next + count > Engine::ShadowMap::LocalFaces)
                continue;
            bool prepared = true;
            for (UINT face = 0; face < count; ++face)
            {
                if (!atlas.BeginLocal(commands, light, next + face, face))
                {
                    prepared = false;
                    break;
                }
                for (const auto& item : items)
                    if (item.object.HasMaterialPass(Engine::MaterialPass::Opaque))
                    {
                        Count(item.object, Engine::MaterialPass::Opaque, 1, telemetry);
                        item.object.GetModel()->DrawShadow(
                            commands, item.object.GetWorldMatrix(), atlas, item.object.GetMaterial().get(),
                            item.object.GetMaterialSlots(),
                            item.skin ? std::span<const std::vector<Engine::SkinMatrix>>(item.skin->palettes)
                                      : std::span<const std::vector<Engine::SkinMatrix>>{});
                    }
                atlas.End(commands);
            }
            if (prepared)
            {
                light.shadowFirst = static_cast<float>(next);
                next += count;
            }
        }
        return next > 0;
    }

    void GatherInstances(const std::vector<Item>& items, size_t index, std::vector<bool>& drawn,
                         std::vector<DirectX::XMFLOAT4X4>& instances)
    {
        const auto& item = items[index];
        if (item.instancing && !item.occlusion && !item.object.GetModel()->Rig())
        {
            for (size_t other = index + 1;
                 other < items.size() && instances.size() < Engine::SkinPaletteBuffer::Capacity; ++other)
                if (!drawn[other] && Compatible(item, items[other]))
                {
                    if (instances.empty())
                        instances.push_back(item.object.GetWorldMatrix());
                    drawn[other] = true;
                    instances.push_back(items[other].object.GetWorldMatrix());
                }
        }
    }

    void Opaque(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
                const Engine::DirectionalLight& light, const std::vector<Item>& items, MeshTelemetry& telemetry,
                Engine::OcclusionRenderer& occlusion)
    {
        std::vector<bool> drawn(items.size());
        for (size_t index = 0; index < items.size(); ++index)
        {
            const auto& item = items[index];
            if (drawn[index] || !item.object.HasMaterialPass(Engine::MaterialPass::Opaque))
                continue;
            std::vector<DirectX::XMFLOAT4X4> instances;
            drawn[index] = true;
            GatherInstances(items, index, drawn, instances);
            Count(item.object, Engine::MaterialPass::Opaque, instances.empty() ? 1 : instances.size(), telemetry);
            if (item.occlusion)
            {
                auto bounds = item.object.GetModel()->Bounds();
                if (item.skin)
                    bounds = {{item.skin->bounds.center[0], item.skin->bounds.center[1], item.skin->bounds.center[2]},
                              {item.skin->bounds.extent[0], item.skin->bounds.extent[1], item.skin->bounds.extent[2]}};
                occlusion.Begin(commands, bounds, item.object.GetWorldMatrix(), camera.GetViewProjectionMatrix());
            }
            item.object.GetModel()->Draw(commands, item.object.GetWorldMatrix(), camera.GetViewProjectionMatrix(),
                                         light, camera.GetPosition(), {}, item.object.GetMaterial().get(),
                                         {item.object.GetMaterialSlots(), Engine::MaterialPass::Opaque,
                                          instances.size() > 1 ? std::span<const DirectX::XMFLOAT4X4>(instances)
                                                               : std::span<const DirectX::XMFLOAT4X4>{},
                                          item.skin
                                              ? std::span<const std::vector<Engine::SkinMatrix>>(item.skin->palettes)
                                              : std::span<const std::vector<Engine::SkinMatrix>>{}});
            if (item.occlusion)
                Engine::OcclusionRenderer::End(commands);
        }
    }
}
