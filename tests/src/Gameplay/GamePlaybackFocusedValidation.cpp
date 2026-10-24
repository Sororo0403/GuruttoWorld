#include "GamePlaybackParityValidation.h"
#include "TitleConfigTransitionValidation.h"
#include "TitleDepartureValidation.h"
#include "TitleIsolationValidation.h"
#include <Engine/Platform/Window.h>
#include <Engine/Core/Log.h>
#include <iostream>
#include <SceneRuntime/Scripts/ScriptModule.h>
#include "../../../Editor/src/Documents/MaterialDocument.h"
int main(int argc, char** argv)
{
    try
    {
        TitleIsolationValidation::Run();
        Engine::Log::Initialize("generated/tests/playback-focused.log");
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        if (!window.Create(L"Playback parity validation", 1280, 720) || !renderer.Initialize(window.GetHandle()))
            throw std::runtime_error("renderer initialization failed");
        const auto root = std::filesystem::absolute("Games/GuruttoWorld");
        if (argc == 2 && std::string_view(argv[1]) == "--departure-only")
        {
            // 一致検証では、共通ホストの更新と出発処理の検証より前に、元のスクリプトモジュールを読み込みます。
            GamePlaybackParityValidation::Run(renderer, root, true);
            TitleDepartureValidation::Run(renderer, root);
            if (!renderer.WaitForIdle())
                throw std::runtime_error("departure GPU completion failed");
            for (const auto& entry : Engine::Log::Recent())
                if (entry.level == Engine::LogLevel::Error)
                    throw std::runtime_error("departure runtime logged error: " + entry.text);
            std::cout
                << "PASS: actual App/Editor START parity, three foreground and five skyline white buildings/white roads, QUIT palette and camera, pause/step/resume, completion\n";
            return 0;
        }
        const auto materialRoot =
            std::filesystem::absolute("generated/tests/playback-material") / std::to_string(GetTickCount64());
        const std::filesystem::path materialPath = "Assets/Materials/effect.mat";
        SceneRuntime::MaterialAsset material;
        material.Save(materialRoot, materialPath);
        Editor::MaterialDocument draft;
        draft.Reload(materialRoot, materialPath);
        draft.Asset().values.shaderParameters = {.75f, 0, -4, 23};
        draft.Observe({});
        if (!draft.Dirty() || !draft.Undo() || draft.Dirty() || draft.Asset().values.shaderParameters[0] != 0 ||
            !draft.Undo(true) || !draft.Dirty() || draft.Asset().values.shaderParameters[0] != .75f)
            throw std::runtime_error("effectAmount-only material Dirty/Undo/Redo regression");
        draft.Save(materialRoot, materialPath);
        if (draft.Dirty() ||
            SceneRuntime::MaterialAsset::Load(materialRoot, materialPath).values.shaderParameters[0] != .75f)
            throw std::runtime_error("effectAmount material save roundtrip regression");
        if (SceneRuntime::MaterialAsset::Load(materialRoot, materialPath).values.shaderParameters !=
            std::array<float, 4>{.75f, 0, -4, 23})
            throw std::runtime_error("all four generic shader parameters round trip");
        draft.Asset().values.shaderParameters[1] = 2;
        draft.Observe({});
        if (!draft.Dirty() || !draft.Undo() || draft.Dirty() || draft.Asset().values.shaderParameters[1] != 0 ||
            !draft.Undo(true) || draft.Asset().values.shaderParameters[1] != 2)
            throw std::runtime_error("effectVariant-only material Dirty/Undo/Redo regression");
        draft.Save(materialRoot, materialPath);
        if (draft.Dirty() ||
            SceneRuntime::MaterialAsset::Load(materialRoot, materialPath).values.shaderParameters[1] != 2)
            throw std::runtime_error("effectVariant material save roundtrip regression");
        GamePlaybackParityValidation::Run(renderer, root);
        TitleConfigTransitionValidation::Run(renderer, root);
        TitleDepartureValidation::Run(renderer, root);
        if (!renderer.WaitForIdle())
            throw std::runtime_error("GPU completion failed");
        for (const auto& entry : Engine::Log::Recent())
            if (entry.level == Engine::LogLevel::Error)
                throw std::runtime_error("runtime logged error: " + entry.text);
        std::cout
            << "PASS: actual App/Editor frame parity, CONFIG independent scene, smooth return, START, pointer, pause/step/resume/restart, isolated settings, opaque silhouette, START eight white buildings and white roads travel, QUIT black/white arrow travel\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        for (const auto& entry : Engine::Log::Recent())
            if (entry.level == Engine::LogLevel::Error || entry.level == Engine::LogLevel::Warning)
                std::cerr << entry.text << '\n';
        return 1;
    }
}
