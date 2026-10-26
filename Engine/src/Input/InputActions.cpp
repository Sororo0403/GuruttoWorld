#include <Engine/Input/InputActions.h>

namespace Engine
{
    auto InputActions::Defaults() -> Bindings
    {
        return {{"MoveLeft", {{DIK_A, DIK_LEFT}, XINPUT_GAMEPAD_DPAD_LEFT, -1}},
                {"MoveRight", {{DIK_D, DIK_RIGHT}, XINPUT_GAMEPAD_DPAD_RIGHT, 1}},
                {"MoveForward", {{DIK_W, DIK_UP}, XINPUT_GAMEPAD_DPAD_UP, 2}},
                {"MoveBack", {{DIK_S, DIK_DOWN}, XINPUT_GAMEPAD_DPAD_DOWN, -2}},
                {"Jump", {{DIK_SPACE}, XINPUT_GAMEPAD_A}},
                {"Confirm", {{DIK_RETURN}, XINPUT_GAMEPAD_A}},
                {"Cancel", {{DIK_ESCAPE}, XINPUT_GAMEPAD_B}},
                {"SelectModel1", {{DIK_1}}},
                {"SelectModel2", {{DIK_2}}}};
    }

    auto InputActions::Validate(const Bindings& bindings) -> void
    {
        if (bindings.size() > 64)
            throw std::runtime_error("Too many input actions");
        for (const auto& [name, binding] : bindings)
            if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos || binding.keys.size() > 4 ||
                std::any_of(binding.keys.begin(), binding.keys.end(),
                            [](unsigned int key)
                            {
                                return key >= 256;
                            }) ||
                binding.buttons > 65535 || binding.mouseButtons > 31 || binding.axis < -4 || binding.axis > 6 ||
                !Engine::InRange(binding.threshold, 0.05f, 1))
                throw std::runtime_error("Invalid input action: " + name);
    }

    auto InputActions::Serialize(const Bindings& bindings) -> Json
    {
        Validate(bindings);
        Json result = Json::object();
        for (const auto& [name, binding] : bindings)
            result[name] = {{"keys", binding.keys},
                            {"buttons", binding.buttons},
                            {"axis", binding.axis},
                            {"threshold", binding.threshold},
                            {"mouseButtons", binding.mouseButtons}};
        return result;
    }

    auto InputActions::Parse(const Json& value) -> Bindings
    {
        Bindings result;
        for (const auto& [name, definition] : JsonObject(value).items())
        {
            auto binding = ParseBinding(definition);
            result[name] = std::move(binding);
        }
        Validate(result);
        return result;
    }

    auto InputActions::SetBindings(Bindings bindings) -> void
    {
        Validate(bindings);
        bindings_ = std::move(bindings);
        values_.clear();
        pressed_.clear();
        initialized_ = false;
    }

    auto InputActions::Capture(const Keyboard& keyboard, const Gamepad* gamepad, bool includeMouse) -> InputSnapshot
    {
        InputSnapshot result;
        result.active = keyboard.IsActive();
        if (!result.active)
            return result;
        for (unsigned int key = 0; key < 256; ++key)
        {
            result.down[key] = keyboard.IsDown(key);
            result.pressed[key] = keyboard.IsPressed(key);
        }
        if (includeMouse)
        {
            constexpr int buttons[]{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
            if (const auto* pointer = keyboard.GetDiagnosticPointer())
                result.mouseButtons = pointer->buttons;
            else
                for (size_t i = 0; i < 5; ++i)
                    if (GetAsyncKeyState(buttons[i]) & 0x8000)
                        result.mouseButtons |= 1u << i;
        }
        if (gamepad && gamepad->IsConnected())
        {
            result.gamepadConnected = true;
            result.stick = gamepad->GetLeftStick();
            result.rightStick = gamepad->GetRightStick();
            result.triggers = {gamepad->GetLeftTrigger(), gamepad->GetRightTrigger()};
            for (unsigned int bit = 0; bit < 16; ++bit)
            {
                const auto mask = static_cast<WORD>(1u << bit);
                if (gamepad->IsDown(mask))
                    result.buttons |= mask;
                if (gamepad->IsPressed(mask))
                    result.pressedButtons |= mask;
            }
        }
        return result;
    }

    auto InputActions::Update(const InputSnapshot& snapshot) -> void
    {
        if (!snapshot.active)
        {
            values_.clear();
            pressed_.clear();
            initialized_ = false;
            previousGamepad_ = false;
            previousMouse_ = 0;
            return;
        }
        pressed_.clear();
        for (const auto& [name, binding] : bindings_)
        {
            float value = 0;
            bool edge = false;
            DigitalInput(snapshot, binding, value, edge);
            const float analog = AnalogInput(snapshot, binding);
            if (analog >= binding.threshold)
            {
                value = std::max(value, std::clamp(analog, 0.0f, 1.0f));
                edge = edge || (previousGamepad_ && Value(name) < binding.threshold);
            }
            pressed_[name] = initialized_ && edge;
            values_[name] = value;
        }
        initialized_ = true;
        previousGamepad_ = snapshot.gamepadConnected;
        previousMouse_ = snapshot.mouseButtons;
    }

    auto InputActions::ParseBinding(const Json& definition) -> InputBinding
    {
        InputBinding binding;
        for (const auto& key : JsonArray(definition.at("keys")))
        {
            if (!IntegerRange(key, 0, 255))
                throw std::runtime_error("Invalid input key");
            binding.keys.push_back(key.get<unsigned int>());
        }
        const auto& buttons = definition.at("buttons");
        const auto& axis = definition.at("axis");
        if (!IntegerRange(buttons, 0, 65535) || !IntegerRange(axis, -4, 6))
            throw std::runtime_error("Invalid input binding");
        binding.buttons = buttons.get<unsigned int>();
        binding.axis = axis.get<int>();
        binding.threshold = static_cast<float>(JsonNumber(definition.at("threshold")));
        if (definition.contains("mouseButtons"))
        {
            const auto& mouse = definition.at("mouseButtons");
            if (!IntegerRange(mouse, 0, 31))
                throw std::runtime_error("Invalid mouse binding");
            binding.mouseButtons = mouse.get<unsigned int>();
        }
        return binding;
    }

    auto InputActions::DigitalInput(const InputSnapshot& snapshot, const InputBinding& binding, float& value,
                                    bool& edge) const -> void
    {
        for (const auto key : binding.keys)
        {
            if (snapshot.down[key])
                value = 1;
            edge = edge || snapshot.pressed[key];
        }
        if (binding.buttons && (snapshot.buttons & binding.buttons) == binding.buttons)
        {
            value = 1;
            edge = edge || (snapshot.pressedButtons & binding.buttons) != 0;
        }
        if (binding.mouseButtons && (snapshot.mouseButtons & binding.mouseButtons) == binding.mouseButtons)
        {
            value = 1;
            edge = edge || (previousMouse_ & binding.mouseButtons) != binding.mouseButtons;
        }
    }

    auto InputActions::AnalogInput(const InputSnapshot& snapshot, const InputBinding& binding) -> float
    {
        float analog = 0;
        const auto axis = std::abs(binding.axis);
        if (axis == 1 || axis == 2)
            analog = snapshot.stick[axis - 1];
        else if (axis == 3 || axis == 4)
            analog = snapshot.rightStick[axis - 3];
        else if (axis == 5 || axis == 6)
            analog = snapshot.triggers[axis - 5];
        if (binding.axis < 0)
            analog = -analog;
        if (!std::isfinite(analog))
            analog = 0;
        return analog;
    }
}
