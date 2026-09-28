#include <Engine/Core/Application.h>
#include <Windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    Engine::Application application;
    return application.Run();
}
