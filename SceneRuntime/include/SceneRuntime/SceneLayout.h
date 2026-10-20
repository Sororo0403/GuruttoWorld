#pragma once
#include <tuple>
#include <SceneRuntime/ComponentDefinitions.h>
#include <SceneRuntime/ComponentObjectReferences.h>
#include <SceneRuntime/ComponentAssetReferences.h>
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

namespace Engine
{
    class AssetDatabase;
}
namespace SceneRuntime
{
    template <class Component> struct ComponentDefinition;
#define ENGINE_SCENE_DEFINITION(Type, member, references, assets, order)                                               \
    template <> struct ComponentDefinition<Type>                                                                       \
    {                                                                                                                  \
        static constexpr unsigned int SaveOrder = order;                                                               \
    };
    ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_DEFINITION, ENGINE_SCENE_DEFINITION)
#undef ENGINE_SCENE_DEFINITION
    struct ScenePlacement
    {
        std::string id;
        std::string name;
        bool persistent = false;
        std::optional<PrefabLink> prefab;
#define ENGINE_SCENE_OPTIONAL(Type, member, references, assets, order) std::optional<Type> member;
#define ENGINE_SCENE_REPEATED(Type, member, references, assets, order) std::vector<Type> member;
        ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_OPTIONAL, ENGINE_SCENE_REPEATED)
#undef ENGINE_SCENE_OPTIONAL
#undef ENGINE_SCENE_REPEATED

        /// <summary>コンポーネントの格納領域を宣言順に編集します。</summary>
        template <class Visitor> void VisitComponentStorage(Visitor&& visitor)
        {
#define ENGINE_SCENE_STORAGE(Type, member, references, assets, order) visitor(member);
            ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_STORAGE, ENGINE_SCENE_STORAGE)
#undef ENGINE_SCENE_STORAGE
        }
        /// <summary>保存対象のコンポーネント格納領域を宣言順に読み取ります。</summary>
        auto Components() const
        {
#define ENGINE_SCENE_STORAGE(Type, member, references, assets, order) member,
            return std::tie(ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_STORAGE, ENGINE_SCENE_STORAGE) persistent);
#undef ENGINE_SCENE_STORAGE
        }
        /// <summary>存在する全コンポーネントを宣言順に走査します。</summary>
        template <class Visitor> void VisitComponents(Visitor&& visitor) const
        {
#define ENGINE_SCENE_OPTIONAL(Type, member, references, assets, order)                                                 \
    if (member)                                                                                                        \
        visitor(*member);
#define ENGINE_SCENE_REPEATED(Type, member, references, assets, order)                                                 \
    for (const auto& component : member)                                                                               \
        visitor(component);
            ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_OPTIONAL, ENGINE_SCENE_REPEATED)
#undef ENGINE_SCENE_OPTIONAL
#undef ENGINE_SCENE_REPEATED
        }
        /// <summary>コンポーネント内の個体参照を走査します。階層とPrefab所有権は操作側で扱います。</summary>
        template <class Visitor> void VisitObjectReferences(Visitor&& visitor)
        {
#define ENGINE_SCENE_OPTIONAL(Type, member, references, assets, order)                                                 \
    if constexpr (references)                                                                                          \
    {                                                                                                                  \
        if (member)                                                                                                    \
            ComponentObjectReferences(*member, visitor);                                                               \
    }
#define ENGINE_SCENE_REPEATED(Type, member, references, assets, order)                                                 \
    if constexpr (references)                                                                                          \
    {                                                                                                                  \
        for (auto& component : member)                                                                                 \
            ComponentObjectReferences(component, visitor);                                                             \
    }
            ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_OPTIONAL, ENGINE_SCENE_REPEATED)
#undef ENGINE_SCENE_OPTIONAL
#undef ENGINE_SCENE_REPEATED
            if (prefab)
                for (auto& [slot, target] : prefab->externalBindings)
                {
                    static_cast<void>(slot);
                    visitor(target.id);
                }
        }
        /// <summary>コンポーネント内のアセット参照を共通の定義で走査します。</summary>
        template <class Visitor> void VisitAssetReferences(Visitor&& visitor)
        {
#define ENGINE_SCENE_OPTIONAL(Type, member, references, assets, order)                                                 \
    if constexpr (assets)                                                                                              \
    {                                                                                                                  \
        if (member)                                                                                                    \
        {                                                                                                              \
            ComponentAssetReferences(*member, visitor);                                                                \
        }                                                                                                              \
    }
#define ENGINE_SCENE_REPEATED(Type, member, references, assets, order)                                                 \
    if constexpr (assets)                                                                                              \
    {                                                                                                                  \
        for (auto& component : member)                                                                                 \
        {                                                                                                              \
            ComponentAssetReferences(component, visitor);                                                              \
        }                                                                                                              \
    }
            ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_OPTIONAL, ENGINE_SCENE_REPEATED)
