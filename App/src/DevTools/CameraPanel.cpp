#include "CameraPanel.h"
#include <imgui.h>

namespace App
{
    void CameraPanel::CancelDrag() noexcept
    {
        dragging_ = false;
    }

    void CameraPanel::Draw(Engine::DebugCamera& camera, const Engine::Keyboard& keyboard, double deltaSeconds)
    {
        UpdateInput(camera, keyboard, deltaSeconds);
        ImGui::SetNextWindowPos(ImVec2(380.0f, 370.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340.0f, 190.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Debug Camera"))
        {
            ImGui::TextUnformatted("Hold RMB on scene: mouse look");
            ImGui::TextUnformatted("RMB + WASD: move / Q,E: down,up");
            ImGui::TextUnformatted("Shift: boost / R: reset");
            auto position = camera.GetPosition();
            if (ImGui::DragFloat3("Position", position.data(), 0.05f)) camera.GetCamera().SetPosition(position);
            float speed = camera.GetMoveSpeed();
            if (ImGui::SliderFloat("Move speed", &speed, 0.1f, 20.0f)) camera.SetMoveSpeed(speed);
            if (ImGui::Button("Reset camera"))
            {
                camera.Reset();
                CancelDrag();
            }
        }
        ImGui::End();
    }

    void CameraPanel::UpdateInput(Engine::DebugCamera& camera, const Engine::Keyboard& keyboard, double deltaSeconds)
    {
        const auto& io = ImGui::GetIO();
        const bool started = keyboard.IsActive() && !io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (started) ImGui::SetWindowFocus(nullptr);
        const bool allowInput = keyboard.IsActive() && !io.WantCaptureMouse && (!io.WantCaptureKeyboard || started);
        if (!allowInput || !ImGui::IsMouseDown(ImGuiMouseButton_Right)) dragging_ = false;
        if (started) dragging_ = true;
        if (dragging_)
        {
            // 操作開始時の移動量は捨て、クリックした瞬間の視点ジャンプを防ぎます。
            if (!started) camera.Rotate(io.MouseDelta.x, io.MouseDelta.y);
            const float right = float(keyboard.IsDown(DIK_D)) - float(keyboard.IsDown(DIK_A));
            const float up = float(keyboard.IsDown(DIK_E)) - float(keyboard.IsDown(DIK_Q));
            const float forward = float(keyboard.IsDown(DIK_W)) - float(keyboard.IsDown(DIK_S));
            camera.Move(right, up, forward, deltaSeconds, keyboard.IsDown(DIK_LSHIFT) || keyboard.IsDown(DIK_RSHIFT));
        }
        ResetFromKeyboard(camera, keyboard);
    }

    void CameraPanel::ResetFromKeyboard(Engine::DebugCamera& camera, const Engine::Keyboard& keyboard)
    {
        const auto& io = ImGui::GetIO();
        if (keyboard.IsActive() && !io.WantCaptureKeyboard && keyboard.IsPressed(DIK_R))
        {
            camera.Reset();
            CancelDrag();
        }
    }
}
