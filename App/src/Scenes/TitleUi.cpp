#include "TitleUi.h"
#include <algorithm>
namespace {
float SelectionOpacity(const App::TitleMenu& menu,App::TitleMenuItem item,const SceneRuntime::UiState& settings) {
    return menu.GetSelected()==item ? 1.0f : std::clamp(settings.Value("inactiveOpacity",.6f),0.0f,1.0f);
}
}
namespace App {
bool TitleUi::Initialize(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root) {
    layout_=SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/TitleStreet.json");
    std::string error; return ui_.Prepare(renderer,root,layout_,error);
}
SceneRuntime::UiState TitleUi::State(const TitleMenu& menu,const SceneRuntime::UiState& settings) {
    SceneRuntime::UiState state;
    state.values={{"screen",menu.UsesPressAnyButton()?2.0f:menu.IsSettingsOpen()?1.0f:0.0f},
        {"screenNotSettings",menu.IsSettingsOpen()?0.0f:1.0f},
        {"selected",static_cast<float>(menu.GetSelected())},{"row",static_cast<float>(menu.GetSettingsRow())},
        {"gamepad",menu.UsesGamepad()?1.0f:0.0f},{"intro",menu.IntroProgress()},{"pulse",settings.Value("selectionOffset",12)*menu.SelectionPulse()},
        {"volume",static_cast<float>(menu.GetSettings().volume)},{"motion",menu.GetSettings().backgroundMotion?1.0f:0.0f},
        {"saveFailed",menu.SaveFailed()?1.0f:0.0f},
        {"cameraFocus",menu.IsSettingsOpen() ? 1.0f : static_cast<float>(menu.GetSelected())},
        {"focusView",menu.IsSettingsOpen() || menu.GetSelected()!=TitleMenuItem::Start ? 1.0f : 0.0f},
        {"startEmphasis",SelectionOpacity(menu,TitleMenuItem::Start,settings)},
        {"configEmphasis",SelectionOpacity(menu,TitleMenuItem::Settings,settings)},
        {"quitEmphasis",SelectionOpacity(menu,TitleMenuItem::Exit,settings)},
        {"quitTransition",menu.GetSelected()==TitleMenuItem::Exit ? menu.TransitionProgress() : 0.0f},
        {"transition",menu.TransitionProgress()},
        {"transitionPink",std::min(1.0f,menu.TransitionProgress()*settings.Value("transitionPinkScale",1.25f))}};
    for(int i=0;i<3;++i) {
        state.values["inactive"+std::to_string(i)]=i!=static_cast<int>(menu.GetSelected())?1.0f:0.0f;
        state.values["rowInactive"+std::to_string(i)]=i!=menu.GetSettingsRow()?1.0f:0.0f;
    }
    return state;
}
void TitleUi::Draw(ID3D12GraphicsCommandList* commands,unsigned int width,unsigned int height,const TitleMenu& menu) const {
    ui_.Draw(commands,layout_,width,height,State(menu));
}
}
