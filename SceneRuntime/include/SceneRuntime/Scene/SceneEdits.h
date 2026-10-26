#pragma once
#include <SceneRuntime/Scene/WorldReadView.h>
#include <DirectXMath.h>
#include <variant>

namespace SceneRuntime
{
    struct TransformEdit
    {
        std::string id;
        LocalTransform value;
    };
    struct WorldTransformEdit
    {
        std::vector<std::string> ids;
        DirectX::XMFLOAT4X4 delta;
    };
    struct TranslationEdit
    {
        std::vector<std::string> ids;
        std::array<float, 3> delta{};
    };
    struct RotationEdit
    {
        std::vector<std::string> ids;
        std::array<float, 3> pivot{};
        DirectX::XMFLOAT4X4 rotation;
    };
    struct ScaleEdit
    {
        std::vector<std::string> ids;
        std::array<float, 3> pivot{};
        DirectX::XMFLOAT4X4 axes;
        std::array<float, 3> factors{1, 1, 1};
    };
    struct ParentEdit
    {
        std::vector<std::string> ids;
        std::string parent;
    };
    struct NameEdit
    {
        std::string id, name;
    };
    struct ComponentsEdit
    {
        std::vector<ScenePlacement> values;
    };
    struct SettingsEdit
    {
        SceneSettings value;
    };
    struct AddObjectEdit
    {
        ScenePlacement value;
    };
    struct DuplicateEdit
    {
        std::vector<std::string> ids;
        std::array<float, 3> offset{};
    };
    struct RemoveObjectsEdit
    {
        std::vector<std::string> ids;
    };
    struct ReplaceDocumentEdit
    {
        SceneLayout value;
    };
    using SceneEdit = std::variant<TransformEdit, WorldTransformEdit, TranslationEdit, RotationEdit, ScaleEdit,
                                   ParentEdit, NameEdit, ComponentsEdit, SettingsEdit, AddObjectEdit, DuplicateEdit,
                                   RemoveObjectsEdit, ReplaceDocumentEdit>;

    struct SceneEditPlan
    {
        SceneLayout layout;
        SceneChangeSet changes;
        std::vector<std::string> createdIds;
        size_t nextId = 1;
    };

    /// <summary>描画・実行・ファイル読込を使わず、保存データの編集候補を準備します。</summary>
    class SceneEdits final
    {
    public:
        /// <summary>全対象の検証成功後だけ候補を返します。失敗時は元データとID採番を保持します。</summary>
        static std::optional<SceneEditPlan> Prepare(const SceneLayout& before, const SceneEdit& edit, size_t nextId,
                                                    std::string& error);
    };
}
