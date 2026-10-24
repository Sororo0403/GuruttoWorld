#include <SceneRuntime/Scene/SceneLayout.h>
#include <SceneRuntime/Scripts/ScriptSchemaFiles.h>
#include <SceneRuntime/Scene/SceneAssetReferences.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Core/FileContents.h>
#include <Engine/Core/FileReplacement.h>
#include <Engine/Core/Json.h>

namespace SceneRuntime
{
    using Engine::Json;
    std::map<std::string, ScriptSchema> ScriptSchema::LoadProject(const std::filesystem::path& root)
    {
        return ScriptSchemaFiles::LoadProject(root);
    }
    std::string SceneLayout::SerializeForSave(const std::filesystem::path& path) const
    {
        auto saved = Normalized();
        const auto root = Engine::AssetDatabase::Root(path);
        if (!root.empty())
            ResolveSceneAssetReferences(saved, Engine::AssetDatabase(root), true, false);
        return Json::parse(saved.Serialize()).dump(2) + "\n";
    }
    void SceneLayout::Save(const std::filesystem::path& path, bool overwrite) const
    {
        Engine::FileReplacement::Save(
            path,
            [&]
            {
                return SerializeForSave(path);
            },
            overwrite);
    }

    void SceneLayout::ResolveAssets(const std::filesystem::path& root)
    {
        ResolveAssets(Engine::AssetDatabase(root));
    }
    void SceneLayout::ResolveAssets(const Engine::AssetDatabase& index)
    {
        auto candidate = Normalized();
        ResolveSceneAssetReferences(candidate, index);
        *this = candidate.Normalized();
    }
    SceneLayout SceneLayout::Load(const std::filesystem::path& path, const std::filesystem::path& assetsRoot)
    {
        const auto source = Engine::FileContents::Read(path);
        if (!source)
            throw std::runtime_error("Cannot open layout: " + path.string());
        auto json = *source;
        // 一般的なテキストエディターが付けるUTF-8 BOMを許可します。
        if (json.starts_with("\xEF\xBB\xBF"))
            json.erase(0, 3);
        auto layout = Parse(json);
        if (!assetsRoot.empty())
            layout.ResolveAssets(assetsRoot);
        return layout;
    }
}
