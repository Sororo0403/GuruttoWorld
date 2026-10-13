#pragma once
#include <Engine/Core/Validation.h>
#include <iterator>
#include <Engine/Core/Json.h>
#include <Engine/Assets/TextureImport.h>
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
#include <cctype>
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
    TextureImportSettings texture;
};
class AssetDatabase final {
    std::filesystem::path root_;
    std::map<std::string,std::filesystem::path> ids_;
    std::map<std::string,std::set<std::filesystem::path>> aliases_;
    bool snapshot_=false;
    std::map<std::string,AssetMetadata> metadata_;
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
        if (json.contains("texture")) {
            const auto& value=json.at("texture"); result.texture.mipmaps=value.value("mipmaps",false); result.texture.srgb=value.value("srgb",false);
            const double limit=JsonNumber(value.value("maxSize",Json(16384)));
            if(limit<1 || limit>16384 || std::floor(limit)!=limit) throw std::runtime_error("Invalid texture size limit");
            result.texture.maxSize=static_cast<unsigned int>(limit); result.texture.compression=value.value("compression",std::string("none"));
        }
        result.texture.Validate();
        if (!ValidId(result.id) || !Engine::InRange(result.scale,0.0001f,10000) || result.previousPaths.size()>1024)
            throw std::runtime_error("Invalid asset metadata: "+Text(path));
        if (std::any_of(std::begin(result.previousPaths),std::end(result.previousPaths),[](const auto& previous) {return !Valid(previous);})) throw std::runtime_error("Invalid asset alias");
        return result;
    }
    static void Write(const std::filesystem::path& path,const AssetMetadata& metadata) {
        metadata.texture.Validate();
        if (!ValidId(metadata.id) || !Engine::InRange(metadata.scale,0.0001f,10000)) throw std::runtime_error("Invalid import settings");
        auto target=Sidecar(path),temporary=target; temporary+=".tmp."+std::to_string(GetCurrentProcessId());
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        output<<Json{{"version",1},{"id",metadata.id},{"scale",metadata.scale},{"flipV",metadata.flipV},{"previousPaths",metadata.previousPaths},{"texture",{{"mipmaps",metadata.texture.mipmaps},{"srgb",metadata.texture.srgb},{"maxSize",metadata.texture.maxSize},{"compression",metadata.texture.compression}}}}.dump(2)<<'\n'; output.close();
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
    /// <summary>snapshot はプレイ中の参照解決用にメタデータを保持します。編集時は既定の最新読み込みを使います。</summary>
    explicit AssetDatabase(std::filesystem::path root,bool snapshot=false) : root_(std::move(root)),snapshot_(snapshot) {
        for (const auto* folder : {"Assets","Shaders"}) {
            if (!std::filesystem::is_directory(root_/folder)) continue;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root_/folder)) {
                if (!entry.is_regular_file() || entry.path().extension()!=".meta") continue;
                auto source=entry.path(); source.replace_extension(); if (!std::filesystem::is_regular_file(source)) continue;
                const auto metadata=Read(source); const auto relative=source.lexically_relative(root_);
                if(snapshot_) metadata_.emplace(Text(relative),metadata);
                if (!ids_.emplace(metadata.id,relative).second) throw std::runtime_error("Duplicate asset ID: "+metadata.id);
                for (const auto& alias : metadata.previousPaths) {
                    aliases_[alias].insert(relative);
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
        if (snapshot_ && metadata_.contains(Text(path))) return path;
        if (std::filesystem::is_regular_file(root_/path)) return path;
        const auto found=aliases_.find(Text(path));
        if(found==aliases_.end()) return path;
        if(found->second.size()!=1) throw std::runtime_error("Ambiguous asset alias: "+Text(path));
        return *found->second.begin();
    }
    void References(Json& json,bool capture=true,bool resolve=true) const {
        std::map<std::string,std::string> references;
        if (json.contains("assetReferences")) references=json.at("assetReferences").get<decltype(references)>();
        std::map<std::string,std::string> updated;

        if (json.contains("objects")) WalkReferences(json["objects"],references,updated,capture,resolve);
        else for (const auto* key : {"shader","texture","normalTexture","environmentTexture","lightmap","startupScene"}) if (json.contains(key)) WalkReferences(json[key],references,updated,capture,resolve);
        if(json.contains("settings")) WalkReferences(json["settings"],references,updated,capture,resolve);
        if (capture) json["assetReferences"]=updated;
    }
    void Move(const std::filesystem::path& from,const std::filesystem::path& to) {
        ValidateDestination(from,to);
        auto metadata=Ensure(root_/from); const auto originalMetadata=metadata; metadata.previousPaths.push_back(Text(from));
        std::filesystem::create_directories((root_/to).parent_path());
        std::string original,revised;
        RewriteDependencies(from,to,original,revised);
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
private:
  static bool AssetField(const Json& owner,const std::string& key)
  {
      static const std::set<std::string> fields{"model","asset","shader","texture","normalTexture","environmentTexture","lightmap","startupScene","skyShader","oceanShader","particleShader","postShader"};
      if(fields.contains(key)) return true;
      const auto type=owner.contains("type") && owner.at("type").is_string() ? owner.at("type").get<std::string>() : "";
      if(key=="clip") return type=="AudioSource";
      if(key=="slots") return type=="Material";
      if(key!="target") return false;
      const auto action=owner.value("action",std::string{});
      return action=="loadScene" || action=="loadSceneAdditive" || action=="unloadScene";
  }
  void ScriptReferences(Json& script,const std::map<std::string,std::string>& references,std::map<std::string,std::string>& updated,bool capture,bool resolve) const
  {
      if(!script.contains("sceneAssets") || !script.contains("data")) return;
      auto& fields=script.at("data").at("value");
      for(const auto& name:script.at("sceneAssets")) {
          const auto key=name.get<std::string>();
          if(!fields.contains(key) || fields.at(key).value("kind",std::string{})!="string") throw std::runtime_error("Invalid Script scene asset field");
          WalkReferences(fields.at(key).at("value"),references,updated,capture,resolve);
      }
  }
  void WalkField(Json& owner,const std::string& key,const std::map<std::string,std::string>& references,std::map<std::string,std::string>& updated,bool capture,bool resolve) const
  {
      auto& item=owner.at(key);
      if(key=="baseline" && item.is_string()) {
          auto baseline=Json::parse(item.get<std::string>());References(baseline,capture,resolve);item=baseline.dump();
      } else if(AssetField(owner,key) && item.is_array()) {
          for(auto& asset:item) WalkReferences(asset,references,updated,capture,resolve);
      } else if(key!="data" && key!="assetReferences" && key!="sceneObjects" &&
          (AssetField(owner,key) || item.is_object() || item.is_array())) WalkReferences(item,references,updated,capture,resolve);
  }
  void ResolveReference(Json& value,const std::map<std::string,std::string>& references,std::map<std::string,std::string>& updated,bool resolve) const
  {
          const auto path = value.get<std::string>(); if (!Valid(Path(path))) return;
          const auto found = references.find(path); const auto resolved = resolve ? Resolve(Path(path), found == references.end() ? "" : found->second) : Path(path);
          value = Text(resolved);
          const auto saved = metadata_.find(Text(resolved));
          const auto metadata = snapshot_ ? (saved == metadata_.end() ? AssetMetadata{} : saved->second) : Read(root_ / resolved);
          if (!metadata.id.empty()) updated[Text(resolved)] = metadata.id;
          else if(!resolve && found!=references.end()) updated[Text(resolved)]=found->second;
  }
  void WalkReferences(Json &value, const std::map<std::string, std::string> &references, std::map<std::string, std::string> &updated, bool capture, bool resolve) const
  {
      if(value.is_string()) ResolveReference(value,references,updated,resolve);
      else if (value.is_object()) {
          const bool script=value.contains("type") && value.at("type").is_string() && value.at("type")=="Script";
          if(script) ScriptReferences(value,references,updated,capture,resolve);
          for (const auto& [key,item]:value.items()) {static_cast<void>(item);WalkField(value,key,references,updated,capture,resolve);}
      } else if (value.is_array()) for (auto &item : value) if(!item.is_string()) WalkReferences(item, references, updated, capture, resolve);

    }

  void ValidateDestination(const std::filesystem::path &from, const std::filesystem::path &to) const
  {
      if (!Valid(from) || !Valid(to) || from.extension() != to.extension() || *from.begin() != *to.begin() || from == to || std::next(from.begin()) == from.end() || std::next(to.begin()) == to.end() || *std::next(from.begin()) != *std::next(to.begin()) || std::filesystem::exists(root_ / to) || std::filesystem::exists(Sidecar(root_ / to)))
          throw std::runtime_error("Invalid asset destination or destination exists");
      const auto base = std::filesystem::weakly_canonical(root_);
      for (const auto &path : {from, to}) {
          const auto relative = std::filesystem::weakly_canonical(root_ / path).lexically_relative(base);
          if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") throw std::runtime_error("Asset destination leaves project root");
      }
  }

  void RewriteDependencies(const std::filesystem::path &from, const std::filesystem::path &to, std::string &original, std::string &revised) const
  {
      auto extension=Text(from.extension());
      std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
      if (from.parent_path() != to.parent_path() && (extension == ".gltf" || extension == ".obj")) {
          std::ifstream input(root_ / from, std::ios::binary); original = {std::istreambuf_iterator<char>(input), {}};
          if (extension == ".gltf") {
              auto json = Json::parse(original);
              for (const auto *key : {"buffers", "images"}) if (json.contains(key)) for (auto &resource : json[key]) {
                          if (!resource.contains("uri")) continue;
                          const auto uri = resource["uri"].get<std::string>();
                          if (uri.starts_with("data:") || uri.find(":") != std::string::npos) continue;
                          resource["uri"] = Text((from.parent_path() / Path(uri)).lexically_normal().lexically_relative(to.parent_path()));
                      } revised = json.dump(2) + "\n";
          } else {
              std::istringstream lines(original); std::string line;
              while (std::getline(lines, line)) {
                  if (line.starts_with("mtllib ")) line = "mtllib " + Text((from.parent_path() / Path(line.substr(7))).lexically_normal().lexically_relative(to.parent_path()));
                  revised += line + "\n";
              }
          }
      }
  }
};
}
