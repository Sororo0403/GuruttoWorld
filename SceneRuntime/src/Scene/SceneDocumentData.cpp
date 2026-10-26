#include <SceneRuntime/Scene/SceneDocumentData.h>

namespace SceneRuntime
{
    auto SceneDocumentData::operator=(const SceneDocumentData& other) -> SceneDocumentData&
    {
        if (this == &other)
            return *this;
        if (view_ && (layout_.Serialize() != other.layout_.Serialize() || context_ != other.context_))
            throw std::runtime_error("Document changes require a projection transaction");
        layout_ = other.layout_;
        context_ = other.context_;
        return *this;
    }
}
