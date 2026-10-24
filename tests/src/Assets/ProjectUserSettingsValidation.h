#pragma once
#include <SceneRuntime/Project/GameSettings.h>
#include <SceneRuntime/Project/ProjectSettings.h>
#include <atomic>
#include <thread>

namespace ProjectUserSettingsValidation
{
    /// <summary>個人領域に触れず、ゲーム別の識別と旧設定の移行・競合・失敗を検証します。</summary>
    inline void Run(const std::filesystem::path& fixture)
    {
        using SceneRuntime::GameSettings;
        using SceneRuntime::ProjectSettings;
        const auto require = [](bool result, const char* message)
        {
            if (!result)
                throw std::runtime_error(message);
        };
        require(!std::filesystem::exists(fixture), "Choose a new user-settings fixture directory");
        const auto write = [](const std::filesystem::path& path, const std::string& text)
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path, std::ios::binary);
            output << text;
            output.close();
            if (!output)
                throw std::runtime_error("Cannot write user-settings fixture");
        };
        const auto read = [](const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(input), {});
        };
        const auto makeProject = [&](const std::filesystem::path& root, bool import)
        {
            write(root / "Assets/Scenes/Main.json", "{}");
            ProjectSettings project;
            project.title = "Same display title";
            project.startupScene = "Assets/Scenes/Main.json";
            project.importLegacySettings = import;
            project.Save(root);
            return Engine::AssetDatabase::Read(root / "Assets/Project.json").id;
        };
        const auto gameA = fixture / "GameA";
        const auto gameB = fixture / "GameB";
        const auto idA = makeProject(gameA, true);
        const auto idB = makeProject(gameB, false);
        require(idA != idB, "New projects have distinct persistent asset IDs");
        const auto users = fixture / "User";
        GameSettings legacy{3, false};
        legacy.values = {{"custom", 42.0f}};
        legacy.volumeGain = .24f;
        require(legacy.Save(users / "settings.json"), "Write legacy JSON settings");
        const auto original = read(users / "settings.json");
        const auto pathA = GameSettings::UserPath(gameA, users);
        const auto pathB = GameSettings::UserPath(gameB, users);
        const auto migrated = GameSettings::Load(pathA);
        require(pathA == users / "games" / idA / "settings.json" && pathA != pathB,
                "Equal titles do not share settings");
        require(migrated.loaded && migrated.volume == 3 && !migrated.backgroundMotion &&
                    migrated.values == legacy.values && migrated.volumeGain == legacy.volumeGain,
                "Import every legacy JSON setting");
        require(!GameSettings::Load(pathB).loaded && !std::filesystem::exists(pathB),
                "Other games never import legacy settings implicitly");
        require(read(users / "settings.json") == original, "Keep the original shared settings unchanged");
        require(GameSettings{7, true}.Save(pathA) && GameSettings{1, false}.Save(pathB),
                "Save independent game settings");
        static_cast<void>(GameSettings::UserPath(gameA, users));
        require(GameSettings::Load(pathA).volume == 7 && GameSettings::Load(pathB).volume == 1,
                "Repeated startup preserves existing per-game settings");

        auto project = ProjectSettings::Load(gameA);
        project.title = "Renamed title";
        project.Save(gameA);
        require(ProjectSettings::Load(gameA).importLegacySettings && GameSettings::UserPath(gameA, users) == pathA,
                "Editor saves preserve migration policy and project identity");
        const auto package = fixture / "RelocatedPackage";
        std::filesystem::create_directories(package / "Assets");
        std::filesystem::copy_file(gameA / "Assets/Project.json", package / "Assets/Project.json");
        std::filesystem::copy_file(gameA / "Assets/Project.json.meta", package / "Assets/Project.json.meta");
        require(GameSettings::UserPath(package, users) == pathA, "Packaging and relocation preserve user settings");
        write(pathA, "invalid current settings");
        static_cast<void>(GameSettings::UserPath(gameA, users));
        require(read(pathA) == "invalid current settings", "Do not replace an existing corrupt game settings file");

        const auto textUsers = fixture / "TextUser";
        write(textUsers / "settings.txt", "WP1_SETTINGS 1 4 0");
        const auto textTarget = GameSettings::UserPath(gameA, textUsers);
        require(GameSettings::Load(textTarget).volume == 4 && !GameSettings::Load(textTarget).backgroundMotion &&
                    read(textUsers / "settings.txt") == "WP1_SETTINGS 1 4 0",
                "Legacy text imports as JSON and keeps the original text");
        const auto scopedUsers = fixture / "ScopedTextUser";
        write(scopedUsers / "settings.txt", "WP1_SETTINGS 1 9 1");
        const auto scopedJson = scopedUsers / "games" / idA / "settings.json";
        auto scopedText = scopedJson;
        scopedText.replace_extension(".txt");
        write(scopedText, "WP1_SETTINGS 1 2 0");
        require(GameSettings::Load(GameSettings::UserPath(gameA, scopedUsers)).volume == 2 &&
                    !std::filesystem::exists(scopedJson),
                "Existing game-local text takes precedence over global legacy settings");
        const auto invalidUsers = fixture / "InvalidLegacy";
        write(invalidUsers / "settings.json", "invalid legacy settings");
        require(!std::filesystem::exists(GameSettings::UserPath(gameA, invalidUsers)), "Ignore invalid legacy data");

        const auto blockedUsers = fixture / "BlockedUser";
        require(legacy.Save(blockedUsers / "settings.json"), "Write legacy settings for a failed migration");
        write(blockedUsers / "games", "directory blocker");
        bool rejected = false;
        try
        {
            static_cast<void>(GameSettings::UserPath(gameA, blockedUsers));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected && GameSettings::Load(blockedUsers / "settings.json").volume == legacy.volume,
                "Report failed migration while retaining legacy settings");

        const auto concurrentUsers = fixture / "ConcurrentUser";
        require(legacy.Save(concurrentUsers / "settings.json"), "Write concurrent migration source");
        std::atomic<bool> failed = false;
        std::vector<std::jthread> threads;
        for (int i = 0; i < 8; ++i)
            threads.emplace_back(
                [&]
                {
                    try
                    {
                        static_cast<void>(GameSettings::UserPath(gameA, concurrentUsers));
                    }
                    catch (...)
                    {
                        failed = true;
                    }
                });
        const auto concurrentTarget = concurrentUsers / "games" / idA / "settings.json";
        require(GameSettings{8, true}.Save(concurrentTarget), "Save while startup migration races");
        for (auto& thread : threads)
            thread.join();
        require(!failed && GameSettings::Load(concurrentTarget).volume == 8,
                "Concurrent migration never overwrites a normal save");
        for (const auto& entry : std::filesystem::recursive_directory_iterator(concurrentUsers))
            require(entry.path().filename().string().find(".import.") == std::string::npos,
                    "Concurrent migration leaves no temporary files");

        write(gameB / "Assets/Project.json.meta", "{\"id\":\"../../escape\"}");
        rejected = false;
        try
        {
            static_cast<void>(GameSettings::UserPath(gameB, users));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected, "Reject unsafe project IDs before using them as directory names");
        require(GameSettings::UserPath(gameA, {}).empty(), "An unavailable user directory never falls back to the CWD");
    }
}
