#include "GameProject.h"

namespace Editor
{
    auto GameProject::Open(std::filesystem::path path) -> std::filesystem::path
    {
        const auto recoveryRoot = path.filename() == "WP1.project.json" ? path.parent_path() : path;
        const auto conflicts = Engine::FileJournal::Recover(recoveryRoot);
        if (!conflicts.empty())
            Engine::Log::Warning("保存を復旧し、外部内容を保管しました：" +
                                 Engine::FileContents::Text(conflicts.front().parent_path()));
        const auto context = Engine::ProjectContext::Load(path);
        context.RequireEngine(Engine::ProjectContext::SourceEngineRoot());
        path = context.root;
        if (!std::filesystem::is_regular_file(path / (context.name + ".vcxproj")))
            throw std::runtime_error("Invalid game project descriptor.");
        if (!std::filesystem::is_regular_file(context.engineRoot / "Tools/ProjectTool/ProjectTool.vcxproj"))
            throw std::runtime_error("Game project engine checkout is missing.");
        if (!std::filesystem::is_regular_file(path / "Assets/Project.json") ||
            !std::filesystem::is_directory(
                Engine::ContentFiles::Resolve(path / "Engine/Shaders/Mesh.hlsl").parent_path()))
            throw std::runtime_error("Game project content is missing.");
        SceneRuntime::ProjectSettings::Load(path);
        return path;
    }

    auto GameProject::Choose() -> std::optional<std::filesystem::path>
    {
        std::array<wchar_t, 32768> file{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.lpstrFilter = L"WP1 project\0WP1.project.json\0\0";
        dialog.lpstrFile = file.data();
        dialog.nMaxFile = static_cast<DWORD>(file.size());
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog))
        {
            if (CommDlgExtendedError())
                throw std::runtime_error("Cannot open project file dialog.");
            return std::nullopt;
        }
        return Open(file.data());
    }

    auto GameProject::BrowseAndOpen() -> void
    {
        const auto selected = Choose();
        if (!selected)
            return;
        const auto& root = *selected;
        std::array<wchar_t, 32768> executable{};
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size())
            throw std::runtime_error("Cannot locate Editor.exe.");
        const auto arguments = L"--project \"" + root.wstring() + L"\"";
        if (reinterpret_cast<INT_PTR>(
                ShellExecuteW(nullptr, L"open", executable.data(), arguments.c_str(), nullptr, SW_SHOWNORMAL)) <= 32)
            throw std::runtime_error("Cannot open game project.");
    }
}
