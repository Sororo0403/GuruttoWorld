#pragma once
#include <SceneRuntime/Animator.h>
#include <imgui.h>
#include <algorithm>

namespace Editor
{
    class BlendTreePanel final
    {
        /// <summary>文字列を編集します。</summary>
        static bool Text(const char* label,std::string& value)
        {
            std::vector<char> buffer(std::max(size_t(1024),value.size()+1)); std::copy(value.begin(),value.end(),buffer.begin());
            if (!ImGui::InputText(label,buffer.data(),buffer.size())) return false;
            value=buffer.data(); return true;
        }
        /// <summary>別のTreeを選んだ時に参照元を更新します。</summary>
        static void Rename(SceneRuntime::AnimatorComponent& animator,const std::string& previous,const std::string& name)
        {
            for (auto& state : animator.states) if (state.blendTree==previous) state.blendTree=name;
            for (auto& tree : animator.blendTrees) for (auto& child : tree.children) if (child.blendTree==previous) child.blendTree=name;
        }
        /// <summary>モードを変更し、重複した初期座標を解消します。</summary>
        static void ChangeType(SceneRuntime::AnimatorBlendTree& tree,SceneRuntime::AnimatorBlendType type)
        {
            tree.type=type;
            if (type==SceneRuntime::AnimatorBlendType::Cartesian2D && tree.parameterX=="speed") tree.parameterX="moveX";
            if (type==SceneRuntime::AnimatorBlendType::Cartesian2D && tree.parameterY=="MoveForward") tree.parameterY="moveY";
            for (size_t index=0;index<tree.children.size();++index)
            {
                auto& child=tree.children[index];
                size_t number=0;
                while (std::any_of(tree.children.begin(),tree.children.begin()+index,[&](const auto& other) { return child.threshold==other.threshold; })) child.threshold=static_cast<float>(number++);
                number=0;
                while (std::any_of(tree.children.begin(),tree.children.begin()+index,[&](const auto& other) { return child.position==other.position; })) child.position={static_cast<float>(number++),0};
            }
        }
        /// <summary>未使用の閾値・座標・重み名を持つ子を追加します。</summary>
        static void AddChild(SceneRuntime::AnimatorBlendTree& tree,const Engine::SkeletonData* rig)
        {
            SceneRuntime::AnimatorBlendMotion child; size_t number=0;
            do { child.threshold=static_cast<float>(number); child.position={static_cast<float>(number),0}; child.parameter="weight"+std::to_string(number++); }
            while (std::any_of(tree.children.begin(),tree.children.end(),[&](const auto& other) { return child.threshold==other.threshold || child.position==other.position || child.parameter==other.parameter; }));
            if (rig && !rig->clips.empty()) child.clip=rig->clips[std::min(tree.children.size(),rig->clips.size()-1)].name;
            tree.children.push_back(std::move(child));
        }
    public:
        static bool RenameParameter(SceneRuntime::AnimatorComponent& animator,const std::string& previous,const std::string& name)
        {
            if (previous==name) return false;
            if (name.empty() || name.size()>128 || !animator.parameters.contains(previous) || animator.parameters.contains(name)) return false;
            auto node=animator.parameters.extract(previous); node.key()=name; animator.parameters.insert(std::move(node));
            for (auto& transition : animator.transitions) if (transition.parameter==previous) transition.parameter=name;
            for (auto& tree : animator.blendTrees) {
                if (tree.parameterX==previous) tree.parameterX=name;
                if (tree.parameterY==previous) tree.parameterY=name;
                for (auto& child : tree.children) if (child.parameter==previous) child.parameter=name;
            }
            return true;
        }
        /// <summary>入力が指定されていない時に使う保存済みパラメーターを編集します。</summary>
        static bool Parameters(SceneRuntime::AnimatorComponent& animator)
        {
            bool edited=false; ImGui::PushID("parameters");
            std::string remove,oldName,newName;
            for (auto& [name,value] : animator.parameters)
            {
                ImGui::PushID(name.c_str()); std::string candidate=name;
                if (Text("パラメーター名###Name",candidate) && (candidate==name || !animator.parameters.contains(candidate))) { oldName=name; newName=candidate; edited=true; }
                edited=ImGui::DragFloat("保存値###Value",&value,.01f,-1000000,1000000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited;
                if (ImGui::Button("パラメーターを削除###Remove")) remove=name;
                ImGui::PopID();
            }
            if (!oldName.empty() && oldName!=newName) edited=RenameParameter(animator,oldName,newName) || edited;
            if (!remove.empty()) { animator.parameters.erase(remove); edited=true; }
            if (animator.parameters.size()<64 && ImGui::Button("パラメーターを追加###Add"))
            {
                size_t number=1; std::string name;
                do { name="Parameter"+std::to_string(number++); } while (animator.parameters.contains(name));
                animator.parameters.emplace(name,0.0f); edited=true;
            }
            ImGui::PopID(); return edited;
        }
        /// <summary>クリップ・初期姿勢・入れ子のTreeを選択します。</summary>
        static bool Motion(const char* label,std::string& clip,std::string& tree,const SceneRuntime::AnimatorComponent& animator,const Engine::SkeletonData* rig,const std::string& exclude={})
        {
            bool edited=false; const auto preview=tree.empty() ? (clip.empty() ? std::string("初期姿勢") : clip) : "Tree: "+tree;
            if (ImGui::BeginCombo(label,preview.c_str()))
            {
                if (ImGui::Selectable("初期姿勢",clip.empty() && tree.empty())) { clip.clear(); tree.clear(); edited=true; }
                if (rig) for (const auto& candidate : rig->clips)
                    if (ImGui::Selectable(("Clip: "+candidate.name).c_str(),tree.empty() && candidate.name==clip)) { clip=candidate.name; tree.clear(); edited=true; }
                for (const auto& candidate : animator.blendTrees)
                    if (candidate.name!=exclude && ImGui::Selectable(("Tree: "+candidate.name).c_str(),tree==candidate.name)) { tree=candidate.name; clip.clear(); edited=true; }
                ImGui::EndCombo();
            }
            return edited;
        }
        /// <summary>Treeの方式・パラメーター・子・速度・参照を編集します。</summary>
        static bool Draw(SceneRuntime::AnimatorComponent& animator,const Engine::SkeletonData* rig)
        {
            bool edited=false; ImGui::PushID("blendTrees");
            ImGui::TextWrapped("1Dは閾値の間、2Dは座標間で混ぜます。Directは各パラメーターの正の値を重みにします。子の速度は共通サイクルの長さに反映します。moveX・moveYは左右・前後の入力です。");
            for (size_t index=0;index<animator.blendTrees.size();++index)
            {
                ImGui::PushID(static_cast<int>(index)); auto& tree=animator.blendTrees[index];
                if (ImGui::TreeNode("tree","Blend Tree: %s",tree.name.c_str()))
                {
                    const auto previous=tree.name;
                    if (Text("名前###Name",tree.name)) { Rename(animator,previous,tree.name); edited=true; }
                    const char* types[]{"1D","2D","Direct"}; int type=static_cast<int>(tree.type);
                    if (ImGui::Combo("方式###Type",&type,types,3)) { ChangeType(tree,static_cast<SceneRuntime::AnimatorBlendType>(type)); edited=true; }
                    if (tree.type!=SceneRuntime::AnimatorBlendType::Direct) edited=Text("パラメーター X###X",tree.parameterX) || edited;
                    if (tree.type==SceneRuntime::AnimatorBlendType::Cartesian2D) edited=Text("パラメーター Y###Y",tree.parameterY) || edited;
                    for (size_t childIndex=0;childIndex<tree.children.size();++childIndex)
                    {
                        ImGui::PushID(static_cast<int>(childIndex)); auto& child=tree.children[childIndex];
                        edited=Motion("Motion###Motion",child.clip,child.blendTree,animator,rig,tree.name) || edited;
                        if (tree.type==SceneRuntime::AnimatorBlendType::OneDimensional) edited=ImGui::InputFloat("閾値###Threshold",&child.threshold) || edited;
                        if (tree.type==SceneRuntime::AnimatorBlendType::Cartesian2D) edited=ImGui::InputFloat2("座標###Position",child.position.data()) || edited;
                        if (tree.type==SceneRuntime::AnimatorBlendType::Direct) edited=Text("重みパラメーター###Weight",child.parameter) || edited;
                        edited=ImGui::DragFloat("子の速度###Speed",&child.speed,.01f,.001f,1000,"%.3f",ImGuiSliderFlags_AlwaysClamp) || edited;
                        if (tree.children.size()>1 && ImGui::Button("子を削除###Remove")) { tree.children.erase(tree.children.begin()+childIndex); edited=true; ImGui::PopID(); break; }
                        ImGui::Separator(); ImGui::PopID();
                    }
                    if (tree.children.size()<32 && ImGui::Button("子を追加###Add")) { AddChild(tree,rig); edited=true; }
                    if (ImGui::Button("Treeを削除（参照は初期姿勢へ）###Remove Tree"))
                    {
                        const auto name=tree.name; Rename(animator,name,""); animator.blendTrees.erase(animator.blendTrees.begin()+index);
                        edited=true; ImGui::TreePop(); ImGui::PopID(); break;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (animator.blendTrees.size()<32 && ImGui::Button("Blend Treeを追加###Add Tree"))
            {
                SceneRuntime::AnimatorBlendTree tree; size_t number=1;
                do { tree.name="Blend"+std::to_string(number++); } while (std::ranges::any_of(animator.blendTrees,[&](const auto& other) { return other.name==tree.name; }));
                AddChild(tree,rig); AddChild(tree,rig); animator.blendTrees.push_back(std::move(tree)); edited=true;
            }
            ImGui::PopID(); return edited;
        }
    };
}
