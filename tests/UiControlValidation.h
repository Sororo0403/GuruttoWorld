#pragma once
#include <SceneRuntime/SceneUi.h>
#include "ScriptUiValidation.h"
#include "EnvironmentValidation.h"
#include "../Editor/src/EditHistory.h"
namespace UiControlValidation
{
    inline void Require(bool success,const char* message) {if(!success) throw std::runtime_error(message);}
    inline SceneRuntime::ScenePlacement Object(const char* id,const char* parent,float width=100,float height=40)
    {
        SceneRuntime::ScenePlacement p; p.id=p.name=id; p.parentId=parent; p.rectTransform.emplace(); p.rectTransform->size={width,height}; return p;
    }
    inline SceneRuntime::SceneLayout Layout()
    {
        SceneRuntime::SceneLayout layout;
        auto canvas=Object("canvas","",400,300); canvas.canvas.emplace(); canvas.canvas->referenceSize={400,300}; canvas.rectTransform.reset();
        auto slider=Object("slider","canvas"); slider.slider.emplace(); slider.slider->binding="gain"; slider.slider->changedEvent="gainChanged";
        auto toggle=Object("toggle","canvas"); toggle.rectTransform->position={0,50}; toggle.toggle.emplace(); toggle.toggle->binding="enabled";
        auto input=Object("input","canvas"); input.rectTransform->position={0,100}; input.inputField.emplace(); input.inputField->binding="name"; input.inputField->maxLength=2; input.inputField->changedEvent="nameChanged"; input.inputField->submittedEvent="nameSubmitted";
        auto scroll=Object("scroll","canvas",100,80); scroll.rectTransform->position={200,0}; scroll.scrollView.emplace(); scroll.scrollView->contentSize={100,240}; scroll.mask.emplace();
        auto item=Object("item","scroll",100,40); item.rectTransform->position={0,100}; item.button.emplace(); item.image.emplace();
        auto group=Object("group","canvas",200,80); group.rectTransform->position={180,150}; group.layoutGroup.emplace(); group.layoutGroup->direction="horizontal"; group.layoutGroup->padding={0,0,0,0}; group.layoutGroup->spacing={10,10}; group.layoutGroup->expandWidth=true;
        auto a=Object("a","group",20,40),b=Object("b","group",20,40); a.image.emplace(); b.image.emplace();
        layout.objects={canvas,slider,toggle,input,scroll,item,group,a,b}; return layout;
    }
    inline void SchemaAndInput()
    {
        ScriptUiValidation::Schema();
        using namespace SceneRuntime;
        auto layout=Layout(); const auto restored=SceneLayout::Parse(layout.Serialize());
        for(size_t i=0;i<layout.objects.size();++i) Require(layout.objects[i].SameComponents(restored.objects[i]),"new UI controls roundtrip all fields");
        auto state=SceneUi::Defaults(layout);
        auto event=SceneUi::Pointer(layout,400,300,75,20,true,true,false,0,state);
        Require(state.Value("gain")==.75f && event.event=="gainChanged" && event.value==.75f,"Slider dragging produces bounded value and changed event");
        SceneUi::Pointer(layout,400,300,150,20,true,false,false,0,state);
        Require(state.Value("gain")==1,"Slider drag clamps outside bounds");
        SceneUi::Pointer(layout,400,300,150,20,false,false,true,0,state);
        SceneUi::Pointer(layout,400,300,50,70,true,true,false,0,state); SceneUi::Pointer(layout,400,300,50,70,false,false,true,0,state);
        Require(state.Value("enabled")==1,"Toggle changes bound state on release");
        SceneUi::Pointer(layout,400,300,50,120,true,true,false,0,state); SceneUi::Pointer(layout,400,300,50,120,false,false,true,0,state);
        event=SceneUi::TextInput(layout,U'日',state); SceneUi::TextInput(layout,U'😀',state); SceneUi::TextInput(layout,U'本',state);
        Require(state.strings.at("name")=="日😀" && event.event=="nameChanged","InputField counts Unicode characters rather than UTF8 bytes");
        SceneUi::TextInput(layout,8,state); Require(state.strings.at("name")=="日","Backspace removes a complete Unicode codepoint");
        event=SceneUi::TextInput(layout,'\r',state); Require(event.event=="nameSubmitted" && event.text=="日","Enter submits the typed string");
        SceneUi::Pointer(layout,400,300,350,120,true,true,false,0,state); Require(state.focused.empty(),"click outside clears input focus");
        Require(SceneUi::Hit(layout,400,300,250,120,state).empty(),"ScrollView clips offscreen child hit testing");
        SceneUi::Pointer(layout,400,300,250,20,false,false,false,-2,state);
        Require(state.scrollOffsets.at("scroll")[1]==80,"wheel moves scroll content");
        Require(SceneUi::Hit(layout,400,300,250,30,state)=="item","scrolling reveals a previously clipped child");
        SceneUi::Pointer(layout,400,300,250,5,true,true,false,0,state); SceneUi::Pointer(layout,400,300,250,-500,true,false,false,0,state);
        Require(state.scrollOffsets.at("scroll")[1]==160,"background scroll dragging clamps to content extent");
        auto persistent=layout; persistent.objects[0].persistent=true; state.values["startRequested"]=1;
        const auto retained=SceneUi::SceneState(persistent,persistent,state,true);
        Require(retained.strings.at("name")=="日" && retained.Value("gain")==1 && retained.scrollOffsets.at("scroll")[1]==160 && retained.Value("startRequested")==0,"Single scene reset preserves persistent UI descendants and resets transition state");
        const auto reset=SceneUi::SceneState(layout,layout,state,true);
        Require(reset.strings.at("name").empty() && reset.Value("gain")==0 && reset.scrollOffsets.at("scroll")[1]==0,"Single scene reset restores nonpersistent UI defaults");
        auto removed=layout; std::erase_if(removed.objects,[](const auto& p){return p.id=="input";}); state.focused="input";
        Require(SceneUi::SceneState(layout,removed,state,false).focused.empty(),"unloading an input field clears its focus");
        const auto a=SceneUi::Resolve(layout,layout.objects[7],400,300),b=SceneUi::Resolve(layout,layout.objects[8],400,300);
        Require(a.size[0]==95 && b.position[0]==285,"horizontal layout distributes width and spacing");
        auto& group=*layout.objects[6].layoutGroup; group.direction="grid"; group.cellSize={30,25}; group.columns=1;
        const auto grid=SceneUi::Resolve(layout,layout.objects[8],400,300);
        Require(grid.size==std::array<float,2>{30,25} && grid.position[1]==185,"grid layout places children in rows");
        group.direction="vertical"; group.expandHeight=true;
        const auto vertical=SceneUi::Resolve(layout,layout.objects[8],400,300);
        Require(vertical.size[1]==35 && vertical.position[1]==195,"vertical layout distributes height");
        Editor::EditHistory history; const auto original=layout.Serialize(); history.Reset({original,"input"});
        layout.objects[3].inputField->placeholder="Changed"; history.Observe({layout.Serialize(),"input"},"ui/input"); history.Commit();
        Require(SceneLayout::Parse(history.Target(false).json).objects[3].inputField->placeholder=="Enter text","UI control changes support Undo");
        auto invalid=layout; invalid.objects[1].slider->maximum=0; bool rejected=false;
        try {invalid.Serialize();} catch(...) {rejected=true;} Require(rejected,"Slider rejects reversed range on save");
        invalid=layout; invalid.objects[6].layoutGroup->columns=0; rejected=false;
        try {invalid.Serialize();} catch(...) {rejected=true;} Require(rejected,"layout rejects zero grid columns");
    }
    inline void SharedResourceReplacement(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        using namespace SceneRuntime;
        SceneLayout layout; auto canvas=Object("cacheCanvas","",64,32); canvas.canvas.emplace(); canvas.canvas->referenceSize={64,32}; canvas.rectTransform.reset();
        auto a=Object("cacheA","cacheCanvas",64,32),b=Object("cacheB","cacheCanvas",64,32);
        a.text.emplace(); a.text->text="█"; a.text->fontSize=64;
        b.text=a.text; b.text->text=""; b.rectTransform->position={100,0};
        layout.objects={canvas,a,b}; SceneUi ui; UiState state; std::string error;
        Require(ui.Prepare(renderer,root,layout,error,state),"distinct cached text resources prepare");
        const auto pixel=[&]() {return EnvironmentValidation::Pixel(renderer,[&](auto* commands){ui.Draw(commands,layout,64,32,state);});};
        const auto original=pixel(); Require(original[0]>64,"original cached text is visible");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float){ui.Draw(commands,layout,64,32,state);})!=Engine::RenderResult::Failed,"cached old sprite is submitted before replacement");
        // B stays alive, so A switches to B's cached signature without uploading or pruning.
        layout.objects[1].text->text=layout.objects[2].text->text;
        Require(ui.Prepare(renderer,root,layout,error,state),"cache-hit replacement synchronizes the old sprite release");
        Require(pixel()==std::array<unsigned char,4>{0,0,0,255},"shared replacement text reaches the GPU framebuffer");
        Require(renderer.WaitForIdle(),"shared text resources finish GPU use before destruction");
    }
    inline void Rendering(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        ScriptUiValidation::Runtime(renderer,root);
        SharedResourceReplacement(renderer,root);
        using namespace SceneRuntime;
        SceneLayout layout; auto canvas=Object("canvas","",64,32); canvas.canvas.emplace(); canvas.canvas->referenceSize={64,32}; canvas.rectTransform.reset();
        auto slider=Object("slider","canvas",64,32); slider.slider.emplace(); slider.slider->value=.75f; slider.slider->fillColor={1,0,0,1};
        layout.objects={canvas,slider}; SceneUi ui; std::string error; auto state=SceneUi::Defaults(layout);
        Require(ui.Prepare(renderer,root,layout,error,state),"Slider synthesizes its GPU background and fill");
        const auto pixel=[&]() {return EnvironmentValidation::Pixel(renderer,[&](auto* commands){ui.Draw(commands,layout,64,32,state);});};
        Require(pixel()==std::array<unsigned char,4>{255,0,0,255},"Slider bound value fills the GPU framebuffer");
        layout.objects[1].slider.reset(); layout.objects[1].toggle.emplace(); layout.objects[1].toggle->checkedColor={0,1,0,1}; state=SceneUi::Defaults(layout); SceneUi::Activate(layout,"slider",state);
        Require(ui.Prepare(renderer,root,layout,error,state) && pixel()==std::array<unsigned char,4>{0,255,0,255},"Toggle checked color reaches GPU");
        layout.objects[1].toggle.reset(); layout.objects[1].inputField.emplace(); layout.objects[1].inputField->text="█"; layout.objects[1].inputField->placeholder=""; layout.objects[1].text.emplace(); layout.objects[1].text->fontSize=64; state=SceneUi::Defaults(layout);
        Require(ui.Prepare(renderer,root,layout,error,state),"InputField text prepares dynamic GPU resources"); const auto original=pixel();
        state.focused="slider"; SceneUi::TextInput(layout,8,state); state.focused.clear();
        Require(ui.Prepare(renderer,root,layout,error,state) && pixel()!=original,"typed text changes rendered pixels through resource preparation");
        auto mask=Object("mask","canvas",8,32); mask.mask.emplace(); layout.objects[1].parentId="mask"; layout.objects[1].inputField.reset(); layout.objects[1].text.reset(); layout.objects[1].image.emplace(); layout.objects[1].image->color={1,0,0,1}; layout.objects.insert(layout.objects.begin()+1,mask);
        Require(ui.Prepare(renderer,root,layout,error) && pixel()==std::array<unsigned char,4>{0,0,0,255},"Mask clips child pixels outside its viewport on GPU");
        Require(renderer.WaitForIdle(),"control resources finish GPU use before destruction");
    }
}
