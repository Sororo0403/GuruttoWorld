#include <Engine/Graphics/Camera.h>
#include <algorithm>
#include <cmath>

namespace Engine
{
    Camera::Camera()
    {
        SetPerspective(fov_, aspect_, near_, far_);
    }
    bool Camera::SetPosition(const std::array<float, 3>& position)
    {
        for (float value : position) if (!std::isfinite(value)) return false;
        position_ = position;
        return true;
    }
    bool Camera::SetRotation(float yaw, float pitch)
    {
        if (!std::isfinite(yaw) || !std::isfinite(pitch)) return false;
        yaw_ = std::remainder(yaw, DirectX::XM_2PI);
        pitch_ = std::clamp(pitch, -DirectX::XM_PIDIV2 + 0.01f, DirectX::XM_PIDIV2 - 0.01f);
        return true;
    }
    bool Camera::SetPerspective(float verticalFov, float aspectRatio, float nearClip, float farClip)
    {
        if (!std::isfinite(verticalFov) || !std::isfinite(aspectRatio) || !std::isfinite(nearClip) || !std::isfinite(farClip) ||
            verticalFov <= 0.001f || verticalFov >= DirectX::XM_PI - 0.001f || aspectRatio <= 0.0f || nearClip <= 0.0f || farClip <= nearClip) return false;
        const auto matrix = DirectX::XMMatrixPerspectiveFovLH(verticalFov, aspectRatio, nearClip, farClip);
        if (DirectX::XMMatrixIsNaN(matrix) || DirectX::XMMatrixIsInfinite(matrix)) return false;
        fov_ = verticalFov;
        aspect_ = aspectRatio;
        near_ = nearClip;
        far_ = farClip;
        DirectX::XMStoreFloat4x4(&projection_, matrix);
        return true;
    }
    bool Camera::SetAspectRatio(float aspectRatio)
    {
        return SetPerspective(fov_, aspectRatio, near_, far_);
    }
    const std::array<float, 3>& Camera::GetPosition() const { return position_; }
    DirectX::XMMATRIX Camera::GetViewMatrix() const
    {
        using namespace DirectX;
        const float horizontal = std::cos(pitch_);
        return XMMatrixLookToLH(XMVectorSet(position_[0], position_[1], position_[2], 1.0f),
            XMVectorSet(std::sin(yaw_) * horizontal, std::sin(pitch_), std::cos(yaw_) * horizontal, 0.0f), XMVectorSet(0, 1, 0, 0));
    }
    DirectX::XMMATRIX Camera::GetProjectionMatrix() const { return DirectX::XMLoadFloat4x4(&projection_); }
    DirectX::XMFLOAT4X4 Camera::GetViewProjectionMatrix() const
    {
        DirectX::XMFLOAT4X4 result;
        DirectX::XMStoreFloat4x4(&result, GetViewMatrix() * GetProjectionMatrix());
        return result;
    }
}
