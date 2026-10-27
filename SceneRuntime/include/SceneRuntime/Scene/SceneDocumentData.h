#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <SceneRuntime/Scene/WorldReadView.h>
#include <SceneRuntime/Scene/SceneEditTarget.h>
#include <memory>
#include <SceneRuntime/Scene/SceneDocumentProjection.h>

namespace Engine
{
    class ContentContext;
}

namespace SceneRuntime
{
    class SceneDocumentData final : public SceneEditTarget
    {
    public:
        /// <summary>描画や実行を所有しない編集データを作成します。</summary>
        explicit SceneDocumentData(SceneLayout layout = {}, std::shared_ptr<const Engine::ContentContext> context = {})
            : layout_(std::move(layout)), context_(std::move(context))
        {
            layout_.Validate();
        }
        /// <summary>複製は独立したデータとし、投影の所有権を引き継ぎません。</summary>
        SceneDocumentData(const SceneDocumentData& other) : layout_(other.layout_), context_(other.context_) {}
        /// <summary>投影中のデータは、投影の確定を通さない変更から保護します。</summary>
        SceneDocumentData& operator=(const SceneDocumentData& other);
        /// <summary>保存・履歴・実行の複製元となるドキュメントを取得します。</summary>
        const SceneLayout& Layout() const override
        {
            return layout_;
        }
        /// <summary>次の編集まで有効な、描画に依存しない読み取りビューを取得します。</summary>
        WorldReadView Read() const noexcept
        {
            return WorldReadView(layout_);
        }
        /// <summary>投影を持たない文書をCPUだけで編集します。投影中は所有側で候補を準備してください。</summary>
        SceneEditResult Edit(const SceneEdit& edit, std::string& error) override;
        /// <summary>成功した文書編集の未取得通知を一度だけ取得します。</summary>
        SceneChangeSet TakeChanges() noexcept;
        /// <summary>編集開始時に固定した参照情報を取得します。</summary>
        const std::shared_ptr<const Engine::ContentContext>& Content() const
        {
            return context_;
        }

        class Transaction final
        {
        public:
            /// <summary>未確定候補を破棄し、文書の準備中状態を解除します。</summary>
            ~Transaction();
            /// <summary>候補の単一書込所有者を複製しません。</summary>
            Transaction(const Transaction&) = delete;
            /// <summary>所有中の候補を置き換えません。</summary>
            Transaction& operator=(const Transaction&) = delete;
            /// <summary>保存値と採番の未公開候補を取得します。</summary>
            SceneEditPlan& Plan() noexcept;
            /// <summary>表示側の参照解決後に検証し、成功通知を準備します。</summary>
            bool PrepareCommit(std::string& error);
            /// <summary>準備済みの文書・採番・通知を確保なしで交換します。</summary>
            bool Commit() noexcept;
            /// <summary>全参加先の確定後に編集権を解放します。未確定なら候補を取消します。</summary>
            void Finish() noexcept;

        private:
            friend class SceneDocumentData;
            /// <summary>この文書だけに所属する編集候補を所有します。</summary>
            Transaction(SceneDocumentData& owner, SceneEditPlan plan);
            SceneDocumentData& owner_;
            SceneEditPlan plan_;
            SceneChangeSet publication_;
            bool ready_ = false;
            bool committed_ = false;
            bool locked_ = true;
        };
        /// <summary>単一の編集所有者だけが書き込める未公開候補を作ります。</summary>
        std::unique_ptr<Transaction> BeginEdit(const SceneEdit& edit, std::string& error,
                                               const SceneEditTarget* owner = nullptr);
        /// <summary>編集・履歴の確定を調停する所有者を登録します。</summary>
        void AttachEditor(SceneEditTarget& editor);
        /// <summary>この文書を調停している所有者だけを登録解除します。</summary>
        void DetachEditor(const SceneEditTarget& editor) noexcept;
        /// <summary>登録した編集所有者を取得します。</summary>
        SceneEditTarget* Editor() const noexcept;
        /// <summary>表示アダプターを一つ登録し、互換投影が使う正本を借用します。</summary>
        SceneLayout& AttachProjection(SceneDocumentProjection& projection);
        /// <summary>登録した表示アダプターだけを解除します。</summary>
        void DetachProjection(const SceneDocumentProjection& projection) noexcept;
        /// <summary>登録した表示準備先を取得します。</summary>
        SceneDocumentProjection* Projection() const noexcept;
        /// <summary>登録した表示アダプターから固定した参照情報を受け取ります。</summary>
        void SetProjectionContent(const SceneDocumentProjection& projection,
                                  std::shared_ptr<const Engine::ContentContext> context);
        /// <summary>未公開の文書候補が所有されているか返します。</summary>
        bool Preparing() const noexcept;

    private:
        SceneDocumentProjection* projection_ = nullptr;
        SceneEditTarget* editor_ = nullptr;
        bool preparing_ = false;
        SceneLayout layout_;
        std::shared_ptr<const Engine::ContentContext> context_;
        SceneChangeSet changes_;
        size_t nextId_ = 1;
    };
}
