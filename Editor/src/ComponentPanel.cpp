#include "ComponentWidgets.h"
#include "ComponentPanel.h"
#include "EnvironmentPanel.h"
#include "UiComponentPanel.h"
#include "AnimationPanel.h"
#include "AnimatorPanel.h"
#include "RagdollPanel.h"
#include "PhysicsPanel.h"
#include "GenreComponentPanel.h"
#include "ScriptDataPanel.h"
#include <SceneRuntime/ScriptRuntime.h>
#include <SceneRuntime/MaterialAsset.h>
#include <imgui.h>
#include <imgui_internal.h>

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

    bool ComponentPanel::DrawLods(SceneRuntime::ScenePlacement &candidate, const ProjectCatalog *catalog)
    {
        bool edited = false;
        auto &mesh = *candidate.meshRenderer;
        for (size_t index = 0; index < mesh.lods.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index)); auto &lod = mesh.lods[index];
            const float low = index ? mesh.lods[index - 1].distance + .01f : .01f;
            const float high = index + 1 < mesh.lods.size() ? mesh.lods[index + 1].distance - .01f : 100000;
            edited |= ImGui::DragFloat("LOD切替距離###LOD distance", &lod.distance, .1f, low, high, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::BeginCombo("LODモデル###LOD model", ProjectCatalog::Text(lod.model.filename()).c_str()))
            { edited |= ChooseModel(lod.model, catalog); ImGui::EndCombo(); }
            if (ImGui::Button("LODを削除###Remove LOD")) { mesh.lods.erase(mesh.lods.begin() + index); edited = true; ImGui::PopID(); break; }
            ImGui::PopID();
        }
        if (mesh.lods.size() < 8 && !candidate.animator && ImGui::Button("LODを追加###Add LOD"))
        { mesh.lods.push_back({mesh.lods.empty() ? 20.0f : std::min(100000.0f, mesh.lods.back().distance + 20), mesh.model}); edited = true; }
        return edited;
    }

    bool ComponentPanel::ChooseMaterial(std::filesystem::path &material, const ProjectCatalog *catalog, const char *emptyLabel)
    {
        bool edited = false;
        if (ImGui::Selectable(emptyLabel, material.empty())) {material.clear();edited = true;}
        if (catalog) for (const auto &asset : catalog->Assets()) if (asset.kind == AssetKind::Material)
                    if (ImGui::Selectable(ProjectCatalog::Text(asset.path).c_str(), asset.path == material)) {material = asset.path;edited = true;}
        return edited;
    }
    bool ComponentPanel::DrawMaterialSlots(EditState &state, SceneRuntime::MaterialComponent &material, const ProjectCatalog *catalog)
    {
        bool edited = false;
        if (ImGui::TreeNode("メッシュごとの割り当て###Material slots")) {
            ImGui::TextWrapped("スロット番号は読み込まれたメッシュの順序です。空欄は共通の割り当てを使用します。モデルを差し替えると対応する順序が変わることがあります。");
            for (size_t index = 0; index < material.slots.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                const auto slotLabel = material.slots[index].empty() ? std::string("共通の割り当て") : ProjectCatalog::Text(material.slots[index]);
                if (ImGui::BeginCombo(("メッシュ " + std::to_string(index) + "###Slot").c_str(), slotLabel.c_str())) {
                    edited |= ChooseMaterial(material.slots[index], catalog, "共通の割り当て");
                    ImGui::EndCombo();
                }
                if (!material.slots[index].empty() && ImGui::SmallButton("アセットを編集###Edit slot asset")) state.InspectAsset(material.slots[index]);
                ImGui::PopID();
            }
            if (material.slots.size() < 4096 && ImGui::Button("スロットを追加###Add material slot")) {material.slots.emplace_back();edited = true;}
            ImGui::SameLine();if (!material.slots.empty() && ImGui::Button("末尾のスロットを削除###Remove last material slot")) {material.slots.pop_back();edited = true;}
            ImGui::TreePop();
        }
        return edited;
    }

    bool ComponentPanel::DrawPlayerController(EditState &state, SceneRuntime::ScenePlacement &candidate)
    {
        bool edited = false;
        if (candidate.playerController && ImGui::CollapsingHeader("プレイヤー操作###PlayerController", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto &player = *candidate.playerController;
            ImGui::PushID(player.id.c_str());
            edited |= ImGui::Checkbox("有効###Enabled", &player.enabled);
            edited |= ImGui::DragFloat("移動速度###Move speed", &player.moveSpeed, 0.1f, 0, 1000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            Editor::TrackComponentInteraction(state);
            edited |= ImGui::Checkbox("重力を使用###Use gravity", &player.useGravity);
            edited |= ImGui::Checkbox("斜面・移動床対応の物理を使用###Use advanced physics", &player.usePhysics);
            if (player.usePhysics)
            {
                edited |= ImGui::SliderFloat("歩ける斜面角度###Max slope", &player.maxSlopeDegrees, 0, 89);
                Editor::TrackComponentInteraction(state);
                edited |= ImGui::DragFloat("登れる段差###Step height", &player.stepHeight, 0.01f, 0, 10, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                Editor::TrackComponentInteraction(state);
            }
            for (const auto &field : {std::pair{"重力加速度###Gravity", &player.gravity}, std::pair{"ジャンプ速度###Jump speed", &player.jumpSpeed}})
            {
                edited |= ImGui::DragFloat(field.first, field.second, 0.1f, 0, 1000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                Editor::TrackComponentInteraction(state);
            }
            ImGui::TextUnformatted("WASD / 矢印キー：移動、Space：接地中にジャンプ");
            if (!candidate.boxCollider || !candidate.boxCollider->enabled) ImGui::TextUnformatted("床や壁で止めるには有効な箱の衝突判定が必要です。");
            if (ImGui::Button("リセット###Reset PlayerController")) { const auto id = player.id; player = SceneRuntime::PlayerControllerComponent{}; player.id = id; edited = true; }
            ImGui::SameLine();
            if (ImGui::Button("削除###Remove PlayerController")) { candidate.playerController.reset(); edited = true; }
            ImGui::PopID();
        }
        return edited;
    }

    bool ComponentPanel::DrawScriptData(EditState &state, SceneRuntime::ScriptComponent &script, const SceneRuntime::ScriptDefinition &definition, const ProjectCatalog *catalog)
    {
        bool edited = false;
        for (const auto &[name, initial] : definition.dataFields) {
            auto value = script.data.contains(name) ? script.data.at(name) : initial;
            const auto purpose=definition.dataPurposes.find(name);
            const auto usage=purpose==definition.dataPurposes.end() ? SceneRuntime::ScriptDataPurpose::Text : purpose->second;
            if (ScriptDataPanel::Draw(state, name.c_str(), value, catalog, false,usage)) { script.data[name] = std::move(value); edited = true; }
        }
        SceneRuntime::ScriptValue::Object extras = script.data;
        for (const auto &[name, initial] : definition.dataFields) {static_cast<void>(initial);extras.erase(name);}
        SceneRuntime::ScriptValue extraValue{extras};
        if (ScriptDataPanel::Draw(state, "追加データ###Additional data", extraValue, catalog, false)) {
            for (const auto &[name, value] : extras) {static_cast<void>(value);script.data.erase(name);}
            for (auto &[name, value] : std::get<SceneRuntime::ScriptValue::Object>(extraValue.value))
                if (!definition.dataFields.contains(name)) script.data[name] = std::move(value);
            edited = true;
        }
        return edited;
    }
    bool ComponentPanel::DrawScripts(EditState &state, SceneRuntime::ScenePlacement &candidate, const ProjectCatalog *catalog)
    {
        bool edited = false;
        for (size_t index = 0; index < candidate.scripts.size();)
        {
            auto &script = candidate.scripts[index];
            ImGui::PushID(script.id.c_str());
            bool remove = false;
            if (ImGui::CollapsingHeader(("ゲーム処理：" + script.behaviour).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                edited |= ImGui::Checkbox("有効###Enabled", &script.enabled);
                const auto definition = SceneRuntime::ScriptRegistry::Definitions().find(script.behaviour);
                if (definition == SceneRuntime::ScriptRegistry::Definitions().end()) ImGui::TextUnformatted("このゲーム処理は登録されていません。設定は保持されます。");
                else
                {
                    for (const auto &[key, field] : definition->second.fields)
                    {
                        const auto found = script.parameters.find(key);
                        float value = found == script.parameters.end() ? field.initial : found->second;
                        if (ImGui::DragFloat(key.c_str(), &value, 0.05f, field.minimum, field.maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp))
                        { script.parameters[key] = value; edited = true; }
                        Editor::TrackComponentInteraction(state);
                    }
                    if (ImGui::Button("リセット###Reset Script"))
                    {
                        script.enabled = true; script.parameters.clear();
                        for (const auto &[key, field] : definition->second.fields) script.parameters[key] = field.initial;
                        script.data = definition->second.dataFields;
                        edited = true;
                    }
                    ImGui::SameLine();
                }
                if (definition != SceneRuntime::ScriptRegistry::Definitions().end()) {
                    edited |= DrawScriptData(state, script, definition->second, catalog);
                } else {
                    for (auto &[name, value] : script.data) edited |= ScriptDataPanel::Draw(state, name.c_str(), value, catalog);
                }
                remove = ImGui::Button("削除###Remove Script");
            }
            ImGui::PopID();
            if (remove) { candidate.scripts.erase(candidate.scripts.begin() + static_cast<std::ptrdiff_t>(index)); edited = true; }
            else ++index;
        }
        return edited;
    }

    bool ComponentPanel::AddVisualComponents(SceneRuntime::ScenePlacement &candidate, const ProjectCatalog *catalog)
    {
        bool edited = false;
        if (ImGui::MenuItem("マテリアル割り当て###Material", nullptr, false, !candidate.material))
        { candidate.material = SceneRuntime::MaterialComponent{NewComponentId(candidate, "material"), true, {}}; edited = true; }
        if (ImGui::BeginMenu("メッシュ描画###MeshRenderer", !candidate.meshRenderer && !candidate.terrain && !candidate.tilemap))
        {
            std::filesystem::path model;
            if (ChooseModel(model, catalog))
            {
                candidate.meshRenderer = SceneRuntime::MeshRendererComponent{NewComponentId(candidate, "mesh"), true, model};
                edited = true; ImGui::CloseCurrentPopup();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("自動回転###Rotator", nullptr, false, !candidate.rotator))
        {
            candidate.rotator = SceneRuntime::RotatorComponent{NewComponentId(candidate, "rotator"), true, {0, 90, 0}};
            edited = true;
        }
        if (ImGui::MenuItem("プレイヤー操作###PlayerController", nullptr, false, !candidate.playerController))
        { candidate.playerController = SceneRuntime::PlayerControllerComponent{NewComponentId(candidate, "player"), true, 5}; edited = true; }
        return edited;
    }

    bool ComponentPanel::AddPhysicsComponents(SceneRuntime::ScenePlacement &candidate)
    {
        bool edited = false;
        if (ImGui::MenuItem("箱の衝突判定###BoxCollider", nullptr, false, !candidate.boxCollider))
        { candidate.boxCollider = SceneRuntime::BoxColliderComponent{}; candidate.boxCollider->id = NewComponentId(candidate, "collider"); edited = true; }
        if (ImGui::MenuItem("剛体###RigidBody", nullptr, false, !candidate.rigidBody && !candidate.playerController))
        {
            candidate.rigidBody.emplace(); candidate.rigidBody->id = NewComponentId(candidate, "rigidbody");
            if (!candidate.boxCollider) { candidate.boxCollider.emplace(); candidate.boxCollider->id = NewComponentId(candidate, "collider"); }
            edited = true;
        }
        if (ImGui::MenuItem("Joint（剛体接続）###Joint", nullptr, false, !candidate.joint))
        {
            candidate.joint.emplace(); candidate.joint->id = NewComponentId(candidate, "joint");
            if (!candidate.rigidBody) candidate.rigidBody.emplace();
            if (!candidate.boxCollider) candidate.boxCollider.emplace();edited = true;
        }
        return edited;
    }

    bool ComponentPanel::AddScripts(SceneRuntime::ScenePlacement &candidate)
    {
        bool edited = false;
        if (ImGui::BeginMenu("ゲーム処理###Script"))
        {
            for (const auto &[name, definition] : SceneRuntime::ScriptRegistry::Definitions())
                if (ImGui::MenuItem(name.c_str(), nullptr, false, candidate.scripts.size() < 32))
                {
                    SceneRuntime::ScriptComponent script; script.id = NewComponentId(candidate, "script"); script.behaviour = name;
                    for (const auto &[key, field] : definition.fields) script.parameters[key] = field.initial;
                    script.data = definition.dataFields;
                    candidate.scripts.push_back(std::move(script)); edited = true;
                }
            ImGui::EndMenu();
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
        { edited |= ChooseModel(mesh.model,catalog); ImGui::EndCombo(); }
        ImGui::TextWrapped("%s",text.c_str());
        std::vector<char> condition(std::max(size_t(16385),mesh.visibleWhen.size()+1));
        std::copy(mesh.visibleWhen.begin(),mesh.visibleWhen.end(),condition.begin());
        if(ImGui::InputText("表示条件###Mesh visible when",condition.data(),condition.size()))
        { mesh.visibleWhen=condition.data(); edited=true; }
        edited |= ImGui::Checkbox("GPUインスタンシング###GPU instancing",&mesh.instancing);
        edited |= ImGui::Checkbox("GPU遮蔽カリング###GPU occlusion culling",&mesh.occlusionCulling);
        edited |= DrawLods(candidate,catalog);
        if (ImGui::Button("描画設定を初期化###Reset MeshRenderer"))
        { mesh.enabled=true; mesh.visibleWhen.clear(); mesh.lods.clear(); mesh.instancing=false; mesh.occlusionCulling=false; edited=true; }
        ImGui::SameLine();
        if (ImGui::Button("メッシュ描画を削除###Remove MeshRenderer")) { candidate.meshRenderer.reset(); candidate.animator.reset(); edited=true; }
        ImGui::PopID();
        return edited;
    }
    bool ComponentPanel::DrawMaterial(EditState& state,SceneRuntime::ScenePlacement& candidate,const ProjectCatalog* catalog)
    {
        if (!candidate.material || !ImGui::CollapsingHeader("マテリアル割り当て###Material",ImGuiTreeNodeFlags_DefaultOpen)) return false;
        auto& material=*candidate.material; ImGui::PushID(material.id.c_str());
        bool edited=ImGui::Checkbox("有効###Enabled",&material.enabled);
        edited |= ImGui::Checkbox("このオブジェクトの表面を上書き###Override surface",&material.surfaceOverride);
        if(material.surfaceOverride) {
            edited |= ImGui::ColorEdit4("色・透明度###Override color",material.color.data());
            edited |= ImGui::SliderFloat("粗さ###Override roughness",&material.roughness,.04f,1);
            edited |= ImGui::SliderFloat("演出の強さ###Effect amount",&material.effectAmount,0,1);
            edited |= ImGui::SliderFloat("演出の種類（0:黒 1:白 2:白黒）###Effect variant",&material.effectVariant,0,2,"%.0f");
            edited |= ImGui::SliderFloat("金属感###Override metallic",&material.metallic,0,1);
        }
        const auto label=material.asset.empty() ? std::string("モデルの設定") : ProjectCatalog::Text(material.asset.filename());
        if (ImGui::BeginCombo("Materialアセット###Material asset",label.c_str())) {
            edited |= ChooseMaterial(material.asset,catalog,"モデルの設定");
            ImGui::EndCombo();
        }
        if (!material.asset.empty() && ImGui::Button("Materialアセットを編集###Edit material asset")) state.InspectAsset(material.asset);
        edited |= DrawMaterialSlots(state,material,catalog);
        if (ImGui::Button("割り当てを初期化###Reset material binding")) {const auto id=material.id;material=SceneRuntime::MaterialComponent{};material.id=id;edited=true;}
        ImGui::SameLine();
        if (ImGui::Button("割り当てを削除###Remove material binding")) {candidate.material.reset(); edited=true;}
        if (!candidate.meshRenderer) ImGui::TextWrapped("メッシュ描画を追加すると、この割り当てで描画します。");
        ImGui::PopID(); return edited;
    }

    bool ComponentPanel::DrawRotator(EditState& state, SceneRuntime::ScenePlacement& candidate)
    {
        if (!candidate.rotator || !ImGui::CollapsingHeader("自動回転###Rotator",ImGuiTreeNodeFlags_DefaultOpen)) return false;
        auto& rotator=*candidate.rotator;
        ImGui::PushID(rotator.id.c_str());
        ImGui::Text("コンポーネントID：%s",rotator.id.c_str());
        bool edited=ImGui::Checkbox("有効###Enabled",&rotator.enabled);
        edited |= ImGui::DragFloat3("角速度（度/秒）###Angular velocity (deg/s)",rotator.angularVelocity.data(),0.5f,-100000,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        Editor::TrackComponentInteraction(state);
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
        edited |= AddVisualComponents(candidate,catalog);
        edited |= AddPhysicsComponents(candidate);
        edited |= AddScripts(candidate);
        if (ImGui::MenuItem("骨格アニメーション###Animator",nullptr,false,!candidate.animator && candidate.meshRenderer && (candidate.Model().extension()==".gltf" || candidate.Model().extension()==".glb")))
        { candidate.animator=SceneRuntime::AnimatorComponent{}; candidate.animator->id=NewComponentId(candidate,"animator"); edited=true; }
        edited |= EnvironmentPanel::Add(candidate);
        if(ImGui::MenuItem("ラグドール###Ragdoll",nullptr,false,candidate.animator && candidate.meshRenderer && !candidate.ragdoll)) {candidate.ragdoll.emplace(); candidate.ragdoll->id=NewComponentId(candidate,"ragdoll"); edited=true;}
        edited |= UiComponentPanel::Add(candidate);
        edited |= GenrePanel::Add(candidate);
        if (ImGui::MenuItem("アニメーション###Animation",nullptr,false,!candidate.animation))
        { candidate.animation=SceneRuntime::AnimationComponent{NewComponentId(candidate,"animation"),true,{}}; edited=true; }
        ImGui::EndPopup();
        return edited;
    }

    void ComponentPanel::Draw(EditState& state, const SceneRuntime::ScenePlacement& placement, const ProjectCatalog* catalog,const SceneRuntime::SceneLayout* layout)
    {
        auto candidate=placement;
        bool edited=DrawMesh(candidate,catalog);
        edited |= DrawMaterial(state,candidate,catalog);
        edited |= DrawRotator(state,candidate);
        edited |= AnimatorPanel::Draw(candidate,catalog);
        edited |= DrawRagdoll(state,candidate);
        edited |= DrawPlayerController(state,candidate);
        edited |= DrawPhysics(state,candidate,catalog);
        edited |= DrawScripts(state,candidate,catalog);
        edited |= EnvironmentPanel::Draw(state,candidate);
        edited |= UiComponentPanel::Draw(state,candidate,catalog);
        edited |= GenrePanel::Draw(state,candidate,catalog,layout);
        edited |= AnimationPanel::Draw(state,candidate);
        edited |= DrawAdd(candidate,catalog);
        if (ImGui::GetActiveID()!=0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            state.SetInteraction("component/"+std::to_string(ImGui::GetActiveID()));
        if (edited && !candidate.SameComponents(placement))
            state.RequestComponents(std::move(candidate),state.Interaction());
    }
}
