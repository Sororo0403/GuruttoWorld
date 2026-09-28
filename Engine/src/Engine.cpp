#include <Engine/Engine.h>
#include <Engine/Core/Log.h>

namespace Engine
{
    int Run()
    {
        if (!Log::Initialize())
        {
            Log::Error("Failed to initialize file logging.");
            return 1;
        }

        Log::Info("Engine started.");
        Log::Info("Engine stopped.");
        Log::Shutdown();
        return 0;
    }
}
