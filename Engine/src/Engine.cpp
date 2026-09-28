#include <Engine/Engine.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/CrashHandler.h>
#include <Engine/Platform/Window.h>

namespace Engine
{
    int Run()
    {
        if (!CrashHandler::Initialize())
        {
            Log::Error("Failed to initialize crash handling.");
            return 1;
        }

        if (!Log::Initialize())
        {
            Log::Error("Failed to initialize file logging.");
            CrashHandler::Shutdown();
            return 1;
        }

        Log::Info("Engine started.");
        int exitCode = 1;
        {
            Window window;
            if (window.Create(L"WP1"))
            {
                window.Show();
                Log::Info("Window opened.");
                exitCode = window.RunMessageLoop();
            }
        }
        Log::Info("Engine stopped.");
        Log::Shutdown();
        CrashHandler::Shutdown();
        return exitCode;
    }
}
