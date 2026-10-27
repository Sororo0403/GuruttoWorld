#include <SceneRuntime/Rendering/EditorSceneProjection.h>
#include <SceneRuntime/Rendering/SceneRenderState.h>
#include <SceneRuntime/Scene/SceneAssetReferences.h>
#include <SceneRuntime/Scene/Prefab.h>

namespace SceneRuntime
{
    class EditorSceneProjection::Preparation final : public SceneDocumentProjection::Prepared
    {
    public:
        /// <summary>文書の準備中は表示所有者の再入と通知取得を禁止します。</summary>
        explicit Preparation(EditorSceneProjection& owner) : owner_(owner)
        {
            owner_.preparing_ = true;
        }
        /// <summary>未確定候補を破棄して準備権を解放します。</summary>
        ~Preparation() override
        {
            Finish();
        }
        /// <summary>投影の候補だけで参照・描画資源・通知・退役を準備します。</summary>
        bool Prepare(SceneEditPlan& plan, bool reload, std::string& error)
        {
            nextId_ = plan.nextId;
            publish_ = !owner_.synchronized_ || owner_.reloading_ || reload || !plan.changes.Empty() ||
                       !owner_.renderState_.Read().assets;
            if (!publish_)
                return true;
            auto layout = plan.layout;
            std::string diagnostic;
            const auto root = owner_.context_ ? owner_.context_->Root() : owner_.renderState_.Read().assetsRoot;
            if (reload)
            {
                try
                {
                    static_cast<void>(
                        ResolveSceneAssetReferencesForEditing(layout, Engine::AssetDatabase(owner_.context_, true)));
                    Prefab::Refresh(layout, root, {}, owner_.context_);
                }
                catch (const std::exception& failure)
                {
                    diagnostic = failure.what();
                }
            }
            const bool reuse = owner_.synchronized_ && !reload && owner_.renderState_.Read().assets &&
                               SceneRenderState::SameResources(layout, owner_.layout_);
            if (owner_.reloading_)
                resources_ = owner_.reloading_->Capture(*owner_.reloadLayout_);
            else if (reuse)
            {
                resources_ = owner_.renderState_.Capture(owner_.layout_);
                resources_.layout = std::move(layout);
                if (!SceneRenderState::PrepareTransforms(resources_.layout, resources_.objects, error, &owner_.layout_))
                    return false;
                if (!owner_.renderState_.PrepareShadows(resources_, error))
                    return false;
            }
            else if (!owner_.renderState_.Gpu().PrepareLayout(std::move(layout), root, error, true, resources_, nullptr,
                                                              owner_.renderState_.View(owner_.layout_), true,
                                                              owner_.context_))
                return false;
            if (!diagnostic.empty())
                resources_.diagnostics.push_back(diagnostic);
            plan.layout = resources_.layout;
            plan.changes = SceneChangeSet::Compare(owner_.layout_, plan.layout);
            changes_ = owner_.changes_;
            changes_.Merge(plan.changes);
            if (owner_.reloading_)
                changes_.fields.push_back({{}, {}, "resources.reload"});
            packet_ = owner_.renderState_.PrepareEditor(resources_, error);
            if (owner_.renderState_.Gpu().Ready() && !packet_)
                return false;
            return owner_.renderState_.PrepareRetirement(resources_, error);
        }
        /// <summary>文書・履歴の候補と同じ成功境界で、表示と通知と採番を交換します。</summary>
        void Commit() noexcept override
        {
            if (committed_)
                return;
            if (publish_)
            {
                packet_ = owner_.reloading_ ? owner_.renderState_.CommitReload(*owner_.reloading_, std::move(packet_))
                                            : owner_.renderState_.CommitPrepared(resources_, std::move(packet_), true);
                owner_.changes_.Swap(changes_);
            }
            owner_.nextId_ = nextId_;
            owner_.synchronized_ = true;
            committed_ = true;
        }
        /// <summary>文書・履歴も確定した後に投影の準備権を解放します。</summary>
        void Finish() noexcept override
        {
            if (locked_)
            {
                owner_.preparing_ = false;
                locked_ = false;
            }
        }

