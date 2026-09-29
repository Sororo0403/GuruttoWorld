#include "LightingPanel.h"

#include <imgui.h>

namespace App
{
    void LightingPanel::Draw(Engine::DirectionalLight& light)
    {
        ImGui::SetNextWindowPos(ImVec2(20.0f, 280.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340.0f, 300.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Lighting"))
        {
            ImGui::Checkbox("Enable lighting", &light.enabled);
            ImGui::DragFloat3("Direction", light.direction.data(), 0.01f, -1.0f, 1.0f, "%.2f");
            ImGui::ColorEdit3("Light color", light.color.data());
            ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 4.0f, "%.2f");
            ImGui::SliderFloat("Ambient", &light.ambientIntensity, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Specular", &light.specularStrength, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Shininess", &light.shininess, 1.0f, 256.0f, "%.0f");
            if (ImGui::Button("Reset lighting"))
            {
                light = Engine::DirectionalLight{};
            }
        }
        ImGui::End();
    }
}
