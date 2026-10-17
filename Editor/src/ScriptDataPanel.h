#pragma once
#include "ComponentWidgets.h"
#include "EditState.h"
#include "ProjectCatalog.h"
#include <SceneRuntime/ScriptValue.h>
#include <imgui.h>
#include <array>
#include <algorithm>
#include <SceneRuntime/ScriptRuntime.h>

namespace Editor
{
    class ScriptDataPanel final
    {
    public:
        /// <summary>型付きデータを編集し、連続操作を一回のUndoへまとめます。</summary>
        static bool Draw(EditState& state, const char* label, SceneRuntime::ScriptValue& field,
                         const ProjectCatalog* catalog = nullptr, bool allowType = true,
                         SceneRuntime::ScriptDataPurpose purpose = SceneRuntime::ScriptDataPurpose::Text)
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

    private:
        /// <summary>数値を保存可能な範囲で編集します。</summary>
        static bool DrawValue(EditState&, const char* label, float& value, const ProjectCatalog*)
        {
            return ImGui::DragFloat(label, &value, 0.05f, -1000000, 1000000, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        }
        /// <summary>真偽値を編集します。</summary>
        static bool DrawValue(EditState&, const char* label, bool& value, const ProjectCatalog*)
        {
            return ImGui::Checkbox(label, &value);
        }
        /// <summary>長さに上限のあるUTF-8文字列を編集します。</summary>
        template <size_t Capacity> static bool DrawString(const char* label, std::string& value)
        {
            std::array<char, Capacity> buffer{};
            std::copy_n(value.data(), std::min(value.size(), buffer.size() - 1), buffer.data());
            const bool edited = Capacity == 129 ? ImGui::InputText(label, buffer.data(), buffer.size())
                                                : ImGui::InputTextMultiline(label, buffer.data(), buffer.size(),
                                                                            {0, ImGui::GetTextLineHeight() * 3});
            if (!edited)
                return false;
            value = buffer.data();
            return true;
        }
        /// <summary>通常の文字列を編集します。</summary>
        static bool DrawText(const char* label, std::string& value, const ProjectCatalog* catalog,
                             SceneRuntime::ScriptDataPurpose purpose)
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
        /// <summary>参照IDの入力とHierarchyからのドラッグを処理します。</summary>
        static bool DrawValue(EditState&, const char* label, SceneRuntime::ScriptObjectReference& value,
                              const ProjectCatalog*)
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
        /// <summary>配列の内容と要素数を編集します。</summary>
        static bool DrawValue(EditState& state, const char* label, SceneRuntime::ScriptValue::Array& value,
                              const ProjectCatalog* catalog)
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
        /// <summary>名前付きの構造化フィールドを編集します。</summary>
        static bool DrawValue(EditState& state, const char* label, SceneRuntime::ScriptValue::Object& value,
                              const ProjectCatalog* catalog)
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
    };
}
