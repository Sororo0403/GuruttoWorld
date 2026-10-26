#pragma once
#include <SceneRuntime/Scene/WorldReadView.h>
#include <SceneRuntime/Scene/WorldState.h>

namespace SceneRuntime
{
    /// <summary>実行表を保存型へ丸ごと複製せず、IDによる既存の読み取り契約へ接続します。</summary>
    WorldReadView MakeWorldReadView(WorldState::ReadView view, const ScenePlacement* callbackOwner = nullptr);
}
