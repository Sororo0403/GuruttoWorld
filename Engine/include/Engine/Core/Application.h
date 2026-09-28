#pragma once

namespace Engine
{
    class Window;
    class DirectX12Renderer;

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
        /// <returns>ウィンドウの終了コード。初期化や描画に失敗した場合は 1。</returns>
        int Run();

    private:
        /// <summary>
        /// クラッシュ処理、ログ、ウィンドウ、描画機能を順番に初期化します。
        /// </summary>
        /// <param name="window">初期化するウィンドウ。</param>
        /// <param name="renderer">初期化する描画機能。</param>
        /// <returns>すべての初期化に成功した場合は true、失敗した場合は false。</returns>
        bool Initialize(Window& window, DirectX12Renderer& renderer);

        /// <summary>
        /// 初期化済みのログとクラッシュ処理を終了します。描画機能とウィンドウの破棄後に呼び出します。
        /// </summary>
        void Shutdown();

        bool logInitialized_ = false;
        bool crashHandlerInitialized_ = false;
    };
}
