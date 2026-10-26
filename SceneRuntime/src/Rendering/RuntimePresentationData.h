#pragma once
#include <SceneRuntime/Scene/WorldState.h>

namespace SceneRuntime
{
    /// <summary>既存のUI・演出アダプターが必要とする定義だけを、不変な表示用の世代へ抽出します。</summary>
    class RuntimePresentationData final
    {
    public:
        /// <summary>UIと演出の設定を抽出します。Prefab・物理・Script・メッシュ設定は複製しません。</summary>
        static std::shared_ptr<const SceneLayout> Extract(WorldState::ReadView view);
        /// <summary>UIと演出の資源準備に使う定義が変更されたかを返します。</summary>
        static bool Changed(const SceneChangeSet& changes);
    };
}