    private:
        EditorSceneProjection& owner_;
        bool locked_ = true;
        PreparedSceneResources resources_;
        std::shared_ptr<const RenderPacket> packet_;
        SceneChangeSet changes_;
        size_t nextId_ = 1;
        bool publish_ = false, committed_ = false;
    };
    EditorSceneProjection::EditorSceneProjection(std::shared_ptr<SceneDocumentData> document,
                                                 SceneRenderState& renderState)
        : document_(document ? std::move(document) : throw std::invalid_argument("Document is null")),
          renderState_(renderState), context_(document_->Content()), layout_(document_->AttachProjection(*this))
    {
    }
    EditorSceneProjection::~EditorSceneProjection()
    {
        document_->DetachProjection(*this);
    }
    void EditorSceneProjection::SetContentContext(std::shared_ptr<const Engine::ContentContext> context)
    {
        if (preparing_ || !context || (context_ && context_ != context))
            throw std::runtime_error("Content context is already owned or preparing");
        document_->SetProjectionContent(*this, context);
        context_ = std::move(context);
    }
    SceneChangeSet EditorSceneProjection::TakeChanges() noexcept
    {
        SceneChangeSet result;
        if (!preparing_ && !document_->Preparing())
            changes_.Swap(result);
        return result;
    }
    SceneLayout& EditorSceneProjection::BorrowLayout() noexcept
    {
        return layout_;
    }
    SceneChangeSet& EditorSceneProjection::BorrowChanges() noexcept
    {
        return changes_;
    }
    size_t& EditorSceneProjection::BorrowNextId() noexcept
    {
        return nextId_;
    }
    std::unique_ptr<SceneDocumentProjection::Prepared> EditorSceneProjection::PrepareDocument(SceneEditPlan& plan,
                                                                                              bool reload,
                                                                                              std::string& error)
    {
        if (preparing_ || !document_->Preparing())
        {
            error = "Document projection is unavailable during frame preparation";
            return {};
        }
        auto prepared = std::make_unique<Preparation>(*this);
        return prepared->Prepare(plan, reload, error) ? std::move(prepared) : nullptr;
    }
    bool EditorSceneProjection::ReloadAssets(const Engine::DirectX12Renderer& renderer,
                                             const std::filesystem::path& assetsRoot,
                                             const std::filesystem::path& shaderPath, std::string& error)
    {
        if (preparing_ || reloading_ || document_->Preparing() || !document_->Editor())
        {
            error = "Document asset reload requires an idle document session";
            return false;
        }
        try
        {
            auto context = context_ ? context_ : Engine::ContentContext::Capture(assetsRoot);
            context->RequireRoot(assetsRoot);
            SceneRenderState candidate;
            std::string diagnostic;
            // 編集用の欠損資源は通常の初期化と同じく診断を保持し、文書を編集可能にします。
            if (context->CheckConfiguration(diagnostic))
                candidate.Initialize(renderer, context->Resolve(std::filesystem::absolute(shaderPath)), &diagnostic);
            PreparedSceneResources resources;
            if (!candidate.Gpu().PrepareLayout(layout_, assetsRoot, error, false, resources, nullptr,
                                               candidate.View(layout_), true, context))
                return false;
            auto packet = candidate.PrepareEditor(resources, error);
            if (candidate.Gpu().Ready() && !packet)
                return false;
            packet = candidate.CommitPrepared(resources, std::move(packet), false);
            struct ReloadScope
            {
                EditorSceneProjection& owner;
                ~ReloadScope()
                {
                    owner.reloading_ = nullptr;
                    owner.reloadLayout_ = nullptr;
                }
            } scope{*this};
            reloading_ = &candidate;
            reloadLayout_ = &resources.layout;
            return document_->Editor()->Edit(ReplaceDocumentEdit{resources.layout}, error).succeeded;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }
}
