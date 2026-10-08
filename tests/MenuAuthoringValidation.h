#pragma once
#include "../App/src/Scenes/TitleBindings.h"
#include "../App/src/Scenes/TitleUi.h"
#include "../App/src/Scenes/SceneFactory.h"
#include "../App/src/Scenes/AuthoredScene.h"
#include "../App/src/Scenes/TitleScene.h"
#include "../Editor/src/EditHistory.h"
#include "../Editor/src/TitlePreview.h"
#include <limits>

namespace MenuAuthoringValidation {
inline void Presentation(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root) {
    auto layout=SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/TitleStreet.json");
    for(auto& object:layout.objects) if(object.canvas && object.canvas->menu) {
        object.canvas->menu->bindings={{"transition","transition","value","",0,.5f,.1f,0,1},{"customFade","transition","value","",0,3,0,0,.75f}};
    }
    SceneRuntime::SceneEnvironment scene; std::string error;
    if(!scene.Initialize(renderer,root,layout,error)) throw std::runtime_error(error);
    scene.SeekAnimation(3,3,.4f);
    if(std::abs(scene.Ui().Value("transition")-.35f)>.0001f || scene.Ui().Value("customFade")!=.75f)
        throw std::runtime_error("scrub must evaluate authored transition outputs");
    scene.Ui().values["startRequested"]=1;
    scene.SeekAnimation(3,3,-1); scene.Update(.1,true,true); scene.Update(.1,true,true);
    if(std::abs(scene.Ui().Value("transition")-.1625f)>.0001f || std::abs(scene.Ui().Value("customFade")-.375f)>.0001f)
        throw std::runtime_error("runtime must preserve authored transition calculations");
}
inline void Run() {
    const auto check=[](bool ok,const char* message){if(!ok) throw std::runtime_error(message);};
    auto layout=SceneRuntime::SceneLayout::Load("Content/Assets/Scenes/TitleStreet.json");
    auto canvas=std::find_if(layout.objects.begin(),layout.objects.end(),[](const auto& o){return o.canvas && o.canvas->menu;});
    check(canvas!=layout.objects.end(),"title contains editable menu configuration");
    auto config=*canvas->canvas->menu;
    config.entries={{10,"setState","custom=2",3,1,"customFocusTime"},{21,"loadScene","Assets/Scenes/Game.json",4,1,"nextFocusTime"},{30,"settings","",5,1,"settingsFocusTime"},{44,"quit","",6,1,"exitFocusTime"}};
    config.settings={{"gamma","number",0,4,.25f,2},{"volume","number",0,20,.5f,5},{"motion","toggle",0,1,1,1},{"","save",0,1,1,0}};
    config.bindings={{"gammaOut","setting:gamma","value","",0,2,1,0,10},{"focus","cameraFocus"}};
    config.cues={"CustomSelect","CustomConfirm","CustomBack","CustomError"};
    config.inputs={"NavigateUp","NavigateDown","NavigateLeft","NavigateRight","Accept","Dismiss"};
    config.focusState="focus";
    canvas->canvas->menu=config;
    {
        auto disabled=layout;
        for(auto& object:disabled.objects) if(object.canvas && object.canvas->menu) object.canvas->enabled=false;
        Editor::TitlePreview preview; SceneRuntime::UiState state;
        preview.Initialize(disabled,state); check(!preview.Active(),"disabled menu Canvas disables Editor menu runtime");
        for(auto& object:disabled.objects) if(object.canvas) {object.canvas->enabled=true; object.canvas->menu.reset();}
        preview.Initialize(disabled,state); check(!preview.Active(),"removed menu configuration is not inferred from legacy button events");
    }
    const auto serialized=layout.Serialize();
    const auto restored=SceneRuntime::SceneLayout::Parse(serialized);
    check(*App::TitleBindings::Configuration(restored)==config,"all menu fields survive scene round trip");
    Editor::EditHistory history; history.Reset({serialized,canvas->id});
    canvas->canvas->menu->cues[0]="EditedCue";
    const auto edited=layout.Serialize(); history.Observe({edited,canvas->id},{});
    check(history.CanUndo(),"menu inspector changes enter scene history");
    const auto undo=SceneRuntime::SceneLayout::Parse(history.Target(false).json); history.Applied(false);
    check(*App::TitleBindings::Configuration(undo)==config,"Undo restores menu configuration");
    const auto redo=SceneRuntime::SceneLayout::Parse(history.Target(true).json); history.Applied(true);
    check(App::TitleBindings::Configuration(redo)->cues[0]=="EditedCue","Redo restores edited cue");
    canvas->canvas->menu=config;
    App::TitleMenu menu; menu.Configure(config);
    check(menu.GetSelected()==static_cast<App::TitleMenuItem>(10),"arbitrary menu index selects first entry");
    menu.ActivateUi("menu:10"); std::string assignments;
    check(menu.TakeAssignments(assignments) && assignments=="custom=2","custom menu state action");
    menu.ActivateUi("menu:30");
    check(menu.IsSettingsOpen(),"authored settings action is independent of index");
    menu.ActivateUi("settings:0");
    check(menu.SettingValues().at("gamma")==2.25f,"custom setting has authored initial and step");
    auto output=App::TitleUi::State(menu);
    check(output.Value("gammaOut")==5.5f && output.Value("focus")==5,"custom state outputs and camera focus");
    menu.ActivateUi("settings:1");
    check(std::abs(menu.GetSettings().Gain()-.275f)<.0001f,"authored volume range retains fractional gain");
    check(menu.ActivateUi("settings:3")==App::TitleMenuAction::SaveSettings,"fourth settings row saves");
    const auto path=std::filesystem::path("generated/tests/menu-settings.json");
    check(menu.GetSettings().Save(path),"save custom settings");
    App::TitleMenu reload; reload.Configure(config); reload.LoadSettings(App::GameSettings::Load(path));
    check(reload.SettingValues()==menu.SettingValues() && std::abs(reload.GetSettings().Gain()-.275f)<.0001f,"custom settings survive disk reload");
    menu.CompleteSave(false); check(menu.IsSettingsOpen() && menu.SaveFailed(),"custom save failure retains draft");
    menu.ActivateUi("back"); check(menu.SettingValues().at("gamma")==2,"cancel restores custom settings");
    menu.ActivateUi("menu:21"); App::TitleMenuInput empty; empty.active=true;
    App::TitleMenuAction action=App::TitleMenuAction::None;
    for(int i=0;i<6;++i) {const auto current=menu.Update(empty,.1); if(current!=App::TitleMenuAction::None) action=current;}
    check(action==App::TitleMenuAction::Start && menu.PendingTarget()=="Assets/Scenes/Game.json","arbitrary item transitions to its own authored target");
    check(menu.Update(empty,.1)==App::TitleMenuAction::None,"custom transition emits once");
    for(int axis:{3,4,5,6}) {
        Engine::InputActions actions;
        Engine::InputBinding binding; binding.axis=axis;
        actions.SetBindings({{"Accept",binding}});
        Engine::InputSnapshot snapshot; snapshot.active=true; snapshot.gamepadConnected=true; actions.Update(snapshot);
        if(axis<5) snapshot.rightStick[axis-3]=1; else snapshot.triggers[axis-5]=1;
        actions.Update(snapshot);
        const auto input=App::TitleBindings::Input(config,actions,true);
        check((input.keyboardButtons&App::MenuConfirm)!=0 && input.anyButtonPressed,"shared title input accepts authored right stick and trigger");
        App::TitleMenu controller; controller.Configure(config); controller.SelectUi(static_cast<App::TitleMenuItem>(21)); controller.Update(empty);
        controller.Update(input);
        check(controller.GetCue()==App::TitleMenuCue::Confirm,"mapped analog input activates menu");
    }
    Engine::InputActions mouse;
    Engine::InputBinding binding; binding.mouseButtons=2; mouse.SetBindings({{"Accept",binding}});
    Engine::InputSnapshot snapshot; snapshot.active=true; mouse.Update(snapshot); snapshot.mouseButtons=2; mouse.Update(snapshot);
    check(App::TitleBindings::Input(config,mouse,true).keyboardButtons&App::MenuConfirm,"shared title input accepts mouse action");
    check(!App::TitleBindings::Input(config,mouse,false).keyboardButtons,"inactive menu ignores mouse action");
    auto invalid=Engine::Json::parse(serialized);
    for(auto& object:invalid["objects"]) for(auto& component:object["components"]) if(component.contains("menu")) component["menu"]["entries"][0]["index"]=1.5;
    bool rejected=false; try {SceneRuntime::SceneLayout::Parse(invalid.dump());} catch(const std::exception&) {rejected=true;}
    check(rejected,"fractional menu indices are rejected");
    config.settings[0].step=0; rejected=false;
    try {SceneRuntime::ValidateMenu(config);} catch(const std::exception&) {rejected=true;}
    check(rejected,"zero setting step rejected");
    const auto root=std::filesystem::absolute("generated/tests/menu-project");
    std::filesystem::create_directories(root/"Assets/Scenes");
    layout.Save(root/"Assets/Scenes/RenamedTitle.json");
    App::SceneFactory factory(root);
    auto scene=factory.Create("Assets/Scenes/RenamedTitle.json");
    check(dynamic_cast<App::TitleScene*>(scene.get()),"renamed title selects menu runtime by saved configuration");
    const auto normal=SceneRuntime::SceneLayout::Load("Content/Assets/Scenes/Game.json");
    normal.Save(root/"Assets/Scenes/Game.json");
    scene=factory.Create("EngineDemo");
    check(dynamic_cast<App::AuthoredScene*>(scene.get()),"legacy engine demo uses editor-authored scene");
}
}
