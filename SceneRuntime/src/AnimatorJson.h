#pragma once
#include <SceneRuntime/Animator.h>
#include <Engine/Core/Json.h>

namespace SceneRuntime
{
    /// <summary>旧形式とBlend Treeを含むAnimatorを読み込み、構造を検証します。</summary>
    AnimatorComponent ReadAnimator(const Engine::Json& component,const std::string& id,bool enabled);
    /// <summary>Animatorの状態・遷移・Blend Treeを保存します。</summary>
    Engine::Json WriteAnimator(const AnimatorComponent& animator);
}
