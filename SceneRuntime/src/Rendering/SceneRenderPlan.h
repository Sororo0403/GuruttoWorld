#pragma once
#include <SceneRuntime/Scene/SceneWorld.h>
#include <SceneRuntime/Ui/SceneUi.h>
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
    };
    /// <summary>Visible の処理を実行します。</summary>
    bool Visible(const ScenePlacement& placement, const UiState& state);
    /// <summary>カメラ距離でLODを選び、保存した個体からそのフレームの描画項目を作ります。</summary>
    std::vector<Item> Items(
        const SceneLayout& layout, const std::vector<Engine::Object3D>& objects,
        const std::map<std::string, std::vector<std::shared_ptr<const Engine::ModelRenderer>>>& levels,
        const Engine::Camera& camera, const UiState& state, std::span<const DirectX::XMFLOAT4X4> renderMatrices = {});
    /// <summary>同じモデル・マテリアル・巻き順を持つ静的個体だけをまとめます。</summary>
    bool Compatible(const Item& first, const Item& second);
    /// <summary>実際の描画呼び出し数と全個体の三角形数を集計します。</summary>
    void Count(const Engine::Object3D& object, Engine::MaterialPass pass, size_t instances, MeshTelemetry& telemetry);
    /// <summary>点光源の六面とスポット円錐を32面の局所影アトラスへ描画します。</summary>
    bool LocalShadows(ID3D12GraphicsCommandList* commands, Engine::DirectionalLight& lighting,
                      const std::vector<Item>& items, Engine::ShadowMap& atlas, MeshTelemetry& telemetry);
    /// <summary>GatherInstances の処理を実行します。</summary>
    void GatherInstances(const std::vector<Item>& items, size_t index, std::vector<bool>& drawn,
                         std::vector<DirectX::XMFLOAT4X4>& instances);
    /// <summary>不透明個体の互換グループをGPUインスタンシングで描画します。</summary>
    void Opaque(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
                const Engine::DirectionalLight& light, const std::vector<Item>& items, MeshTelemetry& telemetry,
                Engine::OcclusionRenderer& occlusion);
}
