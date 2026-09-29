#include <Engine/Input/Gamepad.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "xinput.lib")

namespace
{
    bool Down(WORD state, WORD buttons)
    {
        return buttons != 0 && (state & buttons) == buttons;
    }

    std::array<float, 2> Stick(SHORT x, SHORT y, float deadZone)
    {
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float length = std::sqrt(fx * fx + fy * fy);
        if (length <= deadZone) return {};
        const float magnitude = (std::min(length, 32767.0f) - deadZone) / (32767.0f - deadZone);
        return { fx / length * magnitude, fy / length * magnitude };
    }

    float Trigger(BYTE value)
    {
        return value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 0.0f :
            static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) / (255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    }
}

namespace Engine
{
    Gamepad::Gamepad(unsigned int userIndex) noexcept : userIndex_(userIndex)
    {
    }

    Gamepad::~Gamepad()
    {
        StopVibration();
    }

    void Gamepad::Update(bool active)
    {
        XINPUT_STATE state{};
        const bool wasActive = active_;
        connected_ = userIndex_ < XUSER_MAX_COUNT && XInputGetState(userIndex_, &state) == ERROR_SUCCESS;
        active_ = active && connected_;
        if (!active_)
        {
            current_ = {};
            previousButtons_ = 0;
            StopVibration();
            return;
        }
        previousButtons_ = wasActive ? current_.wButtons : state.Gamepad.wButtons;
        current_ = state.Gamepad;
        if (vibrating_ && std::chrono::steady_clock::now() >= vibrationEnd_)
        {
            StopVibration();
        }
    }

    bool Gamepad::IsConnected() const noexcept
    {
        return connected_;
    }

    bool Gamepad::IsDown(WORD buttons) const noexcept
    {
        return Down(current_.wButtons, buttons);
    }

    bool Gamepad::IsPressed(WORD buttons) const noexcept
    {
        return IsDown(buttons) && !Down(previousButtons_, buttons);
    }

    bool Gamepad::IsReleased(WORD buttons) const noexcept
    {
        return !IsDown(buttons) && Down(previousButtons_, buttons);
    }

    std::array<float, 2> Gamepad::GetLeftStick() const noexcept
    {
        return Stick(current_.sThumbLX, current_.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    }

    std::array<float, 2> Gamepad::GetRightStick() const noexcept
    {
        return Stick(current_.sThumbRX, current_.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    }

    float Gamepad::GetLeftTrigger() const noexcept
    {
        return Trigger(current_.bLeftTrigger);
    }

    float Gamepad::GetRightTrigger() const noexcept
    {
        return Trigger(current_.bRightTrigger);
    }

    bool Gamepad::Vibrate(float left, float right, float seconds)
    {
        if (!active_ || !std::isfinite(left) || !std::isfinite(right) || !std::isfinite(seconds) ||
            left < 0.0f || left > 1.0f || right < 0.0f || right > 1.0f || seconds <= 0.0f || seconds > 60.0f)
        {
            return false;
        }
        XINPUT_VIBRATION vibration{};
        vibration.wLeftMotorSpeed = static_cast<WORD>(left * 65535.0f);
        vibration.wRightMotorSpeed = static_cast<WORD>(right * 65535.0f);
        if (XInputSetState(userIndex_, &vibration) != ERROR_SUCCESS) return false;
        vibrating_ = left > 0.0f || right > 0.0f;
        vibrationEnd_ = std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(seconds));
        return true;
    }

    void Gamepad::StopVibration()
    {
        if (!vibrating_) return;
        XINPUT_VIBRATION vibration{};
        const DWORD result = XInputSetState(userIndex_, &vibration);
        if (result == ERROR_SUCCESS || result == ERROR_DEVICE_NOT_CONNECTED) vibrating_ = false;
        vibrationEnd_ = {};
    }
}
