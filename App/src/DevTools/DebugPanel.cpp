#include "DebugPanel.h"

#include <imgui.h>

namespace App
{
    void DebugPanel::Draw(double& rotationY, float& speedDegrees, bool& rotating,
        std::array<float, 4>& backgroundColor)
    {
        ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340.0f, 240.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("WP1 Debug"))
        {
            ImGui::Text("FPS: %.1f", static_cast<double>(ImGui::GetIO().Framerate));
            ImGui::Separator();
            ImGui::Checkbox("Rotate Y", &rotating);
            ImGui::SliderFloat("Speed", &speedDegrees, -360.0f, 360.0f, "%.0f deg/s");
            float angle = static_cast<float>(rotationY);
            if (ImGui::SliderAngle("Angle Y", &angle, -360.0f, 360.0f))
            {
                rotationY = angle;
            }
            if (ImGui::Button("Reset rotation"))
            {
                rotationY = 0.0;
            }
            ImGui::ColorEdit3("Background", backgroundColor.data());
            ImGui::TextUnformatted("Model: 1 = Cube, 2 = Pyramid");
        }
        ImGui::End();
    }
}
