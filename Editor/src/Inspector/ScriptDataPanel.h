#pragma once
#include "ComponentWidgets.h"
#include "../Editing/EditState.h"
#include "../Project/ProjectCatalog.h"
#include <SceneRuntime/Scripts/ScriptValue.h>
#include <imgui.h>
#include <array>
#include <algorithm>
#include <SceneRuntime/Scripts/ScriptSchema.h>

namespace Editor
{
    class ScriptDataPanel final
    {
    public:
        /// <summary>型付きデータを編集し、連続操作を一回のUndoへまとめます。</summary>
        static bool Draw(EditState& state, const char* label, SceneRuntime::ScriptValue& field,
                         const ProjectCatalog* catalog = nullptr, bool allowType = true,
                         SceneRuntime::ScriptDataPurpose purpose = SceneRuntime::ScriptDataPurpose::Text);

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
                             SceneRuntime::ScriptDataPurpose purpose);
        /// <summary>参照IDの入力とヒエラルキーからのドラッグを処理します。</summary>
        static bool DrawValue(EditState&, const char* label, SceneRuntime::ScriptObjectReference& value,
                              const ProjectCatalog*);
        /// <summary>配列の内容と要素数を編集します。</summary>
        static bool DrawValue(EditState& state, const char* label, SceneRuntime::ScriptValue::Array& value,
                              const ProjectCatalog* catalog);
        /// <summary>名前付きの構造化フィールドを編集します。</summary>
        static bool DrawValue(EditState& state, const char* label, SceneRuntime::ScriptValue::Object& value,
                              const ProjectCatalog* catalog);
    };
}
