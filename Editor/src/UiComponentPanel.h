#pragma once
#include "EditState.h"
#include "ProjectCatalog.h"
namespace Editor {
class UiComponentPanel final {
public:
static bool Draw(EditState&,SceneRuntime::ScenePlacement&,const ProjectCatalog*);
static bool Add(SceneRuntime::ScenePlacement&);
};
}
