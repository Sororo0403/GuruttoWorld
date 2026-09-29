#include <Engine/Core/DiagnosticPaths.h>
#include <Engine/Platform/Window.h>
#include <ShlObj.h>
#include <memory>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace Engine
{
    std::filesystem::path GetDiagnosticsRoot()
    {
        PWSTR directory = nullptr;
        const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &directory);
        const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(directory, CoTaskMemFree);
        if (FAILED(result) || directory == nullptr) return {};
        return std::filesystem::path(directory) / "WP1";
    }
}
