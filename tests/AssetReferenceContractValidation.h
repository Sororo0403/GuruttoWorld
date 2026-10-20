#pragma once
#include <SceneRuntime/SceneAssetReferences.h>
#include <SceneRuntime/ProjectSettings.h>
#include <SceneRuntime/MaterialAsset.h>
#include <fstream>

namespace AssetReferenceContractValidation
{
    /// <summary>型側のアセット走査と実際の改名・保存経路の一致を検証します。</summary>
    template <class Report> void Run(const std::filesystem::path& root, Report&& report)
    {
        using namespace SceneRuntime;
        const auto make = [&](const std::filesystem::path& path)
        {
            std::filesystem::create_directories((root / path).parent_path());
            std::ofstream file(root / path);
            file << "{}";
            file.close();
            Engine::AssetDatabase::Ensure(root / path);
            return path;
        };
        const auto before = make("Assets/Scenes/Before.json");
        const std::filesystem::path after = "Assets/Scenes/After.json";
        const auto texture = make("Assets/Textures/Reference.png");
        const auto model = make("Assets/Models/Reference.glb");
        const auto material = make("Assets/Materials/Reference.mat");
        const auto audio = make("Assets/Audio/Reference.wav");
        const auto shader = make("Shaders/Reference.hlsl");
        ScenePlacement owner;
        owner.id = "assets";
        owner.meshRenderer.emplace().model = model;
        owner.meshRenderer->lods.push_back({20, model});
        owner.material.emplace().asset = material;
        owner.material->slots.push_back(material);
        owner.boxCollider.emplace().model = model;
        owner.image.emplace().texture = texture;
        owner.terrain.emplace().texture = texture;
        owner.tilemap.emplace().texture = texture;
        owner.audioSource.emplace().clip = audio;
        owner.button.emplace().action = "loadScene";
        owner.button->target = before.generic_string();
        owner.button->sound = before.generic_string();
        auto& menu = owner.canvas.emplace().menu.emplace();
        menu.returnScene = before.generic_string();
        menu.entries = {{0, "settings", before.generic_string()},
                        {1, "loadScene", before.generic_string()},
                        {2, "quit", before.generic_string()}};
        menu.configTransition.cameraTarget = before.generic_string();
        ScriptComponent script;
        script.behaviour = "UnloadedAssetReferenceContract";
        script.sceneAssets.insert("scene");
        script.data["scene"] = {before.generic_string()};
        script.data["text"] = {before.generic_string()};
        script.data["object"] = {ScriptObjectReference{before.generic_string()}};
        owner.scripts.push_back(script);
        size_t visited = 0;
        owner.VisitAssetReferences(
            [&](auto&)
            {
                ++visited;
            });
        report("asset-component-coverage", visited == 14);
        SceneLayout layout;
        layout.objects.push_back(owner);
        layout.settings.skyShader = shader;
        layout.settings.oceanShader = shader;
        layout.settings.particleShader = shader;
        layout.settings.postShader = shader;
        ResolveSceneAssetReferences(layout, Engine::AssetDatabase(root));
        report("asset-capture-coverage", layout.assetReferences.size() == 6);
        const Engine::AssetDatabase snapshot(root, true);
        Engine::AssetDatabase(root).Move(before, after);
        ResolveSceneAssetReferences(layout, snapshot);
        report("asset-snapshot-stability", layout.objects.front().button->target == before.generic_string());
        ResolveSceneAssetReferences(layout, Engine::AssetDatabase(root));
        auto& updated = layout.objects.front();
        bool paths = true;
        updated.VisitAssetReferences(
            [&](auto& path)
            {
                using T = std::decay_t<decltype(path)>;
                if constexpr (std::is_same_v<T, std::string>)
                    paths = paths && path == after.generic_string();
            });
        report("asset-rename-kinds",
               paths && updated.button->sound == before.generic_string() &&
                   updated.canvas->menu->configTransition.cameraTarget == before.generic_string() &&
                   updated.canvas->menu->entries.back().target == before.generic_string() &&
                   std::get<std::string>(updated.scripts.front().data.at("text").value) == before.generic_string() &&
                   std::get<ScriptObjectReference>(updated.scripts.front().data.at("object").value).id ==
                       before.generic_string() &&
                   layout.assetReferences.contains(after.generic_string()) &&
                   !layout.assetReferences.contains(before.generic_string()));

        SceneLayout prefabBaseline;
        ScenePlacement bound;
        bound.id = "bound";
        bound.scripts.push_back(script);
        prefabBaseline.objects.push_back(bound);
        ResolveSceneAssetReferences(prefabBaseline, snapshot);
        SceneLayout instance;
        ScenePlacement linked;
        linked.id = "linked";
        linked.prefab = PrefabLink{before, "bound", "linked", prefabBaseline.Serialize()};
        instance.objects.push_back(linked);
        ResolveSceneAssetReferences(instance, Engine::AssetDatabase(root));
        const auto savedBaseline = SceneLayout::Parse(instance.objects.front().prefab->baseline);
        report("asset-prefab-baseline",
               instance.objects.front().prefab->asset == after &&
                   std::get<std::string>(savedBaseline.objects.front().scripts.front().data.at("scene").value) ==
                       after.generic_string());

        ProjectSettings project;
        project.startupScene = after.generic_string();
        project.sceneAliases["next"] = after.generic_string();
        project.Save(root);
        const std::filesystem::path latest = "Assets/Scenes/Latest.json";
        Engine::AssetDatabase(root).Move(after, latest);
        const auto loadedProject = ProjectSettings::Load(root);
        report("asset-project-rename", loadedProject.startupScene == latest.generic_string() &&
                                           loadedProject.sceneAliases.at("next") == latest.generic_string());
        MaterialAsset surface;
        surface.texture = texture;
        surface.normalTexture = texture;
        surface.environmentTexture = texture;
        surface.lightmap = texture;
        surface.shader = shader;
        surface.Save(root, "Assets/Materials/Saved.mat");
        const std::filesystem::path renamedTexture = "Assets/Textures/Renamed.png";
        const std::filesystem::path renamedShader = "Shaders/Renamed.hlsl";
        Engine::AssetDatabase(root).Move(texture, renamedTexture);
        Engine::AssetDatabase(root).Move(shader, renamedShader);
        const auto loadedSurface = MaterialAsset::Load(root, "Assets/Materials/Saved.mat");
        report("asset-material-rename",
               loadedSurface.texture == renamedTexture && loadedSurface.normalTexture == renamedTexture &&
                   loadedSurface.environmentTexture == renamedTexture && loadedSurface.lightmap == renamedTexture &&
                   loadedSurface.shader == renamedShader);
        SceneLayout modelScene;
        ScenePlacement actor;
        actor.id = "model";
        actor.SetModel(model);
        modelScene.objects.push_back(actor);
        const auto modelScenePath = root / "Assets/Scenes/Model.json";
        modelScene.Save(modelScenePath);
        const std::filesystem::path renamedModel = "Assets/Models/Moved/Reference.glb";
        Engine::AssetDatabase(root).Move(model, renamedModel);
        const auto reloadedModel = SceneLayout::Load(modelScenePath, root);
        report(
            "asset-model-rename",
            reloadedModel.objects.front().Model() == renamedModel &&
                Engine::Json::parse(reloadedModel.Serialize()).at("objects").at(0).at("components").at(0).at("model") ==
                    renamedModel.generic_string());
        SceneLayout persisted;
        bound.scripts.front().data["scene"] = {latest.generic_string()};
        persisted.objects.push_back(bound);
        const auto scenePath = root / "Assets/Scenes/Saved.json";
        persisted.Save(scenePath);
        const auto loaded = SceneLayout::Load(scenePath, root);
        report("asset-scene-save-load", loaded.objects.front().scripts.front().sceneAssets == script.sceneAssets &&
                                            loaded.assetReferences.contains(latest.generic_string()));
    }
}
