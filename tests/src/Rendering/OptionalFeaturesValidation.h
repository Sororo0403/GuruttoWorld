#pragma once
#include <SceneRuntime/Scene/SceneEnvironment.h>
#include <SceneRuntime/Gameplay/GamePlayback.h>
#include <SceneRuntime/Rendering/ScenePresentation.h>
#include <Engine/Core/FileContents.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <set>

namespace OptionalFeaturesValidation
{
    /// <summary>空のゲームがタイトル・空・粒子のシェーダーに依存せず、必要になった機能だけを準備することを検証します。</summary>
    inline void Run(const Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime;
        const auto require = [](bool value, const std::string& message)
        {
            if (!value)
                throw std::runtime_error(message);
        };
        const auto root = std::filesystem::absolute("generated/tests/optional-features") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        const auto shaders = Engine::ProjectContext::SourceEngineRoot() / "Engine/Shaders";
        std::filesystem::create_directories(root / "Assets/Scenes");
        const std::set<std::string> optional{"Sky.hlsl",         "GlowParticle.hlsl", "OceanSunset.hlsl",
                                             "PostEffects.hlsl", "Ui.hlsl",           "SceneUi.hlsl"};
        for (const auto& entry : std::filesystem::recursive_directory_iterator(shaders))
        {
            if (!entry.is_regular_file())
                continue;
            auto name = entry.path();
            if (name.extension() == ".meta")
                name.replace_extension();
            const auto relative = entry.path().lexically_relative(shaders);
            if (relative.generic_string().starts_with("Compiled/") || optional.contains(name.filename().string()))
                continue;
            const auto target = root / "Engine/Shaders" / relative;
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(entry.path(), target);
        }
        SceneEnvironment environment;
        std::string error;
        const auto relativeRoot = root.lexically_relative(std::filesystem::current_path());
        SceneWorld relativeWorld;
        require(relativeWorld.Initialize(renderer, relativeRoot, SceneLayout{},
                                         relativeRoot / "Engine/Shaders/Mesh.hlsl", &error),
                "Working-directory shader paths resolve once: " + error);
        MaterialAsset material;
        material.shader = "Engine/Shaders/Mesh.hlsl";
        require(material.Prepare(renderer.GetDevice(), renderer.GetCommandQueue(), relativeRoot,
                                 Engine::ContentContext::Capture(root)) != nullptr,
                "Material shader paths stay relative to their content owner");
        require(environment.Initialize(renderer, relativeRoot, SceneLayout{}, error),
                "Empty game initializes without optional pipelines: " + error);
        require(!TitleBindings::Configuration(environment.World().Layout()) &&
                    !environment.Ui().values.contains("screen") && !environment.Ui().values.contains("intro") &&
                    !environment.Ui().values.contains("selected"),
                "Empty game creates no menu state");
        require(environment.Update(.016, true, true), "Empty game advances");
        require(!std::filesystem::exists(root / "Engine/Shaders/Sky.hlsl") &&
                    !std::filesystem::exists(root / "Engine/Shaders/Ui.hlsl"),
                "Optional shaders remain absent");
        SceneLayout sky;
        ScenePlacement object;
        object.id = "sky";
        object.sky.emplace();
        sky.objects = {object};
        sky.Save(root / "Assets/Scenes/Sky.json");
        const auto empty = environment.World().Layout().Serialize();
        require(!environment.LoadScene("Assets/Scenes/Sky.json", false, error) &&
                    environment.World().Layout().Serialize() == empty,
                "Missing enabled feature rejects only its scene transition");
        std::filesystem::copy_file(shaders / "Sky.hlsl", root / "Engine/Shaders/Sky.hlsl");
        require(environment.LoadScene("Assets/Scenes/Sky.json", false, error),
                "Adding only the sky pipeline enables sky: " + error);
        require(!std::filesystem::exists(root / "Engine/Shaders/GlowParticle.hlsl"), "Sky does not require particles");
        SceneLayout particles = environment.World().Layout();
        object = {};
        object.id = "particles";
        object.particleEmitter.emplace();
        particles.objects.push_back(object);
        particles.Save(root / "Assets/Scenes/Particles.json");
        const auto skyBefore = environment.World().Layout().Serialize();
        require(!environment.LoadScene("Assets/Scenes/Particles.json", false, error) &&
                    environment.World().Layout().Serialize() == skyBefore,
                "Unprepared particle feature preserves the sky scene");
    }
}
