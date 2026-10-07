#include "ObjectPanel.h"
#include "PrefabPanel.h"
#include "PanelLayout.h"
#include <algorithm>
#include <numbers>
#include <iterator>
#include <Engine/Core/Log.h>

namespace
{
    void TrackInspectorEdit(Editor::EditState& state)
    {
        if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("inspector/"+std::to_string(ImGui::GetItemID()));
    }
}

namespace Editor
{
    void ObjectPanel::Draw(SceneRuntime::SceneWorld& world, EditState& state, bool enabled, const ProjectCatalog* catalog)
    {
        editsEnabled_=enabled;
        catalog_=catalog;
        DrawObjects(world, state, enabled);
        if (state.InspectedAsset().empty()) DrawInspector(world, state, enabled);
    }

    void ObjectPanel::DrawObjects(SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        if (state.SelectedIds().empty()) anchorId_.clear();
        PanelLayout::Place(PanelLayout::Panel::Objects);
        if (ImGui::Begin("ヒエラルキー###Objects"))
        {
            filter_.Draw("検索###Search",-1);
            ImGui::Text("オブジェクト：%zu個／選択：%zu個",world.Layout().objects.size(),state.SelectedIds().size());
            ImGui::BeginDisabled(!enabled);
            if (ImGui::Button("空のオブジェクトを作成###Create empty")) state.Request({ObjectAction::AddEmpty,{},{},{}});
            ImGui::SameLine();
            if (ImGui::Button("シーン設定###Scene settings")) state.Select({});
            ImGui::Selectable("ここにドロップして親を解除###Drop here to make root",false);
            DrawReparentTarget(world,state,{});
            if (!parentError_.empty()) ImGui::TextWrapped("%s",parentError_.c_str());
            if (ImGui::BeginChild("Object list",ImVec2(0,0))) DrawHierarchy(world,state,enabled);
            ImGui::EndChild();
            ImGui::EndDisabled();
        }
        ImGui::End();
    }

    std::vector<HierarchyRow> ObjectPanel::VisibleRows(const SceneRuntime::SceneLayout& layout) const
    {
        if (!filter_.IsActive()) return BuildHierarchyRows(layout,collapsed_);
        std::vector<HierarchyRow> rows;
        for (size_t index=0;index<layout.objects.size();++index)
        {
            const auto& object=layout.objects[index];
            if (filter_.PassFilter((object.name+" "+object.id+" "+object.Model().generic_string()).c_str())) rows.push_back({index,0,false});
        }
        return rows;
    }

    void ObjectPanel::DrawHierarchy(SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        const auto rows=VisibleRows(world.Layout());
        std::vector<std::string> visible;
        std::transform(rows.begin(),rows.end(),std::back_inserter(visible),
            [&](const auto& row) { return world.Layout().objects[row.index].id; });
        ImGui::BeginDisabled(!enabled);
        for (const auto& row : rows) DrawRow(world,state,row,visible);
        ImGui::EndDisabled();
    }

