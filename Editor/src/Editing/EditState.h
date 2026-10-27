#pragma once
#include <SceneRuntime/Scene/SceneEditTarget.h>
#include <SceneRuntime/Scene/Prefab.h>
#include <optional>
#include <algorithm>
#include "SelectionSet.h"
#include <string>
#include <utility>

namespace Editor
{
    enum class ObjectAction
    {
        Add,
        AddEmpty,
        Duplicate,
        Delete,
        Components,
        Settings,
        AddPrefab,
        SavePrefab,
        ApplyPrefab,
        RefreshPrefabs,
        UnpackPrefab,
        RevertPrefab,
        RevertPrefabProperty,
        ApplyPrefabProperty,
        BindPrefabReferences,
        SavePrefabVariant,
        GenerateRagdoll
    };
    struct ObjectRequest
    {
        ObjectAction action = ObjectAction::Add;
        std::string id;
        std::filesystem::path model;
        std::array<float, 3> position{};
        std::vector<std::string> ids{};
        std::optional<SceneRuntime::ScenePlacement> components{};
        std::string interaction{};
        std::optional<SceneRuntime::SceneSettings> settings{};
        std::string property{};
        std::vector<SceneRuntime::ScenePlacement> componentBatch{};
        SceneRuntime::PrefabBindings prefabBindings{};
    };

    // パネルや ImGui に依存しない、共通の編集状態と操作です。
    class EditState final
    {
    public:
        void BeginFrame()
        {
            interaction_.clear();
        }
        void SetInteraction(std::string key)
        {
            interaction_ = std::move(key);
        }
        const std::string& Interaction() const
        {
            return interaction_;
        }
        /// <summary>配置の変更を通知します。</summary>
        void ObjectChanged(std::string id);
        void InspectAsset(std::filesystem::path path)
        {
            inspectedAsset_ = std::move(path);
        }
        const std::filesystem::path& InspectedAsset() const
        {
            return inspectedAsset_;
        }
        const std::string& SelectedId() const
        {
            return selection_.Primary();
        }
        const std::vector<std::string>& SelectedIds() const
        {
            return selection_.Ids();
        }
        bool IsSelected(const std::string& id) const
        {
            return selection_.Contains(id);
        }
        bool SingleSelection() const
        {
            return SelectedIds().size() == 1;
        }
        /// <summary>保存した選択状態を復元します。</summary>
        void RestoreSelection(const std::vector<std::string>& ids, const std::string& primary);
        /// <summary>指定範囲の対象を選択します。</summary>
        void SelectRange(const std::vector<std::string>& visible, const std::string& anchor, const std::string& clicked,
                         bool additive);
        /// <summary>指定した対象を選択します。</summary>
        void Select(std::string id, bool additive = false);
        bool HasChanges() const
        {
            return changed_;
        }
        void SetChanged(bool changed)
        {
            changed_ = changed;
        }
        void MarkSaved()
        {
            changed_ = false;
        }
        /// <summary>再読み込み後の状態を更新します。</summary>
        void Reloaded();
        /// <summary>ローカル変換を設定します。</summary>
        bool SetLocalTransform(SceneRuntime::SceneEditTarget& world, const std::string& id,
                               const std::array<float, 3>& position, const std::array<float, 3>& rotation,
                               const std::array<float, 3>& scale);
        /// <summary>選択対象をワールド空間で平行移動します。</summary>
        bool TranslateSelectionWorld(SceneRuntime::SceneEditTarget& world, const std::array<float, 3>& delta);
        /// <summary>選択対象をワールド空間で回転します。</summary>
        bool RotateSelectionWorld(SceneRuntime::SceneEditTarget& world, const std::array<float, 3>& pivot,
                                  const DirectX::XMFLOAT4X4& rotation);
        /// <summary>選択対象をワールド空間で拡縮します。</summary>
        bool ScaleSelectionWorld(SceneRuntime::SceneEditTarget& world, const std::array<float, 3>& pivot,
                                 const DirectX::XMFLOAT4X4& axes, const std::array<float, 3>& factors);
        /// <summary>親の参照を設定します。</summary>
        bool SetParent(SceneRuntime::SceneEditTarget& world, const std::string& id, std::string parentId,
                       std::string& error);
        /// <summary>複数の配置の親の参照を設定します。</summary>
        bool SetParents(SceneRuntime::SceneEditTarget& world, const std::vector<std::string>& ids,
                        const std::string& parent, std::string& error);
        /// <summary>対象の名前と関連する参照を更新します。</summary>
        bool Rename(SceneRuntime::SceneEditTarget& world, const std::string& id, std::string name);
        /// <summary>選択対象の複製を要求します。</summary>
        ObjectRequest DuplicateSelectionRequest() const;
        /// <summary>指定した配置と参照を複製します。</summary>
        bool DuplicateObjects(SceneRuntime::SceneEditTarget& world, const std::vector<std::string>& ids,
                              const std::array<float, 3>& offset, std::string& error);
        /// <summary>選択対象の削除を要求します。</summary>
        ObjectRequest DeleteSelectionRequest() const;
        /// <summary>指定した配置と参照を削除します。</summary>
        bool DeleteObjects(SceneRuntime::SceneEditTarget& world, const std::vector<std::string>& ids,
                           std::string& error);
        /// <summary>変換を初期状態へ戻します。</summary>
        bool ResetTransform(SceneRuntime::SceneEditTarget& world, const std::string& id);
        bool InvalidTransform() const
        {
            return invalidTransform_;
        }
        /// <summary>コンポーネントの変更を要求します。</summary>
        void RequestComponents(SceneRuntime::ScenePlacement placement, std::string interaction);
        void Request(ObjectRequest request)
        {
            request_ = std::move(request);
        }
        /// <summary>複数の配置への変更を要求します。</summary>
        void RequestComponentBatch(std::vector<SceneRuntime::ScenePlacement> placements, std::string interaction);
        /// <summary>保留中の要求を取得して消去します。</summary>
        std::optional<ObjectRequest> TakeRequest();

        /// <summary>準備済みの選択を確保なしで反映します。</summary>
        void CommitSelection(SelectionSet& candidate) noexcept
        {
            selection_.Swap(candidate);
        }

    private:
        std::filesystem::path inspectedAsset_;
        SelectionSet selection_;
        std::string interaction_;
        bool changed_ = false;
        bool invalidTransform_ = false;
        std::optional<ObjectRequest> request_;
    };
}
