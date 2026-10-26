#include <SceneRuntime/Gameplay/MenuConfiguration.h>

namespace SceneRuntime
{
    MenuConfiguration DefaultMenuConfiguration()
    {
        MenuConfiguration c;
        for (const auto* key : {"screen", "screenNotSettings", "selected", "row", "gamepad", "intro", "volume",
                                "motion", "saveFailed", "cameraFocus", "focusView", "quitTransition", "transition"})
            c.bindings.push_back({key, key});
        return c;
    }

    float EvaluateMenuBinding(const MenuStateBinding& binding, float value,
                              const std::map<std::string, float>& defaults)
    {
        const auto found = defaults.find(binding.factor);
        const float factor = binding.factor.empty()    ? 1.0f
                             : found == defaults.end() ? binding.factorDefault
                                                       : found->second;
        if (binding.operation == "equal")
            value = value == binding.compare ? 1.0f : 0.0f;
        else if (binding.operation == "notEqual")
            value = value != binding.compare ? 1.0f : 0.0f;
        else if (binding.operation == "emphasis")
            value = value == binding.compare ? 1.0f : factor;
        if (binding.operation != "emphasis")
            value *= factor;
        return std::clamp(value * binding.scale + binding.offset, binding.minimum, binding.maximum);
    }
}

namespace SceneRuntime::MenuValidation
{
    void Values(const std::map<std::string, float>& values)
    {
        Require(values.size() <= 128);
        for (const auto& [key, value] : values)
            Require(Key(key) && Number(value));
    }

    void Entry(const MenuEntry& entry, std::set<int>& indices)
    {
        Require(Engine::InRange(entry.transitionApproach, .05f, 1));
        Values(entry.transitionValues);
        Require(entry.index >= 0 && entry.index < 128 && indices.insert(entry.index).second && Number(entry.focus) &&
                Number(entry.view) && Engine::ValidString(entry.focusClock, 128) &&
                Engine::ValidString(entry.transitionCameraTarget, 128));
        Require(Engine::OneOf(entry.action, {"loadScene", "quit", "settings", "setState"}));
        Require(Engine::ValidString(entry.target, 16384));
        if (entry.action == "loadScene" && !entry.target.empty())
            Require(entry.target.starts_with("Assets/Scenes/") && entry.target.ends_with(".json") &&
                    entry.target.find("..") == std::string::npos && entry.target.find('\\') == std::string::npos &&
                    entry.target.find(':') == std::string::npos);
    }

    void Setting(const MenuSetting& setting, std::set<std::string>& keys)
    {
        Require(Engine::OneOf(setting.action, {"number", "toggle", "save"}));
        Require(setting.action == "save" || (Key(setting.key) && keys.insert(setting.key).second));
        Require(Number(setting.minimum) && Number(setting.maximum) && Number(setting.step) && Number(setting.initial) &&
                setting.minimum < setting.maximum && setting.step > 0 &&
                Engine::InRange(setting.initial, setting.minimum, setting.maximum));
    }

    void Binding(const MenuStateBinding& binding, std::set<std::string>& keys)
    {
        Require(Key(binding.key) && keys.insert(binding.key).second && Key(binding.source) &&
                Engine::ValidString(binding.factor, 128));
        Require(Engine::OneOf(binding.operation, {"value", "equal", "notEqual", "emphasis"}));
        for (const auto value :
             {binding.compare, binding.scale, binding.offset, binding.minimum, binding.maximum, binding.factorDefault})
            Require(Number(value));
        Require(binding.minimum <= binding.maximum);
    }

}

namespace SceneRuntime
{
    void ValidateMenuTransition(const MenuConfiguration& configuration)
    {
        using namespace MenuValidation;
        const auto& transition = configuration.configTransition;
        Require(Engine::ValidString(transition.clock, 128) && Engine::ValidString(transition.enabledState, 128));
        Require(Engine::ValidString(configuration.transitionClock, 128) &&
                Engine::ValidString(configuration.transitionFallbackClock, 128) &&
                Number(configuration.transitionClockScale));
        Values(transition.idleValues);
        Values(transition.heldValues);
        Values(transition.transitionValues);
        Values(configuration.transitionDefaults);
        Require(Engine::ValidString(transition.cameraTarget, 128) &&
                Engine::ValidString(transition.returnCameraTarget, 128));
        Require(!configuration.settingsScreen ||
                (!configuration.returnScene.empty() && configuration.returnScene.starts_with("Assets/Scenes/") &&
                 configuration.returnScene.ends_with(".json") &&
                 configuration.returnScene.find("..") == std::string::npos &&
                 configuration.returnScene.find_first_of("\\:") == std::string::npos));
        Require(!transition.enabled || !transition.cameraTarget.empty());
        Require(Engine::InRange(transition.enterDuration, .1f, 10) &&
                Engine::InRange(transition.returnDuration, .1f, 10) &&
                Engine::InRange(transition.zoomDelay, 0, transition.enterDuration));
    }

    void ValidateMenu(const MenuConfiguration& configuration)
    {
        using namespace MenuValidation;
        Require(configuration.entries.size() <= 128 && configuration.settings.size() <= 128 &&
                configuration.bindings.size() <= 256);
        std::set<int> indices;
        for (const auto& entry : configuration.entries)
            Entry(entry, indices);
        std::set<std::string> keys;
        for (const auto& setting : configuration.settings)
            Setting(setting, keys);
        keys.clear();
        for (const auto& binding : configuration.bindings)
            Binding(binding, keys);
        for (const auto& input : configuration.inputs)
            Require(Engine::ValidString(input, 128));
        for (const auto& cue : configuration.cues)
            Require(Engine::ValidString(cue, 128));
        Require(Key(configuration.focusState));
        ValidateMenuTransition(configuration);
    }
}
