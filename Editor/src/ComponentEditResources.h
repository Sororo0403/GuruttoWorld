#pragma once
#include "EditState.h"

namespace Editor {
struct ComponentEditResources {
    static bool Changed(const SceneRuntime::ScenePlacement& before,const SceneRuntime::ScenePlacement& after) {
        if(before.meshRenderer!=after.meshRenderer || before.material!=after.material || before.animator!=after.animator) return true;
        if(before.image.has_value()!=after.image.has_value() || before.text.has_value()!=after.text.has_value()) return true;
        if(before.image && before.image->texture!=after.image->texture) return true;
        if(before.text) {
            if(before.text->text!=after.text->text || before.text->font!=after.text->font || before.text->fontSize!=after.text->fontSize) return true;
            const std::array<float,2> defaultSize{200,60};
            const auto oldSize=before.rectTransform?before.rectTransform->size:defaultSize;
            const auto newSize=after.rectTransform?after.rectTransform->size:defaultSize;
            if(oldSize!=newSize) return true;
        }
        return false;
    }
    static bool NeedsIdle(const SceneRuntime::SceneLayout& layout,const ObjectRequest& request) {
        if(request.action!=ObjectAction::Components) return true;
        const auto owner=[&](const SceneRuntime::ScenePlacement& candidate) {
            const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& object){return object.id==candidate.id;});
            return found==layout.objects.end() || Changed(*found,candidate);
        };
        if(!request.componentBatch.empty()) return std::any_of(request.componentBatch.begin(),request.componentBatch.end(),owner);
        if(!request.components || request.components->id!=request.id) return true;
        return owner(*request.components);
    }
};
}
