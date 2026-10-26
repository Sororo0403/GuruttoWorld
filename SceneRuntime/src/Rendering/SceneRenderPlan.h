#pragma once
#include <SceneRuntime/Rendering/RenderPacket.h>
#include <SceneRuntime/Rendering/RenderTelemetry.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <Engine/Graphics/Camera.h>
#include <Engine/Graphics/Renderers/ModelRenderer.h>
#include <Engine/Graphics/Renderers/OcclusionRenderer.h>
#include <SceneRuntime/Ui/UiState.h>
#include <cmath>
#include <cstring>
#include <span>

namespace SceneRuntime::RenderPlan
{
    struct Item
    {
        Engine::Object3D object;
        bool instancing = false;
        float distance = 0;
        bool occlusion = false;
        std::shared_ptr<const RenderSkin> skin;
    };
    /// <summary>同じモデル・マテリアル・巻き順を持つ静的個体だけをまとめます。</summary>
    bool Compatible(const Item& first, const Item& second);
    /// <summary>実際の描画呼び出し数と全個体の三角形数を集計します。</summary>
    void Count(const Engine::Object3D& object, Engine::MaterialPass pass, size_t instances, MeshTelemetry& telemetry);
    /// <summary>点光源の六面とスポット円錐を32面の局所影アトラスへ描画します。</summary>
    bool LocalShadows(ID3D12GraphicsCommandList* commands, Engine::DirectionalLight& lighting,
                      const std::vector<Item>& items, Engine::ShadowMap& atlas, MeshTelemetry& telemetry);
    /// <summary>描画対象のインスタンスを収集します。</summary>
    void GatherInstances(const std::vector<Item>& items, size_t index, std::vector<bool>& drawn,
                         std::vector<DirectX::XMFLOAT4X4>& instances);
    /// <summary>不透明個体の互換グループをGPUインスタンシングで描画します。</summary>
    void Opaque(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
                const Engine::DirectionalLight& light, const std::vector<Item>& items, MeshTelemetry& telemetry,
                Engine::OcclusionRenderer& occlusion);
}
