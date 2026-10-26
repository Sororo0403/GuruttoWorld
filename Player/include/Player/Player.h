#pragma once
#include <functional>
#include <memory>
#include <string>

namespace Engine
{
    class Keyboard;
    class SceneManager;
    class DirectX12Renderer;
    enum class RenderResult;
}
namespace SceneRuntime
{
    class SceneEnvironment;
    class PlaybackFeature;
}
namespace Player
{
    struct Hooks
    {
        // 未指定の更新・描画には共通のシーン処理を使用します。
        std::function<void(double, const Engine::Keyboard&, Engine::SceneManager&, SceneRuntime::SceneEnvironment&)>
            update;
        std::function<Engine::RenderResult(Engine::DirectX12Renderer&, Engine::SceneManager&,
                                           SceneRuntime::SceneEnvironment&)>
            draw;
        // 両方指定すると、ゲーム側が終了を受理するまで実行を続けます。
        std::function<void()> closeRequested;
        std::function<bool()> shouldClose;
    };

    struct Options
    {
        std::string scene;
        bool simulateInactive = false;
        Hooks hooks;
        std::function<std::unique_ptr<SceneRuntime::PlaybackFeature>()> playbackFeature;
    };

    /// <summary>実行ファイルに同梱したプロジェクトを読み、ゲーム側のフックと共通の更新・描画ループを実行します。</summary>
    /// <param name="options">起動シーンの上書きと、ゲーム側で所有する診断処理などのフック。</param>
    /// <returns>ウィンドウの終了コード。準備や描画に失敗した場合は1。</returns>
    int Run(const Options& options = {});

    /// <summary>実行ファイルに同梱した起動シーンとスクリプトを読み、描画資源を検証します。</summary>
    /// <returns>検証に成功した場合は0、読み込みや描画に失敗した場合は1。</returns>
    int ValidatePackage();
}
