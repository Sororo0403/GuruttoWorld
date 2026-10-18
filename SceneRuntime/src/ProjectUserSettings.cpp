#include <SceneRuntime/GameSettings.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Core/DiagnosticPaths.h>
#include <atomic>

namespace
{
    bool HasSettings(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
            return false;
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("Game settings destination must be a regular file");
        return true;
    }

    void ImportLegacySettings(const std::filesystem::path& userRoot, const std::filesystem::path& destination)
    {
        auto textPath = destination;
        textPath.replace_extension(".txt");
        if (HasSettings(destination) || HasSettings(textPath))
            return;
        const auto settings = SceneRuntime::GameSettings::Load(userRoot / "settings.json");
        if (!settings.loaded)
            return;

        // 完成した一時ファイルを上書き禁止で公開し、並行起動や通常保存との競合から既存設定を守ります。
        static std::atomic<unsigned long long> sequence = 0;
        auto temporary = destination;
        temporary += ".import." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(sequence++);
        struct Cleanup
        {
            std::filesystem::path path;
            ~Cleanup()
            {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
                auto incomplete = path;
                incomplete += ".tmp";
                std::filesystem::remove(incomplete, ignored);
            }
        } cleanup{temporary};
        if (!settings.Save(temporary))
            throw std::runtime_error("Cannot import legacy game settings; the original file was retained");
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) && !HasSettings(destination) &&
            !HasSettings(textPath))
            throw std::runtime_error("Cannot publish imported game settings; the original file was retained");
    }
}

namespace SceneRuntime
{
    std::filesystem::path GameSettings::UserPath(const std::filesystem::path& content)
    {
        return UserPath(content, Engine::GetDiagnosticsRoot());
    }

    std::filesystem::path GameSettings::UserPath(const std::filesystem::path& content,
                                                 const std::filesystem::path& userRoot)
    {
        if (userRoot.empty())
            return {};
        const auto project = content / "Assets/Project.json";
        std::ifstream input(project);
        if (!input)
            throw std::runtime_error("Cannot read project settings for the user settings location");
        const auto configuration = Engine::Json::parse(input);
        const auto identity = Engine::AssetDatabase::Read(project).id;
        if (!Engine::AssetDatabase::ValidId(identity))
            throw std::runtime_error("Project.json needs a stable asset ID for its user settings location");
        const auto destination = userRoot / "games" / identity / "settings.json";
        if (configuration.value("importLegacySettings", false))
            ImportLegacySettings(userRoot, destination);
        return destination;
    }
}
