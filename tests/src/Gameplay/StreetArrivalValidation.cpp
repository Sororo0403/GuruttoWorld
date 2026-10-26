#include "../Support/TestGameContent.h"
#include <SceneRuntime/Scripts/ScriptModuleApi.h>
#include <SceneRuntime/Physics/PhysicsWorld.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

bool Register_StreetArrival(void*, SceneRuntime::RegisterModuleScript);
bool Register_SunriseStreet(void*, SceneRuntime::RegisterModuleScript);
bool Register_StreetJourney(void*, SceneRuntime::RegisterModuleScript);

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void ValidateArrival()
    {
        using namespace SceneRuntime;
        ScriptDefinition definition;
        Require(Register_StreetArrival(&definition,
                                       [](void* target, const char* name, const ScriptCallbacks* source)
                                       {
                                           auto& definition = *static_cast<ScriptDefinition*>(target);
                                           static_cast<ScriptCallbacks&>(definition) = *source;
                                           static_cast<ScriptSchema&>(definition) =
                                               ScriptSchema::LoadProject(WP1_TEST_GAME_ROOT).at(name);
                                           return true;
                                       }),
                "arrival registers");
        SceneLayout layout;
        for (const auto* id : {"arrival", "near", "far"})
        {
            ScenePlacement object;
            object.id = id;
            if (object.id != "arrival")
            {
                object.audioSource.emplace();
                object.audioSource->clip = "Assets/Audio/Scan/NearReply.wav";
            }
            layout.objects.push_back(object);
        }
        const auto before = layout.Serialize();
        size_t next = 0;
        ScriptScene scene(layout, next);
        std::map<std::string, float> parameters, state;
        for (const auto& [key, field] : definition.fields)
            parameters[key] = field.initial;
        auto data = definition.dataFields;
        data["nearSound"] = {ScriptObjectReference{"near"}};
        data["farSound"] = {ScriptObjectReference{"far"}};
        ScriptContext context(layout.objects[0], parameters, state, 0, nullptr, nullptr, &scene);
        context.data = &data;
        definition.start(context);
        const auto update = [&](double seconds)
        {
            scene.audioCommands.clear();
            context.seconds = seconds;
            definition.update(context);
            return scene.uiCommands.values.at("arrivalMusicGain");
        };
        Require(std::abs(update(0.92) - 0.06f) < 0.001f, "music leaves space for release");
        Require(scene.audioCommands.empty(), "street waits for scan arrival");
        update(0.08);
        Require(scene.audioCommands.size() == 1 && scene.audioCommands[0].target == "near",
                "near response follows scan arrival");
        update(0.04);
        Require(scene.audioCommands.size() == 1 && scene.audioCommands[0].target == "far",
                "far response follows near response");
        Require(std::abs(update(0.80) - 1.0f) < 0.001f && scene.audioCommands.empty(),
                "music returns to original gain without replaying sounds");
        Require(layout.Serialize() == before && scene.sceneCommands.empty() && scene.impulses.empty(),
                "arrival never changes gameplay objects or physics");
        update(0.22);
        Require(scene.audioCommands.empty(), "completed arrival does not loop");
        Require(scene.sceneCommands.empty(), "completed arrival keeps gameplay running");
        scene.uiCommands.values["arrivalMusicGain"] = 0.37f;
        update(1.0);
        Require(scene.sceneCommands.empty(), "completed arrival never requests a scene transition");
        Require(std::abs(scene.uiCommands.values.at("arrivalMusicGain") - 0.37f) < 0.001f,
                "completed arrival leaves subsequent music changes alone");
        scene.sceneCommands.clear();
        definition.start(context);
        update(1.10);
        Require(scene.audioCommands.size() == 2, "restart resets cues and a long frame skips neither response");
        Require(scene.sceneCommands.empty(), "restart waits for the full presentation");
        update(1.0);
        Require(scene.sceneCommands.empty(), "a long frame completes arrival without leaving gameplay");
    }

    void ValidateGameplayContinues()
    {
        using namespace SceneRuntime;
        const auto add = [](void*, const char* name, const ScriptCallbacks* callbacks)
        {
            ScriptDefinition definition;
            static_cast<ScriptCallbacks&>(definition) = *callbacks;
            static_cast<ScriptSchema&>(definition) = ScriptSchema::LoadProject(WP1_TEST_GAME_ROOT).at(name);
            return ScriptRegistry::Register(name, std::move(definition));
        };
        Require(Register_SunriseStreet(nullptr, add) && Register_StreetJourney(nullptr, add) &&
                    Register_StreetArrival(nullptr, add),
                "gameplay scripts register");
        auto layout = SceneLayout::Load(WP1_TEST_GAME_ROOT "/Assets/Scenes/StreetJourney.json");
        for (auto& object : layout.objects)
            for (auto& script : object.scripts)
                if (script.behaviour == "SunriseSound")
                    script.parameters["volume"] = 0;
        const auto playerPosition = [&]()
        {
            for (const auto& object : layout.objects)
                if (object.id == "park-player")
                    return object.position;
            throw std::runtime_error("gameplay player disappeared");
        };
        PhysicsWorld physics;
        ScenePhysics::States states;
        ScriptRuntime scripts;
        std::string error;
        float musicGain = 0;
        size_t nearReplies = 0, farReplies = 0;
        constexpr double Step = 1.0 / 60;
        const auto tick = [&](float forward, bool cancel)
        {
            const std::map<std::string, float> input{{"MoveForward", forward}};
            const std::map<std::string, bool> pressed{{"Cancel", cancel}};
            Require(scripts.Update(layout, Step, error, input, pressed, &physics, ScriptPhase::FixedUpdate),
                    error.c_str());
            Require(physics.Advance(layout, states, Step, 0, forward, false, WP1_TEST_GAME_ROOT, error), error.c_str());
            Require(scripts.Update(layout, Step, error, input, pressed, &physics), error.c_str());
            Require(scripts.Update(layout, Step, error, input, pressed, &physics, ScriptPhase::LateUpdate),
                    error.c_str());
            for (const auto& [key, value] : scripts.TakeUiCommands().values)
                if (key == "arrivalMusicGain")
                    musicGain = value;
            for (const auto& command : scripts.TakeAudioCommands())
            {
                if (command.target == "arrival-near-audio")
                    ++nearReplies;
                if (command.target == "arrival-far-audio")
                    ++farReplies;
            }
        };
        // 実シーンを旧帰還時刻より長く進め、演出完了後の入力と物理による移動を確認します。
        for (int frame = 0; frame < 180; ++frame)
        {
            tick(0, false);
            Require(scripts.TakeSceneCommands().empty(), "arrival must not leave the gameplay scene");
        }
        const auto before = playerPosition();
        for (int frame = 0; frame < 180; ++frame)
        {
            tick(frame < 60 ? 1.0f : 0.0f, false);
            Require(scripts.TakeSceneCommands().empty(), "gameplay continues after arrival and movement");
        }
        Require(playerPosition()[2] > before[2] + 1.0f, "player can move after arrival completes");
        Require(std::abs(musicGain - 1.0f) < 0.001f, "gameplay music is restored");
        Require(nearReplies == 1 && farReplies == 1, "arrival replies play exactly once during gameplay");
        tick(0, true);
        const auto commands = scripts.TakeSceneCommands();
        Require(commands.size() == 1 && commands[0].scene == "Assets/Scenes/TitleStreet.json",
                "explicit cancel still returns to title");
        scripts.Stop(layout);
    }

}

int main()
{
    try
    {
        ValidateArrival();
        ValidateGameplayContinues();
        std::cout
            << "PASS arrival timing, music restore, exactly-once responses, restart, continued gameplay movement and explicit cancel\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
