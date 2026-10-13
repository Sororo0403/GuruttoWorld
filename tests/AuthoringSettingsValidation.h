#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <SceneRuntime/MaterialAsset.h>
#include <SceneRuntime/ScriptRuntime.h>
#include <SceneRuntime/GameSettings.h>
#include <SceneRuntime/PhysicsWorld.h>
#include "../Editor/src/TitlePreview.h"
#include "../Editor/src/ScriptDataPanel.h"
#include <fstream>
#include <stdexcept>

namespace AuthoringSettingsValidation {
inline void Require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
inline void ScriptPurposeWidgets() {
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={640,480};io.DeltaTime=1.0f/60;io.Fonts->Build();
    ImGui::NewFrame();ImGui::Begin("Explicit script purpose");
    Editor::EditState state;Editor::ProjectCatalog catalog;
    const auto check=[&](const char* name,SceneRuntime::ScriptDataPurpose purpose,const char* expectedWidget) {
        ImGui::PushID(name);const auto expected=ImGui::GetID(expectedWidget);ImGui::PopID();
        SceneRuntime::ScriptValue value{std::string("Assets/Scenes/UiCheck.json")};
        Editor::ScriptDataPanel::Draw(state,name,value,&catalog,false,purpose);
        Require(ImGui::GetItemID()==expected,"Script picker depends on field name instead of declared purpose");
    };
    check("sceneDescription",SceneRuntime::ScriptDataPurpose::Text,"sceneDescription");
    check("destination",SceneRuntime::ScriptDataPurpose::SceneAsset,"シーンアセットを選択###Scene asset");
    ImGui::End();ImGui::Render();ImGui::DestroyContext();
}
inline void AnimationRegistry() {
    using namespace SceneRuntime;
    ScenePlacement object;object.directionalLight.emplace();object.pointLight.emplace();object.spotLight.emplace();object.camera.emplace();
    object.sky.emplace();object.sky->ocean.emplace();object.particleEmitter.emplace();object.material.emplace();
    std::set<std::string> names;
    for(const auto& property:Animation::Properties()) {
        Require(names.insert(property.name).second,"Duplicate animation property");
        if(std::string_view(property.name).starts_with("ui") || std::string_view(property.name)=="opacity") continue;
        Require(property.apply!=nullptr,"Registered animation property has no setter");
        std::array<float,4> value;value.fill((property.minimum+property.maximum)*.5f);
        Require(Animation::Apply(object,property.name,value),"Registered animation setter failed");
    }
    ScriptDefinition schema;schema.update=[](ScriptContext&){};
    schema.dataFields={{"destination",{std::string("Assets/Scenes/UiCheck.json")}}};schema.dataPurposes={{"destination",ScriptDataPurpose::SceneAsset}};
    Require(ScriptRegistry::Register("ExplicitScenePurpose",schema),"Explicit scene purpose rejected");
    schema.dataPurposes={{"unknown",ScriptDataPurpose::SceneAsset}};
    Require(!ScriptRegistry::Register("UnknownScenePurpose",schema),"Purpose for nonexistent data accepted");
    schema.dataFields={{"destination",{1.0f}}};schema.dataPurposes={{"destination",ScriptDataPurpose::SceneAsset}};
    Require(!ScriptRegistry::Register("NumericScenePurpose",schema),"Scene purpose on numeric data accepted");
}
inline void RegisteredParameterDefaults() {
    using namespace SceneRuntime;
    ScriptDefinition definition;definition.fields={{"speed",{17,0,100}}};
    definition.start=[](ScriptContext& context){context.state["startSpeed"]=context.Value("speed",-1);};
    definition.update=[](ScriptContext& context){context.object.position[0]=context.Value("speed",-1);context.object.position[1]=context.state.at("startSpeed");};
    Require(ScriptRegistry::Register("MetadataParameterDefault",definition),"Parameter default fixture rejected");
    ScenePlacement object;object.id="defaults";object.scripts={{"rules",true,"MetadataParameterDefault",{}}};
    SceneLayout scene;scene.objects={object};ScriptRuntime runtime;std::string error;
    Require(runtime.Update(scene,.01,error) && scene.objects[0].position[0]==17 && scene.objects[0].position[1]==17,"Registered defaults did not reach Start/Update");
    Require(scene.objects[0].scripts[0].parameters.empty(),"Runtime defaults changed authored parameters");
    scene.objects[0].scripts[0].parameters["speed"]=23;
    Require(runtime.Update(scene,.01,error) && scene.objects[0].position[0]==23,"Saved parameters did not override defaults");
}
inline void Run() {
    using namespace SceneRuntime;
    AnimationRegistry();
    ScriptPurposeWidgets();
    RegisteredParameterDefaults();
    for(const auto& entry:std::filesystem::directory_iterator("Content/Assets/Scenes")) {
        if(entry.path().extension()!=".json") continue;
        const auto layout=SceneLayout::Load(entry.path());
        Require(SceneLayout::Parse(layout.Serialize()).Serialize()==layout.Serialize(),"Existing scene roundtrip changed behavior data");
    }
    const std::filesystem::path output="generated/tests/authoring-settings";
    std::filesystem::create_directories(output);
    auto scene=SceneLayout::Load("Content/Assets/Scenes/OceanSunset.json");
    scene.settings.gravity={2,-9.81f,1};scene.settings.fixedStep=.02f;scene.settings.maximumFixedSteps=8;
    scene.settings.shadowResolution=1024;scene.settings.localShadowResolution=256;
    scene.settings.sceneRenderScale=.5f;scene.settings.gameRenderScale=.75f;scene.settings.sceneRealtime=false;
    scene.settings.skyShader="Shaders/Sky.hlsl";scene.settings.oceanShader="Shaders/OceanSunset.hlsl";
    scene.settings.particleShader="Shaders/GlowParticle.hlsl";scene.settings.postShader="Shaders/PostEffects.hlsl";
    scene.settings.playerInputs[4]="CustomJump";
    auto& sky=*std::find_if(scene.objects.begin(),scene.objects.end(),[](const auto& p){return p.sky && p.sky->ocean;});
    auto& ocean=*sky.sky->ocean;
    ocean.atmosphereSteps=8;ocean.solarSteps=4;ocean.cloudSteps=12;ocean.cloudLightSteps=2;
    ocean.waveCount=5;ocean.seaSteps=10;ocean.seaRefineSteps=6;ocean.solarIntensity=12;
    ocean.waveDirection=1.1f;ocean.waveSpread=1.5f;ocean.cloudScale=.0008f;ocean.cloudWind={-1,.5f};
    ocean.cloudNoiseWeights={.5f,.3f,.2f};ocean.solarColor={1,.8f,.6f};
    scene.Save(output/"Ocean.json");const auto restored=SceneLayout::Load(output/"Ocean.json");
    Require(restored.settings==scene.settings,"Scene settings did not save");
    const auto& restoredSky=*std::find_if(restored.objects.begin(),restored.objects.end(),[](const auto& p){return p.sky && p.sky->ocean;});
    Require(restoredSky.sky==sky.sky,"Ocean quality/shape did not save");
    auto invalid=scene;invalid.settings.shadowResolution=1000;
    bool rejected=false;try {invalid.Validate();} catch(const std::exception&) {rejected=true;}
    Require(rejected,"Non-power-of-two shadows accepted");
    ScenePlacement actor;actor.id="animated";actor.name="Animated";
    actor.sky.emplace();actor.sky->ocean.emplace();actor.pointLight.emplace();actor.material.emplace();
    Require(Animation::Apply(actor,"scale",{2,3,4,0}) && actor.scale[1]==3,"Scale animation failed");
    Require(Animation::Apply(actor,"ocean.sunElevation",{10,0,0,0}) && actor.sky->ocean->sunElevation==10,"Ocean animation failed");
    Require(Animation::Apply(actor,"pointLight.intensity",{12,0,0,0}) && actor.pointLight->intensity==12,"Light animation failed");
    Require(Animation::Apply(actor,"material.roughness",{.3f,0,0,0}) && actor.material->surfaceOverride,"Material animation failed");
    actor.animation.emplace();actor.animation->tracks.push_back({"ocean.sunElevation","customFocusClock","linear",0,false,{{0,{10,0,0,0}},{1,{20,0,0,0}}}});
    SceneLayout animated;animated.objects={actor};
    Require(SceneLayout::Parse(animated.Serialize()).objects[0].animation->tracks[0].clock=="customFocusClock","Custom clock did not save");
    MaterialAsset material;material.shader="Shaders/Mesh.hlsl";
    const auto materialRoot=output/"material";std::filesystem::create_directories(materialRoot/"Shaders");
    std::filesystem::copy_file("Content/Shaders/Mesh.hlsl",materialRoot/material.shader,std::filesystem::copy_options::overwrite_existing);
    material.Save(materialRoot,"Assets/Materials/Shader.mat");
    Require(MaterialAsset::Load(materialRoot,"Assets/Materials/Shader.mat").shader==material.shader,"Material shader did not save");
    ScriptDefinition definition;definition.update=[](ScriptContext& c){if(std::get<bool>(c.Data("customFlag")->value)) c.object.position[0]=7;};
    Require(ScriptRegistry::Register("AuthoringCustomData",definition),"Extra data test registration failed");
    actor.scripts={{"script",true,"AuthoringCustomData",{},{{"customFlag",{true}},{"targets",{ScriptValue::Array{{ScriptObjectReference{"animated"}}}}}}}};
    animated.objects={actor};auto data=SceneLayout::Parse(animated.Serialize());ScriptRuntime runtime;std::string error;
    Require(runtime.Update(data,.01,error) && data.objects[0].position[0]==7,"Editor-added typed data failed at runtime");
    ScenePlacement canvas;canvas.id="menu";canvas.name="Menu";canvas.canvas.emplace();canvas.canvas->menu=DefaultMenuConfiguration();
    ScenePlacement button;button.id="button";button.name="Button";button.parentId=canvas.id;button.rectTransform.emplace();button.button.emplace();button.button->event="menu:1";
    SceneLayout menu;menu.objects={canvas,button};UiState ui=SceneUi::Defaults(menu);Editor::TitlePreview preview;
    const auto settingsFile=output/"preview-settings.json";
    {GameSettings initial;Require(initial.Save(settingsFile),"Test settings initialization failed");}
    preview.Initialize(menu,ui,settingsFile);
    Require(preview.Active(),"Menu validation fixture is not visible");
    preview.Activate({"", "", "", "", "menu:1"},ui);
    preview.Activate({"", "", "", "", "settings:0"},ui);
    preview.Activate({"", "", "", "", "settings:2"},ui);
    Require(GameSettings::Load(settingsFile).Gain()==0,"Editor preview did not persist menu settings");
    UiState resumed=SceneUi::Defaults(menu);Editor::TitlePreview reopened;
    reopened.Initialize(menu,resumed,settingsFile);
    Require(resumed.Value("volume")==0,"Editor preview did not restore saved settings");
    UiState failureUi=SceneUi::Defaults(menu);Editor::TitlePreview failure;
    failure.Initialize(menu,failureUi,output); // A directory cannot be replaced by a settings file.
    failure.Activate({"", "", "", "", "menu:1"},failureUi);
    failure.Activate({"", "", "", "", "settings:2"},failureUi);
    Require(failureUi.Value("saveFailed")==1,"Editor preview hid a settings save failure");
    ScenePlacement body;body.id="body";body.name="Body";body.position={0,3,0};body.boxCollider.emplace();body.rigidBody.emplace();
    SceneLayout physical;physical.objects={body};physical.settings.gravity={0,0,0};
    PhysicsWorld physics;ScenePhysics::States states;
    Require(physics.Advance(physical,states,.05,0,0,false,"Content",error),"Zero gravity physics failed");
    Require(std::abs(physical.objects[0].position[1]-3)<.001f,"World gravity setting was ignored");
    physical.settings.gravity={0,-10,0};
    Require(physics.Advance(physical,states,.05,0,0,false,"Content",error),"Changed world gravity physics failed");
    Require(physical.objects[0].position[1]<3,"Changed world gravity was not applied");
}
}
