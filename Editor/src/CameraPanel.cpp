#include "CameraPanel.h"
#include "PanelLayout.h"
#include <imgui.h>

namespace Editor
{
    void CameraPanel::CancelDrag() noexcept
    {
        dragging_ = false;
    }

    void CameraPanel::Draw(Engine::DebugCamera& camera, const Engine::Keyboard& keyboard, double deltaSeconds, const SceneViewport& viewport, bool sceneHovered, bool allowMovement)
    {
        if (allowMovement && viewport.Valid()) UpdateInput(camera, keyboard, deltaSeconds, viewport, sceneHovered);
        else CancelDrag();
        PanelLayout::Place(PanelLayout::Panel::Camera);
        if (ImGui::Begin("シーンカメラ###Debug Camera"))
        {
            ImGui::BeginDisabled(!allowMovement);
            ImGui::TextUnformatted("シーンで右ボタンを押したまま：視点を回転");
            ImGui::TextUnformatted("右ボタン＋WASD：移動／Q・E：下降・上昇");
            ImGui::TextUnformatted("Shift：加速／R：リセット");
            auto position = camera.GetPosition();
            if (ImGui::DragFloat3("位置###Position", position.data(), 0.05f)) camera.GetCamera().SetPosition(position);
            float speed = camera.GetMoveSpeed();
            if (ImGui::SliderFloat("移動速度###Move speed", &speed, 0.1f, 20.0f)) camera.SetMoveSpeed(speed);
            if (ImGui::Button("カメラをリセット###Reset camera"))
            {
                camera.Reset();
                CancelDrag();
            }
            ImGui::EndDisabled();
        }
        ImGui::End();
    }

    void CameraPanel::UpdateInput(Engine::DebugCamera& camera, const Engine::Keyboard& keyboard, double deltaSeconds, const SceneViewport& viewport, bool sceneHovered)
    {
        const auto& io = ImGui::GetIO();
        const bool started = keyboard.IsActive() && sceneHovered && viewport.Contains(io.MousePos.x, io.MousePos.y) && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (started) ImGui::SetWindowFocus(nullptr);
        const bool allowInput = keyboard.IsActive() && sceneHovered && (!io.WantCaptureKeyboard || started || dragging_);
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
