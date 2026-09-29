#include "Game.h"
#include <Windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    App::Game game;
    return game.Run();
}
