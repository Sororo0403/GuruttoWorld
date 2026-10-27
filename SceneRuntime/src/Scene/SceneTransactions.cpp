#include <SceneRuntime/Scene/SceneWorld.h>
#include "SceneWorldRuntime.h"
#include <cmath>

namespace SceneRuntime
{
    bool SceneWorld::CanMutateScene(std::string* error) const
    {
        if (!preparingChanges_ && (!document_ || !document_->Preparing()))
            return true;
        if (error)
            *error = "Scene mutation is unavailable during frame preparation";
        return false;
    }
    bool SceneWorld::ReplaceRuntimeLayout(SceneLayout layout, const std::filesystem::path& assetsRoot,
                                          std::string& error)
    {
        if (!runtime_)
        {
            error = "Runtime session is not initialized";
            return false;
        }
        if (!CheckSessionRoot(assetsRoot, error))
            return false;
        return EditSession(ReplaceDocumentEdit{std::move(layout)}, error, true);
    }
    bool SceneWorld::UpdateFrame(double seconds, float horizontal, float vertical, bool jump, bool advancePhysics)
    {
        return ExecuteUpdate(seconds, horizontal, vertical, jump, advancePhysics, true);
    }
    bool SceneWorld::ExecuteUpdate(double seconds, float horizontal, float vertical, bool jump, bool movement,
                                   bool components)
    {
        return runtime_ && std::isfinite(seconds) && seconds > 0 && std::isfinite(horizontal) &&
               std::isfinite(vertical) && UpdateSession(seconds, horizontal, vertical, jump, movement, components);
    }
}
