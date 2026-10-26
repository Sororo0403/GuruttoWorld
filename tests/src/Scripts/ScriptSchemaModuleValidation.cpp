#include <SceneRuntime/Scripts/ScriptModule.h>
#include <LegacyScripts/ScriptRuntime.h>
#include <Engine/Core/FileContents.h>
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include "ScriptAuthoringValidation.h"
#endif
#include <cmath>
#include <iostream>
#include <type_traits>

namespace
{
    using namespace SceneRuntime;

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    float Run(const std::shared_ptr<const ScriptDefinitions>& definitions, bool savedValue = false)
    {
        ScenePlacement object;
        object.id = "mover";
        ScriptComponent script;
        script.id = "movement";
        script.behaviour = "Derived";
        if (savedValue)
            script.parameters["speed"] = 8;
        object.scripts.push_back(script);
        SceneLayout scene;
        scene.objects.push_back(object);
        ScriptRuntime runtime(definitions);
        std::string error;
        for (int tick = 0; tick < 5; ++tick)
        {
            const bool updated = runtime.Update(scene, .1, error);
            Require(updated, "Callback execution failed: " + error);
        }
        runtime.Stop(scene);
        return scene.objects[0].position[0];
    }

    struct RestoreFile
    {
        std::filesystem::path path;
        std::string text;
        ~RestoreFile()
        {
            try
            {
                Engine::FileContents::Write(path, text);
            }
            catch (...)
            {
            }
        }
    };
}

int wmain(int count, wchar_t** arguments)
{
    try
    {
        static_assert(!std::is_base_of_v<ScriptSchema, ScriptCallbacks>);
        if (count != 2)
            throw std::runtime_error(
                "Usage: ScriptSchemaModuleValidation <project> (or --watcher in Debug/Development)");
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        if (std::wstring_view(arguments[1]) == L"--watcher")
        {
            ScriptAuthoringValidation::Run();
            std::cout << "PASS: script source and JSON change watching\n";
            return 0;
        }
#endif
        const auto root = std::filesystem::absolute(arguments[1]);
        std::string error;
        const bool loaded = ScriptModule::Reload(root, error);
        Require(loaded, "Direct JSON loading failed: " + error);
        const auto declared = ScriptSchema::LoadProject(root);
        for (const auto* name : {"Derived", "LongSchema", "Script"})
            Require(static_cast<const ScriptSchema&>(ScriptRegistry::Definitions().at(name)) == declared.at(name),
                    std::string("Host metadata differs from JSON: ") + name);
        const auto& longFields = ScriptRegistry::Definitions().at("LongSchema").dataFields;
        Require(longFields.size() == 32 && std::get<std::string>(longFields.at("field0").value).size() == 3072,
                "Large UTF-8 defaults were not loaded intact");
        const auto original = ScriptRegistry::Capture();
        Require(std::abs(Run(original) - 2) < 1e-5, "Inherited JSON defaults did not reach the callback");

        const auto schemaPath = root / "Assets/Scripts/Derived.schema.json";
        const RestoreFile restore{schemaPath, *Engine::FileContents::Read(schemaPath)};
        auto changed = Engine::Json::parse(restore.text);
        changed["scripts"]["Derived"]["parameters"]["speed"]["initial"] = 6;
        Engine::FileContents::Write(schemaPath, changed.dump());
        const bool reloaded = ScriptModule::Reload(root, error);
        Require(reloaded, "JSON-only reload failed: " + error);
        const auto updated = ScriptRegistry::Capture();
        Require(std::abs(Run(updated) - 3) < 1e-5 && std::abs(Run(original) - 2) < 1e-5,
                "JSON reload did not update defaults or changed an existing session snapshot");
        Require(std::abs(Run(updated, true) - 4) < 1e-5, "JSON reload overwrote saved scene parameters");

        const auto revision = ScriptRegistry::Revision();
        const auto rejects = [&](const std::string& source)
        {
            Engine::FileContents::Write(schemaPath, source);
            Require(!ScriptModule::Reload(root, error) && !error.empty() && ScriptRegistry::Revision() == revision &&
                        ScriptRegistry::Definitions().at("Derived").fields.at("speed").initial == 6,
                    "Invalid declaration changed the active module");
        };
        rejects("{");
        auto invalid = changed;
        invalid["scripts"]["Derived"]["parameters"]["speed"]["maximum"] = 1;
        rejects(invalid.dump());
        invalid = changed;
        invalid["scripts"]["Derived"]["extends"] = "Absent";
        rejects(invalid.dump());
        invalid = changed;
        invalid["scripts"]["Derived"]["extends"] = "Derived";
        rejects(invalid.dump());
        invalid = changed;
        invalid["scripts"]["Base"] = Engine::Json::object();
        rejects(invalid.dump());
        rejects(Engine::Json{{"version", 1}, {"scripts", Engine::Json::object()}}.dump());
        Require(std::abs(Run(ScriptRegistry::Capture()) - 3) < 1e-5,
                "Preserved callbacks could not execute after a rejected reload");
        std::cout << "PASS: direct host JSON loading, cross-file inheritance, UTF-8 defaults, JSON-only reload, "
                     "session snapshots, saved values and atomic rejection\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
