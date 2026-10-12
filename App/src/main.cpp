#include "Game.h"
#include "PackageValidation.h"
#include <Windows.h>
#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int)
{
    auto arguments=std::wstring_view(commandLine ? commandLine : L"");
    while (!arguments.empty() && (arguments.front()==L' ' || arguments.front()==L'\t')) arguments.remove_prefix(1);
    while (!arguments.empty() && (arguments.back()==L' ' || arguments.back()==L'\t')) arguments.remove_suffix(1);
    if (arguments==L"--validate-package") return App::ValidatePackage();
    if (arguments.starts_with(L"--profile-")) {
        App::GameProfileOptions profile;
        int count = 0;
        auto tokens = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!tokens) return 1;
        bool valid = true;
        for (int index = 1; index < count; ++index) {
            const std::wstring_view token(tokens[index]);
            if (token == L"--profile-park") profile.scene = "Assets/Scenes/ParkAdventure.json";
            else if (token == L"--profile-title") profile.scene = "Assets/Scenes/TitleStreet.json";
            else if (token == L"--profile-simulate-inactive") profile.simulateInactive = true;
            else if (token.starts_with(L"--profile-output=")) profile.output = std::wstring(token.substr(17));
            else if (token.starts_with(L"--profile-frames=")) {
                const auto number = token.substr(17);
                unsigned int frames = 0;
                if (number.empty()) valid = false;
                for (const auto digit : number) {
                    if (digit < L'0' || digit > L'9' || frames > 10000) { valid = false; break; }
                    frames = frames * 10 + static_cast<unsigned int>(digit - L'0');
                }
                if (frames == 0 || frames > 100000) valid = false;
                profile.frames = frames;
            } else valid = false;
        }
        LocalFree(tokens);
        if (!valid || profile.scene.empty() || profile.output.empty()) return 1;
        return App::Game::Run(&profile);
    }
    App::Game game;
    return game.Run();
}
