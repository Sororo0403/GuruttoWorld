#pragma once
#include <SceneRuntime/Scene/SceneEdits.h>

namespace SceneRuntime
{
    /// <summary>編集要求の所有者を表し、UIから描画や実行の具体型を隠します。</summary>
    class SceneEditTarget
    {
    public:
        /// <summary>派生した編集対象を解放します。</summary>
        virtual ~SceneEditTarget() = default;
        /// <summary>編集と履歴の正本を読み取ります。</summary>
        virtual const SceneLayout& Layout() const = 0;
        /// <summary>候補を準備して全体を確定し、成功した変更だけを返します。</summary>
        virtual SceneEditResult Edit(const SceneEdit& edit, std::string& error) = 0;
    };
}
