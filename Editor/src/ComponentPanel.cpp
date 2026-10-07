#include "ComponentPanel.h"
#include "EnvironmentPanel.h"
#include "UiComponentPanel.h"
#include "AnimationPanel.h"
#include <SceneRuntime/ScriptRuntime.h>
#include <imgui.h>

namespace
{
    std::string NewComponentId(const SceneRuntime::ScenePlacement& placement, const std::string& base)
    {
        std::string id=base;
        size_t counter=2;
        while (placement.HasComponentId(id)) id=base+"-"+std::to_string(counter++);
        return id;
    }
}

namespace Editor
{
    bool ComponentPanel::ChooseModel(std::filesystem::path& model, const ProjectCatalog* catalog)
    {
        if (!catalog) { ImGui::TextUnformatted("プロジェクトの一覧を更新するとモデルが表示されます。"); return false; }
        bool edited=false;
        for (const auto& asset : catalog->Assets())
        {
            if (asset.kind!=AssetKind::Model) continue;
            const auto path=ProjectCatalog::Text(asset.path);
            if (!path.starts_with("Assets/Models/")) continue;
            if (ImGui::Selectable(path.c_str(),asset.path==model)) { model=asset.path; edited=true; }
        }
        return edited;
    }

    bool ComponentPanel::DrawMesh(SceneRuntime::ScenePlacement& candidate, const ProjectCatalog* catalog)
    {
        if (!candidate.meshRenderer || !ImGui::CollapsingHeader("メッシュ描画###MeshRenderer",ImGuiTreeNodeFlags_DefaultOpen)) return false;
        auto& mesh=*candidate.meshRenderer;
        ImGui::PushID(mesh.id.c_str());
        ImGui::Text("コンポーネントID：%s",mesh.id.c_str());
        bool edited=ImGui::Checkbox("有効###Enabled",&mesh.enabled);
        const auto text=ProjectCatalog::Text(mesh.model);
        if (ImGui::BeginCombo("モデルアセット###Model asset",ProjectCatalog::Text(mesh.model.filename()).c_str()))
        { edited=ChooseModel(mesh.model,catalog) || edited; ImGui::EndCombo(); }
        ImGui::TextWrapped("%s",text.c_str());
        std::vector<char> condition(std::max(size_t(16385),mesh.visibleWhen.size()+1));
        std::copy(mesh.visibleWhen.begin(),mesh.visibleWhen.end(),condition.begin());
        if(ImGui::InputText("表示条件###Mesh visible when",condition.data(),condition.size()))
        { mesh.visibleWhen=condition.data(); edited=true; }
        if (ImGui::Button("メッシュ描画をリセット###Reset MeshRenderer")) { mesh.enabled=true; edited=true; }
        ImGui::SameLine();
        if (ImGui::Button("メッシュ描画を削除###Remove MeshRenderer")) { candidate.meshRenderer.reset(); edited=true; }
        ImGui::PopID();
        return edited;
    }

