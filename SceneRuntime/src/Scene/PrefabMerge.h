#pragma once
#include <Engine/Core/Json.h>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace SceneRuntime::PrefabMerge
{
    using Engine::Json;
    enum class ArrayMode
    {
        Atomic,
        Index,
        Identity
    };
    struct ArrayPolicy
    {
        ArrayMode mode = ArrayMode::Atomic;
        std::string identity;
    };
    /// <summary>スクリプト配列の要素が型付きの安定IDを持つか確認します。</summary>
    bool HasScriptIdentity(const Json& item);
    /// <summary>保存形式のフィールドごとの配列の対応規則を返します。</summary>
    ArrayPolicy Policy(const std::string& type, const std::string& path, const Json& old);
    /// <summary>競合したフィールドを示して、確定前のRefreshを中断します。</summary>
    [[noreturn]] inline void Conflict(const std::string& path, const std::string& reason)
    {
        throw std::runtime_error("Prefab merge conflict " + path + ": " + reason);
    }
    /// <summary>JSON Pointerの名前をエスケープします。</summary>
    std::string Escape(const std::string& name);
    /// <summary>安定IDを取り出し、欠損・不正な型を拒否します。</summary>
    std::string Identity(const Json& item, const ArrayPolicy& policy, const std::string& path);
    /// <summary>配列を重複のない安定IDの対応表にします。</summary>
    std::map<std::string, Json> Items(const Json& array, const ArrayPolicy& policy, const std::string& path);
    /// <summary>保存された基準・現在値・素材の更新を、フィールドの方針に従って統合します。</summary>
    Json Merge(const Json& old, const Json& current, const Json& source, std::string path = "", std::string type = "");
    /// <summary>安定IDごとの変更を統合し、削除と変更の衝突を拒否します。</summary>
    std::map<std::string, Json> MergeItems(const std::map<std::string, Json>& before,
                                           const std::map<std::string, Json>& local,
                                           const std::map<std::string, Json>& remote, const std::string& path,
                                           const std::string& type);
    /// <summary>両側の順序変更と追加位置を統合します。</summary>
    std::vector<std::string> MergeOrder(const Json& old, const Json& current, const Json& source,
                                        const std::map<std::string, Json>& before,
                                        const std::map<std::string, Json>& merged, const std::string& path,
                                        const ArrayPolicy& policy);
    /// <summary>安定IDで追加・削除・並べ替えを対応させ、削除と変更の競合を拒否します。</summary>
    Json MergeIdentity(const Json& old, const Json& current, const Json& source, const std::string& path,
                       const std::string& type, const ArrayPolicy& policy);
    /// <summary>格子の寸法変更とセル編集の競合を検出します。</summary>
    void ValidateGridMerge(const Json& old, const Json& current, const Json& source, const std::string& path,
                           const std::string& type);
    /// <summary>型と格子寸法の変更が上書きと競合しないことを確認します。</summary>
    void ValidateMergeTypes(const Json& old, const Json& current, const Json& source, std::string& path,
                            std::string& type);
    /// <summary>配列の方針と寸法を確認して要素を再帰統合します。</summary>
    Json MergeArray(const Json& old, const Json& current, const Json& source, const std::string& path,
                    const std::string& type);
    /// <summary>オブジェクトの追加・削除・フィールド変更を統合します。</summary>
    Json MergeObject(const Json& old, const Json& current, const Json& source, const std::string& path,
                     const std::string& type);
    /// <summary>変更前と現在の内容を元データへ統合します。</summary>
    Json Merge(const Json& old, const Json& current, const Json& source, std::string path, std::string type);
}
