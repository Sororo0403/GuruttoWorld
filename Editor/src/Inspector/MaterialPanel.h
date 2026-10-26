#pragma once
#include "../Project/ProjectCatalog.h"
#include "../Documents/MaterialDocument.h"
#include <map>
#include <SceneRuntime/Rendering/MaterialAsset.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace Editor
{
    class MaterialPanel final
    {
    public:
        /// <summary>保存前の変更があるかを確認します。</summary>
        bool HasChanges() const;
        bool CanUndo(const std::filesystem::path& path, bool redo) const
        {
            const auto found = documents_.find(path);
            return found != documents_.end() && (redo ? found->second.CanRedo() : found->second.CanUndo());
        }
        bool Undo(const std::filesystem::path& path, bool redo)
        {
            const auto found = documents_.find(path);
            return found != documents_.end() && found->second.Undo(redo);
        }
        /// <summary>編集中の候補を取得します。</summary>
        SceneRuntime::MaterialAsset Draft(const std::filesystem::path& root, const std::filesystem::path& path);
        /// <summary>未保存Materialを一つの取引で保存し、途中失敗時は先行する書き込みも戻します。</summary>
        void SaveAll(const std::filesystem::path& root);
        /// <summary>未保存Materialをすべて検査し、ファイルへ書かずに一括保存へ追加します。</summary>
        void PrepareSaves(const std::filesystem::path& root, std::vector<DocumentSavePlan>& plans);
        /// <summary>移動先に未保存のMaterial編集がある場合はファイル移動前に中止します。</summary>
        void ValidateMove(const std::filesystem::path& source, const std::filesystem::path& destination) const;
        /// <summary>古い移動先の読み込みデータを除去し、移動元の編集と履歴を引き継ぎます。</summary>
        void AssetMoved(const std::filesystem::path& source, const std::filesystem::path& destination);
        /// <summary>編集画面を描画します。</summary>
        void Draw(const std::filesystem::path& root, const std::filesystem::path& path, const ProjectCatalog& catalog,
                  bool enabled);

    private:
        /// <summary>シェーダーの編集画面を描画します。</summary>
        void DrawShader(SceneRuntime::MaterialAsset& asset_, const ProjectCatalog& catalog);

        /// <summary>テクスチャを選択します。</summary>
        static void ChooseTexture(const char* label, const char* emptyLabel, std::filesystem::path& texture,
                                  const ProjectCatalog& catalog);
        /// <summary>テクスチャの編集画面を描画します。</summary>
        void DrawTextures(SceneRuntime::MaterialAsset& asset_, const ProjectCatalog& catalog);

        /// <summary>ドキュメント操作の編集画面を描画します。</summary>
        void DrawDocumentActions(MaterialDocument& document, const std::filesystem::path& root,
                                 const std::filesystem::path& path);

        /// <summary>再読み込みの編集画面を描画します。</summary>
        void DrawReload(MaterialDocument& document, const std::filesystem::path& root,
                        const std::filesystem::path& path, bool enabled);

        /// <summary>保存内容を再読み込みします。</summary>
        void Reload(MaterialDocument& document, const std::filesystem::path& root, const std::filesystem::path& path);
        std::map<std::filesystem::path, MaterialDocument> documents_;
        std::string error_;
    };
}
