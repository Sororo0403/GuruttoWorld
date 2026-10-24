#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <Engine/Assets/AssetDatabase.h>

namespace SceneRuntime
{
    /// <summary>シーンとPrefab基準データのアセット参照を型ごとの走査規則で解決・収集します。</summary>
    void ResolveSceneAssetReferences(SceneLayout& layout, const Engine::AssetDatabase& index, bool capture = true,
                                     bool resolve = true);
    /// <summary>欠損参照の保存値を保持して他の参照を解決し、未解決パスごとの診断を返します。</summary>
    std::map<std::string, std::string> ResolveSceneAssetReferencesForEditing(SceneLayout& layout,
                                                                             const Engine::AssetDatabase& index);
    /// <summary>新しい素材や基準データへ使用中のIDを引き継ぎます。ファイルや索引へはアクセスしません。</summary>
    void InheritSceneAssetReferences(SceneLayout& layout, const std::map<std::string, std::string>& known);
}
