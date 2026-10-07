#pragma once
#include <Engine/Core/Json.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <algorithm>
#include <map>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <set>
#include <cmath>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Engine {
struct AssetMetadata {
    std::string id;
    float scale=1;
    bool flipV=false;
    std::vector<std::string> previousPaths;
};
class AssetDatabase final {
    std::filesystem::path root_;
    std::map<std::string,std::filesystem::path> ids_,aliases_;
public:
    static std::string Text(const std::filesystem::path& path) { const auto s=path.generic_u8string(); return {s.begin(),s.end()}; }
    static std::filesystem::path Path(const std::string& value) { return std::filesystem::path(std::u8string(value.begin(),value.end())); }
    static bool Valid(const std::filesystem::path& path) {
        const auto text=Text(path);
        return !path.is_absolute() && !path.has_root_name() && (text.starts_with("Assets/") || text.starts_with("Shaders/")) &&
            text.find(':')==std::string::npos && text.find('\\')==std::string::npos && text.find('\0')==std::string::npos &&
            std::none_of(path.begin(),path.end(),[](const auto& part) { return part==".." || part=="."; });
    }
    static bool ValidId(const std::string& id) { return id.size()==32 && std::all_of(id.begin(),id.end(),[](char c) { return (c>='0' && c<='9') || (c>='a' && c<='f'); }); }
    static std::filesystem::path Sidecar(std::filesystem::path path) { path+=".meta"; return path; }
    static AssetMetadata Read(const std::filesystem::path& path) {
        AssetMetadata result; std::ifstream input(Sidecar(path)); if (!input) return result;
        const auto json=Json::parse(input); result.id=json.at("id").get<std::string>();
        result.scale=static_cast<float>(JsonNumber(json.value("scale",Json(1)))); result.flipV=json.value("flipV",false);
        result.previousPaths=json.value("previousPaths",std::vector<std::string>{});
        if (!ValidId(result.id) || !std::isfinite(result.scale) || result.scale<0.0001f || result.scale>10000 || result.previousPaths.size()>1024)
            throw std::runtime_error("Invalid asset metadata: "+Text(path));
        for (const auto& previous : result.previousPaths) if (!Valid(previous)) throw std::runtime_error("Invalid asset alias");
        return result;
    }
    static void Write(const std::filesystem::path& path,const AssetMetadata& metadata) {
        if (!ValidId(metadata.id) || !std::isfinite(metadata.scale) || metadata.scale<0.0001f || metadata.scale>10000) throw std::runtime_error("Invalid import settings");
        auto target=Sidecar(path),temporary=target; temporary+=".tmp."+std::to_string(GetCurrentProcessId());
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        output<<Json{{"version",1},{"id",metadata.id},{"scale",metadata.scale},{"flipV",metadata.flipV},{"previousPaths",metadata.previousPaths}}.dump(2)<<'\n'; output.close();
        if (!output || !MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
            std::error_code error; std::filesystem::remove(temporary,error); throw std::runtime_error("Cannot save asset metadata");
        }
    }
    static AssetMetadata Ensure(const std::filesystem::path& path) {
        auto metadata=Read(path); if (!metadata.id.empty()) return metadata;
        if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Asset is missing");
        std::random_device random; constexpr char hex[]="0123456789abcdef";
        for (size_t i=0;i<32;++i) metadata.id+=hex[random()%16];
        Write(path,metadata); return metadata;
    }
    static std::filesystem::path Root(std::filesystem::path path) {
        path=std::filesystem::absolute(path).parent_path();
        while (!path.empty() && path!=path.root_path()) {
            if (path.filename()=="Assets" || path.filename()=="Shaders") return path.parent_path();
            path=path.parent_path();
        } return {};
    }
    explicit AssetDatabase(std::filesystem::path root) : root_(std::move(root)) {
        for (const auto* folder : {"Assets","Shaders"}) {
            if (!std::filesystem::is_directory(root_/folder)) continue;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root_/folder)) {
                if (!entry.is_regular_file() || entry.path().extension()!=".meta") continue;
                auto source=entry.path(); source.replace_extension(); if (!std::filesystem::is_regular_file(source)) continue;
                const auto metadata=Read(source); const auto relative=source.lexically_relative(root_);
                if (!ids_.emplace(metadata.id,relative).second) throw std::runtime_error("Duplicate asset ID: "+metadata.id);
                for (const auto& alias : metadata.previousPaths) {
                    const auto [found,inserted]=aliases_.emplace(alias,relative);
                    if (!inserted && found->second!=relative) throw std::runtime_error("Ambiguous asset alias: "+alias);
                }
            }
        }
    }
    std::filesystem::path Resolve(const std::filesystem::path& path,const std::string& id={}) const {
        if (!Valid(path)) throw std::runtime_error("Invalid asset reference");
        if (!id.empty()) {
            const auto found=ids_.find(id); if (found==ids_.end()) throw std::runtime_error("Missing asset ID: "+id);
            return found->second;
        }
        if (std::filesystem::is_regular_file(root_/path)) return path;
        const auto found=aliases_.find(Text(path)); return found==aliases_.end() ? path : found->second;
    }
    void References(Json& json,bool capture=true,bool resolve=true) const {
        std::map<std::string,std::string> references;
        if (json.contains("assetReferences")) references=json.at("assetReferences").get<decltype(references)>();
        auto updated=references;
        const auto walk=[&](auto&& self,Json& value)->void {
            if (value.is_string()) {
                const auto path=value.get<std::string>(); if (!Valid(Path(path))) return;
                const auto found=references.find(path); const auto resolved=resolve ? Resolve(Path(path),found==references.end() ? "" : found->second) : Path(path);
                value=Text(resolved);
                const auto metadata=Read(root_/resolved);
                if (!metadata.id.empty()) updated[Text(resolved)]=metadata.id;
            } else if (value.is_object()) {
                for (auto& [key,item] : value.items()) {
                    if (key=="baseline" && item.is_string()) {
                        auto baseline=Json::parse(item.get<std::string>()); References(baseline,capture,resolve); item=baseline.dump();
                    } else self(self,item);
                }
            } else if (value.is_array()) for (auto& item : value) self(self,item);
        };
        if (json.contains("objects")) walk(walk,json["objects"]);
        else for (const auto* key : {"texture","normalTexture","startupScene"}) if (json.contains(key)) walk(walk,json[key]);
        if (capture) json["assetReferences"]=updated;
    }
    void Move(const std::filesystem::path& from,const std::filesystem::path& to) {
        if (!Valid(from) || !Valid(to) || from.extension()!=to.extension() || *from.begin()!=*to.begin() || from==to || std::next(from.begin())==from.end() || std::next(to.begin())==to.end() || *std::next(from.begin())!=*std::next(to.begin()) || std::filesystem::exists(root_/to) || std::filesystem::exists(Sidecar(root_/to)))
            throw std::runtime_error("Invalid asset destination or destination exists");
        const auto base=std::filesystem::weakly_canonical(root_);
        for (const auto& path : {from,to}) {
            const auto relative=std::filesystem::weakly_canonical(root_/path).lexically_relative(base);
            if (relative.empty() || relative.is_absolute() || *relative.begin()=="..") throw std::runtime_error("Asset destination leaves project root");
        }
        auto metadata=Ensure(root_/from); const auto originalMetadata=metadata; metadata.previousPaths.push_back(Text(from));
        std::filesystem::create_directories((root_/to).parent_path());
        std::string original,revised;
        if (from.parent_path()!=to.parent_path() && (from.extension()==".gltf" || from.extension()==".obj")) {
            std::ifstream input(root_/from,std::ios::binary); original={std::istreambuf_iterator<char>(input),{}};
            if (from.extension()==".gltf") {
                auto json=Json::parse(original);
                for (const auto* key : {"buffers","images"}) if (json.contains(key)) for (auto& resource : json[key]) {
                    if (!resource.contains("uri")) continue;
                    const auto uri=resource["uri"].get<std::string>();
                    if (uri.starts_with("data:") || uri.find(":")!=std::string::npos) continue;
                    resource["uri"]=Text((from.parent_path()/Path(uri)).lexically_normal().lexically_relative(to.parent_path()));
                } revised=json.dump(2)+"\n";
            } else {
                std::istringstream lines(original); std::string line;
                while (std::getline(lines,line)) {
                    if (line.starts_with("mtllib ")) line="mtllib "+Text((from.parent_path()/Path(line.substr(7))).lexically_normal().lexically_relative(to.parent_path()));
                    revised+=line+"\n";
                }
            }
        }
        std::filesystem::rename(root_/from,root_/to);
        try {
            std::filesystem::rename(Sidecar(root_/from),Sidecar(root_/to)); Write(root_/to,metadata);
            if (!revised.empty()) { std::ofstream output(root_/to,std::ios::binary|std::ios::trunc); output<<revised; output.close(); if (!output) throw std::runtime_error("Cannot update model dependencies"); }
        }
        catch (...) {
            std::error_code error;
            if (std::filesystem::exists(Sidecar(root_/to))) std::filesystem::rename(Sidecar(root_/to),Sidecar(root_/from),error);
            std::filesystem::rename(root_/to,root_/from,error);
            if (!original.empty()) { std::ofstream output(root_/from,std::ios::binary|std::ios::trunc); output<<original; }
            try { if (std::filesystem::is_regular_file(root_/from)) Write(root_/from,originalMetadata); } catch (...) {}
            throw;
        }
    }
};
}
