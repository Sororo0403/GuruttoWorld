#pragma once
#include "../Editing/EditState.h"
#include <SceneRuntime/Scripts/ScriptSchema.h>
#include "../Project/ProjectCatalog.h"

namespace Editor
{
    class ComponentPanel final
    {
    public:
        static void Draw(EditState& state, const SceneRuntime::ScenePlacement& placement, const ProjectCatalog* catalog,
                         const SceneRuntime::SceneLayout* layout = nullptr);

    private:
        /// <summary>実行コールバックと独立したスキーマで公開データを編集します。</summary>
        static bool DrawScriptData(EditState& state, SceneRuntime::ScriptComponent& script,
                                   const SceneRuntime::ScriptSchema& definition, const ProjectCatalog* catalog);
        static bool DrawLods(SceneRuntime::ScenePlacement& candidate, const ProjectCatalog* catalog);
        static bool DrawMaterialSlots(EditState& state, SceneRuntime::MaterialComponent& material,
                                      const ProjectCatalog* catalog);
        static bool DrawPlayerController(EditState& state, SceneRuntime::ScenePlacement& candidate);
        static bool DrawScripts(EditState& state, SceneRuntime::ScenePlacement& candidate,
                                const ProjectCatalog* catalog);
        static bool AddVisualComponents(SceneRuntime::ScenePlacement& candidate, const ProjectCatalog* catalog);
        static bool AddPhysicsComponents(SceneRuntime::ScenePlacement& candidate);
        /// <summary>宣言済みスキーマと移行前の登録情報からゲーム処理を追加します。</summary>
        static bool AddScripts(SceneRuntime::ScenePlacement& candidate, const ProjectCatalog* catalog);
        static bool ChooseMaterial(std::filesystem::path& material, const ProjectCatalog* catalog,
                                   const char* emptyLabel);
        static bool DrawMesh(SceneRuntime::ScenePlacement& candidate, const ProjectCatalog* catalog);
        static bool DrawMaterial(EditState& state, SceneRuntime::ScenePlacement& candidate,
                                 const ProjectCatalog* catalog);
        static bool DrawRotator(EditState& state, SceneRuntime::ScenePlacement& candidate);
        static bool DrawAdd(SceneRuntime::ScenePlacement& candidate, const ProjectCatalog* catalog);
        static bool ChooseModel(std::filesystem::path& model, const ProjectCatalog* catalog);
    };
}
