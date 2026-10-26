#pragma once
#include <array>
#include <string>
#include <vector>
#include <filesystem>

namespace SceneRuntime
{
    struct ScenePlacement;
    struct SceneLayout;

    /// <summary>親基準の位置・ラジアン回転・スケールを値として保持します。</summary>
    struct LocalTransform
    {
        std::array<float, 3> position{}, rotation{}, scale{1, 1, 1};
        /// <summary>保存する三つのベクトルを比較します。</summary>
        bool operator==(const LocalTransform&) const = default;
    };

    enum class TransformField : unsigned int
    {
        None = 0,
        Position = 1,
        Rotation = 2,
        Scale = 4,
        Parent = 8
    };

    struct TransformChange
    {
        std::string objectId;
        TransformField fields = TransformField::None;

        /// <summary>指定したフィールドの変更が含まれるかを返します。</summary>
        bool Has(TransformField field) const;
    };

    enum class SceneChangeKind
    {
        Added,
        Removed,
        Modified
    };

    struct ObjectChange
    {
        std::string objectId;
        SceneChangeKind kind = SceneChangeKind::Modified;
        /// <summary>同じ個体と操作種別かを比較します。</summary>
        bool operator==(const ObjectChange&) const = default;
    };

    struct ComponentChange
    {
        std::string objectId, componentId, componentType;
        SceneChangeKind kind = SceneChangeKind::Modified;
        /// <summary>同じコンポーネントと操作種別かを比較します。</summary>
        bool operator==(const ComponentChange&) const = default;
    };

    struct SceneFieldChange
    {
        std::string objectId, componentId, field;
        /// <summary>対象と変更フィールドを比較します。</summary>
        bool operator==(const SceneFieldChange&) const = default;
    };

    struct ResourceDependencyChange
    {
        std::string objectId, componentId;
        std::vector<std::filesystem::path> before, after;
        /// <summary>対象と変更前後の資源参照を比較します。</summary>
        bool operator==(const ResourceDependencyChange&) const = default;
    };

    /// <summary>成功した操作の対象・フィールド・構造・資源依存を値として保持します。</summary>
    struct SceneChangeSet
    {
        std::vector<TransformChange> transforms;
        std::vector<ObjectChange> objects;
        std::vector<ComponentChange> components;
        std::vector<SceneFieldChange> fields;
        std::vector<ResourceDependencyChange> resources;
        bool structureChanged = false;

        /// <summary>捕捉した変更がないかを返します。</summary>
        bool Empty() const noexcept;
        /// <summary>同じIDのフィールドを統合し、同一値の設定は記録しません。</summary>
        void Record(const ScenePlacement& before, const ScenePlacement& after);
        /// <summary>別の候補で捕捉した変更をIDごとに統合します。</summary>
        void Merge(const SceneChangeSet& changes);
        /// <summary>確定時に確保や通知を行わず、準備済みの変更集合を交換します。</summary>
        void Swap(SceneChangeSet& changes) noexcept;
        /// <summary>互換経路の変更を、両方に存在するIDごとに比較します。並べ替えを変更とみなしません。</summary>
        static SceneChangeSet CompareTransforms(const SceneLayout& before, const SceneLayout& after);
        /// <summary>全保存フィールド・構造・資源依存をIDで比較します。両方の配置は検証済みである必要があります。</summary>
        static SceneChangeSet Compare(const SceneLayout& before, const SceneLayout& after);
        /// <summary>一つの個体のフィールドとコンポーネントを比較します。</summary>
        void RecordObject(const ScenePlacement& before, const ScenePlacement& after);
        /// <summary>追加または削除した個体と、そのコンポーネント・資源参照を記録します。</summary>
        void RecordObject(const ScenePlacement& object, SceneChangeKind kind);
    };

    struct SceneEditResult
    {
        bool succeeded = false;
        SceneChangeSet changes;
    };

    /// <summary>GPUや実行ホストを使わず、型付き変更を候補の個体へ適用します。</summary>
    class SceneTransformEdit final
    {
    public:
        /// <summary>値を検証して変更を捕捉します。失敗時は個体を保持し、親子全体の検証は外側で行います。</summary>
        static SceneEditResult Apply(ScenePlacement& object, const LocalTransform& transform);
    };
}
