#include <Engine/Core/Application.h>
#include <Engine/Core/CrashHandler.h>
#include <Engine/Core/Log.h>
#include <Engine/Graphics/DirectX12Renderer.h>
#include <Engine/Platform/Window.h>

namespace Engine
{
    Application::~Application()
    {
        Shutdown();
    }

    int Application::Run()
    {
        int exitCode = 1;
        {
            // 描画機能を先に破棄し、その後にウィンドウを破棄します。
            Window window;
            DirectX12Renderer renderer;
            if (Initialize(window, renderer))
            {
                constexpr std::array<float, 4> backgroundColor{ 0.08f, 0.20f, 0.40f, 1.0f };
                while (window.ProcessMessages(exitCode))
                {
                    if (!renderer.Render(backgroundColor))
                    {
                        exitCode = 1;
                        break;
                    }
                }
            }
        }
        Shutdown();
        return exitCode;
    }

    bool Application::Initialize(Window& window, DirectX12Renderer& renderer)
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

        if (!window.Create(L"WP1") || !renderer.Initialize(window.GetHandle()))
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
