#pragma once
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <SceneRuntime/Scene/ObjectReferenceDomain.h>
#include <set>
#include <stdexcept>

namespace SceneRuntime
{
    class SceneCollection final
    {
    public:
        struct Entry
        {
            std::string name;
            std::set<std::string> objects;
        };
        /// <summary>初期シーンの所属を登録します。</summary>
        void Reset(const SceneLayout& layout, std::string name = "Main");
        /// <summary>ロード済みシーンと所属IDを取得します。</summary>
        const std::vector<Entry>& Entries() const
        {
            return entries_;
        }
        /// <summary>追加シーンのIDと内部参照を変換し、候補へ追加します。</summary>
        SceneLayout Add(const SceneLayout& current, SceneLayout incoming, const std::string& name, bool active = false,
                        const std::set<std::string>& reserved = {});
        /// <summary>保持対象と子孫を残してシーンを除去し、親変更はワールド姿勢を維持します。</summary>
        SceneLayout Unload(const SceneLayout& current, const std::string& name);

    private:
        /// <summary>保持対象と残るシーンに所属する個体の子孫を集めます。</summary>
        static std::set<std::string> Retained(const SceneLayout& layout, const std::set<std::string>& removed);
        /// <summary>追加シーンの個体参照を一括変換します。</summary>
        static void Remap(ScenePlacement& object, const std::map<std::string, std::string>& ids);
        /// <summary>保存済みの所有情報を復元します。</summary>
        void RestoreSavedOwners(const SceneLayout& layout, std::map<std::string, size_t>& owners);

        /// <summary>祖先の所有情報を設定します。</summary>
        void AssignAncestorOwner(const ScenePlacement& object,
                                 const std::map<std::string, const ScenePlacement*>& objects,
                                 std::map<std::string, size_t>& owners);

        /// <summary>保存済み所属を先に復元し、新しい個体を並び順によらず祖先またはアクティブシーンへ所属させます。</summary>
        void Synchronize(const SceneLayout& layout);
        /// <summary>所属をシーン保存とUndoの対象へ含めます。</summary>
        void Store(SceneLayout& layout) const;
        std::vector<Entry> entries_;
    };
}
