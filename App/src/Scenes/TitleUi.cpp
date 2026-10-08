#include "TitleUi.h"
#include <algorithm>
#include <SceneRuntime/ProjectSettings.h>
namespace App {
bool TitleUi::Initialize(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root,const std::filesystem::path& scene) {
    const auto path=scene.empty()?std::filesystem::path(SceneRuntime::ProjectSettings::Load(root).startupScene):scene;
    layout_=SceneRuntime::SceneLayout::Load(root/path,root);
    std::string error; return ui_.Prepare(renderer,root,layout_,error);
}
SceneRuntime::UiState TitleUi::State(const TitleMenu& menu,const SceneRuntime::UiState& settings) {
    SceneRuntime::UiState signals;
    const auto* entry=menu.SelectedEntry();
    signals.values={{"screen",menu.UsesPressAnyButton()?2.0f:menu.IsSettingsOpen()?1.0f:0.0f},
        {"screenNotSettings",menu.IsSettingsOpen()?0.0f:1.0f},{"selected",static_cast<float>(menu.GetSelected())},
        {"row",static_cast<float>(menu.GetSettingsRow())},{"gamepad",menu.UsesGamepad()?1.0f:0.0f},
        {"intro",menu.IntroProgress()},{"selectionPulse",menu.SelectionPulse()},
        {"volume",static_cast<float>(menu.GetSettings().volume)},{"motion",menu.GetSettings().backgroundMotion?1.0f:0.0f},
        {"saveFailed",menu.SaveFailed()?1.0f:0.0f},{"cameraFocus",entry?entry->focus:0.0f},
        {"focusView",entry?entry->view:0.0f},
        {"quitTransition",entry && entry->action=="quit"?menu.TransitionProgress():0.0f},
        {"transition",menu.TransitionProgress()}};
    for(const auto& [key,value]:menu.SettingValues()) signals.values["setting:"+key]=value;
    for(const auto* key:{"volume","motion"}) if(menu.SettingValues().contains(key)) signals.values[key]=menu.SettingValues().at(key);
    SceneRuntime::UiState state;
    for(const auto& binding:menu.Configuration().bindings) {
        float value=signals.Value(binding.source,settings.Value(binding.source));
        state.values[binding.key]=SceneRuntime::EvaluateMenuBinding(binding,value,settings.values);
    }
    return state;
}

void TitleUi::Draw(ID3D12GraphicsCommandList* commands,unsigned int width,unsigned int height,const TitleMenu& menu) const {
    ui_.Draw(commands,layout_,width,height,State(menu));
}
}
