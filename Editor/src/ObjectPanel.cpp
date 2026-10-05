#include "ObjectPanel.h"
#include <algorithm>
#include <numbers>

namespace Editor
{
    void ObjectPanel::Draw(SceneRuntime::SceneWorld& world)
    {
        const auto& objects = world.Layout().objects;
        ImGui::SetNextWindowPos(ImVec2(20, 140), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340, 360), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Objects"))
        {
            filter_.Draw("Search", -1);
            ImGui::Text("%zu objects", objects.size());
            if (ImGui::BeginChild("Object list", ImVec2(0, 0)))
            {
                for (const auto& object : objects)
                {
                    const auto searchable = object.name + " " + object.id + " " + object.model.generic_string();
                    if (!filter_.PassFilter(searchable.c_str())) continue;
                    ImGui::PushID(object.id.c_str());
                    if (ImGui::Selectable(object.name.c_str(), selectedId_ == object.id))
                    {
                        selectedId_ = object.id;
                        invalidTransform_ = false;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(380, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, 310), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Inspector"))
        {
            const auto found = std::find_if(objects.begin(), objects.end(),
                [&](const auto& object) { return object.id == selectedId_; });
            if (found == objects.end()) ImGui::TextUnformatted("Select an object from the list.");
            else
            {
                ImGui::TextUnformatted(found->name.c_str());
                ImGui::Text("ID: %s", found->id.c_str());
                ImGui::TextWrapped("Model: %s", found->model.generic_string().c_str());
                ImGui::Separator();
                auto position = found->position;
                auto rotation = found->rotation;
                auto scale = found->scale;
                constexpr float ToDegrees = 180.0f / std::numbers::pi_v<float>;
                auto degrees = rotation;
                for (auto& value : degrees) value *= ToDegrees;
                bool edited = ImGui::DragFloat3("Position", position.data(), 0.05f, 0, 0, "%.3f");
                if (ImGui::DragFloat3("Rotation (deg)", degrees.data(), 0.5f, 0, 0, "%.2f"))
                {
                    for (size_t i = 0; i < 3; ++i) rotation[i] = degrees[i] / ToDegrees;
                    edited = true;
                }
                edited |= ImGui::DragFloat3("Scale", scale.data(), 0.05f, 0, 0, "%.3f");
                if (edited && (position != found->position || rotation != found->rotation || scale != found->scale))
                {
                    invalidTransform_ = !world.SetTransform(selectedId_, position, rotation, scale);
                    if (!invalidTransform_) changed_ = true;
                }
                if (invalidTransform_)
                    ImGui::TextWrapped("Invalid transform. Use finite numbers and nonzero scale.");
            }
            ImGui::Separator();
            ImGui::TextWrapped(changed_ ? "Unsaved changes. Saving will be added next." :
                "Changes are temporary. Saving is not available yet.");
        }
        ImGui::End();
    }
}
