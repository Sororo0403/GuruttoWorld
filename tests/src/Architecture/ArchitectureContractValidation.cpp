#include <SceneRuntime/Scene/Prefab.h>
#include <SceneRuntime/Scene/SceneAssetReferences.h>
#include <SceneRuntime/Scripts/ScriptRuntime.h>
#include <SceneRuntime/Scripts/ScriptModuleApi.h>
#include <SceneRuntime/Project/ProjectSettings.h>
#include <Engine/Assets/AssetDatabase.h>
#include "../../../Editor/src/Editing/EditHistory.h"
#include "ComponentContractValidation.h"
#include "../Assets/AssetReferenceContractValidation.h"
#include "../Scripts/SchemaContractValidation.h"
#include "MigrationContractValidation.h"
#include "../Scene/PrefabDesignContractValidation.h"
#include "EditTransactionContractValidation.h"
#include "SessionDesignContractValidation.h"
#include "ExtensibilityContractValidation.h"
#include "ChangeCaptureValidation.h"
#include <iostream>
#include <set>
#include <stdexcept>

namespace
{
    using namespace SceneRuntime;
    bool baseline = false;
    std::set<std::string> required, observed;
    const std::set<std::string> knownGaps{};
    unsigned int failures = 0, gaps = 0;
    void Contract(const std::string& id, bool ok);

    bool CountModuleRegistration(void* context, const char*, const ScriptCallbacks*)
    {
        ++*static_cast<unsigned int*>(context);
        return true;
    }

    void NativeModule(const std::filesystem::path& path)
    {
        struct Module
        {
            HMODULE handle = nullptr;
            ~Module()
            {
                if (handle)
                    FreeLibrary(handle);
            }
        } module{LoadLibraryExW(std::filesystem::absolute(path).c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)};
        if (!module.handle)
            throw std::runtime_error("Cannot load native ABI fixture");
        const auto entry = reinterpret_cast<RegisterScriptModule>(GetProcAddress(module.handle, "Wp1RegisterScripts"));
        if (!entry)
            throw std::runtime_error("Native ABI fixture has no entry point");
        ScriptModuleAbi abi;
        unsigned int registrations = 0;
        const bool compatible = entry(&abi, &registrations, CountModuleRegistration) && registrations > 0;
        auto incompatible = abi;
        incompatible.engineBuild ^= 1;
        registrations = 0;
        const bool buildRejected = !entry(&incompatible, &registrations, CountModuleRegistration) && registrations == 0;
        incompatible = abi;
        ++incompatible.version;
        registrations = 0;
        const bool versionRejected =
            !entry(&incompatible, &registrations, CountModuleRegistration) && registrations == 0;
        Contract("project-native-module-identity", compatible && buildRejected && versionRejected);
    }

    void Require(bool ok, const char* message)
    {
        if (!ok)
            throw std::runtime_error(message);
    }

    void Contract(const std::string& id, bool ok)
    {
        observed.insert(id);
        if (ok)
            std::cout << "PASS: " << id << '\n';
        else if (baseline && knownGaps.contains(id) && !required.contains(id))
        {
            ++gaps;
            std::cout << "KNOWN GAP: " << id << '\n';
        }
        else
        {
            ++failures;
            std::cout << "FAIL: " << id << '\n';
        }
    }

    void Serialization()
    {
        ScenePlacement owner;
        owner.id = "owner";
        ScriptComponent script;
        script.behaviour = "ArchitectureSchema";
        owner.scripts.push_back(script);
        SceneLayout layout;
        layout.objects.push_back(owner);
        const auto before = layout.Serialize();
        ScriptDefinition schema;
        schema.update = [](ScriptContext&) {};
        schema.dataFields["next"] = {std::string("Assets/Scenes/Next.json")};
        schema.dataPurposes["next"] = ScriptDataPurpose::SceneAsset;
        Require(ScriptRegistry::Register("ArchitectureSchema", schema), "register schema");
        Contract("serialization-independent", before == layout.Serialize());
        Contract("serialization-roundtrip", layout.Serialize() == SceneLayout::Parse(layout.Serialize()).Serialize());
    }

