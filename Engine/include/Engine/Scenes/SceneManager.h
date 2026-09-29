#pragma once
#include <Engine/Scenes/ISceneFactory.h>

namespace Engine
{
    class SceneManager final
    {
    public:
        /// <summary>
        /// Factory を参照します。Factory は本クラスより長く生存させてください。
        /// </summary>
        explicit SceneManager(ISceneFactory& factory);
        /// <summary>
        /// GPU 処理完了後に破棄してください。Application::Run 終了後なら安全です。
        /// </summary>
        ~SceneManager() = default;
        /// <summary>
        /// 所有するシーンのコピーを禁止します。
        /// </summary>
        SceneManager(const SceneManager&) = delete;
        /// <summary>
        /// 所有するシーンのコピー代入を禁止します。
        /// </summary>
        SceneManager& operator=(const SceneManager&) = delete;
        /// <summary>
        /// 次の描画前に行う遷移を予約します。既に予約がある場合と空の識別子は false です。
        /// </summary>
        bool RequestChange(std::string name);
        /// <summary>
        /// 現在のシーンに更新を委譲します。予約中は更新しません。
        /// </summary>
        void Update(double deltaSeconds, const Keyboard& keyboard);
        /// <summary>
        /// GPU 完了待機後に予約した遷移を適用し、現在のシーンに描画を委譲します。
        /// </summary>
        RenderResult Draw(DirectX12Renderer& renderer);
        /// <summary>
        /// 現在のシーンの識別子を取得します。未開始時は空です。
        /// </summary>
        const std::string& GetActiveName() const;
    private:
        /// <summary>
        /// 新シーンの生成・初期化に成功した場合のみ旧シーンを置き換えます。
        /// </summary>
        bool ApplyPending(DirectX12Renderer& renderer);
        ISceneFactory& factory_;
        std::unique_ptr<IScene> current_;
        std::string activeName_;
        std::string pendingName_;
    };
}
