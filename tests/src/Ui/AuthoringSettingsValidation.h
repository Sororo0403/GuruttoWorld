#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <SceneRuntime/Rendering/MaterialAsset.h>
#include <SceneRuntime/Scripts/ScriptRuntime.h>
#include <SceneRuntime/Project/GameSettings.h>
#include <SceneRuntime/Physics/PhysicsWorld.h>
#include "../../../Editor/src/Ui/TitlePreview.h"
#include "../../../Editor/src/Inspector/ScriptDataPanel.h"
#include "../Architecture/DeepStateValidation.h"
#include "../Architecture/FollowupStateValidation.h"
#include "../Assets/SharedContentValidation.h"
#include "../Assets/ProjectUserSettingsValidation.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <functional>
#include <stdexcept>
#include <limits>

namespace AuthoringSettingsValidation
{
    inline void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    /// <summary>登録済みの全成分について、直接比較と従来のタプル比較の同値性を確認します。</summary>
    inline void SameComponentsEquivalence()
    {
        using namespace SceneRuntime;
        const auto check = [](const ScenePlacement& left, const ScenePlacement& right, bool expected)
        {
            const bool original = left.Components() == right.Components();
            Require(original == expected && left.SameComponents(right) == original &&
                        right.SameComponents(left) == original,
                    "Direct component comparison differs from the original tuple comparison");
        };
        ScenePlacement empty;
        check(empty, empty, true);
        ScenePlacement populated;
#define ENGINE_SCENE_PRESENT_OPTIONAL(Type, member, references, assets, order) populated.member.emplace();
#define ENGINE_SCENE_PRESENT_REPEATED(Type, member, references, assets, order) populated.member.emplace_back();
        ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_PRESENT_OPTIONAL, ENGINE_SCENE_PRESENT_REPEATED)
#undef ENGINE_SCENE_PRESENT_OPTIONAL
#undef ENGINE_SCENE_PRESENT_REPEATED
        const auto copied = populated;
        check(populated, copied, true);
#define ENGINE_SCENE_COMPARE_OPTIONAL(Type, member, references, assets, order)                                         \
    {                                                                                                                  \
        auto changed = populated;                                                                                      \
        changed.member.reset();                                                                                        \
        check(populated, changed, false);                                                                              \
        changed = populated;                                                                                           \
        changed.member->id += "-changed";                                                                              \
        check(populated, changed, false);                                                                              \
        changed = populated;                                                                                           \
        changed.member->enabled = !changed.member->enabled;                                                            \
        check(populated, changed, false);                                                                              \
    }
#define ENGINE_SCENE_COMPARE_REPEATED(Type, member, references, assets, order)                                         \
    {                                                                                                                  \
        auto changed = populated;                                                                                      \
        changed.member.clear();                                                                                        \
        check(populated, changed, false);                                                                              \
        changed = populated;                                                                                           \
        changed.member.front().id += "-changed";                                                                       \
        check(populated, changed, false);                                                                              \
        changed = populated;                                                                                           \
        changed.member.front().enabled = !changed.member.front().enabled;                                              \
        check(populated, changed, false);                                                                              \
        changed = populated;                                                                                           \
        changed.member.push_back(changed.member.front());                                                              \
        check(populated, changed, false);                                                                              \
    }
        ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_COMPARE_OPTIONAL, ENGINE_SCENE_COMPARE_REPEATED)
