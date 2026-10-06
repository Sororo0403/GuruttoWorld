#pragma once
#include <SceneRuntime/SceneComponents.h>
#include <SceneRuntime/EnvironmentComponents.h>
#include <optional>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace SceneRuntime
{
    struct ScenePlacement
    {
        std::string id;
        std::string name;
        std::optional<MeshRendererComponent> meshRenderer;
        std::optional<RotatorComponent> rotator;
        std::optional<CameraComponent> camera;
        std::optional<DirectionalLightComponent> directionalLight;
        std::optional<SkyComponent> sky;
        std::optional<ParticleEmitterComponent> particleEmitter;
        std::optional<CameraSwayComponent> cameraSway;
        bool HasComponentId(const std::string& componentId) const
        {
            const auto matches=[&](const auto& component) { return component && component->id==componentId; };
            return componentId=="transform" || matches(meshRenderer) || matches(rotator) || matches(camera) ||
                matches(directionalLight) || matches(sky) || matches(particleEmitter) || matches(cameraSway);
        }
        bool SameComponents(const ScenePlacement& other) const
        {
            return meshRenderer==other.meshRenderer && rotator==other.rotator && camera==other.camera &&
                directionalLight==other.directionalLight && sky==other.sky && particleEmitter==other.particleEmitter &&
                cameraSway==other.cameraSway;
        }
        void CopyComponents(const ScenePlacement& other)
        {
            meshRenderer=other.meshRenderer; rotator=other.rotator; camera=other.camera;
            directionalLight=other.directionalLight; sky=other.sky; particleEmitter=other.particleEmitter; cameraSway=other.cameraSway;
        }
        const std::filesystem::path& Model() const
        {
            static const std::filesystem::path empty;
            return meshRenderer ? meshRenderer->model : empty;
        }
        void SetModel(std::filesystem::path path)
        {
            if (!meshRenderer) meshRenderer.emplace();
            meshRenderer->model=std::move(path);
        }
        std::array<float, 3> position{};
        std::array<float, 3> rotation{}; // XYZ、ラジアン。
        std::array<float, 3> scale{ 1.0f, 1.0f, 1.0f };
        std::string parentId; // Empty means root. Position, rotation and scale are always relative to the parent.
    };

    struct FogSettings
    {
        bool enabled=false;
        std::array<float,3> color{0.76f,0.84f,0.85f};
        float start=24, end=155, strength=0.72f;
        bool operator==(const FogSettings&) const = default;
    };
    struct SceneSettings
    {
        std::array<float,4> background{0.66f,0.79f,0.83f,1};
        std::string mainCamera;
        FogSettings fog;
        bool operator==(const SceneSettings&) const = default;
    };

    struct SceneLayout
    {
        std::vector<ScenePlacement> objects;
        SceneSettings settings;
        // 読み込み・検証に失敗した場合は例外。呼び出し元の配置は変更しません。
        static SceneLayout Parse(std::string_view json);
        std::string Serialize() const;
        // With overwrite=false, the final atomic commit also refuses an existing destination.
        void Save(const std::filesystem::path& path, bool overwrite = true) const;
        static SceneLayout Load(const std::filesystem::path& path);
    };
}
