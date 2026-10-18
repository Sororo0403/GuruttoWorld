#pragma once
#include <SceneRuntime/MaterialAsset.h>
#include <SceneRuntime/TitleController.h>
#include <limits>
#include <stdexcept>

namespace TitleIsolationValidation
{
    /// <summary>ゲームに依存しない描画値と演出状態の入出力を検証します。</summary>
    inline void Run()
    {
        const auto require = [](bool valid, const char* message)
        {
            if (!valid)
                throw std::runtime_error(message);
        };
        SceneRuntime::MaterialAsset material;
        material.values.shaderParameters = {-4, 7, 12, 500};
        material.Validate();
        material.values.shaderParameters[3] = std::numeric_limits<float>::infinity();
        bool rejected = false;
        try
        {
            material.Validate();
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        require(rejected, "nonfinite shader parameters are rejected");

        for (const auto& invalid : {Engine::Json::array({1, 2, 3}), Engine::Json::array({1, 2, 3, 4, 5})})
        {
            rejected = false;
            try
            {
                SceneRuntime::MaterialAsset::ReadShaderParameters({{"shaderParameters", invalid}});
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            require(rejected, "shader parameters require exactly four components");
        }
        SceneRuntime::ScenePlacement surface;
        surface.material.emplace();
        const std::array<float, 4> parameters{-4, 7, 12, 500};
        require(SceneRuntime::Animation::Apply(surface, "material.shaderParameters", parameters) &&
                    surface.material->shaderParameters == parameters && surface.material->surfaceOverride,
                "animation applies all generic shader parameters");
        require(SceneRuntime::Animation::Apply(surface, "material.shaderParameterX", {2, 0, 0, 0}) &&
                    surface.material->shaderParameters == std::array<float, 4>{2, 7, 12, 500},
                "component animation preserves the other shader parameters");

        SceneRuntime::SceneLayout layout;
        SceneRuntime::ScenePlacement canvas;
        canvas.id = "canvas";
        canvas.canvas.emplace();
        auto configuration = SceneRuntime::DefaultMenuConfiguration();
        configuration.entries.resize(1);
        configuration.entries[0].transitionCameraTarget = "camera-target";
        configuration.entries[0].transitionApproach = .75f;
        configuration.entries[0].transitionValues = {{"custom-mode", 7.0f}, {"custom-screen", 9.0f}};
        configuration.transitionClock = "custom-clock";
        configuration.transitionFallbackClock = "custom-fallback";
        configuration.transitionClockScale = 6;
        configuration.transitionDefaults = {{"custom-mode", -2.0f}};
        canvas.canvas->menu = configuration;
        layout.objects.push_back(canvas);
        SceneRuntime::ScenePlacement button;
        button.id = "button";
        button.rectTransform.emplace();
        button.parentId = "canvas";
        button.button.emplace();
        button.button->event = "menu:0";
        layout.objects.push_back(button);
        SceneRuntime::ScenePlacement target;
        target.id = "camera-target";
        layout.objects.push_back(target);
        SceneRuntime::ValidateMenu(configuration);
        const auto restored = SceneRuntime::SceneLayout::Parse(layout.Serialize());
        require(*restored.objects.front().canvas->menu == configuration,
                "generic presentation configuration round trips");
        SceneRuntime::UiState state;
        state.values["custom-fallback"] = 3;
        SceneRuntime::TitleController controller;
        controller.Initialize(layout, state);
        require(controller.Active() && state.Value("custom-clock") == 3 && state.Value("custom-mode") == -2,
                "configured fallback clock and idle values are applied");
        SceneRuntime::UiEvent event;
        event.event = "menu:0";
        require(controller.Activate(event, state), "generic menu activates");
        require(state.Value("custom-mode") == 7 && state.Value("custom-screen") == 9,
                "configured transition values have no action-specific palette");
        for (const auto* key : {"departureKind", "titleEffectTime", "configEnterTime", "transitionPink"})
            require(!state.values.contains(key), "generic controller does not create game presentation state");
        SceneRuntime::MenuConfigTransition transition;
        transition.enabled = true;
        transition.cameraTarget = "camera-target";
        transition.clock = "custom-config-clock";
        transition.enabledState = "custom-gate";
        transition.heldValues = {{"custom-stage", 8.0f}};
        auto& animated = layout.objects.back();
        animated.animation.emplace();
        SceneRuntime::AnimationTrack track;
        track.property = "position";
        track.clock = transition.clock;
        track.keys.back().time = 4;
        animated.animation->tracks.push_back(track);
        SceneRuntime::UiState configState;
        SceneRuntime::TitleConfigTransition configController;
        configController.Configure(layout, transition, configState, true);
        require(configState.Value("custom-stage") == 8 && configState.Value(transition.clock) == 4,
                "settings presentation uses its configured state keys and animation clock");
        require(!configState.values.contains("screen") && !configState.values.contains("configEnterTime"),
                "settings presentation creates no implicit game state");
        for (const auto& entry : SceneRuntime::DefaultMenuConfiguration().entries)
            require(entry.focusClock.empty(), "generic defaults have no game camera animation clocks");
    }
}
