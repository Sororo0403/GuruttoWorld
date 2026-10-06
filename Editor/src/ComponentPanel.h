#pragma once
#include "EditState.h"
#include "ProjectCatalog.h"

namespace Editor
{
    class ComponentPanel final
    {
    public:
        static void Draw(EditState& state, const SceneRuntime::ScenePlacement& placement,
            const ProjectCatalog* catalog);
    private:
        static bool DrawMesh(SceneRuntime::ScenePlacement& candidate, const ProjectCatalog* catalog);
        static bool DrawRotator(EditState& state, SceneRuntime::ScenePlacement& candidate);
        static bool DrawAdd(SceneRuntime::ScenePlacement& candidate, const ProjectCatalog* catalog);
        static bool ChooseModel(std::filesystem::path& model, const ProjectCatalog* catalog);
    };
}
