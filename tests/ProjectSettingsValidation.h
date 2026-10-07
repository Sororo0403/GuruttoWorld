#pragma once
#include <SceneRuntime/ProjectSettings.h>

namespace ProjectSettingsValidation
{
    inline void Run()
    {
        using SceneRuntime::ProjectSettings;
        const auto require=[](bool success,const char* message) {if(!success) throw std::runtime_error(message);};
        const auto root=std::filesystem::absolute("generated/tests/project-settings")/std::to_string(GetTickCount64());
        std::filesystem::create_directories(root/"Assets/Scenes");
        require(ProjectSettings::Load(root).startupScene=="Assets/Scenes/TitleStreet.json","missing project settings use compatible startup");
        std::ofstream(root/"Assets/Scenes/Custom.json")<<"{}";
        ProjectSettings settings;
        settings.title="編集したゲーム"; settings.startupScene="Assets/Scenes/Custom.json";
        settings.inputActions["Jump"].keys={DIK_J};
        settings.width=1600; settings.height=900; settings.Save(root);
        const auto loaded=ProjectSettings::Load(root);
        require(loaded.title==settings.title && loaded.startupScene==settings.startupScene &&
            loaded.width==1600 && loaded.height==900 && loaded.inputActions==settings.inputActions,"editor project settings survive save and App load");
        settings.width=0;
        bool rejected=false;
        try {settings.Save(root);} catch(const std::exception&) {rejected=true;}
        require(rejected && ProjectSettings::Load(root).width==1600,"invalid project save preserves previous settings");
        require(!ProjectSettings::ScenePath("Assets/Scenes/../Custom.json") &&
            !ProjectSettings::ScenePath("C:/Custom.json") && !ProjectSettings::ScenePath("Assets/Scenes/a\\..\\b.json"),"startup scene traversal rejected");
        settings.width=1280; settings.startupScene="Assets/Scenes/Missing.json";
        rejected=false; try {settings.Save(root);} catch(const std::exception&) {rejected=true;}
        require(rejected && ProjectSettings::Load(root).startupScene==loaded.startupScene,"missing startup scene cannot replace saved project");
        const Engine::Json malformed={{"title","Bad"},{"startupScene",loaded.startupScene},{"width",1280.5},{"height",720}};
        std::ofstream(root/"Assets/Project.json")<<malformed.dump();
        rejected=false; try {static_cast<void>(ProjectSettings::Load(root));} catch(const std::exception&) {rejected=true;}
        require(rejected,"fractional project dimensions are rejected");
        auto overflowing=malformed; overflowing["width"]=4294968576ULL;
        std::ofstream(root/"Assets/Project.json")<<overflowing.dump();
        rejected=false; try {static_cast<void>(ProjectSettings::Load(root));} catch(const std::exception&) {rejected=true;}
        require(rejected,"overflowing project dimensions cannot wrap into a valid size");
    }
}