#undef ENGINE_SCENE_COMPARE_OPTIONAL
#undef ENGINE_SCENE_COMPARE_REPEATED
        auto changed = populated;
        changed.persistent = !changed.persistent;
        check(populated, changed, false);
        changed = populated;
        changed.id = "another-object";
        changed.name = "Another name";
        changed.parentId = "another-parent";
        changed.position = {1, 2, 3};
        changed.rotation = {1, 2, 3};
        changed.scale = {2, 3, 4};
        changed.prefab.emplace();
        check(populated, changed, true);
    }
    /// <summary>設定操作の各段階と保存内容を記録し、保存失敗と入力・読み込みの失敗を区別します。</summary>
    inline void PreviewSettings(const std::filesystem::path& output, const std::function<void()>& beforeSave = {},
                                bool verifyFailure = true)
    {
        using namespace SceneRuntime;
        ScenePlacement canvas;
        canvas.id = "menu";
        canvas.name = "Menu";
        canvas.canvas.emplace();
        canvas.canvas->menu = DefaultMenuConfiguration();
        ScenePlacement button;
        button.id = "button";
        button.name = "Button";
        button.parentId = canvas.id;
        button.rectTransform.emplace();
        button.button.emplace();
        button.button->event = "menu:1";
        SceneLayout menu;
        menu.objects = {canvas, button};
        UiState ui = SceneUi::Defaults(menu);
        Editor::TitlePreview preview;
        const auto settingsFile = output / "preview-settings.json";
        std::ostringstream trace;
        const auto record = [&](const char* stage, DWORD error)
        {
            std::ifstream source(settingsFile, std::ios::binary);
            const std::string bytes{std::istreambuf_iterator<char>(source), {}};
            trace << stage << ": win32=" << error << ", state=" << Engine::Json(ui.values).dump() << ", file=" << bytes
                  << '\n';
        };
        const auto check = [&](bool condition, const char* message)
        {
            if (!condition)
                throw std::runtime_error(std::string(message) + "\nsettings=" + settingsFile.generic_string() + "\n" +
                                         trace.str());
        };
        {
            GameSettings initial;
            check(initial.Save(settingsFile), "Test settings initialization failed");
        }
        preview.Initialize(menu, ui, settingsFile);
        record("initialized", GetLastError());
        check(preview.Active(), "Menu validation fixture is not visible");
        preview.Activate({"", "", "", "", "menu:1"}, ui);
        record("opened", GetLastError());
        check(ui.Value("screen", -1) == 1, "Editor preview did not open settings");
        preview.Activate({"", "", "", "", "settings:0"}, ui);
        record("changed", GetLastError());
        check(ui.Value("volume", -1) == 0, "Editor preview did not change volume");
        if (beforeSave)
            beforeSave();
        SetLastError(ERROR_SUCCESS);
        preview.Activate({"", "", "", "", "settings:2"}, ui);
        record("saved", GetLastError());
        check(ui.Value("saveFailed", -1) == 0, "Editor preview reported a settings save failure");
        const auto saved = GameSettings::Load(settingsFile);
        check(saved.loaded && saved.Gain() == 0, "Editor preview did not persist menu settings");
        UiState resumed = SceneUi::Defaults(menu);
        Editor::TitlePreview reopened;
        reopened.Initialize(menu, resumed, settingsFile);
        check(resumed.Value("volume") == 0, "Editor preview did not restore saved settings");
        if (verifyFailure)
        {
            UiState failureUi = SceneUi::Defaults(menu);
            Editor::TitlePreview failure;
            failure.Initialize(menu, failureUi, output); // ディレクトリを設定ファイルで置き換えることはできません。
            failure.Activate({"", "", "", "", "menu:1"}, failureUi);
            failure.Activate({"", "", "", "", "settings:2"}, failureUi);
            check(failureUi.Value("saveFailed") == 1, "Editor preview hid a settings save failure");
        }
    }
    inline void ScriptPurposeWidgets()
    {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {640, 480};
        io.DeltaTime = 1.0f / 60;
        io.Fonts->Build();
        ImGui::NewFrame();
        ImGui::Begin("Explicit script purpose");
        Editor::EditState state;
        Editor::ProjectCatalog catalog;
        const auto check = [&](const char* name, SceneRuntime::ScriptDataPurpose purpose, const char* expectedWidget)
        {
            ImGui::PushID(name);
            const auto expected = ImGui::GetID(expectedWidget);
            ImGui::PopID();
            SceneRuntime::ScriptValue value{std::string("Assets/Scenes/UiCheck.json")};
            Editor::ScriptDataPanel::Draw(state, name, value, &catalog, false, purpose);
            Require(ImGui::GetItemID() == expected, "Script picker depends on field name instead of declared purpose");
        };
        check("sceneDescription", SceneRuntime::ScriptDataPurpose::Text, "sceneDescription");
        check("destination", SceneRuntime::ScriptDataPurpose::SceneAsset, "シーンアセットを選択###Scene asset");
        ImGui::End();
        ImGui::Render();
        ImGui::DestroyContext();
    }
    inline void AnimationRegistry()
    {
        using namespace SceneRuntime;
        ScenePlacement object;
        object.directionalLight.emplace();
        object.pointLight.emplace();
        object.spotLight.emplace();
        object.camera.emplace();
        object.sky.emplace();
        object.sky->ocean.emplace();
        object.particleEmitter.emplace();
        object.material.emplace();
        std::set<std::string> names;
        for (const auto& property : Animation::Properties())
        {
            Require(names.insert(property.name).second, "Duplicate animation property");
            if (std::string_view(property.name).starts_with("ui") || std::string_view(property.name) == "opacity")
                continue;
            Require(property.apply != nullptr, "Registered animation property has no setter");
            std::array<float, 4> value;
            value.fill((property.minimum + property.maximum) * .5f);
            Require(Animation::Apply(object, property.name, value), "Registered animation setter failed");
        }
        ScriptDefinition schema;
        schema.update = [](ScriptContext&) {};
        schema.dataFields = {{"destination", {std::string("Assets/Scenes/UiCheck.json")}}};
        schema.dataPurposes = {{"destination", ScriptDataPurpose::SceneAsset}};
        Require(ScriptRegistry::Register("ExplicitScenePurpose", schema), "Explicit scene purpose rejected");
        schema.dataPurposes = {{"unknown", ScriptDataPurpose::SceneAsset}};
        Require(!ScriptRegistry::Register("UnknownScenePurpose", schema), "Purpose for nonexistent data accepted");
        schema.dataFields = {{"destination", {1.0f}}};
        schema.dataPurposes = {{"destination", ScriptDataPurpose::SceneAsset}};
        Require(!ScriptRegistry::Register("NumericScenePurpose", schema), "Scene purpose on numeric data accepted");
    }
    inline void RegisteredParameterDefaults()
    {
        using namespace SceneRuntime;
        ScriptDefinition definition;
        definition.fields = {{"speed", {17, 0, 100}}};
        definition.start = [](ScriptContext& context)
        {
            context.state["startSpeed"] = context.Value("speed", -1);
        };
        definition.update = [](ScriptContext& context)
        {
            context.object.position[0] = context.Value("speed", -1);
            context.object.position[1] = context.state.at("startSpeed");
        };
        Require(ScriptRegistry::Register("MetadataParameterDefault", definition), "Parameter default fixture rejected");
        ScenePlacement object;
        object.id = "defaults";
        object.scripts = {{"rules", true, "MetadataParameterDefault", {}}};
        SceneLayout scene;
        scene.objects = {object};
        ScriptRuntime runtime;
        std::string error;
        Require(runtime.Update(scene, .01, error) && scene.objects[0].position[0] == 17 &&
                    scene.objects[0].position[1] == 17,
                "Registered defaults did not reach Start/Update");
        Require(scene.objects[0].scripts[0].parameters.empty(), "Runtime defaults changed authored parameters");
        scene.objects[0].scripts[0].parameters["speed"] = 23;
        Require(runtime.Update(scene, .01, error) && scene.objects[0].position[0] == 23,
                "Saved parameters did not override defaults");
    }
    /// <summary>成分検証の共有が無効値・構造変更・配置全体の制約を隠さないことを確認します。</summary>
    inline void ComponentValidationReuse()
    {
        using namespace SceneRuntime;
        SceneLayout baseline;
        for (int index = 0; index < 36; ++index)
        {
            ScenePlacement object;
            object.id = "validation-" + std::to_string(index);
            object.name = object.id;
            object.material.emplace();
            baseline.objects.push_back(std::move(object));
        }
        baseline.Validate();
        auto repeated = baseline;
        for (auto& object : repeated.objects)
            object.material->roughness = .7f;
        repeated.ValidateChanges(baseline);
        const auto reject = [&](const SceneLayout& candidate)
        {
            bool rejected = false;
            try
            {
                candidate.ValidateChanges(baseline);
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            Require(rejected, "Component validation reuse concealed an invalid change");
        };
        for (const float invalid : {-1.0f, std::numeric_limits<float>::quiet_NaN()})
        {
            auto candidate = repeated;
            candidate.objects.back().material->roughness = invalid;
            reject(candidate);
        }
        auto candidate = repeated;
        candidate.objects.back().material->id = "transform";
        reject(candidate);
        candidate = repeated;
        candidate.objects.back().pointLight.emplace();
        candidate.objects.back().pointLight->id = "material";
        reject(candidate);
        candidate = repeated;
        candidate.objects.back().scale[0] = 0;
        reject(candidate);
        candidate = repeated;
        candidate.objects.back().position[0] = std::numeric_limits<float>::quiet_NaN();
        reject(candidate);
        candidate = repeated;
        candidate.objects.back().parentId = candidate.objects.back().id;
        reject(candidate);
        candidate = repeated;
        candidate.settings.mainCamera = candidate.objects.back().id;
        reject(candidate);
        candidate = repeated;
        for (auto& object : candidate.objects)
            object.pointLight.emplace();
        reject(candidate);
        candidate = repeated;
        for (size_t index = 0; index < 2; ++index)
        {
            candidate.objects[index].canvas.emplace();
            candidate.objects[index].canvas->menu = DefaultMenuConfiguration();
        }
        reject(candidate);
        candidate = repeated;
        for (size_t index = 0; index < candidate.objects.size(); ++index)
            candidate.objects[index].material->roughness = .1f + float(index % 12) * .05f;
        candidate.ValidateChanges(baseline);
        candidate.objects.back().material->roughness = -1;
        reject(candidate);
        candidate = repeated;
        for (auto& object : candidate.objects)
            object.scripts.emplace_back();
        candidate.ValidateChanges(baseline);
        candidate.objects.back().scripts.push_back(candidate.objects.back().scripts.front());
        reject(candidate);
        const auto accepted = [](const auto& validate)
        {
            try
            {
                validate();
                return true;
            }
            catch (const std::exception&)
            {
                return false;
            }
        };
        const auto sameAcceptance = [&](const SceneLayout& paths)
        {
            const bool complete = accepted(
                [&]()
                {
                    paths.Validate();
                });
            Require(accepted(
                        [&]()
                        {
                            paths.ValidateChanges(baseline);
                        }) == complete,
                    "Component validation reuse concealed a raw path spelling difference");
            return complete;
        };
        candidate = repeated;
        candidate.objects[0].meshRenderer.emplace();
        candidate.objects[0].meshRenderer->model = "Assets/Models/probe.obj";
        candidate.objects[1].meshRenderer = candidate.objects[0].meshRenderer;
        Require(sameAcceptance(candidate), "Canonical model path fixture rejected");
        candidate.objects[1].meshRenderer->model = "Assets\\Models\\probe.obj";
        Require(!sameAcceptance(candidate), "Backslash model path unexpectedly accepted");
        candidate = repeated;
        candidate.objects[0].image.emplace();
        candidate.objects[0].image->texture = "Assets/Textures/probe.png";
        candidate.objects[1].image = candidate.objects[0].image;
        Require(sameAcceptance(candidate), "Canonical image path fixture rejected");
        candidate.objects[1].image->texture = "Assets\\Textures\\probe.png";
        // Imageの保存処理はgeneric_u8stringへ正規化してからAssets/の制約を検証します。
        Require(sameAcceptance(candidate), "Normalized image path changed validation acceptance");
        candidate = repeated;
        candidate.objects[0].tilemap.emplace();
        candidate.objects[0].tilemap->texture = "Assets/Textures/probe.png";
        candidate.objects[1].tilemap = candidate.objects[0].tilemap;
        Require(sameAcceptance(candidate), "Canonical tilemap path fixture rejected");
        candidate.objects[1].tilemap->texture = "Assets\\Textures\\probe.png";
        // Tilemapの保存処理もgeneric_stringで区切り文字を正規化します。
        Require(sameAcceptance(candidate), "Normalized tilemap path changed validation acceptance");
        for (const bool slots : {false, true})
        {
            candidate = repeated;
            if (slots)
            {
                candidate.objects[0].material->slots = {"Assets/Materials/probe.mat"};
                candidate.objects[1].material->slots = {"Assets\\Materials\\probe.mat"};
            }
            else
            {
                candidate.objects[0].material->asset = "Assets/Materials/probe.mat";
                candidate.objects[1].material->asset = "Assets\\Materials\\probe.mat";
            }
            static_cast<void>(sameAcceptance(candidate));
        }
        repeated.ValidateChanges(baseline);
    }
    inline void Run()
    {
        SameComponentsEquivalence();
        ComponentValidationReuse();
        using namespace SceneRuntime;
        DeepStateValidation::Run();
        FollowupStateValidation::Run();
        AnimationRegistry();
        ScriptPurposeWidgets();
        RegisteredParameterDefaults();
        for (const auto& entry : std::filesystem::directory_iterator("Games/GuruttoWorld/Assets/Scenes"))
        {
            if (entry.path().extension() != ".json")
                continue;
            const auto layout = SceneLayout::Load(entry.path());
            Require(SceneLayout::Parse(layout.Serialize()).Serialize() == layout.Serialize(),
                    "Existing scene roundtrip changed behavior data");
        }
        const auto output = std::filesystem::path("generated/tests/authoring-settings") /
                            (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::cout << "Authoring output: " << output.generic_string() << '\n';
        std::filesystem::create_directories(output);
        SharedContentValidation::Run(std::filesystem::absolute(output / "shared-content"));
        ProjectUserSettingsValidation::Run(
            std::filesystem::temp_directory_path() /
            ("settings-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64())));
        auto scene = SceneLayout::Load("Games/GuruttoWorld/Assets/Scenes/OceanSunset.json");
        scene.settings.gravity = {2, -9.81f, 1};
        scene.settings.fixedStep = .02f;
        scene.settings.maximumFixedSteps = 8;
        scene.settings.shadowResolution = 1024;
        scene.settings.localShadowResolution = 256;
        scene.settings.sceneRenderScale = .5f;
        scene.settings.gameRenderScale = .75f;
        scene.settings.sceneRealtime = false;
        scene.settings.skyShader = "Shaders/Sky.hlsl";
        scene.settings.oceanShader = "Shaders/OceanSunset.hlsl";
        scene.settings.particleShader = "Shaders/GlowParticle.hlsl";
        scene.settings.postShader = "Shaders/PostEffects.hlsl";
        scene.settings.playerInputs[4] = "CustomJump";
        auto& sky = *std::find_if(scene.objects.begin(), scene.objects.end(),
                                  [](const auto& p)
                                  {
                                      return p.sky && p.sky->ocean;
                                  });
        auto& ocean = *sky.sky->ocean;
        ocean.atmosphereSteps = 8;
        ocean.solarSteps = 4;
        ocean.cloudSteps = 12;
        ocean.cloudLightSteps = 2;
        ocean.waveCount = 5;
        ocean.seaSteps = 10;
        ocean.seaRefineSteps = 6;
        ocean.solarIntensity = 12;
        ocean.waveDirection = 1.1f;
        ocean.waveSpread = 1.5f;
        ocean.cloudScale = .0008f;
        ocean.cloudWind = {-1, .5f};
        ocean.cloudNoiseWeights = {.5f, .3f, .2f};
        ocean.solarColor = {1, .8f, .6f};
        scene.Save(output / "Ocean.json");
        const auto restored = SceneLayout::Load(output / "Ocean.json");
        Require(restored.settings == scene.settings, "Scene settings did not save");
        const auto& restoredSky = *std::find_if(restored.objects.begin(), restored.objects.end(),
                                                [](const auto& p)
                                                {
                                                    return p.sky && p.sky->ocean;
                                                });
        Require(restoredSky.sky == sky.sky, "Ocean quality/shape did not save");
        auto invalid = scene;
        invalid.settings.shadowResolution = 1000;
        bool rejected = false;
        try
        {
            invalid.Validate();
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected, "Non-power-of-two shadows accepted");
        ScenePlacement actor;
        actor.id = "animated";
        actor.name = "Animated";
        actor.sky.emplace();
        actor.sky->ocean.emplace();
        actor.pointLight.emplace();
        actor.material.emplace();
        Require(Animation::Apply(actor, "scale", {2, 3, 4, 0}) && actor.scale[1] == 3, "Scale animation failed");
        Require(Animation::Apply(actor, "ocean.sunElevation", {10, 0, 0, 0}) && actor.sky->ocean->sunElevation == 10,
                "Ocean animation failed");
        Require(Animation::Apply(actor, "pointLight.intensity", {12, 0, 0, 0}) && actor.pointLight->intensity == 12,
                "Light animation failed");
        Require(Animation::Apply(actor, "material.roughness", {.3f, 0, 0, 0}) && actor.material->surfaceOverride,
                "Material animation failed");
        actor.animation.emplace();
        actor.animation->tracks.push_back(
            {"ocean.sunElevation", "customFocusClock", "linear", 0, false, {{0, {10, 0, 0, 0}}, {1, {20, 0, 0, 0}}}});
        SceneLayout animated;
        animated.objects = {actor};
        Require(SceneLayout::Parse(animated.Serialize()).objects[0].animation->tracks[0].clock == "customFocusClock",
                "Custom clock did not save");
        MaterialAsset material;
        material.shader = "Shaders/Mesh.hlsl";
        const auto materialRoot = output / "material";
        std::filesystem::create_directories(materialRoot / "Shaders");
        std::filesystem::copy_file("Content/Shaders/Mesh.hlsl", materialRoot / material.shader,
                                   std::filesystem::copy_options::overwrite_existing);
        material.Save(materialRoot, "Assets/Materials/Shader.mat");
        Require(MaterialAsset::Load(materialRoot, "Assets/Materials/Shader.mat").shader == material.shader,
                "Material shader did not save");
        ScriptDefinition definition;
        definition.update = [](ScriptContext& c)
        {
            if (std::get<bool>(c.Data("customFlag")->value))
                c.object.position[0] = 7;
        };
        Require(ScriptRegistry::Register("AuthoringCustomData", definition), "Extra data test registration failed");
        actor.scripts = {
            {"script",
             true,
             "AuthoringCustomData",
             {},
             {{"customFlag", {true}}, {"targets", {ScriptValue::Array{{ScriptObjectReference{"animated"}}}}}}}};
        animated.objects = {actor};
        auto data = SceneLayout::Parse(animated.Serialize());
        ScriptRuntime runtime;
        std::string error;
        Require(runtime.Update(data, .01, error) && data.objects[0].position[0] == 7,
                "Editor-added typed data failed at runtime");
        PreviewSettings(output);
        ScenePlacement body;
        body.id = "body";
        body.name = "Body";
        body.position = {0, 3, 0};
        body.boxCollider.emplace();
        body.rigidBody.emplace();
        SceneLayout physical;
        physical.objects = {body};
        physical.settings.gravity = {0, 0, 0};
        PhysicsWorld physics;
        ScenePhysics::States states;
        Require(physics.Advance(physical, states, .05, 0, 0, false, "Games/GuruttoWorld", error),
                "Zero gravity physics failed");
        Require(std::abs(physical.objects[0].position[1] - 3) < .001f, "World gravity setting was ignored");
        physical.settings.gravity = {0, -10, 0};
        Require(physics.Advance(physical, states, .05, 0, 0, false, "Games/GuruttoWorld", error),
                "Changed world gravity physics failed");
        Require(physical.objects[0].position[1] < 3, "Changed world gravity was not applied");
    }
}
