#include <Engine/Core/Validation.h>
#include <SceneRuntime/GameSettings.h>
#include <Engine/Core/DiagnosticPaths.h>
#include <Windows.h>
#include <Engine/Core/Json.h>
#include <Engine/Core/Log.h>
#include <sstream>
#include <fstream>
#include <string>
#include <cmath>
#include <optional>
#include <atomic>

namespace {
struct SettingsFile {
    HANDLE handle=INVALID_HANDLE_VALUE;
    ~SettingsFile() {if(handle!=INVALID_HANDLE_VALUE) CloseHandle(handle);}
};
std::optional<std::string> ReadSettings(const std::filesystem::path& path) {
    SettingsFile file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    if(file.handle==INVALID_HANDLE_VALUE) return std::nullopt;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file.handle,&size) || size.QuadPart<0 || size.QuadPart>65536) return std::nullopt;
    std::string content(static_cast<size_t>(size.QuadPart),'\0');
    size_t offset=0;
    while(offset<content.size()) {
        DWORD read=0;
        if(!ReadFile(file.handle,content.data()+offset,static_cast<DWORD>(content.size()-offset),&read,nullptr) || read==0) return std::nullopt;
        offset+=read;
    }
    return content;
}
bool ReplaceSettings(const std::filesystem::path& temporary,const std::filesystem::path& target,DWORD& error) {
    static std::atomic<unsigned long long> sequence=0;
    auto backup=temporary;
    backup+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(sequence++)+L".previous";
    for(unsigned int attempt=0;attempt<9;++attempt) {
        if(ReplaceFileW(target.c_str(),temporary.c_str(),backup.c_str(),0,nullptr,nullptr)) {DeleteFileW(backup.c_str());return true;}
        error=GetLastError();
        if(error==ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
            if(!MoveFileExW(backup.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)) {
                const auto bytes=backup.generic_u8string();
                Engine::Log::Error("Settings recovery file retained: "+std::string(bytes.begin(),bytes.end()));
            }
            return false;
        }
        if(error==ERROR_FILE_NOT_FOUND) {
            if(MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)) return true;
            error=GetLastError();
        }
        if(error!=ERROR_ACCESS_DENIED && error!=ERROR_SHARING_VIOLATION && error!=ERROR_LOCK_VIOLATION &&
            error!=ERROR_ALREADY_EXISTS && error!=ERROR_FILE_EXISTS && error!=ERROR_UNABLE_TO_REMOVE_REPLACED) return false;
        const auto attributes=GetFileAttributesW(target.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&(FILE_ATTRIBUTE_READONLY|FILE_ATTRIBUTE_DIRECTORY))) return false;
        if(attempt==8) return false;
        Sleep((std::min)(1u<<attempt,16u));
    }
    return false;
}
bool SaveFailure(const std::filesystem::path& path,const char* operation,DWORD code) {
    const auto bytes=path.generic_u8string();
    Engine::Log::Error("Settings save failed ("+std::string(operation)+", Windows error "+std::to_string(code)+"): "+std::string(bytes.begin(),bytes.end()));
    SetLastError(code);return false;
}
SceneRuntime::GameSettings LegacySettings(const std::string& content) {
    std::istringstream input(content); std::string magic,extra;
    int version=0,volume=0,motion=0;
    if(!(input>>magic>>version>>volume>>motion) || magic!="WP1_SETTINGS" || version!=1 ||
        volume<0 || volume>10 || (motion!=0 && motion!=1) || (input>>extra)) return {};
    SceneRuntime::GameSettings settings{volume,motion!=0}; settings.loaded=true; return settings;
}
SceneRuntime::GameSettings JsonSettings(const std::string& content) {
    const auto document=Engine::Json::parse(content);
    Engine::JsonObject(document);
    if(Engine::JsonNumber(document.at("version"))!=1 || !document.at("volume").is_number_integer()) return {};
    const double volume=Engine::JsonNumber(document.at("volume"));
    if(volume<0 || volume>10) return {};
    SceneRuntime::GameSettings settings{static_cast<int>(volume),document.at("backgroundMotion").get<bool>()};
    if(document.contains("volumeGain")) {
        const double gain=Engine::JsonNumber(document.at("volumeGain"));
        if(!Engine::InRange(gain,-1,1)) return {};
        settings.volumeGain=static_cast<float>(gain);
    }
    settings.loaded=true;
    if(document.contains("values")) {
        const auto& values=Engine::JsonObject(document.at("values"));
        if(values.size()>128) return {};
        for(const auto& [key,value]:values.items()) {
            const auto number=Engine::JsonNumber(value);
            if(!Engine::ValidString(key,128,true) || key.find_first_of("=&")!=std::string::npos || !Engine::InRange(number,-100000,100000)) return {};
            settings.values[key]=static_cast<float>(number);
        }
    }
    return settings;
}
}
namespace SceneRuntime
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
        const auto content=ReadSettings(inputPath);
        if(!content) return {};
        try {return JsonSettings(*content);}
        catch(const Engine::Json::exception&) {return LegacySettings(*content);}
        catch(const std::exception&) {return {};}
    }
    bool GameSettings::Save(const std::filesystem::path& path) const
    {
        if (path.empty() || volume < 0 || volume > 10 || values.size()>128 || !Engine::InRange(volumeGain,-1,1)) return false;
        for(const auto& [key,value]:values) if(!Engine::ValidString(key,128,true) || key.find_first_of("=&")!=std::string::npos || !Engine::InRange(value,-100000,100000)) return false;
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
        DWORD replacementError=ERROR_SUCCESS;
        if (ReplaceSettings(temporary,path,replacementError)) return true;
        std::filesystem::remove(temporary, error);
        return SaveFailure(path,"replace",replacementError);
    }
}
