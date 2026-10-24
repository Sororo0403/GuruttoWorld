#pragma once
#include <Engine/Assets/AssetPath.h>
#include <Engine/Core/Validation.h>
#include <Engine/Core/FileContents.h>
#include <Engine/Core/FileReplacement.h>
#include <Engine/Assets/ContentContext.h>
#include <iterator>
#include <Engine/Core/Json.h>
#include <Engine/Assets/TextureImport.h>
#include <Engine/Assets/ContentFiles.h>
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
    struct Index
    {
        std::map<std::string, std::filesystem::path> ids;
        std::map<std::string, std::set<std::filesystem::path>> aliases;
        std::map<std::string, AssetMetadata> metadata;
    };
    /// <summary>同じ素材層の索引を共有し、変更通知がある場合だけ再構築します。</summary>
    static std::shared_ptr<const Index> AcquireIndex(const std::shared_ptr<const ContentContext>& context);
    /// <summary>検証済みの不変な索引を構築します。失敗した索引は公開しません。</summary>
    static std::shared_ptr<const Index> BuildIndex(const ContentContext& context);
    std::shared_ptr<const Index> index_;
    bool snapshot_=false;
    std::shared_ptr<const ContentContext> context_;
public:
    /// <summary>索引が使用する固定した素材層を取得します。</summary>
    const std::shared_ptr<const ContentContext>& Content() const { return context_; }
    /// <summary>論理アセットパスをUTF-8の保存値へ変換します。</summary>
    static std::string Text(const std::filesystem::path& path)
    {
        return AssetPath::Text(path);
    }

    /// <summary>UTF-8の保存値を論理パスへ変換します。</summary>
    static std::filesystem::path Path(const std::string& value)
    {
        return AssetPath::Path(value);
    }

    /// <summary>共通の論理パス規則でルート外への逸脱を拒否します。</summary>
    static bool Valid(const std::filesystem::path& path)
    {
        return AssetPath::Valid(path);
    }

    /// <summary>共通の保存形式でアセットIDを検証します。</summary>
    static bool ValidId(const std::string& id)
    {
        return AssetPath::ValidId(id);
    }

    static std::filesystem::path Sidecar(std::filesystem::path path) { path+=".meta"; return path; }
        /// <summary>既存のメタデータ形式を、副作用なしに解釈して検証します。</summary>
        static AssetMetadata ParseMetadata(std::string_view source)
        {
            const auto json = Json::parse(source);
            AssetMetadata result;
            result.id = json.at("id").get<std::string>();
            result.scale = static_cast<float>(JsonNumber(json.value("scale", Json(1))));
            result.flipV = json.value("flipV", false);
            result.previousPaths = json.value("previousPaths", std::vector<std::string>{});
            if (json.contains("texture"))
            {
                const auto& value = json.at("texture");
                result.texture.mipmaps = value.value("mipmaps", false);
                result.texture.srgb = value.value("srgb", false);
                const double limit = JsonNumber(value.value("maxSize", Json(16384)));
                if (limit < 1 || limit > 16384 || std::floor(limit) != limit)
                    throw std::runtime_error("Invalid texture size limit");
                result.texture.maxSize = static_cast<unsigned int>(limit);
                result.texture.compression = value.value("compression", std::string("none"));
            }
            result.texture.Validate();
            if (!ValidId(result.id) || !Engine::InRange(result.scale, 0.0001f, 10000) ||
                result.previousPaths.size() > 1024)
                throw std::runtime_error("Invalid asset metadata");
            if (std::any_of(std::begin(result.previousPaths), std::end(result.previousPaths),
                            [](const auto& previous)
                            {
                                return !Valid(previous);
                            }))
                throw std::runtime_error("Invalid asset alias");
            return result;
        }
        /// <summary>共有素材を含むメタデータを読みます。存在しない場合だけ既定値を返します。</summary>
        static AssetMetadata Read(const std::filesystem::path& path)
        {
            const auto source = FileContents::Read(ContentFiles::Resolve(Sidecar(path)));
            if (!source)
                return {};
            try
            {
                return ParseMetadata(*source);
            }
            catch (const std::exception& error)
            {
                throw std::runtime_error("Invalid asset metadata: " + Text(path) + " / " + error.what());
            }
        }
        /// <summary>新しいアセットIDと既定のインポート設定を、ファイルを変更せず作成します。</summary>
        static AssetMetadata CreateMetadata()
        {
            AssetMetadata metadata;
            std::random_device random;
            constexpr char Hex[] = "0123456789abcdef";
            for (size_t i = 0; i < 32; ++i)
                metadata.id += Hex[random() % 16];
            return metadata;
        }
        /// <summary>メタデータを検証し、保存用の内容をファイルへ触れずに構築します。</summary>
        static std::string SerializeMetadata(const AssetMetadata& metadata)
        {
            metadata.texture.Validate();
            if (!ValidId(metadata.id) || !Engine::InRange(metadata.scale, 0.0001f, 10000))
                throw std::runtime_error("Invalid import settings");
            if (metadata.previousPaths.size() > 1024 ||
                std::any_of(metadata.previousPaths.begin(), metadata.previousPaths.end(),
                            [](const auto& previous)
                            {
                                return !Valid(previous);
                            }))
                throw std::runtime_error("Invalid asset alias history (maximum 1024 paths)");
            return Json{{"version", 1},
                        {"id", metadata.id},
                        {"scale", metadata.scale},
                        {"flipV", metadata.flipV},
                        {"previousPaths", metadata.previousPaths},
                        {"texture",
                         {{"mipmaps", metadata.texture.mipmaps},
                          {"srgb", metadata.texture.srgb},
                          {"maxSize", metadata.texture.maxSize},
                          {"compression", metadata.texture.compression}}}}
                       .dump(2) +
                   "\n";
        }
        /// <summary>検証済みのメタデータを一時ファイルから置き換えます。</summary>
        static void Write(const std::filesystem::path& path, const AssetMetadata& metadata)
        {
            Write(path, metadata, FileContents::Read(Sidecar(path)));
        }
        /// <summary>読み込み時の内容と照合し、別エディターのインポート設定を上書きしません。</summary>
        static void Write(const std::filesystem::path& path, const AssetMetadata& metadata,
                          const std::optional<std::string>& expected)
        {
            if (ContentFiles::IsShared(path))
                throw std::runtime_error("Shared content is read-only; create a game override first");
            const auto source = SerializeMetadata(metadata);
            FileReplacement::Write(Sidecar(path), expected, source);
        }
        /// <summary>既存のIDを保持し、存在する未登録アセットだけに新しいメタデータを保存します。</summary>
        static AssetMetadata Ensure(const std::filesystem::path& path)
        {
            const auto root = FileSaveLock::Root(path);
            FileSaveLock lock(root);
            if (lock.Outermost())
                FileJournal::RecoverLocked(root);
            auto metadata = Read(path);
            if (!metadata.id.empty())
                return metadata;
            if (!std::filesystem::is_regular_file(path))
                throw std::runtime_error("Asset is missing");
            metadata = CreateMetadata();
            if (ContentFiles::IsShared(path))
                throw std::runtime_error("Shared content is read-only; create a game override first");
            try
            {
                FileContents::Write(Sidecar(path), SerializeMetadata(metadata), false);
            }
            catch (const std::exception&)
            {
                const auto winner = Read(path);
                if (winner.id.empty())
                    throw;
                return winner;
            }
            return metadata;
        }
        /// <summary>参照解決にも保存の排他と同じプロジェクト境界を使います。通常の領域外ファイルは空を返します。</summary>
        static std::filesystem::path Root(const std::filesystem::path& path)
        {
            const auto parent = std::filesystem::weakly_canonical(std::filesystem::absolute(path)).parent_path();
            const auto root = FileSaveLock::Root(path);
            if (root != parent || std::filesystem::is_regular_file(root / "WP1.project.json") ||
                std::filesystem::is_regular_file(root / "WP1.runtime.json"))
                return root;
            return {};
        }
    /// <summary>snapshot はプレイ中の参照解決用にメタデータを保持します。編集時は既定の最新読み込みを使います。</summary>
    explicit AssetDatabase(std::filesystem::path root,bool snapshot=false) : AssetDatabase(ContentContext::Capture(root),snapshot) {}
    /// <summary>所有者が固定した素材層で索引を作成し、descriptorを再解決しません。</summary>
    explicit AssetDatabase(std::shared_ptr<const ContentContext> context, bool snapshot = false);
    std::filesystem::path Resolve(const std::filesystem::path& path,const std::string& id={}) const {
        if (!Valid(path)) throw std::runtime_error("Invalid asset reference");
        if (!id.empty()) {
            const auto found=index_->ids.find(id); if (found==index_->ids.end()) throw std::runtime_error("Missing asset ID: "+id);
            return found->second;
        }
        if (snapshot_ && index_->metadata.contains(Text(path))) return path;
        if (std::filesystem::is_regular_file(root_/path)) return path;
        const auto found=index_->aliases.find(Text(path));
        if(found==index_->aliases.end()) return path;
        if(found->second.size()!=1) throw std::runtime_error("Ambiguous asset alias: "+Text(path));
        return *found->second.begin();
    }
    /// <summary>参照先のIDを取得します。スナップショットでは保持済みメタデータだけを使用します。</summary>
    std::string ReferenceId(const std::filesystem::path& path) const {
        const auto saved=index_->metadata.find(Text(path));
        if (snapshot_) return saved==index_->metadata.end() ? std::string{} : saved->second.id;
        const auto raw = FileContents::Read(context_->Resolve(Sidecar(root_/path)));
        return raw ? ParseMetadata(*raw).id : std::string{};
    }
    void Move(const std::filesystem::path& from,const std::filesystem::path& to) {
        if (!ContentFiles::SharedFile(root_/from).empty() || !ContentFiles::SharedFile(root_/to).empty()) throw std::runtime_error("Shared content and overrides keep their asset ID and path");
        ValidateDestination(from,to);
        auto metadata=Ensure(root_/from); const auto originalMetadata=metadata;
        if(std::find(metadata.previousPaths.begin(),metadata.previousPaths.end(),Text(from))==metadata.previousPaths.end()) {
            if(metadata.previousPaths.size()>=1024) throw std::runtime_error("Asset alias history is full (maximum 1024 paths)");
            metadata.previousPaths.push_back(Text(from));
        }
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
  static bool SameCategory(const std::filesystem::path& from,const std::filesystem::path& to)
  {
      if(*from.begin()!=*to.begin() || std::next(from.begin())==from.end() || std::next(to.begin())==to.end()) return false;
      return *from.begin()!="Assets" || *std::next(from.begin())==*std::next(to.begin());
  }
  void ValidateDestination(const std::filesystem::path &from, const std::filesystem::path &to) const
  {
      if (!Valid(from) || !Valid(to) || from.extension() != to.extension() || from == to)
          throw std::runtime_error("Invalid asset destination or destination exists");
      if (!SameCategory(from,to) || std::filesystem::exists(root_ / to) || std::filesystem::exists(Sidecar(root_ / to)))
          throw std::runtime_error("Invalid asset destination or destination exists");
      const auto base = std::filesystem::weakly_canonical(root_);
      for (const auto &path : {from, to}) {
          const auto relative = std::filesystem::weakly_canonical(root_ / path).lexically_relative(base);
          if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") throw std::runtime_error("Asset destination leaves project root");
      }
  }

  /// <summary>シェーダー移動時に直接includeの相対位置を保持し、書き換えできない指定は移動前に拒否します。</summary>
  static void RelocateIncludeOperand(std::string& line,size_t directive,const std::filesystem::path& from,const std::filesystem::path& to)
  {
      const auto begin=line.find_first_not_of(" \t",directive+7);
      if(begin==std::string::npos || (line[begin]!='\"' && line[begin]!='<'))
          throw std::runtime_error("マクロを使うincludeがあるShaderは、相対参照を保つため同じフォルダー内で改名してください。");
      const auto end=line.find(line[begin]=='\"'?'\"':'>',begin+1);
      if(end==std::string::npos) throw std::runtime_error("Shaderのinclude指定が不正です。");
      const auto reference=Path(line.substr(begin+1,end-begin-1));
      if(!reference.is_absolute() && !reference.has_root_name())
          line.replace(begin+1,end-begin-1,Text((from.parent_path()/reference).lexically_normal().lexically_relative(to.parent_path())));
  }

  static void RelocateShaderLine(std::string& line,const std::filesystem::path& from,const std::filesystem::path& to)
  {
      auto directive=line.find_first_not_of(" \t");
      if(directive!=std::string::npos && line[directive]=='#')
      {
          directive=line.find_first_not_of(" \t",directive+1);
          if(directive!=std::string::npos && line.compare(directive,7,"include")==0 &&
              (directive+7==line.size() || std::isspace(static_cast<unsigned char>(line[directive+7])) || line[directive+7]=='\"' || line[directive+7]=='<'))
          {
              RelocateIncludeOperand(line,directive,from,to);
          }
      }
  }

  static std::string RelocateShaderIncludes(const std::string& original,const std::filesystem::path& from,const std::filesystem::path& to)
  {
      std::istringstream lines(original);std::string line,revised;
      while(std::getline(lines,line))
      {
          RelocateShaderLine(line,from,to);
          revised+=line+'\n';
      }
      return revised;
  }
  static std::string RelocateGltfResources(const std::string& original,const std::filesystem::path& from,const std::filesystem::path& to)
  {
      auto json = Json::parse(original);
      for (const auto *key : {"buffers", "images"}) {
          if(!json.contains(key)) continue;
          for(auto& resource:json[key]) {
              if(!resource.contains("uri")) continue;
              const auto uri=resource["uri"].get<std::string>();
              if(uri.starts_with("data:") || uri.find(":")!=std::string::npos) continue;
              resource["uri"]=Text((from.parent_path()/Path(uri)).lexically_normal().lexically_relative(to.parent_path()));
          }
      }
      return json.dump(2)+"\n";
  }

  void RewriteDependencies(const std::filesystem::path &from, const std::filesystem::path &to, std::string &original, std::string &revised) const
  {
      auto extension=Text(from.extension());
      std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
      if(from.parent_path()!=to.parent_path() && (extension==".hlsl" || extension==".hlsli")) {
          std::ifstream input(root_/from,std::ios::binary);original={std::istreambuf_iterator<char>(input),{}};
          if(!input || input.bad()) throw std::runtime_error("Shaderの移動前データを読み取れません。");
          revised=RelocateShaderIncludes(original,from,to);return;
      }
      if (from.parent_path() != to.parent_path() && (extension == ".gltf" || extension == ".obj")) {
          std::ifstream input(root_ / from, std::ios::binary); original = {std::istreambuf_iterator<char>(input), {}};
          if (extension == ".gltf") {
              revised=RelocateGltfResources(original,from,to);
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
