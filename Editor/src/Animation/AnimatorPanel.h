#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <SceneRuntime/Animation/Animator.h>
#include "../Project/ProjectCatalog.h"
#include "BlendTreePanel.h"
#include "AnimationEventPanel.h"
#include "IkPanel.h"
#include "../Assets/SkeletonPreview.h"
#include <imgui.h>
namespace Editor
{
    class AnimatorPanel final
    {
        /// <summary>文字列を編集します。</summary>
        static bool Text(const char* label, std::string& value);
        /// <summary>スケルトンの情報を読み込みます。</summary>
        static std::shared_ptr<const Engine::SkeletonData> LoadRig(const SceneRuntime::ScenePlacement& object,
                                                                   const ProjectCatalog* catalog);

        /// <summary>ルートモーションの編集画面を描画します。</summary>
        static bool DrawRootMotion(SceneRuntime::AnimatorComponent& animator, const Engine::SkeletonData* rig);

        /// <summary>状態名と遷移元の参照を更新します。</summary>
        static bool RenameState(SceneRuntime::AnimatorComponent& animator,
                                SceneRuntime::AnimatorStateDefinition& state);
        /// <summary>状態の編集画面を描画します。</summary>
        static bool DrawStates(SceneRuntime::AnimatorComponent& animator, const Engine::SkeletonData* rig);

        /// <summary>遷移元と遷移先の編集画面を描画します。</summary>
        static bool DrawTransitionEndpoints(const SceneRuntime::AnimatorComponent& animator,
                                            SceneRuntime::AnimatorTransition& transition);
        /// <summary>遷移の編集画面を描画します。</summary>
        static bool DrawTransitions(SceneRuntime::AnimatorComponent& animator);

    public:
        /// <summary>編集画面を描画します。</summary>
        static bool Draw(SceneRuntime::ScenePlacement& object, const ProjectCatalog* catalog);
    };
}
