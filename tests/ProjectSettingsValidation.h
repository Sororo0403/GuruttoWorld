#pragma once
#include <SceneRuntime/ProjectSettings.h>

namespace ProjectSettingsValidation
{
    inline void Run()
    {
        using SceneRuntime::ProjectSettings;
        const auto require = [](bool success, const char* message)
        {
            if (!success)
                throw std::runtime_error(message);
        };
        const auto root = std::filesystem::absolute("generated/tests/project-settings") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Assets/Scenes");
        bool missingRejected = false;
        try
        {
            static_cast<void>(ProjectSettings::Load(root));
        }
        catch (const std::exception&)
        {
            missingRejected = true;
        }
        require(missingRejected, "missing settings never select a sample game");
        std::ofstream(root / "Assets/Scenes/Custom.json") << "{}";
        ProjectSettings settings;
        require(settings.title == "Game" && settings.startupScene.empty() && settings.sceneAliases.empty(),
                "shared settings contain no game-specific defaults");
        settings.title = "編集したゲーム";
        settings.startupScene = "Assets/Scenes/Custom.json";
        settings.sceneAliases = {{"play", settings.startupScene}};
        settings.inputActions["Jump"].keys = {DIK_J};
        settings.width = 1600;
        settings.height = 900;
        settings.Save(root);
        const auto loaded = ProjectSettings::Load(root);
        const auto savedSource = Engine::FileContents::Read(root / "Assets/Project.json");
        require(loaded.title == settings.title && loaded.startupScene == settings.startupScene &&
                    loaded.width == 1600 && loaded.height == 900 && loaded.inputActions == settings.inputActions &&
                    loaded.sceneAliases == settings.sceneAliases,
                "editor project settings survive save and App load");
        require(loaded.ResolveScene("play") == loaded.startupScene &&
                    loaded.ResolveScene(loaded.startupScene) == loaded.startupScene &&
                    loaded.ResolveScene("EngineDemo").empty(),
                "only project-defined aliases and explicit scene paths resolve");
        settings.width = 0;
        bool rejected = false;
        try
        {
            settings.Save(root);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected && ProjectSettings::Load(root).width == 1600,
                "invalid project save preserves previous settings");
        require(!ProjectSettings::ScenePath("Assets/Scenes/../Custom.json") &&
                    !ProjectSettings::ScenePath("C:/Custom.json") &&
                    !ProjectSettings::ScenePath("Assets/Scenes/a\\..\\b.json"),
                "startup scene traversal rejected");
        settings.width = 1280;
        settings.startupScene = "Assets/Scenes/Missing.json";
        rejected = false;
        try
        {
            settings.Save(root);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected && ProjectSettings::Load(root).startupScene == loaded.startupScene,
                "missing startup scene cannot replace saved project");
        const Engine::Json malformed = {
            {"title", "Bad"}, {"startupScene", loaded.startupScene}, {"width", 1280.5}, {"height", 720}};
        std::ofstream(root / "Assets/Project.json") << malformed.dump();
        rejected = false;
        try
        {
            static_cast<void>(ProjectSettings::Load(root));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected, "fractional project dimensions are rejected");
        auto overflowing = malformed;
        overflowing["width"] = 4294968576ULL;
        std::ofstream(root / "Assets/Project.json") << overflowing.dump();
        rejected = false;
        try
        {
            static_cast<void>(ProjectSettings::Load(root));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected, "overflowing project dimensions cannot wrap into a valid size");
        // 型検証用の外部変更を戻し、改名の検証を独立した保存基準から続けます。
        Engine::FileContents::Write(root / "Assets/Project.json", *savedSource);
        settings.startupScene = loaded.startupScene;
        Engine::AssetDatabase database(root);
        static_cast<void>(database.Ensure(root / settings.startupScene));
        settings.Save(root);
        database.Move(settings.startupScene, "Assets/Scenes/Renamed.json");
        std::ofstream(root / settings.startupScene) << "{}";
        static_cast<void>(database.Ensure(root / settings.startupScene));
        const auto renamed = ProjectSettings::Load(root);
        require(renamed.startupScene == "Assets/Scenes/Renamed.json" &&
                    renamed.ResolveScene("play") == renamed.startupScene,
                "startup and aliases follow asset IDs after rename and old-path reuse");
        auto invalidAlias = renamed;
        invalidAlias.sceneAliases["escape"] = "Assets/Scenes/../Escape.json";
        rejected = false;
        try
        {
            invalidAlias.Save(root);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected && ProjectSettings::Load(root).sceneAliases == renamed.sceneAliases,
                "invalid aliases cannot replace saved settings");
    }
}
