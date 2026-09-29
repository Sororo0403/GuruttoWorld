#include "UvTransformPanel.h"

#include <imgui.h>

namespace
{
    void DrawTransform(const char* label, Engine::UvTransform& transform)
    {
        ImGui::PushID(label);
        if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::DragFloat2("Scale", transform.scale.data(), 0.01f);
            ImGui::SliderAngle("Rotation", &transform.rotation, -180.0f, 180.0f);
            ImGui::DragFloat2("Translation", transform.translation.data(), 0.01f);
            if (ImGui::Button("Reset UV"))
            {
                transform = Engine::UvTransform{};
            }
        }
        ImGui::PopID();
    }
}

namespace App
{
    void UvTransformPanel::Draw(Engine::UvTransform& model, Engine::UvTransform& sprite)
    {
        ImGui::SetNextWindowPos(ImVec2(380.0f, 20.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300.0f, 330.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("UV Transform"))
        {
            DrawTransform("Model", model);
            DrawTransform("Sprite", sprite);
        }
        ImGui::End();
    }
}
