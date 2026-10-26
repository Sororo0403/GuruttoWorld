#pragma once
#include <SceneRuntime/Animation/Animator.h>
#include <imgui.h>
#include <algorithm>
namespace Editor
{
    class IkPanel final
    {
        /// <summary>ボーンの選択項目を表示します。</summary>
        static bool Bone(const char* label, std::string& name, const Engine::SkeletonData* rig);
        /// <summary>制約を追加します。</summary>
        static bool AddConstraint(SceneRuntime::AnimatorComponent& animator, const Engine::SkeletonData* rig);

    public:
        /// <summary>編集画面を描画します。</summary>
        static bool Draw(SceneRuntime::AnimatorComponent& animator, const Engine::SkeletonData* rig);
    };
}
