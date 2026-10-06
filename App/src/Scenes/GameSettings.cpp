#include "GameSettings.h"
#include <Engine/Core/DiagnosticPaths.h>
#include <Windows.h>
#include <Engine/Core/Json.h>
#include <sstream>
#include <fstream>
#include <string>

namespace {
App::GameSettings LegacySettings(const std::string& content) {
    std::istringstream input(content); std::string magic,extra;
    int version=0,volume=0,motion=0;
    if(!(input>>magic>>version>>volume>>motion) || magic!="WP1_SETTINGS" || version!=1 ||
        volume<0 || volume>10 || (motion!=0 && motion!=1) || (input>>extra)) return {};
    return {volume,motion!=0};
}
App::GameSettings JsonSettings(const std::string& content) {
    const auto document=Engine::Json::parse(content);
    Engine::JsonObject(document);
    if(Engine::JsonNumber(document.at("version"))!=1 || !document.at("volume").is_number_integer()) return {};
    const double volume=Engine::JsonNumber(document.at("volume"));
    if(volume<0 || volume>10) return {};
    return {static_cast<int>(volume),document.at("backgroundMotion").get<bool>()};
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
        if(error || size>4096) return {};
        std::ifstream stream(inputPath,std::ios::binary);
        const std::string content{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
        if(stream.bad()) return {};
        try {return JsonSettings(content);}
        catch(const Engine::Json::exception&) {return LegacySettings(content);}
        catch(const std::exception&) {return {};}
    }
    bool GameSettings::Save(const std::filesystem::path& path) const
    {
        if (path.empty() || volume < 0 || volume > 10) return false;
        std::error_code error;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
        if (error) return false;
        auto temporary = path;
        temporary += L".tmp";
        {
            std::ofstream stream(temporary, std::ios::trunc);
            const Engine::Json document={{"version",1},{"volume",volume},{"backgroundMotion",backgroundMotion}};
            stream << document.dump(2) << '\n';
            stream.close();
            if (!stream) return false;
        }
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        std::filesystem::remove(temporary, error);
        return false;
    }
}
