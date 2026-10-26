#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <SceneRuntime/Scene/WorldReadView.h>
#include <SceneRuntime/Scene/SceneEditTarget.h>
#include <memory>

namespace Engine
{
    class ContentContext;
}

namespace SceneRuntime
{
    class SceneWorld;
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

    private:
        friend class SceneWorld;
        SceneWorld* view_ = nullptr;
        SceneLayout layout_;
        std::shared_ptr<const Engine::ContentContext> context_;
        SceneChangeSet changes_;
        size_t nextId_ = 1;
    };
}
