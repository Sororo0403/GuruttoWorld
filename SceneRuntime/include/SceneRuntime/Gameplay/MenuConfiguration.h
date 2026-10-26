#pragma once
#include <string>
#include <Engine/Core/Validation.h>
#include <vector>
#include <array>
#include <stdexcept>
#include <set>
#include <cmath>
#include <map>
#include <algorithm>

namespace SceneRuntime
{
    struct MenuEntry
    {
        int index = 0;
        std::string action = "loadScene", target;
        float focus = 0, view = 0;
        std::string focusClock;
        std::string transitionCameraTarget;
        float transitionApproach = 1;
        std::map<std::string, float> transitionValues;
        bool operator==(const MenuEntry&) const = default;
    };
    struct MenuSetting
    {
        std::string key = "volume", action = "number";
        float minimum = 0, maximum = 10, step = 1, initial = 10;
        bool operator==(const MenuSetting&) const = default;
    };
    struct MenuStateBinding
    {
        std::string key, source = "selected", operation = "value", factor;
        float compare = 0, scale = 1, offset = 0, minimum = -100000, maximum = 100000;
        float factorDefault = 1;
        bool operator==(const MenuStateBinding&) const = default;
    };
    struct MenuConfigTransition
    {
        bool enabled = false;
        std::string cameraTarget, returnCameraTarget;
        float enterDuration = 2, returnDuration = .55f, zoomDelay = .45f;
        std::string clock, enabledState;
        std::map<std::string, float> idleValues, heldValues, transitionValues;
        bool operator==(const MenuConfigTransition&) const = default;
    };
    struct MenuConfiguration
    {
        std::vector<MenuEntry> entries{{0, "loadScene", "", 0, 0}, {1, "settings", "", 1, 1}, {2, "quit", "", 2, 1}};
        std::vector<MenuSetting> settings{
            {"volume", "number", 0, 10, 1, 10}, {"motion", "toggle", 0, 1, 1, 1}, {"", "save", 0, 1, 1, 0}};
        std::array<std::string, 6> inputs{"MoveForward", "MoveBack", "MoveLeft", "MoveRight", "Confirm", "Cancel"};
        std::array<std::string, 4> cues{"Select", "Confirm", "Back", "Error"};
        std::vector<MenuStateBinding> bindings;
        bool pressAnyButton = false;
        std::string focusState = "cameraFocus";
        MenuConfigTransition configTransition;
        bool settingsScreen = false;
        std::string returnScene;
        std::string transitionClock, transitionFallbackClock;
        float transitionClockScale = 1;
        std::map<std::string, float> transitionDefaults;
        bool operator==(const MenuConfiguration&) const = default;
        /// <summary>メニュー遷移で使用する個体参照を走査します。</summary>
        template <class Visitor> void VisitObjectReferences(Visitor&& visitor)
        {
            visitor(configTransition.cameraTarget);
            visitor(configTransition.returnCameraTarget);
            for (auto& entry : entries)
                visitor(entry.transitionCameraTarget);
        }
        /// <summary>複製やPrefab配置に合わせて、メニューの内部カメラ参照を変換します。</summary>
        template <class IdMap> void RemapCameras(const IdMap& ids)
        {
            VisitObjectReferences(
                [&](std::string& id)
                {
                    if (const auto found = ids.find(id); found != ids.end())
                        id = found->second;
                });
        }
    };
    /// <summary>既定のメニュー設定を作成します。</summary>
    MenuConfiguration DefaultMenuConfiguration();
    /// <summary>状態値へメニューの変換規則を適用します。</summary>
    float EvaluateMenuBinding(const MenuStateBinding& binding, float value,
                              const std::map<std::string, float>& defaults);
    namespace MenuValidation
    {
        inline void Require(bool ok)
        {
            if (!ok)
                throw std::runtime_error("Invalid menu configuration");
        }
        inline bool Key(const std::string& value)
        {
            return Engine::ValidString(value, 128, true) && value.find_first_of("=&") == std::string::npos;
        }
        inline bool Number(float value)
        {
            return Engine::InRange(value, -100000, 100000);
        }
        /// <summary>シーンで指定された演出用の状態名と値を検証します。</summary>
        void Values(const std::map<std::string, float>& values);
        /// <summary>メニュー項目の設定を検証します。</summary>
        void Entry(const MenuEntry& entry, std::set<int>& indices);
        /// <summary>メニューの設定項目を検証します。</summary>
        void Setting(const MenuSetting& setting, std::set<std::string>& keys);
        /// <summary>状態割り当ての値と出力キーを検証します。</summary>
        void Binding(const MenuStateBinding& binding, std::set<std::string>& keys);
    }
    /// <summary>設定画面への遷移条件を検証します。</summary>
    void ValidateMenuTransition(const MenuConfiguration& configuration);

    /// <summary>メニュー全体の設定と参照を検証します。</summary>
    void ValidateMenu(const MenuConfiguration& configuration);
}
