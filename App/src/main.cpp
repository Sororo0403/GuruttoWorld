#include "Game.h"
#include "PackageValidation.h"
#include <Windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int)
{
    auto arguments=std::wstring_view(commandLine ? commandLine : L"");
    while (!arguments.empty() && (arguments.front()==L' ' || arguments.front()==L'\t')) arguments.remove_prefix(1);
    while (!arguments.empty() && (arguments.back()==L' ' || arguments.back()==L'\t')) arguments.remove_suffix(1);
    if (arguments==L"--validate-package") return App::ValidatePackage();
    App::Game game;
    return game.Run();
}
