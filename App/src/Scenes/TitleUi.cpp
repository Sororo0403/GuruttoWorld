#include "TitleUi.h"
#include <algorithm>
namespace App {
bool TitleUi::Initialize(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root) {
    layout_=SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/TitleStreet.json");
    std::string error; return ui_.Prepare(renderer,root,layout_,error);
}
SceneRuntime::UiState TitleUi::State(const TitleMenu& menu) {
    SceneRuntime::UiState state;
    state.values={{"screen",menu.UsesPressAnyButton()?2.0f:menu.IsSettingsOpen()?1.0f:0.0f},
        {"screenNotSettings",menu.IsSettingsOpen()?0.0f:1.0f},
        {"selected",static_cast<float>(menu.GetSelected())},{"row",static_cast<float>(menu.GetSettingsRow())},
        {"gamepad",menu.UsesGamepad()?1.0f:0.0f},{"intro",menu.IntroProgress()},{"pulse",12*menu.SelectionPulse()},
        {"volume",static_cast<float>(menu.GetSettings().volume)},{"motion",menu.GetSettings().backgroundMotion?1.0f:0.0f},
        {"saveFailed",menu.SaveFailed()?1.0f:0.0f},
        {"cameraFocus",menu.IsSettingsOpen() || menu.GetSelected()==TitleMenuItem::Settings ? 1.0f : 0.0f},
        {"startEmphasis",menu.GetSelected()==TitleMenuItem::Start ? 1.0f : 0.6f},
        {"configEmphasis",menu.GetSelected()==TitleMenuItem::Settings ? 1.0f : 0.6f},
        {"transition",menu.TransitionProgress()},
        {"transitionPink",std::min(1.0f,menu.TransitionProgress()*1.25f)}};
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
