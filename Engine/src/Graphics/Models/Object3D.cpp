#include <Engine/Graphics/Models/Object3D.h>
#include <Engine/Graphics/Renderers/ModelRenderer.h>
#include <Engine/Graphics/Camera.h>
#include <cmath>
#include <utility>

namespace Engine
{
    Object3D::Object3D()
    {
        DirectX::XMStoreFloat4x4(&world_, DirectX::XMMatrixIdentity());
    }

    void Object3D::SetModel(std::shared_ptr<const ModelRenderer> model)
    {
        model_ = std::move(model);
    }

    const std::shared_ptr<const ModelRenderer>& Object3D::GetModel() const
    {
        return model_;
    }

    bool Object3D::SetTransform(const std::array<float, 3>& position, const std::array<float, 3>& rotation,
        const std::array<float, 3>& scale)
    {
        for (size_t axis = 0; axis < position.size(); ++axis)
        {
            if (!std::isfinite(position[axis]) || !std::isfinite(rotation[axis]) ||
                !std::isfinite(scale[axis]) || scale[axis] == 0.0f) return false;
        }
        using namespace DirectX;
        const XMMATRIX world = XMMatrixScaling(scale[0], scale[1], scale[2]) *
            XMMatrixRotationX(rotation[0]) * XMMatrixRotationY(rotation[1]) * XMMatrixRotationZ(rotation[2]) *
            XMMatrixTranslation(position[0], position[1], position[2]);
        XMVECTOR determinant;
        const XMMATRIX inverse = XMMatrixInverse(&determinant, world);
        if (!std::isfinite(XMVectorGetX(determinant)) || XMVectorGetX(determinant) == 0.0f) return false;
        if (XMMatrixIsNaN(world) || XMMatrixIsInfinite(world) ||
            XMMatrixIsNaN(inverse) || XMMatrixIsInfinite(inverse)) return false;
        position_ = position;
        rotation_ = rotation;
        scale_ = scale;
        XMStoreFloat4x4(&world_, world);
        return true;
    }

    bool Object3D::SetWorldMatrix(const DirectX::XMFLOAT4X4& matrix)
    {
        using namespace DirectX;
        const auto world=XMLoadFloat4x4(&matrix);
        if (XMMatrixIsNaN(world) || XMMatrixIsInfinite(world) ||
            std::abs(matrix._14)>1e-6f || std::abs(matrix._24)>1e-6f ||
            std::abs(matrix._34)>1e-6f || std::abs(matrix._44-1)>1e-6f) return false;
        XMVECTOR determinant;
        const auto inverse=XMMatrixInverse(&determinant,world);
        const auto value=XMVectorGetX(determinant);
        if (!std::isfinite(value) || value==0 || XMMatrixIsNaN(inverse) || XMMatrixIsInfinite(inverse)) return false;
        world_=matrix;
        return true;
    }

    const std::array<float, 3>& Object3D::GetPosition() const
    {
        return position_;
    }

    const std::array<float, 3>& Object3D::GetRotation() const
    {
        return rotation_;
    }

    const std::array<float, 3>& Object3D::GetScale() const
    {
        return scale_;
    }

    const DirectX::XMFLOAT4X4& Object3D::GetWorldMatrix() const
    {
        return world_;
    }

    void Object3D::Draw(ID3D12GraphicsCommandList* commands, const Camera& camera,
        const DirectionalLight& light, const UvTransform& uvTransform) const
    {
        if (model_) model_->Draw(commands, world_, camera.GetViewProjectionMatrix(), light, camera.GetPosition(), material_ ? material_->uv : uvTransform,material_.get());
    }
}