    bool ComponentPanel::DrawRotator(EditState& state, SceneRuntime::ScenePlacement& candidate)
    {
        if (!candidate.rotator || !ImGui::CollapsingHeader("自動回転###Rotator",ImGuiTreeNodeFlags_DefaultOpen)) return false;
        auto& rotator=*candidate.rotator;
        ImGui::PushID(rotator.id.c_str());
        ImGui::Text("コンポーネントID：%s",rotator.id.c_str());
        bool edited=ImGui::Checkbox("有効###Enabled",&rotator.enabled);
        edited=ImGui::DragFloat3("角速度（度/秒）###Angular velocity (deg/s)",rotator.angularVelocity.data(),0.5f,-100000,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp) || edited;
        if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit())
            state.SetInteraction("component/"+std::to_string(ImGui::GetItemID()));
        ImGui::TextUnformatted("再生中にローカルX・Y・Z軸で回転します。");
        if (ImGui::Button("自動回転をリセット###Reset Rotator"))
        { rotator.enabled=true; rotator.angularVelocity={0,90,0}; edited=true; }
        ImGui::SameLine();
        if (ImGui::Button("自動回転を削除###Remove Rotator")) { candidate.rotator.reset(); edited=true; }
        ImGui::PopID();
        return edited;
    }

    bool ComponentPanel::DrawAdd(SceneRuntime::ScenePlacement& candidate, const ProjectCatalog* catalog)
    {
        if (ImGui::Button("コンポーネントを追加###Add Component")) ImGui::OpenPopup("コンポーネントを選択###Add component");
        if (!ImGui::BeginPopup("コンポーネントを選択###Add component")) return false;
        bool edited=false;
        if (ImGui::BeginMenu("メッシュ描画###MeshRenderer",!candidate.meshRenderer))
        {
            std::filesystem::path model;
            if (ChooseModel(model,catalog))
            {
                candidate.meshRenderer=SceneRuntime::MeshRendererComponent{NewComponentId(candidate,"mesh"),true,model};
                edited=true; ImGui::CloseCurrentPopup();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("自動回転###Rotator",nullptr,false,!candidate.rotator))
        {
            candidate.rotator=SceneRuntime::RotatorComponent{NewComponentId(candidate,"rotator"),true,{0,90,0}};
            edited=true;
        }
        if (ImGui::MenuItem("プレイヤー操作###PlayerController",nullptr,false,!candidate.playerController))
        { candidate.playerController=SceneRuntime::PlayerControllerComponent{NewComponentId(candidate,"player"),true,5}; edited=true; }
        if (ImGui::MenuItem("箱の衝突判定###BoxCollider",nullptr,false,!candidate.boxCollider))
        { candidate.boxCollider=SceneRuntime::BoxColliderComponent{}; candidate.boxCollider->id=NewComponentId(candidate,"collider"); edited=true; }
        if (ImGui::BeginMenu("ゲーム処理###Script"))
        {
            for (const auto& [name,definition] : SceneRuntime::ScriptRegistry::Definitions())
                if (ImGui::MenuItem(name.c_str(),nullptr,false,candidate.scripts.size()<32))
                {
                    SceneRuntime::ScriptComponent script; script.id=NewComponentId(candidate,"script"); script.behaviour=name;
                    for (const auto& [key,field] : definition.fields) script.parameters[key]=field.initial;
                    candidate.scripts.push_back(std::move(script)); edited=true;
                }
            ImGui::EndMenu();
        }
        edited=EnvironmentPanel::Add(candidate) || edited;
        edited=UiComponentPanel::Add(candidate) || edited;
        if (ImGui::MenuItem("アニメーション###Animation",nullptr,false,!candidate.animation))
        { candidate.animation=SceneRuntime::AnimationComponent{NewComponentId(candidate,"animation"),true,{}}; edited=true; }
        ImGui::EndPopup();
        return edited;
    }

    void ComponentPanel::Draw(EditState& state, const SceneRuntime::ScenePlacement& placement, const ProjectCatalog* catalog)
    {
        auto candidate=placement;
        bool edited=DrawMesh(candidate,catalog);
        edited=DrawRotator(state,candidate) || edited;
        if (candidate.playerController && ImGui::CollapsingHeader("プレイヤー操作###PlayerController",ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& player=*candidate.playerController;
            ImGui::PushID(player.id.c_str());
            edited=ImGui::Checkbox("有効###Enabled",&player.enabled) || edited;
            edited=ImGui::DragFloat("移動速度###Move speed",&player.moveSpeed,0.1f,0,1000,"%.2f",ImGuiSliderFlags_AlwaysClamp) || edited;
            if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("component/"+std::to_string(ImGui::GetItemID()));
            edited=ImGui::Checkbox("重力を使用###Use gravity",&player.useGravity) || edited;
            for (const auto& field : {std::pair{"重力加速度###Gravity",&player.gravity},std::pair{"ジャンプ速度###Jump speed",&player.jumpSpeed}})
            {
                edited=ImGui::DragFloat(field.first,field.second,0.1f,0,1000,"%.2f",ImGuiSliderFlags_AlwaysClamp) || edited;
                if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("component/"+std::to_string(ImGui::GetItemID()));
            }
            ImGui::TextUnformatted("WASD / 矢印キー：移動、Space：接地中にジャンプ");
            if (!candidate.boxCollider || !candidate.boxCollider->enabled) ImGui::TextUnformatted("床や壁で止めるには有効な箱の衝突判定が必要です。");
            if (ImGui::Button("リセット###Reset PlayerController")) { const auto id=player.id; player=SceneRuntime::PlayerControllerComponent{}; player.id=id; edited=true; }
            ImGui::SameLine();
            if (ImGui::Button("削除###Remove PlayerController")) { candidate.playerController.reset(); edited=true; }
            ImGui::PopID();
        }
        if (candidate.boxCollider && ImGui::CollapsingHeader("箱の衝突判定###BoxCollider",ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& collider=*candidate.boxCollider;
            ImGui::PushID(collider.id.c_str());
            edited=ImGui::Checkbox("有効###Enabled",&collider.enabled) || edited;
            for (const auto& field : {std::pair{"中心###Collider center",&collider.center},std::pair{"大きさ###Collider size",&collider.size}})
            {
                const bool size=field.second==&collider.size;
                edited=ImGui::DragFloat3(field.first,field.second->data(),0.05f,size ? 0.001f : -100000,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited;
                if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("component/"+std::to_string(ImGui::GetItemID()));
            }
            ImGui::TextUnformatted("親の変形を継承する箱です。回転時は外接箱で判定します。");
            if (ImGui::Button("リセット###Reset BoxCollider")) { const auto id=collider.id; collider=SceneRuntime::BoxColliderComponent{}; collider.id=id; edited=true; }
            ImGui::SameLine();
            if (ImGui::Button("削除###Remove BoxCollider")) { candidate.boxCollider.reset(); edited=true; }
            ImGui::PopID();
        }
        for (size_t index=0;index<candidate.scripts.size();)
        {
            auto& script=candidate.scripts[index];
            ImGui::PushID(script.id.c_str());
            bool remove=false;
            if (ImGui::CollapsingHeader(("ゲーム処理："+script.behaviour).c_str(),ImGuiTreeNodeFlags_DefaultOpen))
            {
                edited=ImGui::Checkbox("有効###Enabled",&script.enabled) || edited;
                const auto definition=SceneRuntime::ScriptRegistry::Definitions().find(script.behaviour);
                if (definition==SceneRuntime::ScriptRegistry::Definitions().end()) ImGui::TextUnformatted("このゲーム処理は登録されていません。設定は保持されます。");
                else
                {
                    for (const auto& [key,field] : definition->second.fields)
                    {
                        const auto found=script.parameters.find(key);
                        float value=found==script.parameters.end() ? field.initial : found->second;
                        if (ImGui::DragFloat(key.c_str(),&value,0.05f,field.minimum,field.maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp))
                        { script.parameters[key]=value; edited=true; }
                        if (ImGui::IsItemActive() || ImGui::IsItemDeactivatedAfterEdit()) state.SetInteraction("component/"+std::to_string(ImGui::GetItemID()));
                    }
                    if (ImGui::Button("リセット###Reset Script"))
                    {
                        script.enabled=true; script.parameters.clear();
                        for (const auto& [key,field] : definition->second.fields) script.parameters[key]=field.initial;
                        edited=true;
                    }
                    ImGui::SameLine();
                }
                remove=ImGui::Button("削除###Remove Script");
            }
            ImGui::PopID();
            if (remove) { candidate.scripts.erase(candidate.scripts.begin()+static_cast<std::ptrdiff_t>(index)); edited=true; }
            else ++index;
        }
        edited=EnvironmentPanel::Draw(state,candidate) || edited;
        edited=UiComponentPanel::Draw(state,candidate,catalog) || edited;
        edited=AnimationPanel::Draw(state,candidate) || edited;
        edited=DrawAdd(candidate,catalog) || edited;
        if (edited && !candidate.SameComponents(placement))
            state.RequestComponents(std::move(candidate),state.Interaction());
    }
}
