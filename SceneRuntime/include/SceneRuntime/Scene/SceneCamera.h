#pragma once
namespace SceneRuntime
{
    struct SceneLayout;
    struct ScenePlacement;
    class SceneCamera final
    {
    public:
        /// <summary>保存設定で指定した有効な主カメラを取得します。</summary>
        static const ScenePlacement* Find(const SceneLayout& layout);
    };
}
