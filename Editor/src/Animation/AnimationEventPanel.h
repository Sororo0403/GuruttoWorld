#pragma once
#include <SceneRuntime/Animation/Animator.h>
#include <imgui.h>
#include <algorithm>

namespace Editor
{
    class AnimationEventPanel final
    {
        /// <summary>イベント名と文字列引数を編集します。</summary>
        static bool Text(const char* label, std::string& value);
        /// <summary>クリップの編集画面を描画します。</summary>
        static std::pair<bool, float> DrawClip(SceneRuntime::AnimatorEventKey& key, const Engine::SkeletonData* rig);

    public:
        /// <summary>クリップ時刻・イベント名・各引数・最低ブレンド重みを編集します。</summary>
        static bool Draw(SceneRuntime::AnimatorComponent& animator, const Engine::SkeletonData* rig);
    };
}
