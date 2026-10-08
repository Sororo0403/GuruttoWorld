#pragma once
#include "ProjectCatalog.h"
#include <Engine/Core/Json.h>
#include <fstream>
#include <sstream>
#include <chrono>

namespace Editor {
class AssetDependencies final {
public:
    void Refresh(const std::filesystem::path& root,const ProjectAsset& asset) {
        const auto path=root/asset.path;
        const auto now=std::chrono::steady_clock::now();
        if (path==path_ && now<next_) return;
        next_=now+std::chrono::milliseconds(500);
        try {
            const auto time=std::filesystem::last_write_time(path);
            const auto size=std::filesystem::file_size(path);
            if (path==path_ && loaded_ && time==time_ && size==size_) return;
            path_=path; loaded_=false; values_.clear(); error_.clear();
            auto extension=ProjectCatalog::Text(path.extension());
            std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            const bool json=asset.kind==AssetKind::Scene || asset.kind==AssetKind::Prefab || asset.kind==AssetKind::Material;
            // Binary assets have no supported text dependency format; never read their contents.
            if (json || extension==".gltf" || extension==".obj") {
                std::ifstream input(path,std::ios::binary);
                if (!input) throw std::runtime_error("アセットを読み込めません");
                std::set<std::string> values;
                if (json) {
                    const auto document=Engine::Json::parse(input);
                    const auto walk=[&](auto&& self,const Engine::Json& value)->void {
                        if (value.is_string()) {
                            const auto text=value.get<std::string>();
                            if (Engine::AssetDatabase::Valid(Engine::AssetDatabase::Path(text))) values.insert(text);
                        } else if (value.is_array() || value.is_object()) for (const auto& item:value) self(self,item);
                    }; walk(walk,document);
                } else if (extension==".gltf") {
                    const auto document=Engine::Json::parse(input);
                    for (const auto* key:{"buffers","images"}) if(document.contains(key)) for(const auto& resource:document[key]) {
                        const auto uri=resource.value("uri",std::string{});
                        if (!uri.empty() && !uri.starts_with("data:")) values.insert(uri);
                    }
                } else {
                    std::string line;
                    while(std::getline(input,line)) {
                        std::istringstream tokens(line); std::string keyword;
                        tokens>>keyword;
                        if(keyword=="mtllib") {std::string name; while(tokens>>name) values.insert(name);}
                    }
                }
                values_={values.begin(),values.end()}; ++reads_;
            }
            time_=time; size_=size; loaded_=true;
        } catch(const std::exception& error) {path_=path; loaded_=false; values_.clear(); error_=error.what();}
    }
    const std::vector<std::string>& Values() const {return values_;}
    const std::string& Error() const {return error_;}
    size_t Reads() const {return reads_;}
private:
    std::filesystem::path path_;
    std::filesystem::file_time_type time_{};
    std::uintmax_t size_=0;
    std::chrono::steady_clock::time_point next_{};
    std::vector<std::string> values_;
    std::string error_;
    size_t reads_=0;
    bool loaded_=false;
};
}
