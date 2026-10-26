#include <SceneRuntime/Project/ProjectSettings.h>

namespace SceneRuntime
{
    auto ProjectSettings::ScenePath(const std::string& value) -> bool
    {
        const auto path = std::filesystem::path(std::u8string(value.begin(), value.end()));
        return value.starts_with("Assets/Scenes/") && path.extension() == ".json" && !path.is_absolute() &&
               !path.has_root_name() && value.find("..") == std::string::npos &&
               value.find('\\') == std::string::npos && value.find(':') == std::string::npos &&
               value.find('\0') == std::string::npos;
    }

    auto ProjectSettings::ValidateSceneAliases() const -> void
    {
        if (sceneAliases.size() > 64)
            throw std::runtime_error("Too many scene aliases");
        for (const auto& [name, scene] : sceneAliases)
        {
            if (name.empty() || name.size() > 64 || !ScenePath(scene) ||
                !std::all_of(name.begin(), name.end(),
                             [](unsigned char c)
                             {
                                 return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                        c == '_' || c == '-';
                             }))
                throw std::runtime_error("Invalid scene alias");
        }
    }

    auto ProjectSettings::Validate() const -> void
    {
        Engine::InputActions::Validate(inputActions);
        if (title.empty() || title.size() > 1024 || title.find('\0') != std::string::npos || !ScenePath(startupScene) ||
            width < 320 || width > 8192 || height < 240 || height > 8192)
            throw std::runtime_error("Invalid project settings");
        ValidateSceneAliases();
    }

    auto ProjectSettings::ResolveScene(std::string_view name) const -> std::string
    {
        if (ScenePath(std::string(name)))
            return std::string(name);
        const auto found = sceneAliases.find(std::string(name));
        return found == sceneAliases.end() ? std::string{} : found->second;
    }

    auto ProjectSettings::Load(const std::filesystem::path& root) -> ProjectSettings
    {
        Engine::FileSaveLock saveLock(root);
        if (saveLock.Outermost())
            Engine::FileJournal::RecoverLocked(root);
        const auto path = root / "Assets/Project.json";
        const auto source = Engine::FileContents::Read(path);
        if (!source)
            throw std::runtime_error("Cannot read project settings");
        auto json = Engine::Json::parse(*source);
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
        result.savedPath_ = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
        result.savedSource_ = source;
        return result;
    }

    auto ProjectSettings::Save(const std::filesystem::path& root) const -> void
    {
        Engine::FileSaveLock saveLock(root);
        if (saveLock.Outermost())
            Engine::FileJournal::RecoverLocked(root);
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
        const auto absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
        const auto checkSource = [&]()
        {
            if (!savedPath_.empty() &&
                CompareStringOrdinal(savedPath_.c_str(), -1, absolute.c_str(), -1, TRUE) == CSTR_EQUAL &&
                Engine::FileContents::Read(absolute) != savedSource_)
                throw std::runtime_error("プロジェクト設定が外部で変更・削除されています。再読み込みしてください。");
        };
        checkSource();
        Engine::Json json = {{"title", title},
                             {"importLegacySettings", importLegacySettings},
                             {"startupScene", startupScene},
                             {"width", width},
                             {"height", height},
                             {"inputActions", Engine::InputActions::Serialize(inputActions)}};
        if (!sceneAliases.empty())
            json["sceneAliases"] = sceneAliases;
        References(root, json);
        auto source = std::optional<std::string>{json.dump(2) + '\n'};
        checkSource();
        const bool sameSource = !savedPath_.empty() &&
                                CompareStringOrdinal(savedPath_.c_str(), -1, absolute.c_str(), -1, TRUE) == CSTR_EQUAL;
        Engine::FileReplacement::Write(absolute, sameSource ? savedSource_ : Engine::FileContents::Read(absolute),
                                       *source);
        static_cast<void>(Engine::AssetDatabase::Ensure(path));
        savedPath_ = absolute;
        savedSource_.swap(source);
    }

    auto ProjectSettings::References(const std::filesystem::path& root, Engine::Json& json) -> void
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
}
