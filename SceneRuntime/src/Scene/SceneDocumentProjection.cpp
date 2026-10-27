#include <SceneRuntime/Scene/SceneWorld.h>
#include <SceneRuntime/Rendering/SceneRenderState.h>
#include <SceneRuntime/Scene/SceneAssetReferences.h>
#include <SceneRuntime/Scene/Prefab.h>

namespace SceneRuntime
{
    class SceneWorld::DocumentPreparation final : public SceneDocumentProjection::Prepared
    {
    public:
        /// <summary>文書の準備中は表示所有者の再入と通知取得を禁止します。</summary>
        explicit DocumentPreparation(SceneWorld& world) : world_(world), guard_(world.preparingChanges_) {}
        /// <summary>投影の候補だけで参照・描画資源・通知・退役を準備します。</summary>
        bool Prepare(SceneEditPlan& plan, bool reload, std::string& error)
        {
            nextId_ = plan.nextId;
            publish_ = world_.documentReload_ || reload || !plan.changes.Empty() || !world_.renderState_->Read().assets;
            if (!publish_)
                return true;
            auto layout = plan.layout;
            std::string diagnostic;
            const auto root = world_.context_ ? world_.context_->Root() : world_.renderState_->Read().assetsRoot;
            if (reload)
            {
                try
                {
                    static_cast<void>(
                        ResolveSceneAssetReferencesForEditing(layout, Engine::AssetDatabase(world_.context_, true)));
                    Prefab::Refresh(layout, root, {}, world_.context_);
                }
                catch (const std::exception& failure)
                {
                    diagnostic = failure.what();
                }
            }
            const bool reuse =
                !reload && world_.renderState_->Read().assets && SameRenderResources(layout, world_.layout_);
            if (world_.documentReload_)
                resources_ = world_.documentReload_->RuntimeLayout();
            else if (reuse)
            {
                resources_ = world_.RuntimeLayout();
                resources_.layout = std::move(layout);
                if (!PrepareTransforms(resources_.layout, resources_.objects, error, &world_.layout_))
                    return false;
                if (!world_.renderState_->PrepareShadows(resources_, error))
                    return false;
            }
            else if (!world_.PrepareLayout(std::move(layout), root, error, true, resources_))
                return false;
            if (!diagnostic.empty())
                resources_.diagnostics.push_back(diagnostic);
            plan.layout = resources_.layout;
            plan.changes = SceneChangeSet::Compare(world_.layout_, plan.layout);
            changes_ = world_.changes_;
            changes_.Merge(plan.changes);
            if (world_.documentReload_)
                changes_.fields.push_back({{}, {}, "resources.reload"});
            packet_ = world_.renderState_->PrepareEditor(resources_, error);
            if (world_.renderState_->Gpu().Ready() && !packet_)
                return false;
            return world_.renderState_->PrepareRetirement(resources_, error);
        }
        /// <summary>文書・履歴の候補と同じ成功境界で、表示と通知と採番を交換します。</summary>
        void Commit() noexcept override
        {
            if (committed_)
                return;
            if (publish_)
            {
                packet_ =
                    world_.documentReload_
                        ? world_.renderState_->CommitReload(*world_.documentReload_->renderState_, std::move(packet_))
                        : world_.renderState_->CommitPrepared(resources_, std::move(packet_), true);
                world_.changes_.Swap(changes_);
            }
            world_.nextObjectId_ = nextId_;
            committed_ = true;
        }
        /// <summary>文書・履歴も確定した後に投影の準備権を解放します。</summary>
        void Finish() noexcept override
        {
            guard_.Release();
        }

    private:
        SceneWorld& world_;
        ChangePreparation guard_;
        PreparedSceneResources resources_;
        std::shared_ptr<const RenderPacket> packet_;
        SceneChangeSet changes_;
        size_t nextId_ = 1;
        bool publish_ = false, committed_ = false;
    };
    SceneEditTarget* SceneWorld::DocumentEditor() const noexcept
    {
        return IsDocumentView() ? document_->Editor() : nullptr;
    }
    std::unique_ptr<SceneDocumentProjection::Prepared> SceneWorld::PrepareDocument(SceneEditPlan& plan, bool reload,
                                                                                   std::string& error)
    {
        if (!IsDocumentView() || preparingChanges_ || !document_->Preparing())
        {
            error = "Document projection is unavailable during frame preparation";
            return {};
        }
        auto prepared = std::make_unique<DocumentPreparation>(*this);
        return prepared->Prepare(plan, reload, error) ? std::move(prepared) : nullptr;
    }
    SceneEditResult SceneWorld::ForwardDocumentEdit(const SceneEdit& edit, const std::filesystem::path& root,
                                                    std::string& error)
    {
        if (!CanMutateScene(&error))
            return {};
        try
        {
            if (!root.empty() && context_)
                context_->RequireRoot(root);
            auto* editor = DocumentEditor();
            return editor ? editor->Edit(edit, error) : SceneEditResult{};
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return {};
        }
    }

}
