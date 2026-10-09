#pragma once
#include "EditState.h"
#include <SceneRuntime/ScriptValue.h>
#include <imgui.h>
#include <array>
#include <algorithm>

namespace Editor
{
    class ScriptDataPanel final
    {
    public:
        /// <summary>型付きデータを編集し、連続操作を一回のUndoへまとめます。</summary>
        static bool Draw(EditState& state,const char* label,SceneRuntime::ScriptValue& field)
        {
            const bool edited=std::visit([&](auto& value) { return DrawValue(state,label,value); },field.value);
            if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit())
                state.SetInteraction("component/"+std::to_string(ImGui::GetItemID()));
            return edited;
        }
    private:
        /// <summary>数値を保存可能な範囲で編集します。</summary>
        static bool DrawValue(EditState&,const char* label,float& value)
        { return ImGui::DragFloat(label,&value,0.05f,-1000000,1000000,"%.3f",ImGuiSliderFlags_AlwaysClamp); }
        /// <summary>真偽値を編集します。</summary>
        static bool DrawValue(EditState&,const char* label,bool& value)
        { return ImGui::Checkbox(label,&value); }
        /// <summary>長さに上限のあるUTF-8文字列を編集します。</summary>
        template<size_t Capacity> static bool DrawString(const char* label,std::string& value)
        {
            std::array<char,Capacity> buffer{};
            std::copy_n(value.data(),std::min(value.size(),buffer.size()-1),buffer.data());
            if (!ImGui::InputText(label,buffer.data(),buffer.size())) return false;
            value=buffer.data(); return true;
        }
        /// <summary>通常の文字列を編集します。</summary>
        static bool DrawValue(EditState&,const char* label,std::string& value)
        { return DrawString<4097>(label,value); }
        /// <summary>参照IDの入力とHierarchyからのドラッグを処理します。</summary>
        static bool DrawValue(EditState&,const char* label,SceneRuntime::ScriptObjectReference& value)
        {
            bool edited=DrawString<129>(label,value.id);
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload=ImGui::AcceptDragDropPayload("WP1_HIERARCHY_OBJECT")) {
                    value.id=static_cast<const char*>(payload->Data); edited=true;
                }
                ImGui::EndDragDropTarget();
            }
            return edited;
        }
        /// <summary>配列の内容と要素数を編集します。</summary>
        static bool DrawValue(EditState& state,const char* label,SceneRuntime::ScriptValue::Array& value)
        {
            if (!ImGui::TreeNode(label)) return false;
            bool edited=false; ImGui::Text("%zu / 256",value.size());
            for (size_t index=0;index<value.size();++index) {
                ImGui::PushID(static_cast<int>(index));
                edited=Draw(state,std::to_string(index).c_str(),value[index]) || edited;
                ImGui::PopID();
            }
            ImGui::BeginDisabled(value.size()>=256);
            if (ImGui::Button("要素を追加###Add element")) {
                auto next=value.empty() ? SceneRuntime::ScriptValue{} : value.back(); value.push_back(std::move(next)); edited=true;
            }
            ImGui::EndDisabled(); ImGui::SameLine();
            ImGui::BeginDisabled(value.empty());
            if (ImGui::Button("末尾を削除###Remove element")) { value.pop_back(); edited=true; }
            ImGui::EndDisabled(); ImGui::TreePop(); return edited;
        }
        /// <summary>名前付きの構造化フィールドを編集します。</summary>
        static bool DrawValue(EditState& state,const char* label,SceneRuntime::ScriptValue::Object& value)
        {
            if (!ImGui::TreeNode(label)) return false;
            bool edited=false;
            for (auto& [name,child]:value) edited=Draw(state,name.c_str(),child) || edited;
            ImGui::TreePop(); return edited;
        }
    };
}