    void Prefabs(const std::filesystem::path& root)
    {
        ScenePlacement item;
        item.id = "item";
        item.name = "Item";
        ScriptComponent script;
        script.behaviour = "UnloadedArchitectureBehaviour";
        script.data["items"] = {ScriptValue::Array{ScriptValue{1.0f}, ScriptValue{2.0f}}};
        script.data["target"] = {ScriptObjectReference{"external"}};
        item.scripts.push_back(script);
        SceneLayout asset;
        asset.objects.push_back(item);
        const std::filesystem::path path = "Assets/Prefabs/Contract.prefab";
        asset.Save(root / path);
        SceneLayout scene;
        const auto id = Prefab::Instantiate(scene, asset, path, {});
        ScenePlacement external;
        external.id = "external";
        external.name = "Original external object";
        scene.objects.push_back(external);
        std::get<ScriptValue::Array>(scene.objects[0].scripts[0].data.at("items").value)[0] = {10.0f};
        std::get<ScriptValue::Array>(asset.objects[0].scripts[0].data.at("items").value)[1] = {20.0f};
        asset.Save(root / path);
        Prefab::Refresh(scene, root);
        const auto& merged = std::get<ScriptValue::Array>(scene.objects[0].scripts[0].data.at("items").value);
        Contract("prefab-independent-elements",
                 std::get<float>(merged[0].value) == 10 && std::get<float>(merged[1].value) == 20);
        const auto refreshed = scene.Serialize();
        Prefab::Refresh(scene, root);
        Contract("prefab-refresh-idempotent", refreshed == scene.Serialize());
        const auto extracted = Prefab::Extract(scene, id);
        SceneLayout unrelated;
        external.name = "Different scene object";
        unrelated.objects.push_back(external);
        Prefab::Instantiate(unrelated, extracted, path, {});
        const auto& reference =
            std::get<ScriptObjectReference>(unrelated.objects.back().scripts[0].data.at("target").value);
        Contract("prefab-external-binding", reference.id != external.id);
    }

    void AssetReferences(const std::filesystem::path& root)
    {
        SceneLayout layout;
        ScenePlacement owner;
        owner.id = "menu";
        auto& menu = owner.canvas.emplace().menu.emplace();
        menu.returnScene = "Assets/Scenes/Return.json";
        menu.entries = {{0, "settings", "Assets/Scenes/Settings.json"}};
        layout.objects.push_back(owner);
        for (const auto* name : {"Return", "Settings"})
        {
            const auto path = root / "Assets/Scenes" / (std::string(name) + ".json");
            std::ofstream file(path);
            file << "{}";
            file.close();
            Engine::AssetDatabase::Ensure(path);
        }
        ResolveSceneAssetReferences(layout, Engine::AssetDatabase(root));
        Contract("menu-asset-references", layout.assetReferences.size() == 2);
    }

    void RuntimeCommands()
    {
        int externalEffects = 0;
        ScriptDefinition effect;
        effect.update = [&](ScriptContext& context)
        {
            ++externalEffects;
            context.scene->SetUiValue("uncommitted", 1);
            context.object.scale = {0, 1, 1};
        };
        Require(ScriptRegistry::Register("ArchitectureEffect", effect), "register effect");
        SceneLayout layout;
        ScenePlacement owner;
        owner.id = "effect";
        ScriptComponent script;
        script.behaviour = "ArchitectureEffect";
        owner.scripts.push_back(script);
        layout.objects.push_back(owner);
        const auto before = layout.Serialize();
        ScriptRuntime runtime;
        std::string error;
        Contract("runtime-command-rollback", !runtime.Update(layout, .016, error) && layout.Serialize() == before &&
                                                 runtime.UiCommands().values.empty());
        Require(externalEffects == 1, "callback external side effect characterization");
        std::cout << "OBSERVATION: arbitrary C++ side effects are outside host rollback\n";
    }

