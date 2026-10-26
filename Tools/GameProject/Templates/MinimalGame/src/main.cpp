#include <Player/Player.h>
#include <filesystem>
#include <string_view>
#include <Engine/Graphics/ShaderCompiler.h>
#include <Engine/Core/Log.h>
#include <Windows.h>
#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int)
{
    const std::wstring_view arguments(commandLine ? commandLine : L"");
    if (arguments == L"--validate-package")
        return Player::ValidatePackage();
    if (arguments.starts_with(L"--prepare-shaders"))
    {
        int count = 0;
        auto tokens = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!tokens)
            return 1;
        const bool valid = count == 3 && std::wstring_view(tokens[1]) == L"--prepare-shaders";
        const auto content = valid ? std::filesystem::path(tokens[2]) : std::filesystem::path{};
        LocalFree(tokens);
        std::string error;
        if (!valid || !Engine::PrepareShaderCache(content / "Shaders", error))
        {
            Engine::Log::Error(error);
            return 1;
        }
        return 0;
    }
    return Player::Run();
}
