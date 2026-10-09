#pragma once
#include <SceneRuntime/SceneWorld.h>
#include <Engine/Graphics/Camera.h>
#include <algorithm>
#include <optional>
#include <cmath>

namespace SceneRuntime
{
    // Editor representation: 100 UI pixels per world unit, centered on the root Canvas.
    struct SceneCanvas
    {
        static const ScenePlacement* Root(const SceneLayout& layout,const ScenePlacement& object)
        {
            const ScenePlacement* root=nullptr;
            const auto* node=&object;
            for(size_t depth=0;node && depth<=layout.objects.size();++depth) {
                if(node->canvas) root=node;
                const auto parent=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& p){return p.id==node->parentId;});
                node=parent==layout.objects.end()?nullptr:&*parent;
            }
            return node?nullptr:root;
        }
        static std::optional<DirectX::XMFLOAT4X4> Matrix(const SceneWorld& world,const ScenePlacement& object)
        {
            const auto* root=Root(world.Layout(),object);
            DirectX::XMFLOAT4X4 transform{};
            if(!root || !world.WorldMatrix(root->id,transform)) return std::nullopt;
            using namespace DirectX;
            XMStoreFloat4x4(&transform,XMMatrixScaling(.01f,-.01f,1)*
                XMMatrixTranslation(-root->canvas->referenceSize[0]*.005f,root->canvas->referenceSize[1]*.005f,0)*XMLoadFloat4x4(&transform));
            return transform;
        }
        static std::optional<std::array<float,2>> Intersect(const DirectX::XMFLOAT4X4& matrix,
            const Engine::Camera& camera,float ndcX,float ndcY)
        {
            using namespace DirectX;
            XMVECTOR determinant;
            const auto inverse=XMMatrixInverse(&determinant,XMLoadFloat4x4(&matrix));
            if(std::abs(XMVectorGetX(determinant))<1e-12f) return std::nullopt;
            const auto unproject=XMMatrixInverse(nullptr,camera.GetViewMatrix()*camera.GetProjectionMatrix())*inverse;
            XMFLOAT3 nearPoint,farPoint;
            XMStoreFloat3(&nearPoint,XMVector3TransformCoord(XMVectorSet(ndcX,ndcY,0,1),unproject));
            XMStoreFloat3(&farPoint,XMVector3TransformCoord(XMVectorSet(ndcX,ndcY,1,1),unproject));
            const float dz=farPoint.z-nearPoint.z;
            if(std::abs(dz)<1e-7f) return std::nullopt;
            const float t=-nearPoint.z/dz;
            if(!std::isfinite(t) || t<0 || t>1) return std::nullopt;
            return std::array<float,2>{std::lerp(nearPoint.x,farPoint.x,t),std::lerp(nearPoint.y,farPoint.y,t)};
        }
    };
}
