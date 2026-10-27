#include <SceneRuntime/Scene/SceneDocumentData.h>

namespace SceneRuntime
{
    SceneEditResult SceneDocumentData::Edit(const SceneEdit& edit, std::string& error)
    {
        if (editor_)
            return editor_->Edit(edit, error);
        auto transaction = BeginEdit(edit, error);
        if (!transaction || !transaction->PrepareCommit(error))
            return {};
        SceneEditResult result{true, transaction->Plan().changes};
        if (!transaction->Commit())
            return {};
        error.clear();
        return result;
    }

    SceneChangeSet SceneDocumentData::TakeChanges() noexcept
    {
        SceneChangeSet changes;
        if (!preparing_)
            changes_.Swap(changes);
        return changes;
    }

    auto SceneDocumentData::operator=(const SceneDocumentData& other) -> SceneDocumentData&
    {
        if (this == &other)
            return *this;
        if (preparing_)
            throw std::runtime_error("Document is preparing an edit");
        if ((projection_ || editor_) &&
            (layout_.Serialize() != other.layout_.Serialize() || context_ != other.context_))
            throw std::runtime_error("Document changes require a projection transaction");
        layout_ = other.layout_;
        context_ = other.context_;
        return *this;
    }
    SceneDocumentData::Transaction::Transaction(SceneDocumentData& owner, SceneEditPlan plan)
        : owner_(owner), plan_(std::move(plan))
    {
        owner_.preparing_ = true;
    }
    SceneDocumentData::Transaction::~Transaction()
    {
        Finish();
    }
    SceneEditPlan& SceneDocumentData::Transaction::Plan() noexcept
    {
        return plan_;
    }
    bool SceneDocumentData::Transaction::PrepareCommit(std::string& error)
    {
        if (committed_)
            return false;
        try
        {
            plan_.layout.ValidateChanges(owner_.layout_);
            plan_.changes = SceneChangeSet::Compare(owner_.layout_, plan_.layout);
            publication_ = owner_.changes_;
            publication_.Merge(plan_.changes);
            ready_ = true;
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            ready_ = false;
            error = failure.what();
            return false;
        }
    }
    bool SceneDocumentData::Transaction::Commit() noexcept
    {
        if (!locked_ || !ready_ || committed_)
            return false;
        if (!plan_.changes.Empty())
            owner_.layout_.Swap(plan_.layout);
        owner_.nextId_ = plan_.nextId;
        owner_.changes_.Swap(publication_);
        committed_ = true;
        return true;
    }
    void SceneDocumentData::Transaction::Finish() noexcept
    {
        if (locked_)
        {
            owner_.preparing_ = false;
            locked_ = false;
        }
    }
    std::unique_ptr<SceneDocumentData::Transaction> SceneDocumentData::BeginEdit(const SceneEdit& edit,
                                                                                 std::string& error,
                                                                                 const SceneEditTarget* owner)
    {
        if (preparing_ || (editor_ && editor_ != owner) || (projection_ && !owner))
        {
            error = "Document changes require its idle editing owner";
            return {};
        }
        auto plan = SceneEdits::Prepare(layout_, edit, nextId_, error);
        if (!plan)
            return {};
        return std::unique_ptr<Transaction>(new Transaction(*this, std::move(*plan)));
    }
    void SceneDocumentData::AttachEditor(SceneEditTarget& editor)
    {
        if (preparing_ || (editor_ && editor_ != &editor))
            throw std::runtime_error("Document already has an editing owner");
        editor_ = &editor;
    }
    void SceneDocumentData::DetachEditor(const SceneEditTarget& editor) noexcept
    {
        if (editor_ == &editor)
            editor_ = nullptr;
    }
    SceneEditTarget* SceneDocumentData::Editor() const noexcept
    {
        return editor_;
    }
    SceneLayout& SceneDocumentData::AttachProjection(SceneDocumentProjection& projection)
    {
        if (preparing_ || projection_)
            throw std::runtime_error("Document already has an editing projection");
        projection_ = &projection;
        return layout_;
    }
    void SceneDocumentData::DetachProjection(const SceneDocumentProjection& projection) noexcept
    {
        if (projection_ == &projection)
            projection_ = nullptr;
    }
    SceneDocumentProjection* SceneDocumentData::Projection() const noexcept
    {
        return projection_;
    }
    void SceneDocumentData::SetProjectionContent(const SceneDocumentProjection& projection,
                                                 std::shared_ptr<const Engine::ContentContext> context)
    {
        if (projection_ != &projection || preparing_)
            throw std::runtime_error("Document content requires its idle projection");
        context_ = std::move(context);
    }
    bool SceneDocumentData::Preparing() const noexcept
    {
        return preparing_;
    }

}
