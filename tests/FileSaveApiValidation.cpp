#include <SceneRuntime/SceneLayout.h>
#include <SceneRuntime/MaterialAsset.h>
#include <iostream>
#include "../Editor/src/GameProject.h"
#include "../Editor/src/ProjectCatalog.h"

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("API test arguments");
        const std::wstring mode = argv[1];
        const auto root = std::filesystem::absolute(argv[2]);
        if (mode == L"--open-project")
        {
            Editor::GameProject::Open(root);
            return 0;
        }
        if (mode == L"--scan-project")
        {
            Editor::ProjectCatalog catalog;
            if (!catalog.Scan(root))
                throw std::runtime_error("project scan failed");
            for (const auto& asset : catalog.Assets())
                if (Engine::FileSaveLock::IsJournalPath(asset.path))
                    throw std::runtime_error("journal appeared in the asset catalog");
            return 0;
        }
        SceneRuntime::SceneLayout scene;
        SceneRuntime::MaterialAsset material;
        if (mode != L"--seed-api")
        {
            SceneRuntime::ScenePlacement object;
            object.id = "saved-object";
            object.name = "Saved by public API";
            scene.objects.push_back(std::move(object));
            material.values.roughness = .23f;
        }
        const auto path = root / "Assets/Scenes/scene.json";
        if (mode == L"--no-overwrite-api")
        {
            try
            {
                scene.Save(path, false);
            }
            catch (const std::exception&)
            {
                return Engine::FileContents::Read(path) ? 0 : 1;
            }
            throw std::runtime_error("existing scene was overwritten");
        }
        scene.Save(path);
        material.Save(root, "Assets/Materials/value.mat");
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
