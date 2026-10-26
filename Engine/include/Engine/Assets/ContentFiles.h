#pragma once
#include <Engine/Projects/ProjectContext.h>
#include <map>

namespace Engine
{
    class ContentFiles final
    {
    public:
        /// <summary>論理アセットパスからプロジェクトのルートを求めます。</summary>
        static std::filesystem::path Root(std::filesystem::path path);
        /// <summary>標準シェーダーの論理パスか確認します。</summary>
        static bool IsEngineResource(const std::filesystem::path& relative)
        {
            return relative.generic_string().starts_with("Engine/Shaders/");
        }
        /// <summary>所有者の固定した参照先から標準シェーダーを解決します。旧パスは読み込み互換用です。</summary>
        static std::filesystem::path Resolve(const std::filesystem::path& path, const std::filesystem::path& root,
                                             const std::filesystem::path& shaders);
        /// <summary>ゲームのファイルと、明示したエンジンリソースを読み込みます。</summary>
        static std::filesystem::path Resolve(const std::filesystem::path& path);
        /// <summary>ゲーム自身が所有する素材だけを列挙します。</summary>
        static std::map<std::filesystem::path, std::filesystem::path> List(const std::filesystem::path& root);
    };
}
