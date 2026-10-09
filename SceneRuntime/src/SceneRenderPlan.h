#pragma once
#include <SceneRuntime/SceneWorld.h>
#include <SceneRuntime/SceneUi.h>
#include <cmath>

namespace SceneRuntime::RenderPlan
{
    struct Item
    {
        Engine::Object3D object;
        bool instancing=false;
        float distance=0;
        bool occlusion=false;
    };
    /// <summary>カメラ距離でLODを選び、保存した個体からそのフレームの描画項目を作ります。</summary>
    inline std::vector<Item> Items(const SceneLayout& layout,const std::vector<Engine::Object3D>& objects,
        const std::map<std::string,std::vector<std::shared_ptr<const Engine::ModelRenderer>>>& levels,
        const Engine::Camera& camera,const UiState& state)
    {
        std::vector<Item> result;
        const auto& eye=camera.GetPosition();
        for (size_t index=0;index<objects.size();++index)
        {
            const auto& placement=layout.objects[index];
            if (!objects[index].GetModel()) continue;
            const bool visible=placement.meshRenderer ? placement.meshRenderer->enabled && state.Matches(placement.meshRenderer->visibleWhen) :
                (placement.terrain && placement.terrain->enabled) || (placement.tilemap && placement.tilemap->enabled);
            if (!visible) continue;
            const auto& world=objects[index].GetWorldMatrix();
            Item item{objects[index],placement.meshRenderer && placement.meshRenderer->instancing,
                std::hypot(world._41-eye[0],world._42-eye[1],world._43-eye[2])};
            item.occlusion=placement.meshRenderer && placement.meshRenderer->occlusionCulling;
            const auto found=levels.find(placement.id);
            if (placement.meshRenderer && found!=levels.end())
                for (size_t level=0;level<placement.meshRenderer->lods.size();++level)
                    if (item.distance>=placement.meshRenderer->lods[level].distance) item.object.SetModel(found->second.at(level));
            result.push_back(std::move(item));
        }
        return result;
    }
    /// <summary>同じモデル・マテリアル・巻き順を持つ静的個体だけをまとめます。</summary>
    inline bool Compatible(const Item& first,const Item& second)
    {
        return first.instancing && second.instancing && !first.occlusion && !second.occlusion && !first.object.GetModel()->Rig() &&
            first.object.GetModel()==second.object.GetModel() && first.object.GetMaterial()==second.object.GetMaterial() &&
            first.object.GetMaterialSlots()==second.object.GetMaterialSlots() &&
            (DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(DirectX::XMLoadFloat4x4(&first.object.GetWorldMatrix())))<0)==
            (DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(DirectX::XMLoadFloat4x4(&second.object.GetWorldMatrix())))<0);
    }
    /// <summary>実際の描画呼び出し数と全個体の三角形数を集計します。</summary>
    inline void Count(const Engine::Object3D& object,Engine::MaterialPass pass,size_t instances,MeshTelemetry& telemetry)
    {
        const auto& model=object.GetModel();
        if (!model) return;
        for (size_t mesh=0;mesh<model->MeshCount();++mesh)
            if (Engine::MatchesMaterialPass(object.MaterialForMesh(mesh),pass))
            { ++telemetry.draws; telemetry.triangles+=model->MeshTriangleCount(mesh)*instances; }
    }
    /// <summary>点光源の六面とスポット円錐を32面の局所影アトラスへ描画します。</summary>
    inline bool LocalShadows(ID3D12GraphicsCommandList* commands,Engine::DirectionalLight& lighting,
        const std::vector<Item>& items,Engine::ShadowMap& atlas,MeshTelemetry& telemetry)
    {
        UINT next=0;
        for (auto& light : lighting.localLights)
        {
            light.shadowFirst=-1;
            const UINT count=light.shadowCount>0 ? (light.spot ? 1U : 6U) : 0U;
            if (!count || next+count>Engine::ShadowMap::LocalFaces) continue;
            bool prepared=true;
            for (UINT face=0;face<count;++face)
            {
                if (!atlas.BeginLocal(commands,light,next+face,face)) { prepared=false; break; }
                for (const auto& item : items)
                    if (item.object.HasMaterialPass(Engine::MaterialPass::Opaque))
                    {
                        Count(item.object,Engine::MaterialPass::Opaque,1,telemetry);
                        item.object.GetModel()->DrawShadow(commands,item.object.GetWorldMatrix(),atlas,item.object.GetMaterial().get(),item.object.GetMaterialSlots());
                    }
                atlas.End(commands);
            }
            if (prepared) { light.shadowFirst=static_cast<float>(next); next+=count; }
        }
        return next>0;
    }
    /// <summary>不透明個体の互換グループをGPUインスタンシングで描画します。</summary>
    inline void Opaque(ID3D12GraphicsCommandList* commands,const Engine::Camera& camera,const Engine::DirectionalLight& light,
        const std::vector<Item>& items,MeshTelemetry& telemetry,Engine::OcclusionRenderer& occlusion)
    {
        std::vector<bool> drawn(items.size());
        for (size_t index=0;index<items.size();++index)
        {
            const auto& item=items[index];
            if (drawn[index] || !item.object.HasMaterialPass(Engine::MaterialPass::Opaque)) continue;
            std::vector<DirectX::XMFLOAT4X4> instances{item.object.GetWorldMatrix()};
            drawn[index]=true;
            for (size_t other=index+1;other<items.size() && instances.size()<Engine::SkinPaletteBuffer::Capacity;++other)
                if (!drawn[other] && Compatible(item,items[other]))
                { drawn[other]=true; instances.push_back(items[other].object.GetWorldMatrix()); }
            Count(item.object,Engine::MaterialPass::Opaque,instances.size(),telemetry);
            if (item.occlusion) occlusion.Begin(commands,item.object.GetModel()->Bounds(),item.object.GetWorldMatrix(),camera.GetViewProjectionMatrix());
            item.object.GetModel()->Draw(commands,item.object.GetWorldMatrix(),camera.GetViewProjectionMatrix(),light,camera.GetPosition(),{},
                item.object.GetMaterial().get(),item.object.GetMaterialSlots(),Engine::MaterialPass::Opaque,
                instances.size()>1 ? std::span<const DirectX::XMFLOAT4X4>(instances) : std::span<const DirectX::XMFLOAT4X4>{});
            if (item.occlusion) Engine::OcclusionRenderer::End(commands);
        }
    }
}
