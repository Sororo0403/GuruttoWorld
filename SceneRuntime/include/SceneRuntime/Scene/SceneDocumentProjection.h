#pragma once
#include <SceneRuntime/Scene/SceneEdits.h>
#include <memory>

namespace SceneRuntime
{
    /// <summary>文書の候補を表示へ投影し、文書・履歴と同じ成功境界で確定します。</summary>
    class SceneDocumentProjection
    {
    public:
        class Prepared
        {
        public:
            /// <summary>未確定の表示候補を破棄し、現在の表示を保持します。</summary>
            virtual ~Prepared() = default;
            /// <summary>準備した表示と通知を、確保や利用者コールバックなしで確定します。</summary>
            virtual void Commit() noexcept = 0;
            /// <summary>全参加先の確定後に再入禁止を解除します。旧資源は候補の破棄まで保持できます。</summary>
            virtual void Finish() noexcept {}
        };
        /// <summary>文書から投影先を外した後に破棄します。</summary>
        virtual ~SceneDocumentProjection() = default;
        /// <summary>候補内だけで参照を解決し、表示の交換候補を返します。</summary>
        virtual std::unique_ptr<Prepared> PrepareDocument(SceneEditPlan& plan, bool reload, std::string& error) = 0;
    };
}
