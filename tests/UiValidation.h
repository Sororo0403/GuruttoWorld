#pragma once
#include <SceneRuntime/SceneUi.h>
#include "UiControlValidation.h"
#include <SceneRuntime/SceneAudio.h>
#include "../App/src/Scenes/AuthoredScene.h"
#include "EnvironmentValidation.h"
#include "../Editor/src/ProjectCatalog.h"
#include "../Editor/src/FocusSelection.h"
#include "../Editor/src/UiCanvasPanel.h"
#include <Engine/Graphics/Resources/RenderTargetBinding.h>
namespace UiValidation {
using namespace SceneRuntime;
inline void Require(bool success,const char* message) {if(!success) throw std::runtime_error(message);}
inline SceneLayout Layout() {
    SceneLayout layout;
    ScenePlacement canvas; canvas.id="canvas"; canvas.name="Canvas"; canvas.canvas.emplace(); canvas.canvas->referenceSize={64,32};
    ScenePlacement panel; panel.id="panel"; panel.name="Panel"; panel.parentId=canvas.id; panel.rectTransform.emplace(); panel.rectTransform->size={64,32};
    panel.image.emplace(); panel.image->color={1,0,0,1}; panel.button.emplace(); panel.button->hoverColor={0,1,0,1}; panel.button->target="panel"; panel.button->action="toggle";
    ScenePlacement audio; audio.id="audio"; audio.name="Audio"; audio.audioSource.emplace(); audio.audioSource->clip="Assets/Audio/Title/Select.wav"; audio.audioSource->volume=0;
    layout.objects={canvas,panel,audio}; return layout;
}
inline void SchemaAndLayout() {
    UiControlValidation::SchemaAndInput();
    auto authored=SceneLayout::Load("Content/Assets/Scenes/Game.json");
    const auto authoredRoundtrip=SceneLayout::Parse(authored.Serialize());
    Require(authoredRoundtrip.objects.size()==authored.objects.size(),"Game scene survives editor serialization");
    auto defaults=SceneUi::Defaults(authored);
    const auto shortcut=SceneUi::Shortcut(authored,"2",1280,720,defaults);
    Require(!shortcut.empty(),"authored model shortcut resolves");
    Require(SceneUi::Shortcut(authored,"",1280,720,defaults).empty(),"empty shortcut cannot activate unbound buttons");
    SceneUi::Activate(authored,shortcut,defaults);
    Require(defaults.Value("model")==1,"authored shortcut switches mesh visibility state");
    for(auto& object:authored.objects) if(object.id==shortcut) {object.id="renamed-choice"; object.button->enabled=false;}
    Require(SceneUi::Shortcut(authored,"2",1280,720,defaults).empty(),"disabled shortcut cannot activate");
    bool meshRejected=false;
    authored.objects.front().meshRenderer=MeshRendererComponent{"mesh",true,"Assets/Models/Cube.obj","broken"};
    try {static_cast<void>(authored.Serialize());} catch(const std::exception&) {meshRejected=true;}
    Require(meshRejected,"invalid mesh visibility cannot be saved");
    Require(Editor::ProjectCatalog::Kind("Assets/Audio/sample.m4a")==Editor::AssetKind::Audio && Editor::ProjectCatalog::Kind("Assets/Audio/sample.aac")==Editor::AssetKind::Audio,"Project exposes decoder-supported AAC assets");
    auto layout=Layout(); layout.objects[1].text.emplace(); layout.objects[1].text->text="日本語 UI";
    const auto restored=SceneLayout::Parse(layout.Serialize());
    Require(restored.objects[1].SameComponents(layout.objects[1]) && restored.objects[2].SameComponents(layout.objects[2]),"UI/audio roundtrip preserves values and IDs");
    auto rect=SceneUi::Resolve(layout,layout.objects[1],128,128);
    Require(rect.position==std::array<float,2>{0,32} && rect.size==std::array<float,2>{128,64},"Canvas aspect fit letterboxes");
    layout.objects[0].canvas->scaleWithScreen=false;
    layout.objects[1].rectTransform->anchorMax={1,1}; layout.objects[1].rectTransform->size={0,0};
    rect=SceneUi::Resolve(layout,layout.objects[1],128,128);
    Require(rect.position==std::array<float,2>{0,0} && rect.size==std::array<float,2>{128,128},"pixel Canvas overlays cover the full viewport");
    layout.objects[0].canvas->scaleWithScreen=true;
    layout.objects[1].rectTransform->anchorMin={0.5f,0.5f}; layout.objects[1].rectTransform->anchorMax={0.5f,0.5f}; layout.objects[1].rectTransform->pivot={0.5f,0.5f}; layout.objects[1].rectTransform->size={16,8};
    rect=SceneUi::Resolve(layout,layout.objects[1],64,32);
    Require(rect.position==std::array<float,2>{24,12},"anchors and pivot center UI");
    ScenePlacement child; child.id="child"; child.parentId="panel"; child.rectTransform.emplace(); child.rectTransform->size={4,4}; layout.objects.push_back(child);
    layout.objects[1].rectTransform->rotation=DirectX::XM_PIDIV2;
    const auto nested=SceneUi::Resolve(layout,layout.objects.back(),64,32);
    Require(std::abs(nested.rotation-DirectX::XM_PIDIV2)<0.0001f && nested.Contains(nested.position[0]+2,nested.position[1]+2),"nested rotation and hit testing agree");
    UiState state; const auto before=state.values; Require(!state.Assign("x=2&bad=oops") && state.values==before,"state assignments are transactional");
    state=SceneUi::Defaults(layout); SceneUi::Activate(layout,"panel",state);
    Require(!SceneUi::Resolve(layout,layout.objects.back(),64,32,state).visible,"hiding parent hides descendants");
    SceneUi::Activate(layout,"panel",state); Require(SceneUi::Resolve(layout,layout.objects.back(),64,32,state).visible,"toggle restores parent visibility");
    auto invalid=layout; invalid.objects[1].image->texture="../outside.png";
    bool rejected=false; try{invalid.Serialize();}catch(...){rejected=true;} Require(rejected,"UI rejects asset traversal");
    invalid=layout; invalid.objects[2].audioSource->volume=2; rejected=false;
    try{invalid.Serialize();}catch(...){rejected=true;} Require(rejected,"audio validates volume");
    invalid=layout; invalid.objects[1].button->action="loadScene"; invalid.objects[1].button->target="../outside.json"; rejected=false;
    try{invalid.Serialize();}catch(...){rejected=true;} Require(rejected,"button scene target stays inside scenes");
    const auto title=SceneLayout::Load("Content/Assets/Scenes/TitleStreet.json");
    Require(std::count_if(title.objects.begin(),title.objects.end(),[](const auto& p){return p.audioSource.has_value();})==5,"all title sounds are authored");
    Require(std::any_of(title.objects.begin(),title.objects.end(),[](const auto& p){return p.canvas.has_value();}),"title canvas is authored");
}
inline void EditingAndAudio(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root) {
    auto layout=Layout(); layout.objects[2].audioSource->playOnAwake=true; layout.objects[2].audioSource->loop=true;
    SceneWorld world; std::string error;
    Require(world.Initialize(renderer,root,layout,root/"Shaders/Mesh.hlsl",&error),"UI editing world initializes");
    Editor::EditHistory history; history.Reset({world.Layout().Serialize(),"panel",{"panel"}});
    const auto original=world.Layout().Serialize(); auto panel=world.Layout().objects[1];
    for(const float x:{20.0f,40.0f}) {
        panel.rectTransform->position[0]=x;
        Require(world.SetComponents(panel.id,panel,root,error),"RectTransform updates through component command");
        history.Observe({world.Layout().Serialize(),"panel",{"panel"}},"ui/drag/panel");
    }
    history.Commit();
    Require(world.ReplaceLayout(SceneLayout::Parse(history.Target(false).json),root,error),"UI Undo restores layout"); history.Applied(false);
    Require(world.Layout().Serialize()==original && !history.CanUndo(),"one Undo restores full UI drag");
    Require(world.ReplaceLayout(SceneLayout::Parse(history.Target(true).json),root,error),"UI Redo applies"); history.Applied(true);
    std::vector<std::string> created;
    Require(world.DuplicateObjects({"canvas","panel","audio"},{0,0,0},created,error),"UI hierarchy duplicates");
    Require(world.Layout().objects[4].parentId==created[0] && world.Layout().objects[4].button->target==created[1],"copied UI references target copied hierarchy");
    Require(world.Layout().objects[5].audioSource==layout.objects[2].audioSource,"AudioSource settings duplicate");
    SceneAudio audio; Require(audio.Initialize(root,layout,error),"authored audio loads");
    audio.Update(layout,{},true); Require(audio.IsPlaying("audio") && audio.Volume("audio")==0,"awake audio respects muted volume");
    audio.Pause(true); Require(audio.IsPlaying("audio"),"scene pause retains audio buffer"); audio.Pause(false);
    audio.Stop(); Require(!audio.IsPlaying("audio"),"scene stop stops voices");
    layout.objects[2].audioSource->clip="Assets/Audio/missing.wav";
    Require(!audio.Initialize(root,layout,error) && !audio.IsPlaying("audio"),"failed audio load is reported without a live voice");
}
inline void Rendering(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root) {
    UiControlValidation::Rendering(renderer,root);
    auto layout=Layout(); SceneUi ui; std::string error;
    Require(ui.Prepare(renderer,root,layout,error),"UI prepares resources outside draw");
    const auto pixel=[&](const UiState& state=UiState{}) {return EnvironmentValidation::Pixel(renderer,[&](auto* commands){ui.Draw(commands,layout,64,32,state);});};
    Require(pixel()==std::array<unsigned char,4>{255,0,0,255},"authored UI image is drawn without Camera");
    {
        SceneWorld world;
        Require(world.Initialize(renderer,root,layout,root/"Shaders/Mesh.hlsl",&error),"Scene Canvas world initializes");
        Engine::Camera camera; camera.SetPosition({0,0,-1}); camera.SetRotation(0,0); camera.SetPerspective(DirectX::XM_PIDIV4,2,.1f,100);
        const auto scenePixel=[&] {return EnvironmentValidation::Pixel(renderer,[&](auto* commands){ui.DrawScene(commands,world,camera);});};
        Require(scenePixel()==std::array<unsigned char,4>{255,0,0,255},"Scene Canvas plane draws at its authored world location");
        const auto occluded=EnvironmentValidation::Pixel(renderer,[&](auto* commands) {
            Engine::RenderTargetBinding target;
            Require(Engine::RenderTargetBinding::Current(commands,target),"Scene Canvas depth target is available");
            commands->ClearDepthStencilView(target.depth,D3D12_CLEAR_FLAG_DEPTH,0,0,0,nullptr);
            ui.DrawScene(commands,world,camera);
        });
        Require(occluded==std::array<unsigned char,4>{0,0,0,255},"Scene Canvas respects foreground depth");
        SceneEnvironment environment;
        Require(environment.Initialize(renderer,root,layout,error),"Scene Canvas presentation initializes");
        const auto presentationPixel=[&](bool visible) {return EnvironmentValidation::Pixel(renderer,[&](auto* commands){environment.Draw(commands,64,32,&camera,visible);});};
        Require(presentationPixel(true)==std::array<unsigned char,4>{255,0,0,255},"Scene presentation includes the Canvas plane");
        Require(presentationPixel(false)==std::array<unsigned char,4>{0,0,0,255},"Scene UI display toggle hides Canvas content");
        const auto matrix=SceneCanvas::Matrix(world,world.Layout().objects[1]);
        Require(matrix.has_value(),"Scene Canvas resolves the root transform");
        const auto hit=SceneCanvas::Intersect(*matrix,camera,0,0);
        Require(hit && std::abs((*hit)[0]-32)<.001f && std::abs((*hit)[1]-16)<.001f,"Scene ray resolves Canvas pixels");
        Require(Editor::FocusPosition(world,{"canvas","panel"},camera).has_value(),"Canvas and UI support F focus without mesh bounds");
        Require(world.SetLocalTransform("canvas",{3,0,0},{0,0,0},{1,1,1}),"Scene Canvas transform edits apply");
        Require(scenePixel()==std::array<unsigned char,4>{0,0,0,255},"moving Canvas moves UI out of view rather than pinning it to the screen");
        Require(pixel()==std::array<unsigned char,4>{255,0,0,255},"Game overlay remains independent of the Scene Canvas transform");
        camera.SetPosition({3,0,-1});
        Require(scenePixel()==std::array<unsigned char,4>{255,0,0,255},"Scene camera movement follows the Canvas world plane");
        const auto movedMatrix=SceneCanvas::Matrix(world,world.Layout().objects[1]);
        const auto movedHit=SceneCanvas::Intersect(*movedMatrix,camera,0,0);
        Require(movedHit && std::abs((*movedHit)[0]-32)<.001f,"picking follows the translated Canvas");
        Require(world.SetLocalTransform("canvas",{3,0,0},{0,.4f,.2f},{1.5f,.8f,1}),"rotated and scaled Canvas transform applies");
        const auto rotatedMatrix=SceneCanvas::Matrix(world,world.Layout().objects[1]);
        const auto rotatedHit=SceneCanvas::Intersect(*rotatedMatrix,camera,0,0);
        Require(rotatedHit && std::abs((*rotatedHit)[0]-32)<.01f && std::abs((*rotatedHit)[1]-16)<.01f,"picking agrees with a rotated and scaled Canvas plane");
        Require(scenePixel()==std::array<unsigned char,4>{255,0,0,255},"rotated Canvas projects its UI into the Scene framebuffer");
        Require(world.SetLocalTransform("canvas",{3,0,0},{0,0,0},{1,1,1}),"Canvas transform restores before drag test");
        camera.SetPosition({3,0,1});
        Require(!SceneCanvas::Intersect(*movedMatrix,camera,0,0),"Canvas behind the Scene camera cannot be picked");
        camera.SetPosition({3,0,-1}); camera.SetPerspective(DirectX::XM_PIDIV4,2,.1f,100);
        struct ContextScope {
            ImGuiContext* previous=ImGui::GetCurrentContext();
            ImGuiContext* context=ImGui::CreateContext();
            ContextScope() {ImGui::SetCurrentContext(context);}
            ~ContextScope() {ImGui::DestroyContext(context);ImGui::SetCurrentContext(previous);}
        } context;
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={640,480};io.DeltaTime=1.0f/60;io.Fonts->Build();
        Editor::EditState edit; Editor::UiCanvasPanel panel;
        const Editor::SceneViewport viewport{0,0,640,320};
        const auto frame=[&](float x,bool down) {
            io.AddMousePosEvent(x,160);io.AddMouseButtonEvent(0,down);
            ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({640,480});
            ImGui::Begin("Scene Canvas validation",nullptr,ImGuiWindowFlags_NoTitleBar);
            edit.BeginFrame();panel.DrawScene(world,camera,edit,viewport,true);ImGui::End();ImGui::Render();
        };
        frame(320,false);frame(320,false);frame(320,true);
        Require(edit.SelectedId()=="panel" && panel.IsDragging() && panel.ConsumesMouse(),"Scene UI selects on first press and consumes world selection");
        edit.TakeRequest();frame(340,true);
        const auto request=edit.TakeRequest();
        Require(request && request->components && request->components->rectTransform->position[0]>0,"Scene UI drag edits RectTransform in Canvas coordinates");
        frame(340,false);Require(!panel.IsDragging(),"Scene UI drag finishes on release");
        Require(renderer.WaitForIdle(),"Scene UI work completes before resource release");
    }
    UiState state; state.hovered="panel"; Require(pixel(state)==std::array<unsigned char,4>{0,255,0,255},"button hover tint reaches GPU");
    SceneUi::Activate(layout,"panel",state); Require(pixel(state)==std::array<unsigned char,4>{0,0,0,255},"hidden UI is omitted");
    layout.objects[1].image.reset(); layout.objects[1].text.emplace(); layout.objects[1].text->text="█"; layout.objects[1].rectTransform->position={20,0};
    Require(ui.Prepare(renderer,root,layout,error),"Unicode text rasterizes and uploads");
    const auto textPixel=pixel(); Require(textPixel[0]>0 && textPixel[1]>0 && textPixel[2]>0,"text pixels reach the Game framebuffer");
    layout.objects[1].image.emplace(); layout.objects[1].image->texture="Assets/Textures/missing.png";
    Require(!ui.Prepare(renderer,root,layout,error),"missing UI image reports failure and keeps previous resources");
    Require(renderer.WaitForIdle(),"UI GPU work completes before resources are destroyed");
    EditingAndAudio(renderer,root);
    App::AuthoredScene appScene(root,"Assets/Scenes/UiAudioDemo.json");
    Require(appScene.Initialize(renderer),"App initializes an authored UI scene");
    Require(appScene.Draw(renderer)!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"App renders authored UI through the shared runtime");
    App::AuthoredScene game(root,"Assets/Scenes/Game.json");
    Require(game.Initialize(renderer),"App loads editor-authored Game scene");
    Require(game.Draw(renderer)!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"authored Game meshes, particles, images and UI render together");
}
}
