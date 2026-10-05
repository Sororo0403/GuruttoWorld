#include <Engine/DevTools/DebugCamera.h>
#include <algorithm>
#include <cmath>

namespace Engine
{
    void DebugCamera::Reset() noexcept
    {
        camera_.SetPosition(resetPosition_);
        camera_.SetRotation(resetYaw_, resetPitch_);
        yaw_ = resetYaw_;
        pitch_ = resetPitch_;
    }

    bool DebugCamera::SetResetPose(const std::array<float, 3>& position, float yaw, float pitch) noexcept
    {
        if (!std::all_of(position.begin(), position.end(), [](float v) { return std::isfinite(v); }) ||
            !std::isfinite(yaw) || !std::isfinite(pitch)) return false;
        resetPosition_ = position;
        resetYaw_ = std::remainder(yaw, DirectX::XM_2PI);
        resetPitch_ = std::clamp(pitch, -DirectX::XM_PIDIV2 + 0.01f, DirectX::XM_PIDIV2 - 0.01f);
        Reset();
        return true;
    }

    void DebugCamera::Rotate(float deltaX, float deltaY) noexcept
    {
        if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) return;
        constexpr float Sensitivity = 0.003f;
        yaw_ = std::remainder(yaw_ + deltaX * Sensitivity, DirectX::XM_2PI);
        pitch_ = std::clamp(pitch_ - deltaY * Sensitivity, -DirectX::XM_PIDIV2 + 0.01f, DirectX::XM_PIDIV2 - 0.01f);
        camera_.SetRotation(yaw_, pitch_);
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
        const auto& position = camera_.GetPosition();
        camera_.SetPosition({ position[0] + offset.x, position[1] + offset.y, position[2] + offset.z });
    }

    const std::array<float, 3>& DebugCamera::GetPosition() const noexcept
    {
        return camera_.GetPosition();
    }

    DirectX::XMMATRIX DebugCamera::GetViewMatrix() const noexcept
    {
        return camera_.GetViewMatrix();
    }

    Camera& DebugCamera::GetCamera() noexcept { return camera_; }
    const Camera& DebugCamera::GetCamera() const noexcept { return camera_; }

    void DebugCamera::SetMoveSpeed(float speed) noexcept
    {
        if (std::isfinite(speed) && speed >= 0.1f && speed <= 50.0f) moveSpeed_ = speed;
    }

    float DebugCamera::GetMoveSpeed() const noexcept
    {
        return moveSpeed_;
    }
}
