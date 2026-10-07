#pragma once
#include <Engine/Core/Json.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Input/InputActions.h>
#include <filesystem>
#include <fstream>
#include <Engine/Platform/Window.h>

namespace SceneRuntime
{
    struct ProjectSettings
    {
        std::string title="ぐるっとワールド";
        std::string startupScene="Assets/Scenes/TitleStreet.json";
        int width=1280,height=720;
        Engine::InputActions::Bindings inputActions=Engine::InputActions::Defaults();

        static bool ScenePath(const std::string& value)
        {
            const auto path=std::filesystem::path(std::u8string(value.begin(),value.end()));
            return value.starts_with("Assets/Scenes/") && path.extension()==".json" &&
                !path.is_absolute() && !path.has_root_name() && value.find("..")==std::string::npos &&
                value.find('\\')==std::string::npos && value.find(':')==std::string::npos && value.find('\0')==std::string::npos;
        }
        void Validate() const
        {
            Engine::InputActions::Validate(inputActions);
            if(title.empty() || title.size()>1024 || title.find('\0')!=std::string::npos ||
                !ScenePath(startupScene) || width<320 || width>8192 || height<240 || height>8192)
                throw std::runtime_error("Invalid project settings");
        }
        static ProjectSettings Load(const std::filesystem::path& root)
        {
            const auto path=root/"Assets/Project.json";
            if(!std::filesystem::exists(path)) return {};
            std::ifstream input(path);
            if(!input) throw std::runtime_error("Cannot read project settings");
            auto json=Engine::Json::parse(input); Engine::AssetDatabase(root).References(json);
            ProjectSettings result;
            result.title=json.at("title").get<std::string>();
            result.startupScene=json.at("startupScene").get<std::string>();
            for(const char* key:{"width","height"}) {
                if(!json.at(key).is_number_integer() || Engine::JsonNumber(json.at(key))<1 || Engine::JsonNumber(json.at(key))>8192)
                    throw std::runtime_error("Project dimensions must be integers within range");
            }
            result.width=json.at("width").get<int>(); result.height=json.at("height").get<int>();
            if (json.contains("inputActions")) result.inputActions=Engine::InputActions::Parse(json.at("inputActions"));
            result.Validate(); return result;
        }
        void Save(const std::filesystem::path& root) const
        {
            Validate();
            const auto scene=root/std::filesystem::path(std::u8string(startupScene.begin(),startupScene.end()));
            if(!std::filesystem::is_regular_file(scene)) throw std::runtime_error("Startup scene does not exist");
            const auto path=root/"Assets/Project.json";
            auto temporary=path; temporary+=".tmp."+std::to_string(GetCurrentProcessId())+"."+std::to_string(GetTickCount64());
            try {
                std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
                Engine::Json json={{"title",title},{"startupScene",startupScene},{"width",width},{"height",height},{"inputActions",Engine::InputActions::Serialize(inputActions)}};
                Engine::AssetDatabase(root).References(json);
                output<<json.dump(2)<<'\n'; output.close();
                if(!output || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Cannot save project settings");
            } catch(...) {std::error_code error; std::filesystem::remove(temporary,error); throw;}
        }
    };
}
