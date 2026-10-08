#include "GameSettings.h"
#include <Engine/Core/DiagnosticPaths.h>
#include <Windows.h>
#include <Engine/Core/Json.h>
#include <sstream>
#include <fstream>
#include <string>
#include <cmath>

namespace {
App::GameSettings LegacySettings(const std::string& content) {
    std::istringstream input(content); std::string magic,extra;
    int version=0,volume=0,motion=0;
    if(!(input>>magic>>version>>volume>>motion) || magic!="WP1_SETTINGS" || version!=1 ||
        volume<0 || volume>10 || (motion!=0 && motion!=1) || (input>>extra)) return {};
    App::GameSettings settings{volume,motion!=0}; settings.loaded=true; return settings;
}
App::GameSettings JsonSettings(const std::string& content) {
    const auto document=Engine::Json::parse(content);
    Engine::JsonObject(document);
    if(Engine::JsonNumber(document.at("version"))!=1 || !document.at("volume").is_number_integer()) return {};
    const double volume=Engine::JsonNumber(document.at("volume"));
    if(volume<0 || volume>10) return {};
    App::GameSettings settings{static_cast<int>(volume),document.at("backgroundMotion").get<bool>()};
    if(document.contains("volumeGain")) {
        const double gain=Engine::JsonNumber(document.at("volumeGain"));
        if(!std::isfinite(gain) || gain < -1 || gain > 1) return {};
        settings.volumeGain=static_cast<float>(gain);
    }
    settings.loaded=true;
    if(document.contains("values")) {
        const auto& values=Engine::JsonObject(document.at("values"));
        if(values.size()>128) return {};
        for(const auto& [key,value]:values.items()) {
            const auto number=Engine::JsonNumber(value);
            if(key.empty() || key.size()>128 || key.find_first_of("=&")!=std::string::npos || key.find('\0')!=std::string::npos || !std::isfinite(number) || std::abs(number)>100000) return {};
            settings.values[key]=static_cast<float>(number);
        }
    }
    return settings;
}
}
namespace App
{
    std::filesystem::path GameSettings::UserPath()
    {
        const auto root = Engine::GetDiagnosticsRoot();
        return root.empty() ? std::filesystem::path{} : root / "settings.json";
    }
    GameSettings GameSettings::Load(const std::filesystem::path& path)
    {
        std::error_code error;
        auto inputPath=path;
        if(path.extension()==L".json" && !std::filesystem::exists(path,error)) inputPath.replace_extension(L".txt");
        const auto size=std::filesystem::file_size(inputPath,error);
        if(error || size>65536) return {};
        std::ifstream stream(inputPath,std::ios::binary);
        const std::string content{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
        if(stream.bad()) return {};
        try {return JsonSettings(content);}
        catch(const Engine::Json::exception&) {return LegacySettings(content);}
        catch(const std::exception&) {return {};}
    }
    bool GameSettings::Save(const std::filesystem::path& path) const
    {
        if (path.empty() || volume < 0 || volume > 10 || values.size()>128 || !std::isfinite(volumeGain) || volumeGain < -1 || volumeGain > 1) return false;
        for(const auto& [key,value]:values) if(key.empty() || key.size()>128 || key.find_first_of("=&")!=std::string::npos || key.find('\0')!=std::string::npos || !std::isfinite(value) || std::abs(value)>100000) return false;
        std::error_code error;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
        if (error) return false;
        auto temporary = path;
        temporary += L".tmp";
        {
            std::ofstream stream(temporary, std::ios::trunc);
            const Engine::Json document={{"version",1},{"volume",volume},{"backgroundMotion",backgroundMotion},{"values",values},{"volumeGain",volumeGain}};
            stream << document.dump(2) << '\n';
            stream.close();
            if (!stream) return false;
        }
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        std::filesystem::remove(temporary, error);
        return false;
    }
}
