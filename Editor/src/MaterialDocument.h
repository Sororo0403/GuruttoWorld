#pragma once
#include <SceneRuntime/MaterialAsset.h>
#include <optional>
#include <chrono>

namespace Editor
{
    // Each asset retains its own draft. Failed loads never expose another asset's values.
    class MaterialDocument final
    {
    public:
        SceneRuntime::MaterialAsset& Asset() { return asset_; }
        bool Loaded() const { return loaded_; }
        bool Dirty() const { return loaded_ && Values(asset_)!=savedValues_; }
        bool Conflict() const { return conflict_; }
        void Refresh(const std::filesystem::path& root,const std::filesystem::path& path,bool force=false)
        {
            const auto now=std::chrono::steady_clock::now();
            if (!force && loaded_ && now<nextCheck_) return;
            nextCheck_=now+std::chrono::milliseconds(500);
            const auto source=Read(root/path);
            if (loaded_ && source==savedSource_) { conflict_=false; return; }
            if (Dirty()) { conflict_=true; return; }
            Reload(root,path);
        }
        void Reload(const std::filesystem::path& root,const std::filesystem::path& path)
        {
            const auto source=Read(root/path);
            auto candidate=SceneRuntime::MaterialAsset::Load(root,path);
            const auto values=Values(candidate);
            asset_=std::move(candidate); savedValues_=values; savedSource_=source;
            loaded_=true; conflict_=false;
        }
        void Save(const std::filesystem::path& root,const std::filesystem::path& path)
        {
            if (!loaded_) throw std::runtime_error("読み込みに成功していないMaterialは保存できません。");
            if (Read(root/path)!=savedSource_)
            { conflict_=true; throw std::runtime_error("Materialが外部で変更されています。編集を保持したまま保存を中止しました。"); }
            asset_.Save(root,path);
            savedSource_=Read(root/path); savedValues_=Values(asset_); conflict_=false;
        }
    private:
        static std::optional<std::string> Read(const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path)) return std::nullopt;
            std::ifstream stream(path,std::ios::binary);
            if (!stream) throw std::runtime_error("Materialを読み取れません。");
            std::string source{std::istreambuf_iterator<char>(stream),{}};
            if (stream.bad()) throw std::runtime_error("Materialを読み取れません。");
            return source;
        }
        static Engine::Json Values(const SceneRuntime::MaterialAsset& asset)
        {
            const auto& v=asset.values;
            return {{"color",v.color},{"roughness",v.roughness},{"metallic",v.metallic},
                {"transparent",v.transparent},{"physicallyBased",v.physicallyBased},{"normalFlipY",v.normalFlipY},
                {"scale",v.uv.scale},{"rotation",v.uv.rotation},{"translation",v.uv.translation},
                {"texture",asset.texture.generic_string()},{"normalTexture",asset.normalTexture.generic_string()}};
        }
        SceneRuntime::MaterialAsset asset_;
        Engine::Json savedValues_;
        std::optional<std::string> savedSource_;
        bool loaded_=false, conflict_=false;
        std::chrono::steady_clock::time_point nextCheck_{};
    };
}
