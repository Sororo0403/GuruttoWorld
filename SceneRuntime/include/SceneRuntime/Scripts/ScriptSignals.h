#pragma once
#include <SceneRuntime/Animation/AnimationEvents.h>
#include <map>
#include <optional>
#include <string>

namespace SceneRuntime
{
    enum class ScriptPhase
    {
        Update,
        FixedUpdate,
        LateUpdate
    };
    struct SceneCommand
    {
        std::string scene;
        bool additive = false, unload = false;
        /// <summary>確定後に実行するシーン要求を比較します。</summary>
        bool operator==(const SceneCommand&) const = default;
    };
    struct ScriptUiCommands
    {
        std::map<std::string, float> values;
        std::map<std::string, std::string> texts;
        /// <summary>予約したUI値を比較します。</summary>
        bool operator==(const ScriptUiCommands&) const = default;
    };
    struct ScriptEvent
    {
        std::string name, sender, target;
        float value = 0;
        std::optional<AnimatorEventOccurrence> animation;
        std::string text;
        /// <summary>イベントの引数とAnimation情報を含めて比較します。</summary>
        bool operator==(const ScriptEvent&) const = default;
    };
}
