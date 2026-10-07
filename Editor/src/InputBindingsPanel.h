#pragma once
#include <Engine/Input/InputActions.h>
#include <imgui.h>

namespace Editor
{
    class InputBindingsPanel final
    {
    public:
        static void Draw(Engine::InputActions::Bindings& bindings)
        {
            if (!ImGui::CollapsingHeader("入力Action###Input actions")) return;
            ImGui::TextUnformatted("ゲーム操作のキー・パッド・左スティックを設定します。");
            ImGui::BeginChild("Input binding entries",ImVec2(620,320),ImGuiChildFlags_Borders);
            for (auto iterator=bindings.begin();iterator!=bindings.end();)
            {
                auto& binding=iterator->second;
                ImGui::PushID(iterator->first.c_str());
                bool remove=false;
                if (ImGui::TreeNode(iterator->first.c_str()))
                {
                    size_t index=0;
                    for (auto key=binding.keys.begin();key!=binding.keys.end();)
                    {
                        ImGui::PushID(static_cast<int>(index++));
                        DrawKey(*key); ImGui::SameLine();
                        const bool erase=ImGui::SmallButton("削除###Remove key");
                        ImGui::PopID();
                        if (erase) key=binding.keys.erase(key); else ++key;
                    }
                    if (binding.keys.size()<4 && ImGui::SmallButton("キーを追加###Add key")) binding.keys.push_back(DIK_SPACE);
                    const std::pair<const char*,unsigned int> buttons[]={{"なし",0},{"A",XINPUT_GAMEPAD_A},{"B",XINPUT_GAMEPAD_B},{"X",XINPUT_GAMEPAD_X},{"Y",XINPUT_GAMEPAD_Y},
                        {"LB",XINPUT_GAMEPAD_LEFT_SHOULDER},{"RB",XINPUT_GAMEPAD_RIGHT_SHOULDER},{"Start",XINPUT_GAMEPAD_START},{"Back",XINPUT_GAMEPAD_BACK},
                        {"十字キー左",XINPUT_GAMEPAD_DPAD_LEFT},{"十字キー右",XINPUT_GAMEPAD_DPAD_RIGHT},{"十字キー上",XINPUT_GAMEPAD_DPAD_UP},{"十字キー下",XINPUT_GAMEPAD_DPAD_DOWN}};
                    const char* label="ボタンの組み合わせ";
                    for (const auto& [name,mask] : buttons) if (mask==binding.buttons) label=name;
                    if (ImGui::BeginCombo("パッドボタン###Gamepad button",label))
                    { for (const auto& [name,mask] : buttons) if (ImGui::Selectable(name,mask==binding.buttons)) binding.buttons=mask; ImGui::EndCombo(); }
                    const std::pair<const char*,int> axes[]={{"なし",0},{"左スティック右",1},{"左スティック左",-1},{"左スティック上",2},{"左スティック下",-2}};
                    label="なし"; for (const auto& [name,axis] : axes) if (axis==binding.axis) label=name;
                    if (ImGui::BeginCombo("スティック###Stick axis",label))
                    { for (const auto& [name,axis] : axes) if (ImGui::Selectable(name,axis==binding.axis)) binding.axis=axis; ImGui::EndCombo(); }
                    if (binding.axis) ImGui::SliderFloat("反応するしきい値###Threshold",&binding.threshold,0.05f,1,"%.2f");
                    remove=ImGui::SmallButton("Actionを削除###Remove action");
                    ImGui::TreePop();
                }
                ImGui::PopID();
                if (remove) iterator=bindings.erase(iterator); else ++iterator;
            }
            ImGui::EndChild();
            static std::array<char,129> name{};
            ImGui::InputText("新しいAction名###New action",name.data(),name.size());
            if (ImGui::Button("Actionを追加###Add action") && name[0] && bindings.size()<64)
            { bindings.emplace(name.data(),Engine::InputBinding{}); name.fill(0); }
            ImGui::SameLine(); if (ImGui::Button("標準の入力に戻す###Reset input actions")) bindings=Engine::InputActions::Defaults();
        }
    private:
        static void DrawKey(unsigned int& key)
        {
            const std::pair<const char*,unsigned int> keys[]={{"A",DIK_A},{"B",DIK_B},{"C",DIK_C},{"D",DIK_D},{"E",DIK_E},{"F",DIK_F},{"G",DIK_G},{"H",DIK_H},{"I",DIK_I},{"J",DIK_J},{"K",DIK_K},{"L",DIK_L},{"M",DIK_M},{"N",DIK_N},{"O",DIK_O},{"P",DIK_P},{"Q",DIK_Q},{"R",DIK_R},{"S",DIK_S},{"T",DIK_T},{"U",DIK_U},{"V",DIK_V},{"W",DIK_W},{"X",DIK_X},{"Y",DIK_Y},{"Z",DIK_Z},
                {"上",DIK_UP},{"下",DIK_DOWN},{"左",DIK_LEFT},{"右",DIK_RIGHT},{"Space",DIK_SPACE},{"Enter",DIK_RETURN},{"Escape",DIK_ESCAPE},{"左Shift",DIK_LSHIFT},{"右Shift",DIK_RSHIFT},{"左Ctrl",DIK_LCONTROL},{"右Ctrl",DIK_RCONTROL},
                {"0",DIK_0},{"1",DIK_1},{"2",DIK_2},{"3",DIK_3},{"4",DIK_4},{"5",DIK_5},{"6",DIK_6},{"7",DIK_7},{"8",DIK_8},{"9",DIK_9}};
            const char* label="その他のキー";
            for (const auto& [name,code] : keys) if (code==key) label=name;
            if (ImGui::BeginCombo("キー###Key",label))
            { for (const auto& [name,code] : keys) if (ImGui::Selectable(name,code==key)) key=code; ImGui::EndCombo(); }
        }
    };
}
