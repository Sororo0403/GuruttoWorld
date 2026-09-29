#include <Engine/DevTools/DebugCamera.h>
#include <algorithm>
#include <cmath>

namespace Engine
{
    void DebugCamera::Reset() noexcept
    {
        position_ = { 0.0f, 0.0f, -3.5f };
        yaw_ = 0.0f;
        pitch_ = 0.0f;
    }

    void DebugCamera::Rotate(float deltaX, float deltaY) noexcept
    {
        if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) return;
        constexpr float Sensitivity = 0.003f;
        yaw_ = std::remainder(yaw_ + deltaX * Sensitivity, DirectX::XM_2PI);
        pitch_ = std::clamp(pitch_ - deltaY * Sensitivity, -DirectX::XM_PIDIV2 + 0.01f, DirectX::XM_PIDIV2 - 0.01f);
    }

    DirectX::XMVECTOR DebugCamera::GetForward() const noexcept
    {
        const float horizontal = std::cos(pitch_);
        return DirectX::XMVectorSet(std::sin(yaw_) * horizontal, std::sin(pitch_), std::cos(yaw_) * horizontal, 0.0f);
    }

    void DebugCamera::Move(float right, float up, float forward, double deltaSeconds, bool fast) noexcept
    {
        if (!std::isfinite(right) || !std::isfinite(up) || !std::isfinite(forward) ||
            !std::isfinite(deltaSeconds) || deltaSeconds < 0.0) return;
        using namespace DirectX;
        const XMVECTOR side = XMVectorSet(std::cos(yaw_), 0.0f, -std::sin(yaw_), 0.0f);
        XMVECTOR direction = side * std::clamp(right, -1.0f, 1.0f) +
            XMVectorSet(0.0f, std::clamp(up, -1.0f, 1.0f), 0.0f, 0.0f) + GetForward() * std::clamp(forward, -1.0f, 1.0f);
        const float length = XMVectorGetX(XMVector3Length(direction));
        if (length > 1.0f) direction /= length;
        const float distance = static_cast<float>(moveSpeed_ * deltaSeconds * (fast ? 3.0 : 1.0));
        if (!std::isfinite(distance)) return;
        XMFLOAT3 offset;
        XMStoreFloat3(&offset, direction * distance);
        position_[0] += offset.x;
        position_[1] += offset.y;
        position_[2] += offset.z;
    }

    const std::array<float, 3>& DebugCamera::GetPosition() const noexcept
    {
        return position_;
    }

    DirectX::XMMATRIX DebugCamera::GetViewMatrix() const noexcept
    {
        using namespace DirectX;
        return XMMatrixLookToLH(XMVectorSet(position_[0], position_[1], position_[2], 1.0f),
            GetForward(), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    }

    void DebugCamera::SetMoveSpeed(float speed) noexcept
    {
        if (std::isfinite(speed) && speed >= 0.1f && speed <= 50.0f) moveSpeed_ = speed;
    }

    float DebugCamera::GetMoveSpeed() const noexcept
    {
        return moveSpeed_;
    }
}
