#pragma once
#include <SceneRuntime/SceneComponents.h>
#include <SceneRuntime/GenreComponents.h>
#include <SceneRuntime/JointComponent.h>
#include <SceneRuntime/EnvironmentComponents.h>
#include <SceneRuntime/UiComponents.h>
#include <SceneRuntime/Animation.h>
#include <SceneRuntime/Animator.h>
#include <SceneRuntime/Ragdoll.h>
#include <Engine/Graphics/Materials/PostEffectSettings.h>
#include <optional>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

namespace SceneRuntime
{
    struct ScenePlacement
    {
        std::string id;
        std::string name;
        bool persistent=false;
        std::optional<PrefabLink> prefab;
        std::optional<MeshRendererComponent> meshRenderer;
        std::optional<MaterialComponent> material;
        std::optional<RotatorComponent> rotator;
        std::optional<PlayerControllerComponent> playerController;
        std::optional<BoxColliderComponent> boxCollider;
        std::optional<RigidBodyComponent> rigidBody;
        std::optional<JointComponent> joint;
        std::vector<ScriptComponent> scripts;
        std::optional<CameraComponent> camera;
        std::optional<DirectionalLightComponent> directionalLight;
        std::optional<PointLightComponent> pointLight;
        std::optional<SpotLightComponent> spotLight;
        std::optional<SkyComponent> sky;
        std::optional<ParticleEmitterComponent> particleEmitter;
        std::optional<CameraSwayComponent> cameraSway;
        std::optional<CanvasComponent> canvas;
        std::optional<RectTransformComponent> rectTransform;
        std::optional<ImageComponent> image;
        std::optional<TextComponent> text;
        std::optional<ButtonComponent> button;
        std::optional<InputFieldComponent> inputField;
        std::optional<SliderComponent> slider;
        std::optional<ToggleComponent> toggle;
        std::optional<ScrollViewComponent> scrollView;
        std::optional<MaskComponent> mask;
        std::optional<LayoutGroupComponent> layoutGroup;
        std::optional<TerrainComponent> terrain;
        std::optional<TilemapComponent> tilemap;
        std::optional<NavMeshComponent> navMesh;
        std::optional<NavAgentComponent> navAgent;
        std::optional<AudioSourceComponent> audioSource;
        std::optional<AudioListenerComponent> audioListener;
        std::optional<AudioMixerComponent> audioMixer;
        std::optional<AnimationComponent> animation;
        std::optional<AnimatorComponent> animator;
        std::optional<RagdollComponent> ragdoll;
        bool HasComponentId(const std::string& componentId) const
        {
            if(joint && joint->id==componentId) return true;
            const auto matches=[&](const auto& component) { return component && component->id==componentId; };
            if(matches(ragdoll)) return true;
            if(matches(inputField) || matches(slider) || matches(toggle) || matches(scrollView) || matches(mask) || matches(layoutGroup)) return true;
            const std::initializer_list<bool> found{componentId=="transform",matches(meshRenderer),matches(material),matches(rotator),matches(playerController),matches(boxCollider),matches(rigidBody),matches(camera),
                matches(directionalLight),matches(pointLight),matches(spotLight),matches(sky),matches(particleEmitter),matches(cameraSway),matches(canvas),
                matches(rectTransform),matches(image),matches(text),matches(button),matches(terrain),matches(tilemap),matches(navMesh),matches(navAgent),matches(audioSource),matches(audioListener),matches(audioMixer),matches(animation),matches(animator)};
            return std::any_of(found.begin(),found.end(),[](bool value){return value;}) ||
                std::any_of(scripts.begin(),scripts.end(),[&](const auto& script) { return script.id==componentId; });
        }
        bool SameComponents(const ScenePlacement& other) const
        {
            if(ragdoll!=other.ragdoll) return false;
            if(inputField!=other.inputField || slider!=other.slider || toggle!=other.toggle || scrollView!=other.scrollView || mask!=other.mask || layoutGroup!=other.layoutGroup) return false;
            return joint==other.joint && persistent==other.persistent && meshRenderer==other.meshRenderer && material==other.material && rotator==other.rotator && playerController==other.playerController && boxCollider==other.boxCollider && rigidBody==other.rigidBody && scripts==other.scripts && camera==other.camera &&
                directionalLight==other.directionalLight && pointLight==other.pointLight && spotLight==other.spotLight && sky==other.sky && particleEmitter==other.particleEmitter &&
                cameraSway==other.cameraSway && canvas==other.canvas && rectTransform==other.rectTransform && image==other.image && text==other.text && button==other.button && terrain==other.terrain && tilemap==other.tilemap && navMesh==other.navMesh && navAgent==other.navAgent && audioSource==other.audioSource && audioListener==other.audioListener && audioMixer==other.audioMixer && animation==other.animation && animator==other.animator;
        }
        void CopyComponents(const ScenePlacement& other)
        {
            ragdoll=other.ragdoll;
            persistent=other.persistent;
            joint=other.joint;
            inputField=other.inputField; slider=other.slider; toggle=other.toggle; scrollView=other.scrollView; mask=other.mask; layoutGroup=other.layoutGroup;
            meshRenderer=other.meshRenderer; material=other.material; rotator=other.rotator; playerController=other.playerController; boxCollider=other.boxCollider; rigidBody=other.rigidBody; scripts=other.scripts; camera=other.camera;
            directionalLight=other.directionalLight; sky=other.sky; particleEmitter=other.particleEmitter; cameraSway=other.cameraSway;
            pointLight=other.pointLight; spotLight=other.spotLight;
            canvas=other.canvas; rectTransform=other.rectTransform; image=other.image; text=other.text; button=other.button; terrain=other.terrain; tilemap=other.tilemap; navMesh=other.navMesh; navAgent=other.navAgent; audioSource=other.audioSource; audioListener=other.audioListener; audioMixer=other.audioMixer;
            animation=other.animation; animator=other.animator;
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
        const std::filesystem::path& Material() const
        {
            static const std::filesystem::path empty;
            return material && material->enabled ? material->asset : empty;
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
        Engine::PostEffectSettings postEffects;
        bool operator==(const SceneSettings&) const = default;
    };

    struct SceneLayout
    {
        std::vector<ScenePlacement> objects;
        SceneSettings settings;
        std::map<std::string,std::string> assetReferences;
        std::map<std::string,std::vector<std::string>> sceneObjects;
        void ResolveAssets(const std::filesystem::path& root);
        // 読み込み・検証に失敗した場合は例外。呼び出し元の配置は変更しません。
        static SceneLayout Parse(std::string_view json);
        std::string Serialize() const;
        // With overwrite=false, the final atomic commit also refuses an existing destination.
        void Save(const std::filesystem::path& path, bool overwrite = true) const;
        static SceneLayout Load(const std::filesystem::path& path,const std::filesystem::path& assetsRoot={});
    };
}
