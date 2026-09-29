#include "InputPanel.h"
#include <imgui.h>

namespace App
{
    void InputPanel::Draw(Engine::Gamepad& gamepad)
    {
        ImGui::SetNextWindowPos(ImVec2(900.0f, 420.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300.0f, 260.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Input"))
        {
            ImGui::TextUnformatted("Keyboard: Space = sound");
            ImGui::Text("Gamepad 0: %s", gamepad.IsConnected() ? "Connected" : "Disconnected");
            ImGui::TextUnformatted("A: sound + rumble / B: stop rumble");
            const auto left = gamepad.GetLeftStick();
            const auto right = gamepad.GetRightStick();
            ImGui::Text("L: %.2f, %.2f / R: %.2f, %.2f", left[0], left[1], right[0], right[1]);
            ImGui::Text("LT: %.2f / RT: %.2f", gamepad.GetLeftTrigger(), gamepad.GetRightTrigger());
            static float leftMotor = 0.3f;
            static float rightMotor = 0.3f;
            ImGui::SliderFloat("Left motor", &leftMotor, 0.0f, 1.0f);
            ImGui::SliderFloat("Right motor", &rightMotor, 0.0f, 1.0f);
            ImGui::BeginDisabled(!gamepad.IsConnected());
            if (ImGui::Button("Vibrate 0.5s")) gamepad.Vibrate(leftMotor, rightMotor, 0.5f);
            ImGui::SameLine();
            if (ImGui::Button("Stop")) gamepad.StopVibration();
            ImGui::EndDisabled();
        }
        ImGui::End();
    }
}