#undef ENGINE_SCENE_OPTIONAL
#undef ENGINE_SCENE_REPEATED
        }
        /// <summary>複製対象に含まれるコンポーネント参照だけを置き換えます。</summary>
        template <class IdMap> void RemapComponentReferences(const IdMap& ids)
        {
            VisitObjectReferences(
                [&](std::string& id)
                {
                    if (const auto found = ids.find(id); found != ids.end())
                        id = found->second;
                });
        }
        /// <summary>Transformを含むコンポーネントIDの存在を確認します。</summary>
        bool HasComponentId(const std::string& componentId) const
        {
            bool found = componentId == "transform";
            VisitComponents(
                [&](const auto& component)
                {
                    found = found || component.id == componentId;
                });
            return found;
        }
        /// <summary>永続化指定と全コンポーネントを比較します。</summary>
        bool SameComponents(const ScenePlacement& other) const
        {
            return Components() == other.Components();
        }
        /// <summary>個体の識別子・階層・姿勢を維持して全コンポーネントをコピーします。</summary>
        void CopyComponents(const ScenePlacement& other)
        {
            persistent = other.persistent;
#define ENGINE_SCENE_COPY(Type, member, references, assets, order) member = other.member;
            ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_COPY, ENGINE_SCENE_COPY)
#undef ENGINE_SCENE_COPY
        }
        const std::filesystem::path& Model() const
        {
            static const std::filesystem::path empty;
            return meshRenderer ? meshRenderer->model : empty;
        }
        void SetModel(std::filesystem::path path)
        {
            if (!meshRenderer)
                meshRenderer.emplace();
            meshRenderer->model = std::move(path);
        }
        const std::filesystem::path& Material() const
        {
            static const std::filesystem::path empty;
            return material && material->enabled ? material->asset : empty;
        }
        std::array<float, 3> position{};
        std::array<float, 3> rotation{}; // XYZ、ラジアン。
        std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
        std::string parentId; // 空の場合はルートです。位置・回転・スケールは常に親を基準とします。
    };

    struct FogSettings
    {
        bool enabled = false;
        std::array<float, 3> color{0.76f, 0.84f, 0.85f};
        float start = 24, end = 155, strength = 0.72f;
        bool operator==(const FogSettings&) const = default;
    };
    struct SceneSettings
    {
        std::array<float, 3> gravity{0, -20, 0};
        float fixedStep = 1.0f / 60.0f;
        unsigned int maximumFixedSteps = 4;
        unsigned int shadowResolution = 2048, localShadowResolution = 512;
        float sceneRenderScale = 1, gameRenderScale = 1;
        bool sceneRealtime = true, gameRealtime = true;
        std::filesystem::path skyShader, oceanShader, particleShader, postShader;
        std::array<std::string, 5> playerInputs{"MoveRight", "MoveLeft", "MoveForward", "MoveBack", "Jump"};
        std::array<float, 4> background{0.66f, 0.79f, 0.83f, 1};
        std::string mainCamera;
        FogSettings fog;
        Engine::PostEffectSettings postEffects;
        bool operator==(const SceneSettings&) const = default;
    };

    struct SceneLayout
    {
        std::vector<ScenePlacement> objects;
        SceneSettings settings;
        std::map<std::string, std::string> assetReferences;
        std::map<std::string, std::string> prefabSlots;
        std::map<std::string, std::vector<std::string>> sceneObjects;
        void ResolveAssets(const std::filesystem::path& root);
        /// <summary>ロード済み索引で参照を解決し、ディレクトリの再探索を避けます。</summary>
        void ResolveAssets(const Engine::AssetDatabase& index);
        // 読み込み・検証に失敗した場合は例外。呼び出し元の配置は変更しません。
        static SceneLayout Parse(std::string_view json);
        /// <summary>文字列への変換を行わず、保存時と同じ規則で配置を検証します。</summary>
        void Validate() const;
        /// <summary>検証済みの変更前配置との成分差分を検証します。未検証の配置を変更前に指定してはいけません。</summary>
        void ValidateChanges(const SceneLayout& validatedBefore) const;
        std::string Serialize() const;
        /// <summary>保存先に対応するアセットIDを含む内容を準備します。ファイルと編集データは変更しません。</summary>
        std::string SerializeForSave(const std::filesystem::path& path) const;
        /// <summary>
        /// overwrite=false の場合は、最後のアトミックな確定処理でも既存の出力先を拒否します。
        /// </summary>
        void Save(const std::filesystem::path& path, bool overwrite = true) const;
        static SceneLayout Load(const std::filesystem::path& path, const std::filesystem::path& assetsRoot = {});
    };
}
