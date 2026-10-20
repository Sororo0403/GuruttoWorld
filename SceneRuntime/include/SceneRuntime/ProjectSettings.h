#pragma once
#include <Engine/Assets/AssetReferenceResolver.h>
#include <Engine/Core/Json.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Input/InputActions.h>
#include <filesystem>
#include <fstream>
#include <Engine/Platform/Window.h>
#include <map>
#include <string_view>
#include <algorithm>

namespace SceneRuntime
{
    struct ProjectSettings
    {
        std::string title = "Game";
        std::string startupScene;
        bool importLegacySettings = false;
        std::map<std::string, std::string> sceneAliases;
        int width = 1280, height = 720;
        Engine::InputActions::Bindings inputActions = Engine::InputActions::Defaults();

        static bool ScenePath(const std::string& value)
        {
            const auto path = std::filesystem::path(std::u8string(value.begin(), value.end()));
            return value.starts_with("Assets/Scenes/") && path.extension() == ".json" && !path.is_absolute() &&
                   !path.has_root_name() && value.find("..") == std::string::npos &&
                   value.find('\\') == std::string::npos && value.find(':') == std::string::npos &&
                   value.find('\0') == std::string::npos;
        }
        void Validate() const
        {
            Engine::InputActions::Validate(inputActions);
            if (title.empty() || title.size() > 1024 || title.find('\0') != std::string::npos ||
                !ScenePath(startupScene) || width < 320 || width > 8192 || height < 240 || height > 8192)
                throw std::runtime_error("Invalid project settings");
            if (sceneAliases.size() > 64)
                throw std::runtime_error("Too many scene aliases");
            for (const auto& [name, scene] : sceneAliases)
            {
                if (name.empty() || name.size() > 64 || !ScenePath(scene) ||
                    !std::all_of(name.begin(), name.end(),
                                 [](unsigned char c)
                                 {
                                     return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                            (c >= '0' && c <= '9') || c == '_' || c == '-';
                                 }))
                    throw std::runtime_error("Invalid scene alias");
            }
        }
        /// <summary>プロジェクトで定義した別名、または明示的なシーンパスを解決します。</summary>
        std::string ResolveScene(std::string_view name) const
        {
            if (ScenePath(std::string(name)))
                return std::string(name);
            const auto found = sceneAliases.find(std::string(name));
            return found == sceneAliases.end() ? std::string{} : found->second;
        }
        static ProjectSettings Load(const std::filesystem::path& root)
        {
            const auto path = root / "Assets/Project.json";
            std::ifstream input(path);
            if (!input)
                throw std::runtime_error("Cannot read project settings");
            auto json = Engine::Json::parse(input);
            References(root, json);
            ProjectSettings result;
            result.title = json.at("title").get<std::string>();
            result.importLegacySettings = json.value("importLegacySettings", false);
            result.startupScene = json.at("startupScene").get<std::string>();
            if (json.contains("sceneAliases"))
                result.sceneAliases = json.at("sceneAliases").get<decltype(sceneAliases)>();
            for (const char* key : {"width", "height"})
            {
                if (!json.at(key).is_number_integer() || Engine::JsonNumber(json.at(key)) < 1 ||
                    Engine::JsonNumber(json.at(key)) > 8192)
                    throw std::runtime_error("Project dimensions must be integers within range");
            }
            result.width = json.at("width").get<int>();
            result.height = json.at("height").get<int>();
            if (json.contains("inputActions"))
                result.inputActions = Engine::InputActions::Parse(json.at("inputActions"));
            result.Validate();
            return result;
        }
        void Save(const std::filesystem::path& root) const
        {
            Validate();
            const auto scene = root / std::filesystem::path(std::u8string(startupScene.begin(), startupScene.end()));
            if (!std::filesystem::is_regular_file(scene))
                throw std::runtime_error("Startup scene does not exist");
            for (const auto& [name, target] : sceneAliases)
            {
                static_cast<void>(name);
                if (!std::filesystem::is_regular_file(root / Engine::AssetDatabase::Path(target)))
                    throw std::runtime_error("Aliased scene does not exist");
            }
            const auto path = root / "Assets/Project.json";
            auto temporary = path;
            temporary += ".tmp." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(GetTickCount64());
            try
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                Engine::Json json = {{"title", title},
                                     {"importLegacySettings", importLegacySettings},
                                     {"startupScene", startupScene},
                                     {"width", width},
                                     {"height", height},
                                     {"inputActions", Engine::InputActions::Serialize(inputActions)}};
                if (!sceneAliases.empty())
                    json["sceneAliases"] = sceneAliases;
                References(root, json);
                output << json.dump(2) << '\n';
                output.close();
                if (!output ||
                    !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Cannot save project settings");
                static_cast<void>(Engine::AssetDatabase::Ensure(path));
            }
            catch (...)
            {
                std::error_code error;
                std::filesystem::remove(temporary, error);
                throw;
            }
        }

    private:
        /// <summary>起動先と別名の参照を既存のアセット ID 管理へ渡し、改名後も同じシーンを追跡します。</summary>
        static void References(const std::filesystem::path& root, Engine::Json& json)
        {
            const auto saved = json.value("assetReferences", Engine::AssetReferenceResolver::References{});
            const Engine::AssetDatabase index(root);
            Engine::AssetReferenceResolver references(index, saved);
            references.Visit(json.at("startupScene").get_ref<std::string&>());
            if (json.contains("sceneAliases"))
            {
                if (!json.at("sceneAliases").is_object())
                    throw std::runtime_error("Scene aliases must be an object");
                for (auto& value : json.at("sceneAliases"))
                    references.Visit(value.get_ref<std::string&>());
            }
            json["assetReferences"] = references.Captured();
        }
    };
}
