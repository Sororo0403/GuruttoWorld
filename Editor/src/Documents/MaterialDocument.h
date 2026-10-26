#pragma once
#include <SceneRuntime/Rendering/MaterialAsset.h>
#include <optional>
#include <chrono>
#include <vector>
#include <string_view>
#include "DocumentSave.h"

namespace Editor
{
    // 各アセットが個別の編集中データを保持します。読み込みに失敗しても別のアセットの値を表示しません。
    class MaterialDocument final
    {
    public:
        SceneRuntime::MaterialAsset& Asset()
        {
            return asset_;
        }
        bool Loaded() const
        {
            return loaded_;
        }
        bool Dirty() const
        {
            return loaded_ && Values(asset_) != savedValues_;
        }
        bool Conflict() const
        {
            return conflict_;
        }
        /// <summary>素材移動後の参照を更新します。</summary>
        void AssetMoved(const std::filesystem::path& source, const std::filesystem::path& destination);
        bool CanUndo() const
        {
            return loaded_ && (cursor_ > 0 || (!history_.empty() && Values(asset_) != Values(history_[cursor_])));
        }
        bool CanRedo() const
        {
            return loaded_ && cursor_ + 1 < history_.size() && Values(asset_) == Values(history_[cursor_]);
        }
        /// <summary>素材の変更を監視対象へ追加します。</summary>
        void Observe(std::string_view interaction);
        /// <summary>履歴に従って変更を戻し、または再適用します。</summary>
        bool Undo(bool redo = false);
        /// <summary>現在の素材から依存情報を更新します。</summary>
        void Refresh(const std::filesystem::path& root, const std::filesystem::path& path, bool force = false);
        /// <summary>保存内容を再読み込みします。</summary>
        void Reload(const std::filesystem::path& root, const std::filesystem::path& path);
        /// <summary>競合を検査してMaterialを保存し、成功後に保存基準を更新します。</summary>
        void Save(const std::filesystem::path& root, const std::filesystem::path& path)
        {
            DocumentSave::Apply({PrepareSave(root, path)});
        }
        /// <summary>履歴を確定して競合を検査し、保存基準は一括保存の成功まで保持します。</summary>
        DocumentSavePlan PrepareSave(const std::filesystem::path& root, const std::filesystem::path& path);

    private:
        static std::string Text(const std::filesystem::path& path)
        {
            const auto bytes = path.generic_u8string();
            return {bytes.begin(), bytes.end()};
        }
        /// <summary>準備済みの変更を確定します。</summary>
        void Commit();
        static std::optional<std::string> Read(const std::filesystem::path& path)
        {
            return Engine::FileContents::Read(path);
        }
        /// <summary>保存する値の範囲を検証します。</summary>
        static Engine::Json Values(const SceneRuntime::MaterialAsset& asset);
        SceneRuntime::MaterialAsset asset_;
        Engine::Json savedValues_;
        std::optional<std::string> savedSource_;
        bool loaded_ = false, conflict_ = false;
        std::chrono::steady_clock::time_point nextCheck_{};
        std::vector<SceneRuntime::MaterialAsset> history_;
        size_t cursor_ = 0;
        std::optional<SceneRuntime::MaterialAsset> pending_;
        std::string interaction_;
    };
}
