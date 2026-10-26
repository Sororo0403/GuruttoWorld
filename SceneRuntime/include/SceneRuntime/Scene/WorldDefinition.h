#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <memory>

namespace SceneRuntime
{
    /// <summary>実行の構築元を不変に保持します。通常の実行値の更新では作り直しません。</summary>
    class WorldDefinition final
    {
    public:
        /// <summary>保存データを検証し、編集元と独立した不変の定義を構築します。</summary>
        static std::shared_ptr<const WorldDefinition> Compile(SceneLayout source);
        /// <summary>構築・構造変更・互換変換に使用する不変の保存定義を取得します。</summary>
        const SceneLayout& Source() const noexcept;

    private:
        /// <summary>検証済みの定義を受け取り、公開後の変更を禁止します。</summary>
        explicit WorldDefinition(SceneLayout source);
        const SceneLayout source_;
    };
}
