#include <Engine/Core/Application.h>
#include <Engine/Core/CrashHandler.h>
#include <Engine/Core/Log.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Platform/Window.h>
#include <Engine/Input/Keyboard.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Engine
{
    Application::~Application()
    {
        Shutdown();
    }

    int Application::Run(const ApplicationSettings& settings, const ApplicationCallbacks& callbacks)
    {
        if (!callbacks.draw || settings.inactiveWaitMilliseconds == 0 ||
            !std::isfinite(settings.maxDeltaSeconds) || settings.maxDeltaSeconds <= 0.0)
        {
            return 1;
        }
        int exitCode = 1;
        {
            // 描画機能を先に破棄し、その後にウィンドウを破棄します。
            Window window;
            DirectX12Renderer renderer;
            Keyboard keyboard;
            if (Initialize(settings, window, renderer) && keyboard.Initialize(window.GetHandle()))
            {
                auto previousTime = std::chrono::steady_clock::now();
                const bool deferClose = callbacks.closeRequested && callbacks.shouldClose;
                while (window.ProcessMessages(exitCode, deferClose))
                {
                    if (deferClose && window.TakeCloseRequest())
                    {
                        if (IsIconic(window.GetHandle())) ShowWindow(window.GetHandle(), SW_RESTORE);
                        callbacks.closeRequested();
                    }
                    if (deferClose && callbacks.shouldClose()) break;
                    const auto currentTime = std::chrono::steady_clock::now();
                    const double elapsedSeconds = std::chrono::duration<double>(currentTime - previousTime).count();
                    const double deltaSeconds = std::min(elapsedSeconds, settings.maxDeltaSeconds);
                    previousTime = currentTime;
                    keyboard.Update();
                    if (callbacks.update)
                    {
                        callbacks.update(deltaSeconds, keyboard);
                    }
                    const RenderResult result = callbacks.draw(renderer);
                    if (result == RenderResult::Failed)
                    {
                        exitCode = 1;
                        break;
                    }
                    if (result == RenderResult::Paused)
                    {
                        MsgWaitForMultipleObjectsEx(0, nullptr, settings.inactiveWaitMilliseconds,
                            QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    }
                }
            }
        }
        Shutdown();
        return exitCode;
    }

    bool Application::Initialize(const ApplicationSettings& settings, Window& window, DirectX12Renderer& renderer)
    {
        if (!CrashHandler::Initialize())
        {
            Log::Error("Failed to initialize crash handling.");
            return false;
        }
        crashHandlerInitialized_ = true;

        if (!Log::Initialize())
        {
            Log::Error("Failed to initialize file logging.");
            return false;
        }
        logInitialized_ = true;
        Log::Info("Engine started.");

        if (!window.Create(settings.title.c_str(), settings.width, settings.height) || !renderer.Initialize(window.GetHandle()))
        {
            return false;
        }
        window.Show();
        Log::Info("Window opened.");
        return true;
    }

    void Application::Shutdown()
    {
        if (logInitialized_)
        {
            Log::Info("Engine stopped.");
            Log::Shutdown();
            logInitialized_ = false;
        }
        if (crashHandlerInitialized_)
        {
            CrashHandler::Shutdown();
            crashHandlerInitialized_ = false;
        }
    }
}
