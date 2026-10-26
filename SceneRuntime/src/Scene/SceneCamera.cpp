#include <SceneRuntime/Scene/SceneCamera.h>
#include <SceneRuntime/Scene/SceneLayout.h>

namespace SceneRuntime
{
    const ScenePlacement* SceneCamera::Find(const SceneLayout& layout)
    {
        const auto found =
            std::find_if(layout.objects.begin(), layout.objects.end(),
                         [&](const auto& object)
                         {
                             return object.camera && object.camera->enabled &&
                                    (layout.settings.mainCamera.empty() || object.id == layout.settings.mainCamera);
                         });
        return found == layout.objects.end() ? nullptr : &*found;
    }
}
