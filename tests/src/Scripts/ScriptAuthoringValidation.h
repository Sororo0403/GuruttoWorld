#pragma once
#include "../../../Editor/src/Project/ScriptAuthoringPanel.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace ScriptAuthoringValidation
{
    inline std::string Read(const std::filesystem::path& path)
    {
        return Engine::FileContents::Read(path).value_or(std::string{});
    }
    inline void Require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    struct Fixture final
    {
        std::filesystem::path root = std::filesystem::absolute("generated/tests/script-authoring/" +
                                                               std::to_string(GetCurrentProcessId()) + "-" +
                                                               std::to_string(GetTickCount64()) + "/Content"),
                              source = root / "Assets/Scripts/ValidationAuthoringWatcher.cpp";
        std::filesystem::path manifest =
            root / "Assets/Scripts/Bin" / SceneRuntime::ScriptModule::Configuration() / "module.json";
        std::string original = Read(manifest);
        bool existed = std::filesystem::exists(manifest), created = false;
        ~Fixture()
        {
            try
            {
                if (created)
                    std::filesystem::remove(source);
                if (existed)
                {
                    std::ofstream output(manifest, std::ios::binary);
                    output << original;
                }
                else
                    std::filesystem::remove(manifest);
                std::string ignored;
                SceneRuntime::ScriptModule::Reload(root, ignored);
            }
            catch (...)
            {
            }
        }
    };
    inline void Until(const std::function<bool()>& condition)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (condition())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        throw std::runtime_error(
            "Automatic script compilation/reload timed out; inspect generated/script-builds diagnostics");
    }
    inline void Run()
    {
        Fixture fixture;
        Editor::ScriptAuthoringPanel panel;
        std::string error;
        std::filesystem::path source;
        Require(SceneRuntime::ScriptModule::CreateSource(fixture.root, "ValidationAuthoringWatcher", source, error),
                "watcher creates a new source without overwriting");
        fixture.created = true;
        panel.Poll(fixture.root, false);
        Require(!panel.Ready(), "uncompiled source blocks Play");
        Until(
            [&]
            {
                panel.Poll(fixture.root, false);
                return Read(fixture.manifest) != fixture.original;
            });
        Require(!panel.Ready() && !SceneRuntime::ScriptRegistry::Definitions().contains("ValidationAuthoringWatcher"),
                "successful compilation defers module installation during Play");
        Until(
            [&]
            {
                panel.Poll(fixture.root, true);
                return panel.Ready();
            });
        Require(SceneRuntime::ScriptRegistry::Definitions().contains("ValidationAuthoringWatcher"),
                "Stop installs the watched script into the real registry");
        Require(panel.TakeReloaded() && !panel.TakeReloaded(),
                "successful module installation notifies the catalog exactly once");
        const auto compiled = Read(fixture.manifest);
        const auto schemaPath = source.parent_path() / "ValidationAuthoringWatcher.schema.json";
        auto schema = Engine::Json::parse(Read(schemaPath));
        schema["scripts"]["ValidationAuthoringWatcher"]["parameters"]["speed"]["initial"] = 5;
        Engine::FileContents::Write(schemaPath, schema.dump());
        Until(
            [&]
            {
                panel.Poll(fixture.root, false);
                return !panel.Ready();
            });
        Require(
            SceneRuntime::ScriptRegistry::Definitions().at("ValidationAuthoringWatcher").fields.at("speed").initial ==
                    2 &&
                Read(fixture.manifest) == compiled,
            "JSON changes wait for Stop and retain the running module");
        Require(!panel.TakeReloaded(), "pending JSON changes do not notify the catalog during Play");
        Until(
            [&]
            {
                panel.Poll(fixture.root, true);
                return panel.Ready();
            });
        Require(
            SceneRuntime::ScriptRegistry::Definitions().at("ValidationAuthoringWatcher").fields.at("speed").initial ==
                    5 &&
                Read(fixture.manifest) == compiled,
            "Stop reloads JSON defaults without recompiling the DLL");
        Require(panel.TakeReloaded() && !panel.TakeReloaded(), "JSON reload notifies the catalog exactly once");
        Engine::FileContents::Write(schemaPath, "{");
        Until(
            [&]
            {
                panel.Poll(fixture.root, true);
                return panel.Failed();
            });
        Require(!panel.Ready() &&
                    SceneRuntime::ScriptRegistry::Definitions()
                            .at("ValidationAuthoringWatcher")
                            .fields.at("speed")
                            .initial == 5 &&
                    Read(fixture.manifest) == compiled,
                "invalid JSON blocks Play and preserves the installed module");
        Require(!panel.TakeReloaded(), "rejected JSON does not notify the catalog");
        schema["scripts"]["ValidationAuthoringWatcher"]["parameters"]["speed"]["initial"] = 7;
        Engine::FileContents::Write(schemaPath, schema.dump());
        Until(
            [&]
            {
                panel.Poll(fixture.root, true);
                return panel.Ready();
            });
        Require(!panel.Failed() && Read(fixture.manifest) == compiled &&
                    SceneRuntime::ScriptRegistry::Definitions()
                            .at("ValidationAuthoringWatcher")
                            .fields.at("speed")
                            .initial == 7,
                "correcting JSON recovers without recompiling the DLL");
        Require(panel.TakeReloaded() && !panel.TakeReloaded(), "corrected JSON notifies the catalog exactly once");
        const auto sourceTime = std::filesystem::last_write_time(source);
        auto revisedSource = Read(source);
        const auto separator = revisedSource.find(' ');
        Require(separator != std::string::npos, "watcher fixture contains editable whitespace");
        revisedSource[separator] = '\t';
        {
            std::ofstream output(source, std::ios::binary | std::ios::trunc);
            output << revisedSource;
        }
        std::filesystem::last_write_time(source, sourceTime);
        Until(
            [&]
            {
                panel.Poll(fixture.root, false);
                const auto manifest = Read(fixture.manifest);
                return !manifest.empty() && manifest != compiled;
            });
        Require(!panel.Ready() && SceneRuntime::ScriptRegistry::Definitions().contains("ValidationAuthoringWatcher"),
                "editing recompiles while preserving the active definition until Stop");
        const auto validSource = Read(source), published = Read(fixture.manifest);
        {
            std::ofstream output(source, std::ios::app);
            output << "\nINVALID_LATEST_SCRIPT_REVISION\n";
        }
        Until(
            [&]
            {
                panel.Poll(fixture.root, false);
                return panel.Failed();
            });
        const auto retained = Read(fixture.manifest);
        if (retained != published)
        {
            Engine::FileContents::Write(fixture.root / "expected-manifest.json", published);
            Engine::FileContents::Write(fixture.root / "retained-manifest.json", retained);
        }
        Require(retained == published, "failed revision retains published module");
        for (int i = 0; i < 10; ++i)
            panel.Poll(fixture.root, true);
        Require(!panel.Ready(), "Stop after a failed latest revision must not install the earlier pending module");
        schema["scripts"]["ValidationAuthoringWatcher"]["parameters"]["speed"]["initial"] = 8;
        Engine::FileContents::Write(schemaPath, schema.dump());
        for (int i = 0; i < 30; ++i)
        {
            panel.Poll(fixture.root, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        Require(panel.Failed() && !panel.Ready() && Read(fixture.manifest) == published &&
                    SceneRuntime::ScriptRegistry::Definitions()
                            .at("ValidationAuthoringWatcher")
                            .fields.at("speed")
                            .initial == 7,
                "JSON changes cannot bypass a failed latest C++ revision");
        {
            std::ofstream output(source, std::ios::binary | std::ios::trunc);
            output << validSource;
        }
        Until(
            [&]
            {
                panel.Poll(fixture.root, true);
                return panel.Ready();
            });
    }
}
