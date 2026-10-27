#pragma once
#include <SceneRuntime/Scene/SceneDocumentData.h>

namespace Engine
{
    class DirectX12Renderer;
}
namespace SceneRuntime
{
    class SceneRenderState;

    /// <summary>編集文書と表示状態を接続し、実行Worldを使わずに表示候補を準備します。</summary>
    class EditorSceneProjection final : public SceneDocumentProjection
    {
    public:
        /// <summary>ホストが所有する表示状態へ文書を接続します。表示状態は接続より長く保持してください。</summary>
        EditorSceneProjection(std::shared_ptr<SceneDocumentData> document, SceneRenderState& renderState);
        /// <summary>文書から表示の接続を解除します。準備候補は接続より先に破棄してください。</summary>
        ~EditorSceneProjection();
        /// <summary>文書に登録した接続と表示状態の借用を複製しません。</summary>
        EditorSceneProjection(const EditorSceneProjection&) = delete;
        /// <summary>登録済みの接続を置き換えません。</summary>
        EditorSceneProjection& operator=(const EditorSceneProjection&) = delete;
        /// <summary>文書と表示の準備に使う固定した素材参照を設定します。</summary>
        void SetContentContext(std::shared_ptr<const Engine::ContentContext> context);
        /// <summary>文書の準備権を確認し、現在の表示を保持したまま交換候補を作ります。</summary>
        std::unique_ptr<Prepared> PrepareDocument(SceneEditPlan& plan, bool reload, std::string& error) override;
        /// <summary>新しいGPU資源と文書・履歴を同じ取引で準備し、成功時だけ資源世代を交換します。</summary>
        bool ReloadAssets(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
                          const std::filesystem::path& shaderPath, std::string& error);
        /// <summary>確定済みの表示変更通知を一度だけ取得し、文書の準備中は取得を保留します。</summary>
        SceneChangeSet TakeChanges() noexcept;
        /// <summary>互換ファサードが使う文書の正本を借用します。編集は文書セッションを通してください。</summary>
        SceneLayout& BorrowLayout() noexcept;
        /// <summary>互換ファサードと表示接続で共有する変更通知の格納先を借用します。</summary>
        SceneChangeSet& BorrowChanges() noexcept;
        /// <summary>互換ファサードと文書候補で共有する次の採番値を借用します。</summary>
        size_t& BorrowNextId() noexcept;

    private:
        class Preparation;
        std::shared_ptr<SceneDocumentData> document_;
        SceneRenderState& renderState_;
        std::shared_ptr<const Engine::ContentContext> context_;
        SceneLayout& layout_;
        SceneChangeSet changes_;
        size_t nextId_ = 1;
        bool preparing_ = false;
        bool synchronized_ = false;
        SceneRenderState* reloading_ = nullptr;
        const SceneLayout* reloadLayout_ = nullptr;
    };
}
