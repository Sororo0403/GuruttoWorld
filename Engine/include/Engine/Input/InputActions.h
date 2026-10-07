#pragma once
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
        unsigned int buttons=0;
        int axis=0; // +/-1: left X, +/-2: left Y.
        float threshold=0.5f;
        bool operator==(const InputBinding&) const = default;
    };
    struct InputSnapshot
    {
        bool active=false,gamepadConnected=false;
        std::array<bool,256> down{},pressed{};
        unsigned int buttons=0,pressedButtons=0;
        std::array<float,2> stick{};
    };
    class InputActions final
    {
    public:
        using Bindings=std::map<std::string,InputBinding>;
        InputActions():bindings_(Defaults()) {}
        static Bindings Defaults()
        {
            return {{"MoveLeft",{{DIK_A,DIK_LEFT},XINPUT_GAMEPAD_DPAD_LEFT,-1}},
                {"MoveRight",{{DIK_D,DIK_RIGHT},XINPUT_GAMEPAD_DPAD_RIGHT,1}},
                {"MoveForward",{{DIK_W,DIK_UP},XINPUT_GAMEPAD_DPAD_UP,2}},
                {"MoveBack",{{DIK_S,DIK_DOWN},XINPUT_GAMEPAD_DPAD_DOWN,-2}},
                {"Jump",{{DIK_SPACE},XINPUT_GAMEPAD_A}},
                {"Confirm",{{DIK_RETURN},XINPUT_GAMEPAD_A}},
                {"Cancel",{{DIK_ESCAPE},XINPUT_GAMEPAD_B}}};
        }
        static void Validate(const Bindings& bindings)
        {
            if (bindings.size()>64) throw std::runtime_error("Too many input actions");
            for (const auto& [name,binding] : bindings)
                if (name.empty() || name.size()>128 || name.find('\0')!=std::string::npos || binding.keys.size()>4 ||
                    std::any_of(binding.keys.begin(),binding.keys.end(),[](unsigned int key) { return key>=256; }) ||
                    binding.buttons>65535 || binding.axis<-2 || binding.axis>2 || !std::isfinite(binding.threshold) || binding.threshold<0.05f || binding.threshold>1)
                    throw std::runtime_error("Invalid input action: "+name);
        }
        static Json Serialize(const Bindings& bindings)
        {
            Validate(bindings); Json result=Json::object();
            for (const auto& [name,binding] : bindings)
                result[name]={{"keys",binding.keys},{"buttons",binding.buttons},{"axis",binding.axis},{"threshold",binding.threshold}};
            return result;
        }
        static Bindings Parse(const Json& value)
        {
            Bindings result;
            for (const auto& [name,definition] : JsonObject(value).items())
            {
                InputBinding binding;
                for (const auto& key : JsonArray(definition.at("keys")))
                {
                    if (!key.is_number_integer() || JsonNumber(key)<0 || JsonNumber(key)>255) throw std::runtime_error("Invalid input key");
                    binding.keys.push_back(key.get<unsigned int>());
                }
                const auto& buttons=definition.at("buttons"); const auto& axis=definition.at("axis");
                if (!buttons.is_number_integer() || JsonNumber(buttons)<0 || JsonNumber(buttons)>65535 ||
                    !axis.is_number_integer() || JsonNumber(axis)<-2 || JsonNumber(axis)>2) throw std::runtime_error("Invalid input binding");
                binding.buttons=buttons.get<unsigned int>(); binding.axis=axis.get<int>(); binding.threshold=static_cast<float>(JsonNumber(definition.at("threshold")));
                result[name]=std::move(binding);
            }
            Validate(result); return result;
        }
        void SetBindings(Bindings bindings) { Validate(bindings); bindings_=std::move(bindings); values_.clear(); pressed_.clear(); initialized_=false; }
        const Bindings& GetBindings() const { return bindings_; }
        const std::map<std::string,float>& Values() const { return values_; }
        const std::map<std::string,bool>& PressedValues() const { return pressed_; }
        static InputSnapshot Capture(const Keyboard& keyboard,const Gamepad* gamepad=nullptr)
        {
            InputSnapshot result; result.active=keyboard.IsActive();
            if (!result.active) return result;
            for (unsigned int key=0;key<256;++key) { result.down[key]=keyboard.IsDown(key); result.pressed[key]=keyboard.IsPressed(key); }
            if (gamepad && gamepad->IsConnected())
            {
                result.gamepadConnected=true; result.stick=gamepad->GetLeftStick();
                for (unsigned int bit=0;bit<16;++bit)
                {
                    const auto mask=static_cast<WORD>(1u<<bit);
                    if (gamepad->IsDown(mask)) result.buttons|=mask;
                    if (gamepad->IsPressed(mask)) result.pressedButtons|=mask;
                }
            }
            return result;
        }
        void Update(const InputSnapshot& snapshot)
        {
            if (!snapshot.active) { values_.clear(); pressed_.clear(); initialized_=false; previousGamepad_=false; return; }
            pressed_.clear();
            for (const auto& [name,binding] : bindings_)
            {
                float value=0; bool edge=false;
                for (const auto key : binding.keys) { if (snapshot.down[key]) value=1; edge=edge || snapshot.pressed[key]; }
                if (binding.buttons && (snapshot.buttons&binding.buttons)==binding.buttons)
                { value=1; edge=edge || (snapshot.pressedButtons&binding.buttons)!=0; }
                float analog=0;
                if (binding.axis) analog=std::max(0.0f,snapshot.stick[static_cast<size_t>(std::abs(binding.axis)-1)]*(binding.axis>0 ? 1.0f : -1.0f));
                if (analog>=binding.threshold)
                { value=std::max(value,std::clamp(analog,0.0f,1.0f)); edge=edge || (previousGamepad_ && Value(name)<binding.threshold); }
                pressed_[name]=initialized_ && edge; values_[name]=value;
            }
            initialized_=true; previousGamepad_=snapshot.gamepadConnected;
        }
        float Value(const std::string& name) const { const auto found=values_.find(name); return found==values_.end() ? 0 : found->second; }
        bool Down(const std::string& name) const { return Value(name)>0; }
        bool Pressed(const std::string& name) const { const auto found=pressed_.find(name); return found!=pressed_.end() && found->second; }
    private:
        Bindings bindings_;
        std::map<std::string,float> values_;
        std::map<std::string,bool> pressed_;
        bool initialized_=false,previousGamepad_=false;
    };
}
