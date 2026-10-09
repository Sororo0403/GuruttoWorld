#pragma once
#include <SceneRuntime/SceneEnvironment.h>
#include <SceneRuntime/ScriptModuleApi.h>
#include "EnvironmentValidation.h"
#include <limits>
namespace ScriptUiValidation
{
    inline void Require(bool success,const char* message) {if(!success) throw std::runtime_error(message);}
    inline void Schema()
    {
        using namespace SceneRuntime; SceneLayout layout; size_t nextId=1; ScriptScene scene(layout,nextId);
        scene.SetUiValue("gain",.5f); scene.SetUiText("name","日本😀"); scene.Commit();
        Require(scene.uiCommands.values.at("gain")==.5f && scene.uiCommands.texts.at("name")=="日本😀","Script UI commands store typed bindings");
        const auto reject=[&](auto action) {bool failed=false;try {action();} catch(...) {failed=true;} Require(failed,"invalid Script UI command is rejected");};
        reject([&]{scene.SetUiValue("gain",std::numeric_limits<float>::infinity());});
        reject([&]{scene.SetUiValue("bad=key",1);}); reject([&]{scene.SetUiText("name",std::string(4097,'x'));});
        reject([&]{scene.SetUiText("name",std::string("\xc0\x80"));});
        Require(scene.uiCommands.texts.at("name")=="日本😀","rejected text leaves the queued value unchanged");
        ScriptScene limited(layout,nextId);for(int i=0;i<256;++i) limited.SetUiValue(std::to_string(i),0);
        reject([&]{limited.SetUiText("overflow","");});
        ScriptModuleAbi abi; Require(abi.version==3 && abi.sceneBytes==sizeof(ScriptScene),"module ABI describes the ScriptScene layout");
        auto incompatible=abi; ++incompatible.sceneBytes; Require(incompatible!=abi,"module ABI rejects ScriptScene layout mismatches");
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        using namespace SceneRuntime;
        auto stage=std::make_shared<int>(1); ScriptDefinition definition;
        definition.dataFields["uiResource"]=ScriptValue{0.0f};
        definition.fixedUpdate=[stage](ScriptContext& c) {if(*stage<3) c.scene->SetUiValue("gain",.25f);if(*stage==4) {c.scene->SetUiValue("gain",.125f);c.scene->SetUiText("name","fixedFailed");throw std::runtime_error("Validation fixed UI failure");}};
        definition.update=[stage](ScriptContext& c) {if(*stage<3) {c.scene->SetUiValue("gain",.5f);c.scene->SetUiValue("toggle",1);}if(*stage==5) {c.scene->SetUiValue("gain",.125f);c.scene->SetUiText("name","updateFailed");throw std::runtime_error("Validation update UI failure");}if(*stage==6) {c.dataState->try_emplace("uiResource",ScriptValue{0.0f});c.object.position[0]=++c.state["uiResourceTicks"]+(++std::get<float>(c.dataState->at("uiResource").value));c.scene->SetUiValue("gain",.125f);c.scene->SetUiText("name","");}};
        definition.lateUpdate=[stage](ScriptContext& c) {if(*stage<3) {c.scene->SetUiValue("gain",1);c.scene->SetUiText("name",*stage==1?"█":"failed");if(*stage==2) throw std::runtime_error("Validation UI failure");}};
        std::string error; Require(ScriptRegistry::InstallModule("ValidationUiCommandOwner",{{"ValidationUiCommands",definition}},error),"Script UI test behaviour registers");
        SceneLayout layout; ScenePlacement canvas;canvas.id=canvas.name="canvas";canvas.canvas.emplace();canvas.canvas->referenceSize={64,32};
        canvas.scripts.push_back({});canvas.scripts.back().behaviour="ValidationUiCommands";
        ScenePlacement slider;slider.id=slider.name="slider";slider.parentId="canvas";slider.rectTransform.emplace();slider.rectTransform->size={64,32};slider.slider.emplace();slider.slider->binding="gain";slider.slider->fillColor={1,0,0,1};
        ScenePlacement input;input.id=input.name="input";input.parentId="canvas";input.rectTransform.emplace();input.rectTransform->size={64,32};input.inputField.emplace();input.inputField->binding="name";input.inputField->placeholder="";input.text.emplace();input.text->fontSize=64;input.image.emplace();input.image->color={0,0,0,0};
        ScenePlacement toggle;toggle.id=toggle.name="toggleControl";toggle.parentId="canvas";toggle.rectTransform.emplace();toggle.rectTransform->size={8,8};toggle.toggle.emplace();toggle.toggle->binding="toggle";
        layout.objects={canvas,slider,input,toggle}; {
        SceneEnvironment environment;
        Require(environment.Initialize(renderer,root,layout,error),error.c_str());
        const auto pixel=[&]() {return EnvironmentValidation::Pixel(renderer,[&](auto* commands){environment.Draw(commands,64,32);});};
        const auto initial=pixel();environment.Update(1.0/60,true,true);
        Require(environment.Ui().Value("gain")==1 && environment.Ui().Value("toggle")==1 && environment.Ui().strings.at("name")=="█","Fixed Update and Late UI commands apply in phase order after successful frame");
        const auto updated=pixel();Require(updated!=initial && updated[1]>64 && updated[2]>64,"Script-set InputField binding reaches GPU text rendering");
        *stage=2;environment.Update(1.0/60,true,true);
        Require(environment.Ui().strings.at("name")=="█" && environment.Ui().Value("gain")==1 && pixel()==updated,"failed LateUpdate preserves UI state and rendered pixels");
        *stage=3;environment.Update(1.0/60,true,true);
        Require(environment.Ui().strings.at("name")=="█" && environment.Ui().Value("gain")==1,"failed frame UI commands do not leak into the next successful frame");
        for(const int failedPhase:{4,5}) {
            *stage=failedPhase;environment.Update(1.0/60,true,true);
            Require(environment.Ui().strings.at("name")=="█" && environment.Ui().Value("gain")==1,"failed Fixed or Update preserves Script UI values");
            *stage=3;environment.Update(1.0/60,true,true);
            Require(environment.Ui().strings.at("name")=="█" && environment.Ui().Value("gain")==1,"failed Fixed or Update commands do not leak into later frames");
        }
        definition.fixedUpdate={}; Require(ScriptRegistry::InstallModule("ValidationUiCommandOwner",{{"ValidationUiCommands",definition}},error),"resource-failure fixture disables fixed callbacks");
        bool rejectResources=true;
        const auto prepareUi=environment.World().UiPreparation();
        environment.World().SetUiPreparation([&](const SceneLayout& candidate,const ScriptUiCommands& commands,std::string& diagnostic) {
            if(!rejectResources) return prepareUi(candidate,commands,diagnostic);
            auto invalid=candidate;
            const auto field=std::find_if(invalid.objects.begin(),invalid.objects.end(),[](const auto& object){return object.id=="input";});
            field->image->texture="Assets/Textures/ValidationMissingUiTexture.png";
            return prepareUi(invalid,commands,diagnostic);
        });
        const auto beforeLayout=environment.World().Layout().Serialize(); const auto beforeClock=environment.MotionSeconds();
        *stage=6;environment.Update(1.0/60,true,true);
        Require(environment.World().Layout().Serialize()==beforeLayout && environment.MotionSeconds()==beforeClock && environment.Ui().strings.at("name")=="█" && environment.Ui().Value("gain")==1 && pixel()==updated,"UI resource rejection preserves Script position state UI clocks and GPU pixels");
        rejectResources=false;environment.Update(1.0/60,true,true);
        Require(environment.World().Layout().objects[0].position[0]==2 && environment.Ui().strings.at("name").empty() && environment.Ui().Value("gain")==.125f,"next accepted frame resumes from unchanged numeric and typed Script state");
        Require(pixel()!=updated,"accepted transactional UI resources replace the previous rendered text");
        environment.World().SetUiPreparation(prepareUi);
        Require(renderer.WaitForIdle(),"Script UI resources finish before destruction");
        }
        Require(ScriptRegistry::InstallModule("ValidationUiCommandOwner",{},error),"Script UI test behaviour releases");
    }
}
