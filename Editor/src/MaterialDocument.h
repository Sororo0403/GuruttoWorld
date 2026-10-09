#pragma once
#include <SceneRuntime/MaterialAsset.h>
#include <optional>
#include <chrono>
#include <vector>
#include <string_view>

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
        void AssetMoved(const std::filesystem::path& source,const std::filesystem::path& destination) {
            const auto remap=[&](SceneRuntime::MaterialAsset& asset) {
                if(asset.texture==source) asset.texture=destination;
                if(asset.normalTexture==source) asset.normalTexture=destination;
                if(asset.environmentTexture==source) asset.environmentTexture=destination;
                if(asset.lightmap==source) asset.lightmap=destination;
            };
            remap(asset_);for(auto& state:history_) remap(state);if(pending_) remap(*pending_);
            for(const char* key:{"texture","normalTexture","environmentTexture","lightmap"}) if(savedValues_.contains(key) && savedValues_[key]==Text(source)) savedValues_[key]=Text(destination);
        }
        bool CanUndo() const {return loaded_ && (cursor_>0 || (!history_.empty() && Values(asset_)!=Values(history_[cursor_])));}
        bool CanRedo() const {return loaded_ && cursor_+1<history_.size() && Values(asset_)==Values(history_[cursor_]);}
        void Observe(std::string_view interaction) {
            if(!loaded_) return;
            if(!interaction.empty() && interaction_!=interaction && pending_) Commit();
            interaction_=interaction;pending_=asset_;
            if(interaction.empty()) Commit();
        }
        bool Undo(bool redo=false) {
            Commit();
            if(redo ? !CanRedo():!CanUndo()) return false;
            if(redo) ++cursor_;else --cursor_;
            asset_=history_[cursor_];return true;
        }
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
            history_={asset_};cursor_=0;pending_.reset();interaction_.clear();
        }
        void Save(const std::filesystem::path& root,const std::filesystem::path& path)
        {
            if (!loaded_) throw std::runtime_error("読み込みに成功していないMaterialは保存できません。");
            pending_=asset_;Commit();
            if (Read(root/path)!=savedSource_)
            { conflict_=true; throw std::runtime_error("Materialが外部で変更されています。編集を保持したまま保存を中止しました。"); }
            asset_.Save(root,path);
            savedSource_=Read(root/path); savedValues_=Values(asset_); conflict_=false;
        }
    private:
        static std::string Text(const std::filesystem::path& path) {const auto bytes=path.generic_u8string();return {bytes.begin(),bytes.end()};}
        void Commit() {
            interaction_.clear();
            if(!loaded_) return;
            if(!pending_) pending_=asset_;
            if(history_.empty()) {history_.push_back(asset_);cursor_=0;}
            if(Values(*pending_)!=Values(history_[cursor_])) {
                history_.resize(cursor_+1);history_.push_back(std::move(*pending_));
                if(history_.size()>101) history_.erase(history_.begin());
                cursor_=history_.size()-1;
            }
            pending_.reset();
        }
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
                {"texture",Text(asset.texture)},{"normalTexture",Text(asset.normalTexture)},
                {"environmentTexture",Text(asset.environmentTexture)},{"environmentIntensity",v.environmentIntensity},{"lightmap",Text(asset.lightmap)}};
        }
        SceneRuntime::MaterialAsset asset_;
        Engine::Json savedValues_;
        std::optional<std::string> savedSource_;
        bool loaded_=false, conflict_=false;
        std::chrono::steady_clock::time_point nextCheck_{};
        std::vector<SceneRuntime::MaterialAsset> history_;
        size_t cursor_=0;
        std::optional<SceneRuntime::MaterialAsset> pending_;
        std::string interaction_;
    };
}
