#include <SceneRuntime/Ui/SceneCanvas.h>

namespace SceneRuntime
{
    auto SceneCanvas::Root(const SceneLayout& layout, const ScenePlacement& object) -> const ScenePlacement*
    {
        const ScenePlacement* root = nullptr;
        const auto* node = &object;
        for (size_t depth = 0; node && depth <= layout.objects.size(); ++depth)
        {
            if (node->canvas)
                root = node;
            const auto parent = std::find_if(layout.objects.begin(), layout.objects.end(),
                                             [&](const auto& p)
                                             {
                                                 return p.id == node->parentId;
                                             });
            node = parent == layout.objects.end() ? nullptr : &*parent;
        }
        return node ? nullptr : root;
    }

    auto SceneCanvas::Matrix(const SceneWorld& world, const ScenePlacement& object)
        -> std::optional<DirectX::XMFLOAT4X4>
    {
        const auto* root = Root(world.Layout(), object);
        DirectX::XMFLOAT4X4 transform{};
        if (!root || !world.WorldMatrix(root->id, transform))
            return std::nullopt;
        using namespace DirectX;
        XMStoreFloat4x4(&transform, XMMatrixScaling(.01f, -.01f, 1) *
                                        XMMatrixTranslation(-root->canvas->referenceSize[0] * .005f,
                                                            root->canvas->referenceSize[1] * .005f, 0) *
                                        XMLoadFloat4x4(&transform));
        return transform;
    }

    auto SceneCanvas::Intersect(const DirectX::XMFLOAT4X4& matrix, const Engine::Camera& camera, float ndcX, float ndcY)
        -> std::optional<std::array<float, 2>>
    {
        using namespace DirectX;
        XMVECTOR determinant;
        const auto inverse = XMMatrixInverse(&determinant, XMLoadFloat4x4(&matrix));
        if (std::abs(XMVectorGetX(determinant)) < 1e-12f)
            return std::nullopt;
        const auto unproject =
            XMMatrixInverse(nullptr, camera.GetViewMatrix() * camera.GetProjectionMatrix()) * inverse;
        XMFLOAT3 nearPoint, farPoint;
        XMStoreFloat3(&nearPoint, XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0, 1), unproject));
        XMStoreFloat3(&farPoint, XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1, 1), unproject));
        const float dz = farPoint.z - nearPoint.z;
        if (std::abs(dz) < 1e-7f)
            return std::nullopt;
        const float t = -nearPoint.z / dz;
        if (!Engine::InRange(t, 0, 1))
            return std::nullopt;
        return std::array<float, 2>{std::lerp(nearPoint.x, farPoint.x, t), std::lerp(nearPoint.y, farPoint.y, t)};
    }
}
