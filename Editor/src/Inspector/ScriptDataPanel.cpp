#include "ScriptDataPanel.h"

namespace Editor
{
    auto ScriptDataPanel::Draw(EditState& state, const char* label, SceneRuntime::ScriptValue& field,
                               const ProjectCatalog* catalog, bool allowType, SceneRuntime::ScriptDataPurpose purpose)
        -> bool
    {
        ImGui::PushID(label);
        bool edited = false;
        if (allowType)
        {
            int type = static_cast<int>(field.value.index());
            if (ImGui::Combo("型###Field type", &type, "数値\0真偽値\0文字列\0オブジェクト参照\0配列\0構造\0"))
            {
                switch (type)
                {
                case 0:
                    field.value = 0.0f;
                    break;
                case 1:
                    field.value = false;
                    break;
                case 2:
                    field.value = std::string{};
                    break;
                case 3:
                    field.value = SceneRuntime::ScriptObjectReference{};
                    break;
                case 4:
                    field.value = SceneRuntime::ScriptValue::Array{};
                    break;
                case 5:
                    field.value = SceneRuntime::ScriptValue::Object{};
                    break;
                }
                edited = true;
            }
        }
        edited |= std::visit(
            [&](auto& value)
            {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>)
                    return DrawText(label, value, catalog, purpose);
                else
                    return DrawValue(state, label, value, catalog);
            },
            field.value);
        Editor::TrackComponentInteraction(state);
        ImGui::PopID();
        return edited;
    }

    auto ScriptDataPanel::DrawText(const char* label, std::string& value, const ProjectCatalog* catalog,
                                   SceneRuntime::ScriptDataPurpose purpose) -> bool
    {
        bool edited = DrawString<4097>(label, value);
        if (catalog && purpose == SceneRuntime::ScriptDataPurpose::SceneAsset &&
            ImGui::BeginCombo("シーンアセットを選択###Scene asset", value.c_str()))
        {
            for (const auto& asset : catalog->Assets())
                if (asset.kind == AssetKind::Scene)
                    if (ImGui::Selectable(ProjectCatalog::Text(asset.path).c_str(),
                                          ProjectCatalog::Text(asset.path) == value))
                    {
                        value = ProjectCatalog::Text(asset.path);
                        edited = true;
                    }
            ImGui::EndCombo();
        }
        return edited;
    }

    auto ScriptDataPanel::DrawValue(EditState&, const char* label, SceneRuntime::ScriptObjectReference& value,
                                    const ProjectCatalog*) -> bool
    {
        bool edited = DrawString<129>(label, value.id);
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto* payload = ImGui::AcceptDragDropPayload("WP1_HIERARCHY_OBJECT"))
            {
                value.id = static_cast<const char*>(payload->Data);
                edited = true;
            }
            ImGui::EndDragDropTarget();
        }
        return edited;
    }

    auto ScriptDataPanel::DrawValue(EditState& state, const char* label, SceneRuntime::ScriptValue::Array& value,
                                    const ProjectCatalog* catalog) -> bool
    {
        if (!ImGui::TreeNode(label))
            return false;
        bool edited = false;
        ImGui::Text("%zu / 256", value.size());
        for (size_t index = 0; index < value.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            edited |= Draw(state, std::to_string(index).c_str(), value[index], catalog);
            ImGui::PopID();
        }
        ImGui::BeginDisabled(value.size() >= 256);
        if (ImGui::Button("要素を追加###Add element"))
        {
            auto next = value.empty() ? SceneRuntime::ScriptValue{} : value.back();
            value.push_back(std::move(next));
            edited = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(value.empty());
        if (ImGui::Button("末尾を削除###Remove element"))
        {
            value.pop_back();
            edited = true;
        }
        ImGui::EndDisabled();
        ImGui::TreePop();
        return edited;
    }

    auto ScriptDataPanel::DrawValue(EditState& state, const char* label, SceneRuntime::ScriptValue::Object& value,
                                    const ProjectCatalog* catalog) -> bool
    {
        if (!ImGui::TreeNode(label))
            return false;
        bool edited = false;
        std::string remove;
        for (auto& [name, child] : value)
        {
            edited |= Draw(state, name.c_str(), child, catalog);
            ImGui::PushID(name.c_str());
            if (ImGui::SmallButton("項目を削除###Remove field"))
                remove = name;
            ImGui::PopID();
        }
        if (!remove.empty())
        {
            value.erase(remove);
            edited = true;
        }
        static std::map<ImGuiID, std::array<char, 129>> names;
        auto& key = names[ImGui::GetID("New field")];
        ImGui::InputText("新しい項目名###New field", key.data(), key.size());
        if (value.size() < 64 && ImGui::Button("項目を追加###Add field") && key[0])
        {
            edited |= value.try_emplace(key.data(), SceneRuntime::ScriptValue{}).second;
            key.fill(0);
        }
        ImGui::TreePop();
        return edited;
    }
}
