#include "MaterialDocument.h"

namespace Editor
{
    auto MaterialDocument::AssetMoved(const std::filesystem::path& source, const std::filesystem::path& destination)
        -> void
    {
        const auto remap = [&](SceneRuntime::MaterialAsset& asset)
        {
            if (asset.texture == source)
                asset.texture = destination;
            if (asset.normalTexture == source)
                asset.normalTexture = destination;
            if (asset.environmentTexture == source)
                asset.environmentTexture = destination;
            if (asset.lightmap == source)
                asset.lightmap = destination;
            if (asset.shader == source)
                asset.shader = destination;
        };
        remap(asset_);
        for (auto& state : history_)
            remap(state);
        if (pending_)
            remap(*pending_);
        for (const char* key : {"texture", "normalTexture", "environmentTexture", "lightmap", "shader"})
            if (savedValues_.contains(key) && savedValues_[key] == Text(source))
                savedValues_[key] = Text(destination);
    }

    auto MaterialDocument::Observe(std::string_view interaction) -> void
    {
        if (!loaded_)
            return;
        if (!interaction.empty() && interaction_ != interaction && pending_)
            Commit();
        interaction_ = interaction;
        pending_ = asset_;
        if (interaction.empty())
            Commit();
    }

    auto MaterialDocument::Undo(bool redo) -> bool
    {
        Commit();
        if (redo ? !CanRedo() : !CanUndo())
            return false;
        if (redo)
            ++cursor_;
        else
            --cursor_;
        asset_ = history_[cursor_];
        return true;
    }

    auto MaterialDocument::Refresh(const std::filesystem::path& root, const std::filesystem::path& path, bool force)
        -> void
    {
        const auto now = std::chrono::steady_clock::now();
        if (!force && loaded_ && now < nextCheck_)
            return;
        nextCheck_ = now + std::chrono::milliseconds(500);
        const auto source = Read(root / path);
        if (loaded_ && source == savedSource_)
        {
            conflict_ = false;
            return;
        }
        if (Dirty())
        {
            conflict_ = true;
            return;
        }
        Reload(root, path);
    }

    auto MaterialDocument::Reload(const std::filesystem::path& root, const std::filesystem::path& path) -> void
    {
        const auto source = Read(root / path);
        auto candidate = SceneRuntime::MaterialAsset::Load(root, path);
        const auto values = Values(candidate);
        asset_ = std::move(candidate);
        savedValues_ = values;
        savedSource_ = source;
        loaded_ = true;
        conflict_ = false;
        history_ = {asset_};
        cursor_ = 0;
        pending_.reset();
        interaction_.clear();
    }

    auto MaterialDocument::PrepareSave(const std::filesystem::path& root, const std::filesystem::path& path)
        -> DocumentSavePlan
    {
        if (!loaded_)
            throw std::runtime_error("読み込みに成功していないMaterialは保存できません。");
        pending_ = asset_;
        Commit();
        auto file = FileTransaction::Prepare(root / path, asset_.SerializeForSave(root, path));
        if (file.before != savedSource_)
        {
            conflict_ = true;
            throw std::runtime_error("Materialが外部で変更されています。編集を保持したまま保存を中止しました。");
        }
        auto accept = [this, source = file.after, values = Values(asset_)]() mutable noexcept
        {
            savedSource_.swap(source);
            savedValues_.swap(values);
            conflict_ = false;
        };
        return {std::move(file), std::move(accept)};
    }

    auto MaterialDocument::Commit() -> void
    {
        interaction_.clear();
        if (!loaded_)
            return;
        if (!pending_)
            pending_ = asset_;
        if (history_.empty())
        {
            history_.push_back(asset_);
            cursor_ = 0;
        }
        if (Values(*pending_) != Values(history_[cursor_]))
        {
            history_.resize(cursor_ + 1);
            history_.push_back(std::move(*pending_));
            if (history_.size() > 101)
                history_.erase(history_.begin());
            cursor_ = history_.size() - 1;
        }
        pending_.reset();
    }

    auto MaterialDocument::Values(const SceneRuntime::MaterialAsset& asset) -> Engine::Json
    {
        const auto& v = asset.values;
        return {{"shader", Text(asset.shader)},
                {"color", v.color},
                {"roughness", v.roughness},
                {"metallic", v.metallic},
                {"shaderParameters", v.shaderParameters},
                {"transparent", v.transparent},
                {"physicallyBased", v.physicallyBased},
                {"normalFlipY", v.normalFlipY},
                {"scale", v.uv.scale},
                {"rotation", v.uv.rotation},
                {"translation", v.uv.translation},
                {"texture", Text(asset.texture)},
                {"normalTexture", Text(asset.normalTexture)},
                {"environmentTexture", Text(asset.environmentTexture)},
                {"environmentIntensity", v.environmentIntensity},
                {"lightmap", Text(asset.lightmap)}};
    }
}
