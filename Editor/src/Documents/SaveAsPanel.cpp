#include "SaveAsPanel.h"

namespace Editor
{
    auto SaveAsPanel::Request(const std::filesystem::path& current) -> void
    {
        const auto name = ProjectCatalog::Text(current.filename());
        filename_.fill(0);
        std::copy_n(name.data(), std::min(name.size(), filename_.size() - 1), filename_.data());
        requested_ = true;
        target_.reset();
        error_.clear();
    }
}
