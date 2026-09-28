#include <Engine/Engine.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/CrashHandler.h>

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
        Log::Info("Engine stopped.");
        Log::Shutdown();
        CrashHandler::Shutdown();
        return 0;
    }
}
