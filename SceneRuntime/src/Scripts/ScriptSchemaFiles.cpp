#include <SceneRuntime/Scripts/ScriptSchemaFiles.h>

namespace SceneRuntime
{
    auto ScriptSchemaFiles::LoadProject(const std::filesystem::path& root) -> std::map<std::string, ScriptSchema>
    {
        auto declarations = Engine::Json::object();
        const auto source = root / "Assets/Scripts";
        if (!std::filesystem::is_directory(source))
            return {};
        for (const auto& entry : std::filesystem::recursive_directory_iterator(source))
        {
            if (!entry.is_regular_file() || !entry.path().filename().generic_string().ends_with(".schema.json"))
                continue;
            std::ifstream file(entry.path());
            if (!file)
                throw std::runtime_error("Cannot read script schema");
            const auto document = Engine::Json::parse(file);
            static_cast<void>(ScriptSchema::ValidateDocument(document));
            for (const auto& [name, declaration] : document.at("scripts").items())
            {
                if (declarations.contains(name) || declarations.size() >= 256)
                    throw std::runtime_error("Duplicate script schema or too many behaviours");
                declarations[name] = declaration;
            }
        }
        return ScriptSchema::Parse(Engine::Json{{"version", 2}, {"scripts", declarations}});
    }
}
