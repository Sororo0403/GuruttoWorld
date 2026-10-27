#pragma once
#include "SceneDocument.h"
#include "SceneSnapshotCache.h"
#include "../Editing/EditHistory.h"
#include "../Editing/EditState.h"

namespace Editor
{
    /// <summary>文書・表示候補・選択・履歴を準備し、同じ成功境界で確定します。</summary>
    class DocumentSession final : public SceneRuntime::SceneEditTarget
    {
    public:
        class Prepared final
        {
        public:
            /// <summary>未確定の文書・表示・履歴を破棄し、現在の状態を保持します。</summary>
            ~Prepared();
            /// <summary>準備済みの候補を確保なしで一度だけ確定します。</summary>
            bool Commit() noexcept;
            /// <summary>確定前に成功結果の領域を呼出側へ準備できます。</summary>
            const SceneRuntime::SceneEditResult& Result() const noexcept;
            /// <summary>候補内で採番した個体IDを取得します。確定前は外部へ公開しません。</summary>
            const std::vector<std::string>& CreatedIds() const noexcept;

        private:
            friend class DocumentSession;
            struct Impl;
            /// <summary>セッションが準備した候補だけを受け取ります。</summary>
            explicit Prepared(std::unique_ptr<Impl> impl);
            std::unique_ptr<Impl> impl_;
        };
        /// <summary>描画に依存せず、文書と編集状態と履歴の所有者を作成します。</summary>
        explicit DocumentSession(std::filesystem::path path = {},
                                 std::shared_ptr<const Engine::ContentContext> context = {});
        /// <summary>文書から編集の所有者を解除します。</summary>
        ~DocumentSession();
        /// <summary>文書と編集所有権を暗黙に複製しません。</summary>
        DocumentSession(const DocumentSession&) = delete;
        /// <summary>接続済みの編集所有者を置き換えません。</summary>
        DocumentSession& operator=(const DocumentSession&) = delete;
        /// <summary>保存先・外部変更照合・保存取引を担当する文書を取得します。</summary>
        SceneDocument& Document() noexcept;
        /// <summary>選択と現在の操作単位を取得します。</summary>
        EditState& Editing() noexcept;
        /// <summary>確定済みの履歴を読み取ります。</summary>
        const EditHistory& History() const noexcept;
        /// <summary>文書の正本を読み取ります。</summary>
        const SceneRuntime::SceneLayout& Layout() const override;
        /// <summary>履歴・保存・復旧に使用する現在のJSONを取得します。</summary>
        const std::string& Json();
        /// <summary>現在の操作単位で文書・表示・履歴の候補を準備します。</summary>
        std::unique_ptr<Prepared> Prepare(const SceneRuntime::SceneEdit& edit, std::string& error);
        /// <summary>すべての準備が成功した編集だけを確定します。</summary>
        SceneRuntime::SceneEditResult Edit(const SceneRuntime::SceneEdit& edit, std::string& error) override;
        /// <summary>一回の読込から文書・表示・保存基準と初期履歴を準備します。</summary>
        bool Open(const std::filesystem::path& path, std::string& error);
        /// <summary>確認済みの読込要求を、保存基準・履歴と一緒に適用します。</summary>
        bool ApplyPending(std::string& error);
        /// <summary>Prefabなどのファイル変更と、文書・表示・履歴を同じ取引で確定します。</summary>
        bool ApplyFiles(SceneRuntime::SceneLayout layout, std::vector<FileChange> files, std::string& error);
        /// <summary>文書・表示・選択・ファイルとUndo/Redoの位置を一緒に復元します。</summary>
        bool RestoreHistory(bool redo, std::string& error);
        /// <summary>選択や操作終了を現在の履歴へ反映します。編集のないフレームではJSONを複製しません。</summary>
        void ObserveCurrent();
        /// <summary>継続中の編集操作を履歴へ区切ります。</summary>
        void CommitInteraction();
        /// <summary>現在の文書を履歴の初期値へ戻します。</summary>
        void ResetHistory();
        /// <summary>保存の成功後に履歴の保存基準を更新します。</summary>
        void MarkSaved();

    private:
        struct Request;
        /// <summary>読込・Undo・ファイル変更も共通の候補準備へ渡します。</summary>
        std::unique_ptr<Prepared> PrepareRequest(const SceneRuntime::SceneEdit& edit, Request request,
                                                 std::string& error);
        /// <summary>現在の選択と文書を履歴値にまとめます。</summary>
        EditHistory::State Snapshot(const std::string& json) const;
        SceneDocument document_;
        EditHistory history_;
        EditState editing_;
        SceneSnapshotCache snapshots_;
        bool preparing_ = false;
    };
}