    void Projects(const std::filesystem::path& root)
    {
        const auto engine = Engine::ProjectContext::SourceEngineRoot();
        Contract("project-host-origin", std::filesystem::equivalent(engine, std::filesystem::current_path()));
        const ScriptModuleAbi abi;
        auto incompatible = abi;
        incompatible.engineBuild ^= 1;
        Contract("project-build-identity", abi.engineBuild != 0 && abi != incompatible);
        const std::string identity(32, 'a');
        const auto write = [](const std::filesystem::path& path, const Engine::Json& document)
        {
            std::ofstream output(path);
            output << document.dump(2);
            output.close();
            Require(static_cast<bool>(output), "write project fixture");
        };
        auto descriptor = Engine::Json{{"version", 1},
                                       {"name", "Minimal"},
                                       {"engineRoot", engine.generic_string()},
                                       {"project", "Minimal.vcxproj"},
                                       {"solution", "Minimal.slnx"}};
        write(root / "WP1.project.json", descriptor);
        write(root / "Assets/Project.json.meta", {{"id", identity}});
        const auto legacy = Engine::ProjectContext::Load(root);
        legacy.RequireEngine(engine);
        Contract("project-legacy-identity", Engine::ProjectContext::Identity(root) == identity);
        descriptor["projectId"] = identity;
        descriptor["engineContract"] = 1;
        write(root / "WP1.project.json", descriptor);
        write(root / "Assets/Project.json.meta", {{"id", std::string(32, 'b')}});
        const auto current = Engine::ProjectContext::Load(root / "WP1.project.json");
        Contract("project-explicit-identity",
                 current.projectId == identity && Engine::ProjectContext::Identity(root) == identity);
        bool rejected = false;
        try
        {
            current.RequireEngine(root);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Contract("project-wrong-host", rejected);
        descriptor["engineContract"] = 999;
        write(root / "WP1.project.json", descriptor);
        rejected = false;
        try
        {
            static_cast<void>(Engine::ProjectContext::Load(root));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Contract("project-incompatible-contract", rejected);
        descriptor["engineContract"] = 1;
        descriptor["projectId"] = "";
        write(root / "WP1.project.json", descriptor);
        rejected = false;
        try
        {
            static_cast<void>(Engine::ProjectContext::Identity(root));
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Contract("project-invalid-identity", rejected);
        std::filesystem::remove(root / "WP1.project.json");
        write(root / "WP1.runtime.json", {{"version", 1}, {"projectId", identity}});
        Contract("project-packaged-identity", Engine::ProjectContext::Identity(root) == identity);
    }
}

int main(int argc, char** argv)
{
    try
    {
        std::filesystem::path modulePath;
        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];
            if (argument == "--baseline")
                baseline = true;
            else if (argument.starts_with("--require="))
                required.insert(argument.substr(10));
            else if (argument.starts_with("--module="))
                modulePath = argument.substr(9);
            else
                throw std::runtime_error("Unknown validation argument");
        }
        const auto root = std::filesystem::absolute("generated/tests/architecture-contracts") /
                          std::to_string(GetCurrentProcessId()) / std::to_string(GetTickCount64());
        std::filesystem::create_directories(root / "Assets/Prefabs");
        std::filesystem::create_directories(root / "Assets/Scenes");
        ComponentContractValidation::Run(Contract);
        ChangeCaptureValidation::Run(root / "change-capture", Contract);
        AssetReferenceContractValidation::Run(root / "asset-contracts", Contract);
        SchemaContractValidation::Run(root / "schema-contracts", Contract);
        MigrationContractValidation::Run(root / "migration-contracts", Contract);
        PrefabDesignContractValidation::Run(root / "prefab-design", Contract);
        EditTransactionContractValidation::Run(root / "edit-transactions", Contract);
        SessionDesignContractValidation::Run(root / "session-design", Contract);
        ExtensibilityContractValidation::Run(root / "extensibility", Contract);
        Serialization();
        Prefabs(root);
        AssetReferences(root);
        RuntimeCommands();
        Projects(root);
        if (!modulePath.empty())
            NativeModule(modulePath);
        const auto fixture = std::filesystem::path("Tools/GameProject/Templates/MinimalGame");
        const auto project = ProjectSettings::Load(fixture);
        const auto minimal = SceneLayout::Load(fixture / project.startupScene);
        Contract("minimal-game", minimal.objects.empty() && project.title == "Minimal game");
        for (const auto& id : required)
            Require(observed.contains(id), "Unknown required contract ID");
        std::cout << "Contracts: " << observed.size() << ", known gaps: " << gaps << ", failures: " << failures << '\n';
        return failures ? 1 : 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
