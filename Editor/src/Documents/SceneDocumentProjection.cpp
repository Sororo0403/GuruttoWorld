#include "SceneDocument.h"
#include <SceneRuntime/Scene/SceneWorld.h>

namespace Editor
{
    auto SceneDocument::Apply(SceneRuntime::SceneWorld& world, const std::filesystem::path& root,
                              OpenCandidate candidate, std::string& error) -> bool
    {
        if (data_->Editor())
        {
            error = "Opened data must be accepted through its document session";
            return false;
        }
        const bool success = candidate.create_ ? world.ReplaceLayout(std::move(candidate.layout_), root, error)
                                               : world.ReloadLayout(root, std::move(candidate.layout_), error, true);
        if (!success)
            return false;
        path_.swap(candidate.path_);
        savedSource_.swap(candidate.source_);
        unsavedNew_ = candidate.create_;
        Cancel();
        return true;
    }

    auto SceneDocument::Open(SceneRuntime::SceneWorld& world, const std::filesystem::path& root,
                             const std::filesystem::path& path, std::string& error) -> bool
    {
        try
        {
            return Apply(world, root, PrepareOpen(path), error);
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    auto SceneDocument::Apply(SceneRuntime::SceneWorld& world, const std::filesystem::path& root, std::string& error)
        -> bool
    {
        if (!Ready())
            return false;
        auto request = std::move(*request_);
        Cancel();
        try
        {
            return Apply(world, root, PrepareOpen(std::move(request.path), request.create), error);
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
}
