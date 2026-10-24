#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <memory>
#include <Engine/Assets/ContentContext.h>

namespace SceneRuntime
{
    class SceneWorld;
    class SceneDocumentData final
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
        SceneDocumentData& operator=(const SceneDocumentData& other)
        {
            if (this == &other)
                return *this;
            if (view_ && (layout_.Serialize() != other.layout_.Serialize() || context_ != other.context_))
                throw std::runtime_error("Document changes require a projection transaction");
            layout_ = other.layout_;
            context_ = other.context_;
            return *this;
        }
        /// <summary>保存・履歴・実行の複製元となるドキュメントを取得します。</summary>
        const SceneLayout& Layout() const
        {
            return layout_;
        }
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
    };
}
