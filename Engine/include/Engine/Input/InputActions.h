#pragma once
#include <Engine/Core/Validation.h>
#include <Engine/Input/Keyboard.h>
#include <Engine/Input/Gamepad.h>
#include <Engine/Core/Json.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace Engine
{
    struct InputBinding
    {
        std::vector<unsigned int> keys;
        unsigned int buttons = 0;
        int axis = 0; // +/-1,2 は左スティックの X,Y、+/-3,4 は右の X,Y、5,6 は左右のトリガーです。
        float threshold = 0.5f;
        unsigned int mouseButtons = 0; // ビットは左・右・中央・X1・X2 の順です。
        bool operator==(const InputBinding&) const = default;
    };
    struct InputSnapshot
    {
        bool active = false, gamepadConnected = false;
        std::array<bool, 256> down{}, pressed{};
        unsigned int buttons = 0, pressedButtons = 0;
        std::array<float, 2> stick{};
        std::array<float, 2> rightStick{}, triggers{};
        unsigned int mouseButtons = 0;
    };
    class InputActions final
    {
    public:
        using Bindings = std::map<std::string, InputBinding>;
        InputActions() : bindings_(Defaults()) {}
        /// <summary>既定の設定を作成します。</summary>
        static Bindings Defaults();
        /// <summary>設定の整合性を検証します。</summary>
        static void Validate(const Bindings& bindings);
        /// <summary>保存形式へ変換します。</summary>
        static Json Serialize(const Bindings& bindings);
        /// <summary>保存形式を読み取り検証します。</summary>
        static Bindings Parse(const Json& value);
        /// <summary>入力の割り当てを更新し、以前の状態を消去します。</summary>
        void SetBindings(Bindings bindings);
        const Bindings& GetBindings() const
        {
            return bindings_;
        }
        const std::map<std::string, float>& Values() const
        {
            return values_;
        }
        const std::map<std::string, bool>& PressedValues() const
        {
            return pressed_;
        }
        /// <summary>現在の状態を取得します。</summary>
        static InputSnapshot Capture(const Keyboard& keyboard, const Gamepad* gamepad = nullptr,
                                     bool includeMouse = false);
        /// <summary>状態を更新します。</summary>
        void Update(const InputSnapshot& snapshot);
        float Value(const std::string& name) const
        {
            const auto found = values_.find(name);
            return found == values_.end() ? 0 : found->second;
        }
        bool Down(const std::string& name) const
        {
            return Value(name) > 0;
        }
        bool Pressed(const std::string& name) const
        {
            const auto found = pressed_.find(name);
            return found != pressed_.end() && found->second;
        }

    private:
        static bool IntegerRange(const Json& value, double minimum, double maximum)
        {
            return value.is_number_integer() && JsonNumber(value) >= minimum && JsonNumber(value) <= maximum;
        }

        /// <summary>入力の割り当てを読み取り検証します。</summary>
        static InputBinding ParseBinding(const Json& definition);

        /// <summary>デジタル入力の値と押下を取得します。</summary>
        void DigitalInput(const InputSnapshot& snapshot, const InputBinding& binding, float& value, bool& edge) const;

        /// <summary>アナログ入力の値を取得します。</summary>
        static float AnalogInput(const InputSnapshot& snapshot, const InputBinding& binding);

        Bindings bindings_;
        std::map<std::string, float> values_;
        std::map<std::string, bool> pressed_;
        bool initialized_ = false, previousGamepad_ = false;
        unsigned int previousMouse_ = 0;
    };
}
