#pragma once
#include "../Editing/EditState.h"
#include "../Project/ProjectCatalog.h"
namespace Editor
{
    class UiComponentPanel final
    {
    public:
        static bool Draw(EditState&, SceneRuntime::ScenePlacement&, const ProjectCatalog*);
        static bool Add(SceneRuntime::ScenePlacement&);
    };
}
