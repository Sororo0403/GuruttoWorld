#pragma once

#include <functional>
#include <string>

namespace Engine
{
    class Window;
    class DirectX12Renderer;
    enum class RenderResult;

    struct ApplicationSettings
    {
        std::wstring title;
        int width = 1280;
        int height = 720;
        unsigned int inactiveWaitMilliseconds = 16;
        /// <summary>
        /// 更新に渡す経過秒数の上限。有限の正数を指定し、超過した時間は持ち越しません。
        /// </summary>
        double maxDeltaSeconds = 0.1;
    };

    struct ApplicationCallbacks
    {
        std::function<void(double)> update;
        std::function<RenderResult(DirectX12Renderer&)> draw;
    };

    class Application final
    {
    public:
        /// <summary>
        /// アプリケーションの実行を管理するオブジェクトを初期化します。
        /// </summary>
        Application() = default;

        /// <summary>
        /// 初期化済みのログとクラッシュ処理を終了します。
        /// </summary>
        ~Application();

        /// <summary>
        /// 初期化状態の二重管理を防ぐため、コピー生成を禁止します。
        /// </summary>
        Application(const Application&) = delete;

        /// <summary>
        /// 初期化状態の二重管理を防ぐため、コピー代入を禁止します。
        /// </summary>
        Application& operator=(const Application&) = delete;

        /// <summary>
        /// 初期化後、メッセージ処理と描画を繰り返し、終了時にリソースを解放します。
        /// メインスレッドから呼び出してください。
        /// </summary>
        /// <param name="settings">アプリ層が指定するウィンドウ設定、非表示時の待機時間、更新時間の上限。</param>
        /// <param name="callbacks">更新と描画の処理。更新には上限を適用した経過秒数を渡します。描画は必須です。</param>
        /// <returns>ウィンドウの終了コード。初期化や描画に失敗した場合は 1。</returns>
        int Run(const ApplicationSettings& settings, const ApplicationCallbacks& callbacks);

    private:
        /// <summary>
        /// クラッシュ処理、ログ、ウィンドウ、描画機能を順番に初期化します。
        /// </summary>
        /// <param name="settings">アプリ層が指定する初期化設定。</param>
        /// <param name="window">初期化するウィンドウ。</param>
        /// <param name="renderer">初期化する描画機能。</param>
        /// <returns>すべての初期化に成功した場合は true、失敗した場合は false。</returns>
        bool Initialize(const ApplicationSettings& settings, Window& window, DirectX12Renderer& renderer);

        /// <summary>
        /// 初期化済みのログとクラッシュ処理を終了します。描画機能とウィンドウの破棄後に呼び出します。
        /// </summary>
        void Shutdown();

        bool logInitialized_ = false;
        bool crashHandlerInitialized_ = false;
    };
}
