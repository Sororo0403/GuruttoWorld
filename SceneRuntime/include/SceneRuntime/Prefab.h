#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Assets/ContentContext.h>

namespace SceneRuntime
{
    class PrefabReferenceMigrationRequired : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };
    struct PrefabOverride
    {
        std::string path, value, source;
    };
    class Prefab final
    {
    public:
        static bool ValidPath(const std::filesystem::path& path);
        static SceneLayout Extract(const SceneLayout& scene, const std::string& rootId);
        /// <summary>内部参照を変換し、指定された外部接続だけを解決して配置します。</summary>
        static std::string Instantiate(SceneLayout& scene, const SceneLayout& asset, const std::filesystem::path& path,
                                       const std::array<float, 3>& position, const PrefabBindings& bindings = {});
        /// <summary>Prefab内の外部接続口を列挙します。旧素材の範囲外参照も接続口として扱います。</summary>
        static std::map<std::string, std::string> ExternalSlots(const SceneLayout& asset);
        /// <summary>個体の接続先を取得します。旧個体の接続先は未指定として提示します。</summary>
        static PrefabBindings ExternalBindings(const SceneLayout& scene, const std::string& rootId);
        /// <summary>接続先を明示的に変更します。未指定は空参照とし、旧個体の移行も同じ操作で行います。</summary>
        static void BindReferences(SceneLayout& scene, const std::string& rootId, const PrefabBindings& bindings);
        /// <summary>
        /// 個体ごとのプロパティ変更を保持しながら元データの更新を統合します。例外は呼び出し側が確定する前に送出します。
        /// </summary>
        static void Refresh(SceneLayout& scene, const std::filesystem::path& assetsRoot);
        /// <summary>未保存の素材候補を優先して更新を検証します。ファイルへは書き込みません。</summary>
        static void Refresh(SceneLayout& scene, const std::filesystem::path& assetsRoot,
                            const std::map<std::filesystem::path, SceneLayout>& preparedAssets,
                            std::shared_ptr<const Engine::ContentContext> context = {});
        static void Bind(SceneLayout& scene, const std::string& rootId, const std::filesystem::path& path);
        static void Unpack(SceneLayout& scene, const std::string& rootId);
        static std::vector<PrefabOverride> Overrides(const SceneLayout& scene, const std::string& objectId);
        static void RevertOverride(SceneLayout& scene, const std::string& objectId, const std::string& property);
        static void RevertInstance(SceneLayout& scene, const std::string& rootId,
                                   const std::filesystem::path& assetsRoot);
        static void ApplyOverride(const SceneLayout& scene, const std::string& objectId, const std::string& property,
                                  const std::filesystem::path& assetsRoot);
        /// <summary>読み取り済みの素材候補へ上書きを反映し、ファイルを変更せず検証します。</summary>
        static void EditOverride(SceneLayout& asset, const SceneLayout& scene, const std::string& objectId,
                                 const std::string& property);
        static SceneLayout Variant(const SceneLayout& scene, const std::string& rootId);
    };
}
