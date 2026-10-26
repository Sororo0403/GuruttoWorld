#pragma once
#include <SceneRuntime/Animation/Animator.h>
#include <imgui.h>
#include <algorithm>

namespace Editor
{
    class BlendTreePanel final
    {
        /// <summary>文字列を編集します。</summary>
        static bool Text(const char* label, std::string& value);
        /// <summary>別のツリーを選んだ時に参照元を更新します。</summary>
        static void Rename(SceneRuntime::AnimatorComponent& animator, const std::string& previous,
                           const std::string& name);
        /// <summary>モードを変更し、重複した初期座標を解消します。</summary>
        static void ChangeType(SceneRuntime::AnimatorBlendTree& tree, SceneRuntime::AnimatorBlendType type);
        /// <summary>未使用の閾値・座標・重み名を持つ子を追加します。</summary>
        static void AddChild(SceneRuntime::AnimatorBlendTree& tree, const Engine::SkeletonData* rig);

    public:
        /// <summary>パラメーター名と参照元を更新します。</summary>
        static bool RenameParameter(SceneRuntime::AnimatorComponent& animator, const std::string& previous,
                                    const std::string& name);
        /// <summary>入力が指定されていない時に使う保存済みパラメーターを編集します。</summary>
        static bool Parameters(SceneRuntime::AnimatorComponent& animator);
        /// <summary>クリップ・初期姿勢・入れ子のツリーを選択します。</summary>
        static bool Motion(const char* label, std::string& clip, std::string& tree,
                           const SceneRuntime::AnimatorComponent& animator, const Engine::SkeletonData* rig,
                           const std::string& exclude = {});
        /// <summary>子要素の編集画面を描画します。</summary>
        static bool DrawChildren(SceneRuntime::AnimatorComponent& animator, SceneRuntime::AnimatorBlendTree& tree,
                                 const Engine::SkeletonData* rig);
        /// <summary>ツリーの方式・パラメーター・子・速度・参照を編集します。</summary>
        static bool Draw(SceneRuntime::AnimatorComponent& animator, const Engine::SkeletonData* rig);
    };
}
