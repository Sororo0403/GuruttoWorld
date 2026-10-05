#include "ObjectPanel.h"
#include "PanelLayout.h"
#include <algorithm>
#include <numbers>

namespace Editor
{
    void ObjectPanel::Draw(SceneRuntime::SceneWorld& world, const std::array<float, 3>& suggestedPosition, bool enabled)
    {
        const auto& objects = world.Layout().objects;
        PanelLayout::Place(PanelLayout::Panel::Objects);
        if (ImGui::Begin("Objects"))
        {
            filter_.Draw("Search", -1);
            ImGui::Text("%zu objects", objects.size());
            if (ImGui::BeginChild("Object list", ImVec2(0, 0)))
            {
                ImGui::BeginDisabled(!enabled);
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
                ImGui::EndDisabled();
            }
            ImGui::EndChild();
        }
        ImGui::End();

        PanelLayout::Place(PanelLayout::Panel::Inspector);
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
                ImGui::BeginDisabled(!enabled);
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
                if (ImGui::Button("Duplicate")) request_ = ObjectRequest{ ObjectAction::Duplicate, selectedId_, {}, {} };
                ImGui::SameLine();
                if (ImGui::Button("Delete")) request_ = ObjectRequest{ ObjectAction::Delete, selectedId_, {}, {} };
                ImGui::EndDisabled();
                if (invalidTransform_)
                    ImGui::TextWrapped("Invalid transform. Use finite numbers and nonzero scale.");
            }
            ImGui::Separator();
            ImGui::TextWrapped(changed_ ? "Unsaved changes." :
                "No unsaved changes.");
        }
        ImGui::End();
        DrawModels(suggestedPosition, enabled);
    }
    void ObjectPanel::ScanModels(const std::filesystem::path& root)
    {
        models_.clear();
        catalogError_.clear();
        try
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "Assets/Models/Title"))
                if (entry.is_regular_file() && entry.path().extension() == ".obj")
                    models_.push_back(entry.path().lexically_relative(root));
            std::sort(models_.begin(), models_.end());
        }
        catch (const std::exception& error) { catalogError_ = error.what(); }
    }

    std::optional<ObjectRequest> ObjectPanel::TakeRequest()
    {
        auto request = std::move(request_);
        request_.reset();
        return request;
    }

    void ObjectPanel::DrawModels(const std::array<float, 3>& suggestedPosition, bool enabled)
    {
        if (!positionInitialized_) { addPosition_ = suggestedPosition; positionInitialized_ = true; }
        PanelLayout::Place(PanelLayout::Panel::Models);
        if (ImGui::Begin("Models"))
        {
            modelFilter_.Draw("Search models", -1);
            if (!catalogError_.empty()) ImGui::TextWrapped("%s", catalogError_.c_str());
            ImGui::Text("%zu models", models_.size());
            if (ImGui::BeginChild("Model list", ImVec2(0, 280), true))
            {
                for (const auto& model : models_)
                {
                    const auto path = model.generic_string();
                    if (!modelFilter_.PassFilter(path.c_str())) continue;
                    if (ImGui::Selectable(path.substr(std::string("Assets/Models/Title/").size()).c_str(),
                        model == selectedModel_)) selectedModel_ = model;
                }
            }
            ImGui::EndChild();
            ImGui::DragFloat3("Add position", addPosition_.data(), 0.1f);
            if (ImGui::Button("Use camera front")) addPosition_ = suggestedPosition;
            ImGui::BeginDisabled(!enabled || selectedModel_.empty());
            if (ImGui::Button("Add selected model"))
                request_ = ObjectRequest{ ObjectAction::Add, {}, selectedModel_, addPosition_ };
            ImGui::EndDisabled();
            ImGui::TextWrapped("New objects use scale 4. Adjust them in Inspector.");
        }
        ImGui::End();
    }
}
