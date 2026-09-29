#include <Engine/Input/Keyboard.h>
#include <Engine/Core/Log.h>
#include <format>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

namespace
{
    bool Check(HRESULT result, const char* operation)
    {
        if (SUCCEEDED(result)) return true;
        Engine::Log::Error(std::format("{} failed: 0x{:08X}", operation, static_cast<unsigned long>(result)));
        return false;
    }
}

namespace Engine
{
    Keyboard::~Keyboard()
    {
        Shutdown();
    }

    bool Keyboard::Initialize(HWND window)
    {
        if (input_ || !IsWindow(window)) return false;
        if (!Check(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
            reinterpret_cast<void**>(input_.GetAddressOf()), nullptr), "Create DirectInput") ||
            !Check(input_->CreateDevice(GUID_SysKeyboard, device_.GetAddressOf(), nullptr), "Create keyboard") ||
            !Check(device_->SetDataFormat(&c_dfDIKeyboard), "Set keyboard format") ||
            !Check(device_->SetCooperativeLevel(window, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE), "Set keyboard cooperative level"))
        {
            Shutdown();
            return false;
        }
        window_ = window;
        return true;
    }

    void Keyboard::Shutdown()
    {
        if (device_) device_->Unacquire();
        device_.Reset();
        input_.Reset();
        window_ = nullptr;
        ClearState();
    }

    void Keyboard::ClearState() noexcept
    {
        current_.fill(0);
        previous_.fill(0);
        active_ = false;
    }

    void Keyboard::Update()
    {
        if (!device_ || GetForegroundWindow() != window_)
        {
            if (device_) device_->Unacquire();
            ClearState();
            return;
        }
        std::array<BYTE, 256> state{};
        HRESULT result = device_->GetDeviceState(static_cast<DWORD>(state.size()), state.data());
        if (result == DIERR_INPUTLOST || result == DIERR_NOTACQUIRED)
        {
            ClearState();
            result = device_->Acquire();
            if (SUCCEEDED(result))
            {
                result = device_->GetDeviceState(static_cast<DWORD>(state.size()), state.data());
            }
        }
        if (FAILED(result))
        {
            ClearState();
            return;
        }
        previous_ = active_ ? current_ : state;
        current_ = state;
        active_ = true;
    }

    bool Keyboard::IsActive() const noexcept
    {
        return active_;
    }

    bool Keyboard::IsDown(unsigned int key) const noexcept
    {
        return key < current_.size() && (current_[key] & 0x80) != 0;
    }

    bool Keyboard::IsPressed(unsigned int key) const noexcept
    {
        return IsDown(key) && (previous_[key] & 0x80) == 0;
    }

    bool Keyboard::IsReleased(unsigned int key) const noexcept
    {
        return key < current_.size() && (current_[key] & 0x80) == 0 && (previous_[key] & 0x80) != 0;
    }
}
