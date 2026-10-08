#pragma once
#include "ProjectCatalog.h"
#include <SceneRuntime/SceneLayout.h>
#include <fstream>
#include <cmath>
#include <Windows.h>

namespace Editor {
class SceneRecovery final {
public:
    static std::filesystem::path Path(const std::filesystem::path& directory,const std::filesystem::path& scene) {
        const auto text=ProjectCatalog::Text(std::filesystem::absolute(scene).lexically_normal());
        std::uint64_t hash=14695981039346656037ull;
        for(unsigned char c:text) {hash^=c;hash*=1099511628211ull;}
        return directory/(std::to_string(hash)+".json");
    }
    static void Write(const std::filesystem::path& directory,const std::filesystem::path& scene,const std::string& json) {
        static_cast<void>(SceneRuntime::SceneLayout::Parse(json));
        const Engine::Json packet{{"version",1},{"scene",ProjectCatalog::Text(std::filesystem::absolute(scene).lexically_normal())},{"snapshot",json}};
        std::filesystem::create_directories(directory);
        const auto target=Path(directory,scene),temporary=std::filesystem::path(target.wstring()+L".tmp");
        try {
            { std::ofstream output(temporary,std::ios::binary|std::ios::trunc); output<<packet.dump(); output.flush();
              if(!output) throw std::runtime_error("復旧用シーンを書き込めません"); }
            if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("復旧用シーンの置き換えに失敗しました");
        } catch(...) {std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
    }
    static SceneRuntime::SceneLayout Read(const std::filesystem::path& directory,const std::filesystem::path& scene) {
        std::ifstream input(Path(directory,scene),std::ios::binary);
        if(!input) throw std::runtime_error("復旧用シーンがありません");
        const auto packet=Engine::Json::parse(input);
        if(packet.at("version")!=1 || packet.at("scene")!=ProjectCatalog::Text(std::filesystem::absolute(scene).lexically_normal()))
            throw std::runtime_error("復旧用シーンの対象が一致しません");
        return SceneRuntime::SceneLayout::Parse(packet.at("snapshot").get<std::string>());
    }
    void Clear(const std::filesystem::path& directory,const std::filesystem::path& scene) {
        try {std::filesystem::remove(Path(directory,scene)); available_=false;last_.clear();error_.clear();}
        catch(const std::exception& e) {error_=e.what();}
    }
    void Poll(const std::filesystem::path& directory,const std::filesystem::path& scene,const std::string& json,bool dirty,double seconds) {
        if(scene!=scene_) {scene_=scene;elapsed_=30;check_=1;last_.clear();}
        if(!std::isfinite(seconds) || seconds<=0) return;
        check_+=std::min(seconds,1.0); elapsed_+=std::min(seconds,1.0);
        try {
            if(check_>=1) {available_=std::filesystem::is_regular_file(Path(directory,scene));check_=0;}
            if(!dirty || elapsed_<30 || json==last_ || (available_ && last_.empty())) return;
            elapsed_=0;
            Write(directory,scene,json);last_=json;available_=true;error_.clear();
        } catch(const std::exception& e) {elapsed_=0;error_=e.what();}
    }
    bool Available() const {return available_;}
    bool PreviousSession() const {return available_ && last_.empty();}
    const std::string& Error() const {return error_;}
private:
    std::filesystem::path scene_;
    std::string last_,error_;
    double elapsed_=0,check_=0;
    bool available_=false;
};
}