    void ObjectPanel::DrawRow(SceneRuntime::SceneWorld& world, EditState& state,
        const HierarchyRow& row, const std::vector<std::string>& visible)
    {
        const auto& object=world.Layout().objects[row.index];
        const float left=ImGui::GetCursorPosX();
        ImGui::SetCursorPosX(left+static_cast<float>(row.depth)*ImGui::GetTreeNodeToLabelSpacing());
        ImGui::PushID(object.id.c_str());
        auto flags=ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (!row.children) flags |= ImGuiTreeNodeFlags_Leaf;
        if (state.IsSelected(object.id)) flags |= ImGuiTreeNodeFlags_Selected;
        const bool open=ImGui::TreeNodeEx("##object",flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) SelectObject(state,visible,object.id);
        if (row.children)
        {
            if (open) collapsed_.erase(object.id); else collapsed_.insert(object.id);
        }
        auto text=ImGui::GetItemRectMin();
        text.x+=ImGui::GetTreeNodeToLabelSpacing();
        text.y+=(ImGui::GetItemRectSize().y-ImGui::GetTextLineHeight())*0.5f;
        ImGui::GetWindowDrawList()->AddText(text,ImGui::GetColorU32(ImGuiCol_Text),object.name.c_str());
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("WP1_HIERARCHY_OBJECT",object.id.c_str(),object.id.size()+1);
            ImGui::TextUnformatted(object.name.c_str());
            ImGui::EndDragDropSource();
        }
        DrawReparentTarget(world,state,object.id);
        ImGui::PopID();
    }

    void ObjectPanel::DrawReparentTarget(SceneRuntime::SceneWorld& world, EditState& state, const std::string& parent)
    {
        if (!editsEnabled_ || !ImGui::BeginDragDropTarget()) return;
        if (const auto* payload=ImGui::AcceptDragDropPayload("WP1_HIERARCHY_OBJECT"))
        {
            const auto* id=static_cast<const char*>(payload->Data);
            if (payload->DataSize>1 && id[payload->DataSize-1]=='\0' &&
                std::char_traits<char>::length(id)==static_cast<size_t>(payload->DataSize-1)) ChangeParent(world,state,id,parent);
        }
        ImGui::EndDragDropTarget();
    }

    void ObjectPanel::ChangeParent(SceneRuntime::SceneWorld& world, EditState& state,
        const std::string& id, const std::string& parent)
    {
        if (!editsEnabled_) return;
        if (!state.SetParent(world,id,parent,parentError_)) Engine::Log::Warning("Reparent: "+parentError_);
    }

    void ObjectPanel::DrawParent(SceneRuntime::SceneWorld& world, EditState& state, const SceneRuntime::ScenePlacement& placement)
    {
        const auto preview=placement.parentId.empty() ? std::string("〈親なし〉") : placement.parentId;
        if (ImGui::BeginCombo("親###Parent",preview.c_str()))
        {
            if (ImGui::Selectable("〈親なし〉",placement.parentId.empty())) ChangeParent(world,state,placement.id,{});
            for (const auto& candidate : world.Layout().objects)
            {
                if (candidate.id==placement.id) continue;
                ImGui::PushID(candidate.id.c_str());
                const auto text=ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##parent",candidate.id==placement.parentId)) ChangeParent(world,state,placement.id,candidate.id);
                ImGui::GetWindowDrawList()->AddText(text,ImGui::GetColorU32(ImGuiCol_Text),candidate.name.c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (!parentError_.empty()) ImGui::TextWrapped("%s",parentError_.c_str());
    }

    void ObjectPanel::SelectObject(EditState& state, const std::vector<std::string>& visible, const std::string& id)
    {
        const auto& io=ImGui::GetIO();
        if (io.KeyShift) state.SelectRange(visible,anchorId_,id,io.KeyCtrl);
        else { state.Select(id,io.KeyCtrl); anchorId_=id; }
    }

    void ObjectPanel::DrawInspector(SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        const auto& objects = world.Layout().objects;
        PanelLayout::Place(PanelLayout::Panel::Inspector);
        if (ImGui::Begin("インスペクター###Inspector"))
        {
            const auto found = std::find_if(objects.begin(), objects.end(),
                [&](const auto& object) { return object.id == state.SelectedId(); });
            if (state.SelectedIds().size()>1)
            {
                DrawMultiInspector(world,state,enabled);
            }
            else if (found == objects.end()) DrawSettings(world,state,enabled);
            else
            {
                ImGui::PushID(found->id.c_str());
                ImGui::BeginDisabled(!enabled);
                DrawName(world, state, *found);
                ImGui::Text("ID: %s", found->id.c_str());
                DrawParent(world,state,*found);
                ImGui::Separator();
                DrawTransform(world, state, *found);
                ComponentPanel::Draw(state,*found,catalog_);
                DrawPrefabOverrides(state,world.Layout(),*found);
                if (ImGui::Button("複製###Duplicate")) state.Request(state.DuplicateSelectionRequest());
                ImGui::SameLine();
                if (ImGui::Button("削除###Delete")) state.Request(state.DeleteSelectionRequest());
                ImGui::EndDisabled();
                ImGui::PopID();
                if (state.InvalidTransform())
                    ImGui::TextWrapped("変換値が不正です。有限の数値とゼロ以外のスケールを指定してください。");
            }
            ImGui::Separator();
            ImGui::TextWrapped(state.HasChanges() ? "未保存の変更があります。" :
                "未保存の変更はありません。");
        }
        ImGui::End();
    }

    void ObjectPanel::DrawSettings(SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        ImGui::BeginDisabled(!enabled);
        ImGui::TextUnformatted("シーン設定");
        auto settings=world.Layout().settings;
        bool settingsEdited=ImGui::ColorEdit4("背景色###Background",settings.background.data());
        TrackInspectorEdit(state);
        const auto label=settings.mainCamera.empty() ? std::string("最初の有効なカメラ") : settings.mainCamera;
        if (ImGui::BeginCombo("ゲームカメラ###Game camera",label.c_str()))
        {
            if (ImGui::Selectable("最初の有効なカメラ",settings.mainCamera.empty()))
            { settings.mainCamera.clear(); settingsEdited=true; }
            for (const auto& object : world.Layout().objects)
            {
                if (!object.camera) continue;
                if (ImGui::Selectable((object.name+"##"+object.id).c_str(),settings.mainCamera==object.id))
                { settings.mainCamera=object.id; settingsEdited=true; }
            }
            ImGui::EndCombo();
        }
        settingsEdited=ImGui::Checkbox("霧を有効化###Fog enabled",&settings.fog.enabled) || settingsEdited;
        settingsEdited=ImGui::ColorEdit3("霧の色###Fog color",settings.fog.color.data()) || settingsEdited;
        TrackInspectorEdit(state);
        settingsEdited=ImGui::DragFloat("霧の開始距離###Fog start",&settings.fog.start,0.1f,0,settings.fog.end-0.001f,"%.2f",ImGuiSliderFlags_AlwaysClamp) || settingsEdited;
        TrackInspectorEdit(state);
        settingsEdited=ImGui::DragFloat("霧の終了距離###Fog end",&settings.fog.end,0.1f,settings.fog.start+0.001f,1000000,"%.2f",ImGuiSliderFlags_AlwaysClamp) || settingsEdited;
        TrackInspectorEdit(state);
        settingsEdited=ImGui::SliderFloat("霧の強さ###Fog strength",&settings.fog.strength,0,1) || settingsEdited;
        TrackInspectorEdit(state);
        if (settingsEdited)
        {
            TrackInspectorEdit(state);
            ObjectRequest request;
            request.action=ObjectAction::Settings;
            request.settings=settings;
            request.interaction=state.Interaction();
            state.Request(std::move(request));
        }
        TrackInspectorEdit(state);
        ImGui::EndDisabled();
    }

    void ObjectPanel::DrawMultiInspector(SceneRuntime::SceneWorld& world, EditState& state, bool enabled)
    {
        ImGui::Text("%zu個のオブジェクトを選択中",state.SelectedIds().size());
        ImGui::Text("アクティブID：%s",state.SelectedId().c_str());
        const auto& objects=world.Layout().objects;
        const auto found=std::find_if(objects.begin(),objects.end(),[&](const auto& object) { return object.id==state.SelectedId(); });
        if (found==objects.end()) return;
        DirectX::XMFLOAT4X4 matrix;
        if (!world.WorldMatrix(found->id,matrix)) return;
        const std::array<float,3> previous{matrix._41,matrix._42,matrix._43};
        auto position=previous;
        ImGui::PushID(found->id.c_str());
        ImGui::BeginDisabled(!enabled);
        if (ImGui::DragFloat3("アクティブ対象の位置###Active position",position.data(),0.05f,0,0,"%.3f") && position!=previous)
        {
            const std::array<float,3> delta{position[0]-previous[0],position[1]-previous[1],position[2]-previous[2]};
            state.TranslateSelectionWorld(world,delta);
        }
        TrackInspectorEdit(state);
        if (ImGui::Button("選択対象を複製###Duplicate selected")) state.Request(state.DuplicateSelectionRequest());
        ImGui::SameLine();
        if (ImGui::Button("選択対象を削除###Delete selected")) state.Request(state.DeleteSelectionRequest());
        ImGui::EndDisabled();
        ImGui::PopID();
        ImGui::TextWrapped("移動・回転・拡縮の中心はアクティブ対象です。回転・拡縮はシーンのギズモで操作します。拡縮はアクティブ対象のローカル軸を使い、対象間の間隔も変えます。複製・削除は選択全体に適用します。");
        if (state.InvalidTransform()) ImGui::TextWrapped("移動できません。対応範囲内の有限の座標を指定してください。");
    }

    void ObjectPanel::DrawName(SceneRuntime::SceneWorld& world, EditState& state,
        const SceneRuntime::ScenePlacement& placement)
    {
        if (inspectedId_!=placement.id || observedName_!=placement.name)
        {
            inspectedId_=placement.id;
            observedName_=placement.name;
            nameBuffer_.assign(placement.name.begin(), placement.name.end());
            nameBuffer_.resize(std::max(size_t{1024},placement.name.size()+256), '\0');
            invalidName_=false;
        }
        if (ImGui::InputText("名前###Name", nameBuffer_.data(), nameBuffer_.size()))
        {
            invalidName_=!state.Rename(world,placement.id,nameBuffer_.data());
            if (!invalidName_) observedName_=placement.name;
        }
        TrackInspectorEdit(state);
        if (ImGui::IsItemDeactivatedAfterEdit() && invalidName_)
        {
            observedName_.clear(); // Restore the last valid name on the next frame.
        }
        if (invalidName_) ImGui::TextWrapped("名前には空白以外の文字を含めてください。");
    }

    void ObjectPanel::DrawTransform(SceneRuntime::SceneWorld& world, EditState& state,
        const SceneRuntime::ScenePlacement& placement)
    {
        if (!ImGui::CollapsingHeader("トランスフォーム###Transform", ImGuiTreeNodeFlags_DefaultOpen)) return;
        ImGui::TextUnformatted("座標：ローカル（親からの相対値）");
        auto position=placement.position, rotation=placement.rotation, scale=placement.scale;
        constexpr float ToDegrees=180.0f/std::numbers::pi_v<float>;
        auto degrees=rotation;
        std::transform(degrees.begin(),degrees.end(),degrees.begin(), [](float value) { return value*ToDegrees; });
        bool edited=ImGui::DragFloat3("ローカル位置###Local position",position.data(),0.05f,0,0,"%.3f");
        TrackInspectorEdit(state);
        if (ImGui::DragFloat3("ローカル回転（度）###Local rotation (deg)",degrees.data(),0.5f,0,0,"%.2f"))
        {
            for (size_t i=0;i<3;++i) rotation[i]=degrees[i]/ToDegrees;
            edited=true;
        }
        TrackInspectorEdit(state);
        edited |= ImGui::DragFloat3("ローカルスケール###Local scale",scale.data(),0.05f,0,0,"%.3f");
        TrackInspectorEdit(state);
        if (edited && (position!=placement.position || rotation!=placement.rotation || scale!=placement.scale))
            state.SetLocalTransform(world,placement.id,position,rotation,scale);
        if (ImGui::Button("トランスフォームをリセット###Reset Transform")) state.ResetTransform(world,placement.id);
    }

}
